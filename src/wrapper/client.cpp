#include "wrapper/client.hpp"
#include "core/log.h"
#include "core/queue.h"
#include <stdexcept>
#include <chrono>
#include <thread>

namespace rabbitmq {

// Message implementation
Message::Message(const std::string& exchange, const std::string& routingKey, 
                 const void* body, size_t bodySize)
    : m_owned(true), m_deliveryTag(0) {
    m_msg = ::message_create(exchange.c_str(), routingKey.c_str(), body, bodySize);
    if (!m_msg) {
        throw std::runtime_error("Failed to create message");
    }
}

Message::Message(::Message* msg, bool owned, uint64_t deliveryTag)
    : m_msg(msg), m_owned(owned), m_deliveryTag(deliveryTag) {
    if (!m_msg) {
        throw std::runtime_error("Cannot wrap NULL message");
    }
}

Message::~Message() {
    if (m_owned && m_msg) {
        ::message_free(m_msg);
    }
}

Message::Message(Message&& other)
    : m_msg(other.m_msg), m_owned(other.m_owned), m_deliveryTag(other.m_deliveryTag) {
    other.m_msg = nullptr;
    other.m_owned = false;
    other.m_deliveryTag = 0;
}

Message& Message::operator=(Message&& other) {
    if (this != &other) {
        if (m_owned && m_msg) {
            ::message_free(m_msg);
        }
        m_msg = other.m_msg;
        m_owned = other.m_owned;
        m_deliveryTag = other.m_deliveryTag;
        other.m_msg = nullptr;
        other.m_owned = false;
        other.m_deliveryTag = 0;
    }
    return *this;
}

std::string Message::getExchange() const {
    return m_msg->exchange ? m_msg->exchange : "";
}

std::string Message::getRoutingKey() const {
    return m_msg->routing_key ? m_msg->routing_key : "";
}

const void* Message::getBody() const {
    return m_msg->body;
}

size_t Message::getBodySize() const {
    return m_msg->body_size;
}

uint32_t Message::getPriority() const {
    return m_msg->priority;
}

uint64_t Message::getExpiration() const {
    return m_msg->expiration;
}

bool Message::isPersistent() const {
    return m_msg->persistent != 0;
}

uint64_t Message::getDeliveryTag() const {
    return m_deliveryTag;
}

void Message::setPriority(uint32_t priority) {
    ::message_set_priority(m_msg, priority);
}

void Message::setExpiration(uint64_t expiration) {
    ::message_set_expiration(m_msg, expiration);
}

void Message::setPersistent(bool persistent) {
    ::message_set_persistent(m_msg, persistent ? 1 : 0);
}

::Message* Message::getHandle() const {
    return m_msg;
}

// Client implementation
struct ClientContext {
    std::shared_ptr<Consumer> consumer;
    std::promise<Message>* messagePromise = nullptr;
    std::string currentQueue;
    uint64_t lastDeliveryTag = 0;
};

void Client::onMessageCallback(void* context, ::Message* message) {
    ClientContext* clientContext = static_cast<ClientContext*>(context);
    if (!clientContext) {
        ::message_free(message);
        return;
    }

    try {
        if (clientContext->messagePromise) {
            // For get() operation
            clientContext->messagePromise->set_value(Message(message, true, clientContext->lastDeliveryTag));
            clientContext->messagePromise = nullptr;
        } else if (clientContext->consumer) {
            // For consume() operation
            clientContext->consumer->onMessage(Message(message, true, clientContext->lastDeliveryTag));
        } else {
            // No handler, free the message
            ::message_free(message);
        }
    } catch (const std::exception& ex) {
        LOG_ERROR("Exception in message handler: %s", ex.what());
        ::message_free(message);
    }
}

void Client::onConnectCallback(void* context) {
    LOG_INFO("Connected to broker");
}

void Client::onDisconnectCallback(void* context) {
    LOG_INFO("Disconnected from broker");
}

Client::Client(const ConnectionOptions& options)
    : m_conn(nullptr), m_context(nullptr), m_autoAck(options.autoAck) {
    // Initialize the C client
    m_conn = ::connection_create(options.host.c_str(), options.port);
    if (!m_conn) {
        throw std::runtime_error("Failed to create connection");
    }

    // Create context object
    m_context = new ClientContext();
    if (!m_context) {
        ::connection_free(m_conn);
        throw std::runtime_error("Failed to create client context");
    }

    // Set callbacks
    ::connection_set_on_connect(m_conn, &Client::onConnectCallback, m_context);
    ::connection_set_on_disconnect(m_conn, &Client::onDisconnectCallback, m_context);
    ::connection_set_on_message(m_conn, &Client::onMessageCallback, m_context);
}

Client::~Client() {
    disconnect();
    
    // Clean up queues
    for (auto& pair : m_queues) {
        // The broker owns the queues, so we don't free them here
    }
    m_queues.clear();
    
    if (m_conn) {
        ::connection_free(m_conn);
    }
    
    if (m_context) {
        delete static_cast<ClientContext*>(m_context);
    }
}

Client::Client(Client&& other)
    : m_conn(other.m_conn), m_consumer(std::move(other.m_consumer)), 
      m_context(other.m_context), m_autoAck(other.m_autoAck),
      m_queues(std::move(other.m_queues)) {
    other.m_conn = nullptr;
    other.m_context = nullptr;
}

Client& Client::operator=(Client&& other) {
    if (this != &other) {
        disconnect();
        
        if (m_conn) {
            ::connection_free(m_conn);
        }
        
        if (m_context) {
            delete static_cast<ClientContext*>(m_context);
        }
        
        m_conn = other.m_conn;
        m_consumer = std::move(other.m_consumer);
        m_context = other.m_context;
        m_autoAck = other.m_autoAck;
        m_queues = std::move(other.m_queues);
        
        other.m_conn = nullptr;
        other.m_context = nullptr;
    }
    return *this;
}

bool Client::connect() {
    if (!m_conn) {
        return false;
    }
    
    return ::connection_connect(m_conn) != 0;
}

void Client::disconnect() {
    if (m_conn) {
        ::connection_disconnect(m_conn);
    }
}

bool Client::isConnected() const {
    if (!m_conn) {
        return false;
    }
    
    return ::connection_get_status(m_conn) == CONNECTION_STATUS_CONNECTED;
}

void Client::declareQueue(const std::string& name, size_t capacity, bool durable) {
    // TODO: Implement with broker API
}

void Client::bindQueue(const std::string& queueName, const std::string& exchangeName, 
                       const std::string& routingKey) {
    // TODO: Implement with broker API
}

void Client::declareExchange(const std::string& name, ExchangeType type) {
    // TODO: Implement with broker API
}

void Client::publish(const std::string& exchange, const std::string& routingKey, 
                     const void* data, size_t size) {
    if (!m_conn || !isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    Message message(exchange, routingKey, data, size);
    publish(message);
}

void Client::publish(const Message& message) {
    if (!m_conn || !isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    if (!::connection_send(m_conn, message.getHandle())) {
        throw std::runtime_error("Failed to publish message");
    }
}

void Client::consume(const std::string& queueName, std::shared_ptr<Consumer> consumer) {
    if (!m_conn || !isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    ClientContext* context = static_cast<ClientContext*>(m_context);
    context->consumer = consumer;
    context->currentQueue = queueName;
    
    // Store queue reference for ack/reject operations
    // In a real implementation, we would get this from the broker
    // For now, we'll assume it exists
}

std::future<Message> Client::get(const std::string& queueName, 
                                 std::chrono::milliseconds timeout) {
    if (!m_conn || !isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    ClientContext* context = static_cast<ClientContext*>(m_context);
    context->currentQueue = queueName;
    
    // Create promise for async result
    std::promise<Message> promise;
    std::future<Message> future = promise.get_future();
    context->messagePromise = &promise;
    
    // In a real implementation, we would dequeue from the broker
    // For now, we'll just wait for a message to arrive via callback
    
    // Wait for the message with timeout
    auto status = future.wait_for(timeout);
    if (status != std::future_status::ready) {
        context->messagePromise = nullptr;
        throw std::runtime_error("Timeout waiting for message");
    }
    
    return future;
}

void Client::ack(const Message& message) {
    if (!m_conn || !isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    ClientContext* context = static_cast<ClientContext*>(m_context);
    if (context->currentQueue.empty()) {
        throw std::runtime_error("No active queue for acknowledgment");
    }
    
    // Find the queue in our map
    auto it = m_queues.find(context->currentQueue);
    if (it == m_queues.end()) {
        throw std::runtime_error("Queue not found for acknowledgment");
    }
    
    // Acknowledge the message
    if (!::queue_ack(it->second, message.getDeliveryTag())) {
        throw std::runtime_error("Failed to acknowledge message");
    }
    
    LOG_INFO("Acknowledged message with delivery tag %lu", message.getDeliveryTag());
}

void Client::reject(const Message& message, bool requeue) {
    if (!m_conn || !isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    ClientContext* context = static_cast<ClientContext*>(m_context);
    if (context->currentQueue.empty()) {
        throw std::runtime_error("No active queue for rejection");
    }
    
    // Find the queue in our map
    auto it = m_queues.find(context->currentQueue);
    if (it == m_queues.end()) {
        throw std::runtime_error("Queue not found for rejection");
    }
    
    // Reject the message
    if (!::queue_reject(it->second, message.getDeliveryTag(), requeue ? 1 : 0)) {
        throw std::runtime_error("Failed to reject message");
    }
    
    LOG_INFO("Rejected message with delivery tag %lu (requeue: %d)", 
             message.getDeliveryTag(), requeue);
}

} // namespace rabbitmq
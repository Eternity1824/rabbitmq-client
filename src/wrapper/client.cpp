#include "wrapper/client.hpp"
#include "core/log.h"
#include <stdexcept>
#include <chrono>
#include <thread>

namespace rabbitmq {

// Message implementation
Message::Message(const std::string& exchange, const std::string& routingKey, 
                 const void* body, size_t bodySize)
    : m_owned(true) {
    m_msg = ::message_create(exchange.c_str(), routingKey.c_str(), body, bodySize);
    if (!m_msg) {
        throw std::runtime_error("Failed to create message");
    }
}

Message::Message(::Message* msg, bool owned)
    : m_msg(msg), m_owned(owned) {
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
    : m_msg(other.m_msg), m_owned(other.m_owned) {
    other.m_msg = nullptr;
    other.m_owned = false;
}

Message& Message::operator=(Message&& other) {
    if (this != &other) {
        if (m_owned && m_msg) {
            ::message_free(m_msg);
        }
        m_msg = other.m_msg;
        m_owned = other.m_owned;
        other.m_msg = nullptr;
        other.m_owned = false;
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
            clientContext->messagePromise->set_value(Message(message, true));
            clientContext->messagePromise = nullptr;
        } else if (clientContext->consumer) {
            // For consume() operation
            clientContext->consumer->onMessage(Message(message, true));
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
    : m_conn(nullptr), m_context(nullptr) {
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
    
    if (m_conn) {
        ::connection_free(m_conn);
    }
    
    if (m_context) {
        delete static_cast<ClientContext*>(m_context);
    }
}

Client::Client(Client&& other)
    : m_conn(other.m_conn), m_consumer(std::move(other.m_consumer)), m_context(other.m_context) {
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
    // Not implemented in this simple version
    LOG_WARNING("declareQueue is a no-op in this implementation");
}

void Client::bindQueue(const std::string& queueName, const std::string& exchangeName, 
                       const std::string& routingKey) {
    // Not implemented in this simple version
    LOG_WARNING("bindQueue is a no-op in this implementation");
}

void Client::declareExchange(const std::string& name, ExchangeType type) {
    // Not implemented in this simple version
    LOG_WARNING("declareExchange is a no-op in this implementation");
}

void Client::publish(const std::string& exchange, const std::string& routingKey, 
                     const void* data, size_t size) {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    Message msg(exchange, routingKey, data, size);
    publish(msg);
}

void Client::publish(const Message& message) {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    if (!::connection_send(m_conn, message.getHandle())) {
        throw std::runtime_error("Failed to send message");
    }
}

void Client::consume(const std::string& queueName, std::shared_ptr<Consumer> consumer) {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    ClientContext* context = static_cast<ClientContext*>(m_context);
    context->consumer = consumer;
    
    // In a real implementation, we would send a consume command to the broker
    // For now, we just store the consumer to handle future messages
}

std::future<Message> Client::get(const std::string& queueName, 
                                 std::chrono::milliseconds timeout) {
    if (!isConnected()) {
        throw std::runtime_error("Not connected to broker");
    }
    
    ClientContext* context = static_cast<ClientContext*>(m_context);
    
    // Set up promise/future
    std::promise<Message> promise;
    std::future<Message> future = promise.get_future();
    context->messagePromise = &promise;
    
    // In a real implementation, we would send a get command to the broker
    // For now, we just wait for the next message to arrive
    
    std::thread([this, timeout, queueName, &future]() {
        if (std::future_status::ready != future.wait_for(timeout)) {
            // Timeout occurred
            ClientContext* ctx = static_cast<ClientContext*>(m_context);
            if (ctx->messagePromise) {
                try {
                    ctx->messagePromise->set_exception(
                        std::make_exception_ptr(std::runtime_error("Get operation timed out")));
                } catch (...) {
                    // Promise might have been fulfilled in the meantime
                }
                ctx->messagePromise = nullptr;
            }
        }
    }).detach();
    
    return future;
}

} // namespace rabbitmq 
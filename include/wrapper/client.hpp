#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <chrono>
#include <future>

extern "C" {
#include "core/message.h"
#include "core/connection.h"
}

namespace rabbitmq {

/**
 * C++ wrapper for Message
 */
class Message {
private:
    ::Message* m_msg;
    bool m_owned;

public:
    Message(const std::string& exchange, const std::string& routingKey, 
            const void* body, size_t bodySize);
    Message(::Message* msg, bool owned = true);
    ~Message();

    // No copy constructor, only move semantics
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
    Message(Message&& other);
    Message& operator=(Message&& other);

    // Getters
    std::string getExchange() const;
    std::string getRoutingKey() const;
    const void* getBody() const;
    size_t getBodySize() const;
    uint32_t getPriority() const;
    uint64_t getExpiration() const;
    bool isPersistent() const;

    // Setters
    void setPriority(uint32_t priority);
    void setExpiration(uint64_t expiration);
    void setPersistent(bool persistent);

    // Access the underlying C structure
    ::Message* getHandle() const;
};

/**
 * Connection options
 */
struct ConnectionOptions {
    std::string host = "localhost";
    uint16_t port = 5672;
    std::string username = "guest";
    std::string password = "guest";
    std::string vhost = "/";
    std::chrono::seconds timeout = std::chrono::seconds(30);
};

/**
 * Consumer interface
 */
class Consumer {
public:
    virtual ~Consumer() = default;
    virtual void onMessage(Message message) = 0;
};

/**
 * Client class for RabbitMQ operations
 */
class Client {
private:
    ::Connection* m_conn;
    std::shared_ptr<Consumer> m_consumer;
    void* m_context;

    static void onMessageCallback(void* context, ::Message* message);
    static void onConnectCallback(void* context);
    static void onDisconnectCallback(void* context);

public:
    Client(const ConnectionOptions& options = ConnectionOptions());
    ~Client();

    // No copy constructor, only move semantics
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&& other);
    Client& operator=(Client&& other);

    // Connection management
    bool connect();
    void disconnect();
    bool isConnected() const;

    // Queue operations
    void declareQueue(const std::string& name, size_t capacity = 1000, bool durable = false);
    void bindQueue(const std::string& queueName, const std::string& exchangeName, 
                   const std::string& routingKey = "");

    // Exchange operations
    enum class ExchangeType { Direct, Fanout, Topic };
    void declareExchange(const std::string& name, ExchangeType type);

    // Message operations
    void publish(const std::string& exchange, const std::string& routingKey, 
                 const void* data, size_t size);
    void publish(const Message& message);

    // Consume messages
    void consume(const std::string& queueName, std::shared_ptr<Consumer> consumer);
    std::future<Message> get(const std::string& queueName, 
                             std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));
};

} // namespace rabbitmq

#endif /* CLIENT_HPP */ 
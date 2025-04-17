#include "wrapper/client.hpp"
#include "core/log.h"
#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <signal.h>

// Flag to indicate if we should exit
volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    running = 0;
}

// Message consumer implementation with manual acknowledgment
class AckConsumer : public rabbitmq::Consumer {
private:
    rabbitmq::Client& m_client;
    bool m_autoAck;

public:
    AckConsumer(rabbitmq::Client& client, bool autoAck = false)
        : m_client(client), m_autoAck(autoAck) {}

    virtual void onMessage(rabbitmq::Message message) override {
        // Convert body to string for display
        std::string body(static_cast<const char*>(message.getBody()), message.getBodySize());
        
        LOG_INFO("Received message: %s", body.c_str());
        LOG_INFO("  Exchange: %s", message.getExchange().c_str());
        LOG_INFO("  Routing Key: %s", message.getRoutingKey().c_str());
        LOG_INFO("  Priority: %u", message.getPriority());
        LOG_INFO("  Persistent: %s", message.isPersistent() ? "true" : "false");
        LOG_INFO("  Delivery Tag: %lu", message.getDeliveryTag());
        
        // Process the message (simulated work)
        LOG_INFO("Processing message...");
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Simulate processing time
        
        // Randomly decide whether to acknowledge or reject the message
        // In a real application, this would be based on the success of processing
        bool success = (rand() % 10) < 8; // 80% success rate
        
        if (!m_autoAck) {
            if (success) {
                // Acknowledge the message
                try {
                    m_client.ack(message);
                    LOG_INFO("Message acknowledged successfully");
                } catch (const std::exception& ex) {
                    LOG_ERROR("Failed to acknowledge message: %s", ex.what());
                }
            } else {
                // Reject the message and requeue it
                try {
                    bool requeue = true;
                    m_client.reject(message, requeue);
                    LOG_INFO("Message rejected and requeued");
                } catch (const std::exception& ex) {
                    LOG_ERROR("Failed to reject message: %s", ex.what());
                }
            }
        } else {
            LOG_INFO("Auto-acknowledgment mode - no manual ack required");
        }
    }
};

int main(int argc, char* argv[]) {
    // Initialize logging
    log_set_level(LOG_LEVEL_INFO);
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Seed random number generator
    srand(time(NULL));
    
    // Parse command line arguments
    std::string host = "localhost";
    uint16_t port = 5672;
    std::string queueName = "test";
    bool autoAck = false;
    // 强制使用默认参数，忽略命令行输入
    // if (argc > 1) host = argv[1];
    // if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));
    // if (argc > 3) queueName = argv[3];
    // if (argc > 4) autoAck = (std::string(argv[4]) == "true" || std::string(argv[4]) == "1");
    
    try {
        // Create connection options
        rabbitmq::ConnectionOptions options;
        options.host = host;
        options.port = port;
        options.autoAck = autoAck;
        
        // Create client
        rabbitmq::Client client(options);
        
        LOG_INFO("Connecting to %s:%d...", host.c_str(), port);
        if (!client.connect()) {
            LOG_ERROR("Failed to connect to broker");
            return 1;
        }
        
        // Declare queue
        LOG_INFO("Registering as consumer for queue '%s'", queueName.c_str());
        rabbitmq::Message registerMsg("consumer", "register", queueName.c_str(), queueName.length());
        try {
            client.publish(registerMsg);
            LOG_INFO("Consumer registration message sent");
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to register as consumer: %s", ex.what());
            return 1;
        }
        
        // Create message consumer with acknowledgment
        auto consumer = std::make_shared<AckConsumer>(client, autoAck);
        
        // Start consuming messages
        LOG_INFO("Consuming messages from queue '%s' (autoAck: %s)...", 
                queueName.c_str(), autoAck ? "true" : "false");
        client.consume(queueName, consumer);
        
        // Wait until signal is received
        LOG_INFO("Press Ctrl+C to exit");
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Disconnect
        client.disconnect();
        LOG_INFO("Disconnected from broker");
        
        return 0;
    } catch (const std::exception& ex) {
        LOG_ERROR("Exception: %s", ex.what());
        return 1;
    }
}

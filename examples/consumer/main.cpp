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

// Message consumer implementation
class MessageConsumer : public rabbitmq::Consumer {
public:
    virtual void onMessage(rabbitmq::Message message) override {
        // Convert body to string for display
        std::string body(static_cast<const char*>(message.getBody()), message.getBodySize());
        
        LOG_INFO("Received message: %s", body.c_str());
        LOG_INFO("  Exchange: %s", message.getExchange().c_str());
        LOG_INFO("  Routing Key: %s", message.getRoutingKey().c_str());
        LOG_INFO("  Priority: %u", message.getPriority());
        LOG_INFO("  Persistent: %s", message.isPersistent() ? "true" : "false");
    }
};

int main(int argc, char* argv[]) {
    // Initialize logging
    log_set_level(LOG_LEVEL_INFO);
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Parse command line arguments
    std::string host = "localhost";
    uint16_t port = 5672;
    std::string queueName = "test";
    
    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));
    if (argc > 3) queueName = argv[3];
    
    try {
        // Create connection options
        rabbitmq::ConnectionOptions options;
        options.host = host;
        options.port = port;
        
        // Create client
        rabbitmq::Client client(options);
        
        LOG_INFO("Connecting to %s:%d...", host.c_str(), port);
        if (!client.connect()) {
            LOG_ERROR("Failed to connect to broker");
            return 1;
        }
        
        // Create message consumer
        auto consumer = std::make_shared<MessageConsumer>();
        
        // Start consuming messages
        LOG_INFO("Consuming messages from queue '%s'...", queueName.c_str());
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
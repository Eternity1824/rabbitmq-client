#include "wrapper/client.hpp"
#include "core/log.h"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
    // Initialize logging
    log_set_level(LOG_LEVEL_INFO);
    
    // Parse command line arguments
    std::string host = "localhost";
    uint16_t port = 5672;
    std::string exchange = "amq.direct";
    std::string routingKey = "test";
    // if (argc > 1) host = argv[1];
    // if (argc > 2) port = static_cast<uint16_t>(std::stoi(argv[2]));
    // if (argc > 3) exchange = argv[3];
    // if (argc > 4) routingKey = argv[4];
    
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
        
        // Send 10 messages
        for (int i = 0; i < 10; i++) {
            std::string message = "Message #" + std::to_string(i);
            LOG_INFO("Sending message: %s", message.c_str());
            
            try {
                client.publish(exchange, routingKey, message.c_str(), message.length());
                LOG_INFO("Message sent successfully");
            } catch (const std::exception& ex) {
                LOG_ERROR("Failed to send message: %s", ex.what());
            }
            
            // Sleep for a bit
            std::this_thread::sleep_for(std::chrono::seconds(1));
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
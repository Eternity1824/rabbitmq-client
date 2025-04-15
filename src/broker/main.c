#include "core/broker.h"
#include "core/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

// Global broker instance
static Broker* g_broker = NULL;

// Flag to indicate if we should exit
static volatile sig_atomic_t running = 1;

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    running = 0;
}

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -h <host>      Host to bind to (default: 0.0.0.0)\n");
    printf("  -p <port>      Port to listen on (default: 5672)\n");
    printf("  -v             Verbose output (debug logging)\n");
    printf("  -q <count>     Maximum number of queues (default: 16)\n");
    printf("  -e <count>     Maximum number of exchanges (default: 8)\n");
    printf("  -b <count>     Maximum number of bindings (default: 32)\n");
    printf("  -s <size>      Default queue size (default: 1000)\n");
    printf("  -f <file>      Log to file instead of stdout\n");
    printf("  -h, --help     Display this help message\n");
}

int main(int argc, char* argv[]) {
    // Default configuration
    const char* host = "0.0.0.0";
    int port = 5672;
    int verbose = 0;
    const char* log_file = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Set up logging
    log_set_level(verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);
    if (log_file) {
        FILE* log_fp = fopen(log_file, "a");
        if (log_fp) {
            log_set_output(log_fp);
        } else {
            fprintf(stderr, "Failed to open log file: %s\n", log_file);
            return 1;
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create broker
    g_broker = broker_create();
    if (!g_broker) {
        LOG_FATAL("Failed to create broker");
        return 1;
    }
    
    // Create default exchange
    Exchange* direct_exchange = broker_create_exchange(g_broker, "amq.direct", EXCHANGE_TYPE_DIRECT);
    if (!direct_exchange) {
        LOG_ERROR("Failed to create direct exchange");
    }
    
    Exchange* fanout_exchange = broker_create_exchange(g_broker, "amq.fanout", EXCHANGE_TYPE_FANOUT);
    if (!fanout_exchange) {
        LOG_ERROR("Failed to create fanout exchange");
    }
    
    Exchange* topic_exchange = broker_create_exchange(g_broker, "amq.topic", EXCHANGE_TYPE_TOPIC);
    if (!topic_exchange) {
        LOG_ERROR("Failed to create topic exchange");
    }
    
    // Create test queue
    Queue* test_queue = broker_create_queue(g_broker, "test", 1000);
    if (!test_queue) {
        LOG_ERROR("Failed to create test queue");
    } else {
        // Bind test queue to all exchanges
        if (direct_exchange) {
            broker_bind_queue(g_broker, direct_exchange, test_queue, "test");
        }
        if (fanout_exchange) {
            broker_bind_queue(g_broker, fanout_exchange, test_queue, NULL);
        }
        if (topic_exchange) {
            broker_bind_queue(g_broker, topic_exchange, test_queue, "test.#");
        }
    }
    
    // Start broker
    LOG_INFO("Starting broker on %s:%d", host, port);
    if (!broker_start(g_broker)) {
        LOG_FATAL("Failed to start broker");
        broker_free(g_broker);
        return 1;
    }
    
    LOG_INFO("Broker started successfully");
    LOG_INFO("Press Ctrl+C to exit");
    
    // Main loop
    while (running) {
        // Sleep to avoid busy waiting
        sleep(1);
    }
    
    // Cleanup
    LOG_INFO("Shutting down broker...");
    broker_stop(g_broker);
    broker_free(g_broker);
    LOG_INFO("Broker shutdown complete");
    
    return 0;
} 
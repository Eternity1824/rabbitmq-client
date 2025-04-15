#include "core/broker.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// Event loop sleep time in milliseconds
#define EVENT_LOOP_SLEEP_MS 100

// Forward declarations
static void* broker_event_loop(void* arg);
static int broker_route_message(Broker* broker, Exchange* exchange, Message* message);
static int match_topic(const char* pattern, const char* topic);

Broker* broker_create() {
    Broker* broker = (Broker*)malloc(sizeof(Broker));
    if (!broker) {
        LOG_ERROR("Failed to allocate memory for broker");
        return NULL;
    }

    // Initialize broker properties
    memset(broker->queues, 0, sizeof(broker->queues));
    memset(broker->exchanges, 0, sizeof(broker->exchanges));
    memset(broker->bindings, 0, sizeof(broker->bindings));
    broker->queue_count = 0;
    broker->exchange_count = 0;
    broker->binding_count = 0;
    broker->running = 0;

    // Initialize mutex
    if (pthread_mutex_init(&broker->mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize broker mutex");
        free(broker);
        return NULL;
    }

    LOG_INFO("Broker created successfully");
    return broker;
}

void broker_free(Broker* broker) {
    if (!broker) {
        return;
    }

    // Stop the broker if it's running
    if (broker->running) {
        broker_stop(broker);
    }

    pthread_mutex_lock(&broker->mutex);

    // Free queues
    for (size_t i = 0; i < broker->queue_count; i++) {
        queue_free(broker->queues[i]);
    }

    // Free exchanges
    for (size_t i = 0; i < broker->exchange_count; i++) {
        free(broker->exchanges[i]->name);
        free(broker->exchanges[i]);
    }

    // Free bindings
    for (size_t i = 0; i < broker->binding_count; i++) {
        free(broker->bindings[i]->routing_key);
        free(broker->bindings[i]);
    }

    pthread_mutex_unlock(&broker->mutex);
    pthread_mutex_destroy(&broker->mutex);

    free(broker);
    LOG_INFO("Broker freed successfully");
}

int broker_start(Broker* broker) {
    if (!broker) {
        LOG_ERROR("Cannot start NULL broker");
        return 0;
    }

    pthread_mutex_lock(&broker->mutex);
    if (broker->running) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_WARNING("Broker is already running");
        return 1;
    }

    broker->running = 1;
    pthread_mutex_unlock(&broker->mutex);

    // Create event loop thread
    if (pthread_create(&broker->event_loop_thread, NULL, broker_event_loop, broker) != 0) {
        pthread_mutex_lock(&broker->mutex);
        broker->running = 0;
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Failed to create broker event loop thread");
        return 0;
    }

    LOG_INFO("Broker started successfully");
    return 1;
}

int broker_stop(Broker* broker) {
    if (!broker) {
        LOG_ERROR("Cannot stop NULL broker");
        return 0;
    }

    pthread_mutex_lock(&broker->mutex);
    if (!broker->running) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_WARNING("Broker is not running");
        return 1;
    }

    broker->running = 0;
    pthread_mutex_unlock(&broker->mutex);

    // Wait for event loop thread to terminate
    pthread_join(broker->event_loop_thread, NULL);

    LOG_INFO("Broker stopped successfully");
    return 1;
}

Queue* broker_create_queue(Broker* broker, const char* name, size_t capacity) {
    if (!broker || !name) {
        LOG_ERROR("Invalid parameters for broker_create_queue");
        return NULL;
    }

    pthread_mutex_lock(&broker->mutex);

    // Check if queue already exists
    for (size_t i = 0; i < broker->queue_count; i++) {
        if (strcmp(broker->queues[i]->name, name) == 0) {
            Queue* existing_queue = broker->queues[i];
            pthread_mutex_unlock(&broker->mutex);
            LOG_INFO("Queue '%s' already exists", name);
            return existing_queue;
        }
    }

    // Check if we can add more queues
    if (broker->queue_count >= MAX_QUEUES) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Maximum number of queues reached");
        return NULL;
    }

    // Create the queue
    Queue* queue = queue_create(name, capacity);
    if (!queue) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Failed to create queue '%s'", name);
        return NULL;
    }

    // Add the queue to the broker
    broker->queues[broker->queue_count++] = queue;

    pthread_mutex_unlock(&broker->mutex);
    LOG_INFO("Queue '%s' created with capacity %zu", name, capacity);
    return queue;
}

Queue* broker_get_queue(Broker* broker, const char* name) {
    if (!broker || !name) {
        LOG_ERROR("Invalid parameters for broker_get_queue");
        return NULL;
    }

    pthread_mutex_lock(&broker->mutex);

    Queue* found_queue = NULL;
    for (size_t i = 0; i < broker->queue_count; i++) {
        if (strcmp(broker->queues[i]->name, name) == 0) {
            found_queue = broker->queues[i];
            break;
        }
    }

    pthread_mutex_unlock(&broker->mutex);

    if (!found_queue) {
        LOG_WARNING("Queue '%s' not found", name);
    }

    return found_queue;
}

Exchange* broker_create_exchange(Broker* broker, const char* name, ExchangeType type) {
    if (!broker || !name) {
        LOG_ERROR("Invalid parameters for broker_create_exchange");
        return NULL;
    }

    pthread_mutex_lock(&broker->mutex);

    // Check if exchange already exists
    for (size_t i = 0; i < broker->exchange_count; i++) {
        if (strcmp(broker->exchanges[i]->name, name) == 0) {
            Exchange* existing_exchange = broker->exchanges[i];
            pthread_mutex_unlock(&broker->mutex);
            LOG_INFO("Exchange '%s' already exists", name);
            return existing_exchange;
        }
    }

    // Check if we can add more exchanges
    if (broker->exchange_count >= MAX_EXCHANGES) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Maximum number of exchanges reached");
        return NULL;
    }

    // Create the exchange
    Exchange* exchange = (Exchange*)malloc(sizeof(Exchange));
    if (!exchange) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Failed to allocate memory for exchange");
        return NULL;
    }

    exchange->name = strdup(name);
    exchange->type = type;

    // Add the exchange to the broker
    broker->exchanges[broker->exchange_count++] = exchange;

    pthread_mutex_unlock(&broker->mutex);
    LOG_INFO("Exchange '%s' created with type %d", name, type);
    return exchange;
}

Exchange* broker_get_exchange(Broker* broker, const char* name) {
    if (!broker || !name) {
        LOG_ERROR("Invalid parameters for broker_get_exchange");
        return NULL;
    }

    pthread_mutex_lock(&broker->mutex);

    Exchange* found_exchange = NULL;
    for (size_t i = 0; i < broker->exchange_count; i++) {
        if (strcmp(broker->exchanges[i]->name, name) == 0) {
            found_exchange = broker->exchanges[i];
            break;
        }
    }

    pthread_mutex_unlock(&broker->mutex);

    if (!found_exchange) {
        LOG_WARNING("Exchange '%s' not found", name);
    }

    return found_exchange;
}

int broker_bind_queue(Broker* broker, Exchange* exchange, Queue* queue, const char* routing_key) {
    if (!broker || !exchange || !queue) {
        LOG_ERROR("Invalid parameters for broker_bind_queue");
        return 0;
    }

    pthread_mutex_lock(&broker->mutex);

    // Check if we can add more bindings
    if (broker->binding_count >= MAX_BINDINGS) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Maximum number of bindings reached");
        return 0;
    }

    // Create the binding
    Binding* binding = (Binding*)malloc(sizeof(Binding));
    if (!binding) {
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Failed to allocate memory for binding");
        return 0;
    }

    binding->exchange = exchange;
    binding->queue = queue;
    binding->routing_key = routing_key ? strdup(routing_key) : NULL;

    // Add the binding to the broker
    broker->bindings[broker->binding_count++] = binding;

    pthread_mutex_unlock(&broker->mutex);
    LOG_INFO("Queue '%s' bound to exchange '%s' with routing key '%s'",
             queue->name, exchange->name, routing_key ? routing_key : "(none)");
    return 1;
}

int broker_publish(Broker* broker, Exchange* exchange, Message* message) {
    if (!broker || !exchange || !message) {
        LOG_ERROR("Invalid parameters for broker_publish");
        return 0;
    }

    // Clone the message for routing
    Message* msg_clone = message_clone(message);
    if (!msg_clone) {
        LOG_ERROR("Failed to clone message for routing");
        return 0;
    }

    LOG_DEBUG("Publishing message to exchange '%s' with routing key '%s'",
              exchange->name, message->routing_key);

    int routed = broker_route_message(broker, exchange, msg_clone);
    if (!routed) {
        LOG_WARNING("Message could not be routed to any queue");
        message_free(msg_clone);
        return 0;
    }

    return 1;
}

// Private function to handle message routing
static int broker_route_message(Broker* broker, Exchange* exchange, Message* message) {
    pthread_mutex_lock(&broker->mutex);

    int routed = 0;
    
    // Route based on exchange type
    switch (exchange->type) {
        case EXCHANGE_TYPE_FANOUT:
            // Route to all bound queues
            for (size_t i = 0; i < broker->binding_count; i++) {
                Binding* binding = broker->bindings[i];
                if (binding->exchange == exchange) {
                    Message* msg_clone = message_clone(message);
                    if (msg_clone) {
                        if (queue_try_enqueue(binding->queue, msg_clone)) {
                            routed++;
                            LOG_DEBUG("Message routed to queue '%s' via fanout exchange",
                                      binding->queue->name);
                        } else {
                            message_free(msg_clone);
                            LOG_WARNING("Failed to enqueue message to queue '%s'",
                                        binding->queue->name);
                        }
                    }
                }
            }
            break;

        case EXCHANGE_TYPE_DIRECT:
            // Route to queues with matching routing key
            for (size_t i = 0; i < broker->binding_count; i++) {
                Binding* binding = broker->bindings[i];
                if (binding->exchange == exchange && 
                    binding->routing_key && 
                    strcmp(binding->routing_key, message->routing_key) == 0) {
                    Message* msg_clone = message_clone(message);
                    if (msg_clone) {
                        if (queue_try_enqueue(binding->queue, msg_clone)) {
                            routed++;
                            LOG_DEBUG("Message routed to queue '%s' via direct exchange",
                                      binding->queue->name);
                        } else {
                            message_free(msg_clone);
                            LOG_WARNING("Failed to enqueue message to queue '%s'",
                                        binding->queue->name);
                        }
                    }
                }
            }
            break;

        case EXCHANGE_TYPE_TOPIC:
            // Route to queues with matching topic pattern
            for (size_t i = 0; i < broker->binding_count; i++) {
                Binding* binding = broker->bindings[i];
                if (binding->exchange == exchange && 
                    binding->routing_key && 
                    match_topic(binding->routing_key, message->routing_key)) {
                    Message* msg_clone = message_clone(message);
                    if (msg_clone) {
                        if (queue_try_enqueue(binding->queue, msg_clone)) {
                            routed++;
                            LOG_DEBUG("Message routed to queue '%s' via topic exchange",
                                      binding->queue->name);
                        } else {
                            message_free(msg_clone);
                            LOG_WARNING("Failed to enqueue message to queue '%s'",
                                        binding->queue->name);
                        }
                    }
                }
            }
            break;
    }

    // Free the original message if no routing happened
    if (routed == 0) {
        message_free(message);
    } else if (routed == 1) {
        // If only routed to one queue, we can use the original message
        // So free any clones made during routing
    } else {
        // Message was cloned for each route, so free the original
        message_free(message);
    }

    pthread_mutex_unlock(&broker->mutex);
    return routed;
}

// Event loop function for broker
static void* broker_event_loop(void* arg) {
    Broker* broker = (Broker*)arg;
    LOG_INFO("Broker event loop started");

    while (1) {
        // Check if broker is still running
        pthread_mutex_lock(&broker->mutex);
        int running = broker->running;
        pthread_mutex_unlock(&broker->mutex);

        if (!running) {
            break;
        }

        // Sleep for a bit to avoid busy waiting
        usleep(EVENT_LOOP_SLEEP_MS * 1000);
    }

    LOG_INFO("Broker event loop terminated");
    return NULL;
}

// Topic pattern matching (simple implementation)
static int match_topic(const char* pattern, const char* topic) {
    // Simple wildcard matching for topic exchanges
    // '*' matches exactly one word
    // '#' matches zero or more words
    
    // For simplicity, we'll just check if the pattern is "*" or "#"
    // or if it's an exact match
    if (strcmp(pattern, "#") == 0) {
        return 1;  // Match everything
    }
    
    if (strcmp(pattern, "*") == 0) {
        // Check if topic has no dots (single word)
        return strchr(topic, '.') == NULL;
    }
    
    // Check for exact match
    return strcmp(pattern, topic) == 0;
} 
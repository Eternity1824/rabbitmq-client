#ifndef BROKER_H
#define BROKER_H

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include "core/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum number of queues and exchanges the broker can manage
 */
#define MAX_QUEUES 16
#define MAX_EXCHANGES 8
#define MAX_BINDINGS 32

/**
 * Exchange types
 */
typedef enum {
    EXCHANGE_TYPE_DIRECT,
    EXCHANGE_TYPE_FANOUT,
    EXCHANGE_TYPE_TOPIC
} ExchangeType;

/**
 * Exchange structure
 */
typedef struct Exchange {
    char* name;
    ExchangeType type;
} Exchange;

/**
 * Binding between exchange and queue
 */
typedef struct Binding {
    Exchange* exchange;
    Queue* queue;
    char* routing_key;   // Used for direct and topic exchanges
} Binding;

/**
 * Broker structure
 */
typedef struct Broker {
    Queue* queues[MAX_QUEUES];
    Exchange* exchanges[MAX_EXCHANGES];
    Binding* bindings[MAX_BINDINGS];
    size_t queue_count;
    size_t exchange_count;
    size_t binding_count;
    pthread_mutex_t mutex;
    uint8_t running;
    pthread_t event_loop_thread;
} Broker;

/**
 * Create a new broker instance
 */
Broker* broker_create();

/**
 * Free broker and all associated resources
 */
void broker_free(Broker* broker);

/**
 * Start the broker event loop
 */
int broker_start(Broker* broker);

/**
 * Stop the broker event loop
 */
int broker_stop(Broker* broker);

/**
 * Create a new queue in the broker
 */
Queue* broker_create_queue(Broker* broker, const char* name, size_t capacity);

/**
 * Get an existing queue by name
 */
Queue* broker_get_queue(Broker* broker, const char* name);

/**
 * Create a new exchange in the broker
 */
Exchange* broker_create_exchange(Broker* broker, const char* name, ExchangeType type);

/**
 * Get an existing exchange by name
 */
Exchange* broker_get_exchange(Broker* broker, const char* name);

/**
 * Bind a queue to an exchange with an optional routing key
 */
int broker_bind_queue(Broker* broker, Exchange* exchange, Queue* queue, const char* routing_key);

/**
 * Publish a message to an exchange
 */
int broker_publish(Broker* broker, Exchange* exchange, Message* message);

#ifdef __cplusplus
}
#endif

#endif /* BROKER_H */ 
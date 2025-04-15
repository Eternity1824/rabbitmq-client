#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Message structure used for storing and transmitting data
 * between producer and consumer via broker
 */
typedef struct Message {
    char* exchange;          // Exchange name
    char* routing_key;       // Routing key for message
    void* body;              // Message payload
    size_t body_size;        // Size of message payload
    uint32_t priority;       // Message priority (0-9)
    uint64_t expiration;     // Message expiration time (milliseconds)
    uint8_t persistent;      // Message persistence flag (0 or 1)
} Message;

/**
 * Create a new message
 */
Message* message_create(const char* exchange, const char* routing_key, 
                        const void* body, size_t body_size);

/**
 * Free message and all associated memory
 */
void message_free(Message* message);

/**
 * Clone a message
 */
Message* message_clone(const Message* message);

/**
 * Set message properties
 */
void message_set_priority(Message* message, uint32_t priority);
void message_set_expiration(Message* message, uint64_t expiration);
void message_set_persistent(Message* message, uint8_t persistent);

#ifdef __cplusplus
}
#endif

#endif /* MESSAGE_H */ 
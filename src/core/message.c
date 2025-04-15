#include "core/message.h"
#include <stdlib.h>
#include <string.h>

Message* message_create(const char* exchange, const char* routing_key, 
                      const void* body, size_t body_size) {
    if (!exchange || !routing_key || (!body && body_size > 0)) {
        return NULL;
    }

    Message* message = (Message*)malloc(sizeof(Message));
    if (!message) {
        return NULL;
    }

    // Initialize with defaults
    message->exchange = strdup(exchange);
    message->routing_key = strdup(routing_key);
    message->priority = 0;
    message->expiration = 0;  // No expiration by default
    message->persistent = 0;  // Non-persistent by default

    // Copy message body
    if (body_size > 0) {
        message->body = malloc(body_size);
        if (!message->body) {
            free(message->exchange);
            free(message->routing_key);
            free(message);
            return NULL;
        }
        memcpy(message->body, body, body_size);
        message->body_size = body_size;
    } else {
        message->body = NULL;
        message->body_size = 0;
    }

    return message;
}

void message_free(Message* message) {
    if (!message) {
        return;
    }

    free(message->exchange);
    free(message->routing_key);
    free(message->body);
    free(message);
}

Message* message_clone(const Message* message) {
    if (!message) {
        return NULL;
    }

    Message* clone = message_create(message->exchange, message->routing_key, 
                                  message->body, message->body_size);
    if (!clone) {
        return NULL;
    }

    // Copy properties
    clone->priority = message->priority;
    clone->expiration = message->expiration;
    clone->persistent = message->persistent;

    return clone;
}

void message_set_priority(Message* message, uint32_t priority) {
    if (message) {
        // RabbitMQ priorities range from 0-9
        message->priority = priority > 9 ? 9 : priority;
    }
}

void message_set_expiration(Message* message, uint64_t expiration) {
    if (message) {
        message->expiration = expiration;
    }
}

void message_set_persistent(Message* message, uint8_t persistent) {
    if (message) {
        message->persistent = persistent ? 1 : 0;
    }
} 
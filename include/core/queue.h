#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include "core/message.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Queue structure for managing messages
 */
typedef struct Queue {
    char* name;                  // Queue name
    Message** messages;          // Array of message pointers
    size_t capacity;             // Maximum queue capacity
    size_t size;                 // Current number of messages
    size_t head;                 // Index of the first message
    size_t tail;                 // Index of the next empty slot
    pthread_mutex_t mutex;       // Mutex for thread-safe operations
    pthread_cond_t not_empty;    // Condition for waiting when queue is empty
    pthread_cond_t not_full;     // Condition for waiting when queue is full
    uint8_t durable;             // Whether queue persists after restart
} Queue;

/**
 * Create a new queue with given name and capacity
 */
Queue* queue_create(const char* name, size_t capacity);

/**
 * Free queue and all associated memory
 */
void queue_free(Queue* queue);

/**
 * Enqueue a message (blocks if queue is full)
 */
int queue_enqueue(Queue* queue, Message* message);

/**
 * Dequeue a message (blocks if queue is empty)
 */
Message* queue_dequeue(Queue* queue);

/**
 * Try to enqueue a message (non-blocking)
 * Returns 1 on success, 0 if queue is full
 */
int queue_try_enqueue(Queue* queue, Message* message);

/**
 * Try to dequeue a message (non-blocking)
 * Returns message on success, NULL if queue is empty
 */
Message* queue_try_dequeue(Queue* queue);

/**
 * Get current queue size
 */
size_t queue_size(Queue* queue);

/**
 * Set queue durability
 */
void queue_set_durable(Queue* queue, uint8_t durable);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */ 
#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "core/message.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Message delivery status
 */
typedef enum {
    MESSAGE_STATUS_READY,       // Message is ready to be delivered
    MESSAGE_STATUS_DELIVERED,   // Message has been delivered but not acknowledged
    MESSAGE_STATUS_ACKNOWLEDGED // Message has been acknowledged
} MessageStatus;

/**
 * Structure to track message delivery status
 */
typedef struct DeliveryInfo {
    Message* message;           // Pointer to the message
    uint64_t delivery_tag;     // Unique delivery tag
    time_t delivery_time;      // Time when the message was delivered
    MessageStatus status;      // Current status of the message
} DeliveryInfo;

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
    
    // For message acknowledgment
    DeliveryInfo* unacked_messages; // Array of unacknowledged messages
    size_t unacked_capacity;     // Capacity of unacked_messages array
    size_t unacked_count;        // Number of unacknowledged messages
    uint64_t next_delivery_tag;  // Next delivery tag to assign
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
 * With acknowledgment enabled, the message is not removed from the queue
 * until it is acknowledged with queue_ack
 */
Message* queue_dequeue(Queue* queue, uint64_t* delivery_tag);

/**
 * Try to enqueue a message (non-blocking)
 * Returns 1 on success, 0 if queue is full
 */
int queue_try_enqueue(Queue* queue, Message* message);

/**
 * Try to dequeue a message (non-blocking)
 * Returns message on success, NULL if queue is empty
 * With acknowledgment enabled, the message is not removed from the queue
 * until it is acknowledged with queue_ack
 */
Message* queue_try_dequeue(Queue* queue, uint64_t* delivery_tag);

/**
 * Acknowledge a message with the given delivery tag
 * Returns 1 on success, 0 if the delivery tag is not found
 */
int queue_ack(Queue* queue, uint64_t delivery_tag);

/**
 * Reject a message with the given delivery tag
 * If requeue is 1, the message will be requeued
 * Returns 1 on success, 0 if the delivery tag is not found
 */
int queue_reject(Queue* queue, uint64_t delivery_tag, int requeue);

/**
 * Process unacknowledged messages that have timed out
 * Requeues messages that have been delivered but not acknowledged
 * within the timeout period (in seconds)
 */
void queue_process_timeouts(Queue* queue, int timeout_seconds);

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
#include "core/queue.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

// Default unacknowledged messages capacity
#define DEFAULT_UNACKED_CAPACITY 100

Queue* queue_create(const char* name, size_t capacity) {
    if (!name || capacity == 0) {
        return NULL;
    }

    Queue* queue = (Queue*)malloc(sizeof(Queue));
    if (!queue) {
        return NULL;
    }

    // Initialize queue properties
    queue->name = strdup(name);
    queue->capacity = capacity;
    queue->size = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->durable = 0;

    // Allocate messages array
    queue->messages = (Message**)malloc(capacity * sizeof(Message*));
    if (!queue->messages) {
        free(queue->name);
        free(queue);
        return NULL;
    }

    // Initialize acknowledgment tracking
    queue->unacked_capacity = DEFAULT_UNACKED_CAPACITY;
    queue->unacked_count = 0;
    queue->next_delivery_tag = 1; // Start from 1
    queue->unacked_messages = (DeliveryInfo*)malloc(queue->unacked_capacity * sizeof(DeliveryInfo));
    if (!queue->unacked_messages) {
        free(queue->messages);
        free(queue->name);
        free(queue);
        return NULL;
    }

    // Initialize mutex and condition variables
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->unacked_messages);
        free(queue->messages);
        free(queue->name);
        free(queue);
        return NULL;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->unacked_messages);
        free(queue->messages);
        free(queue->name);
        free(queue);
        return NULL;
    }

    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->unacked_messages);
        free(queue->messages);
        free(queue->name);
        free(queue);
        return NULL;
    }

    LOG_INFO("Queue '%s' created with capacity %zu", name, capacity);
    return queue;
}

void queue_free(Queue* queue) {
    if (!queue) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);

    // Free all messages in the queue
    for (size_t i = 0; i < queue->size; i++) {
        size_t index = (queue->head + i) % queue->capacity;
        message_free(queue->messages[index]);
    }

    // Free all unacknowledged messages
    for (size_t i = 0; i < queue->unacked_count; i++) {
        if (queue->unacked_messages[i].status != MESSAGE_STATUS_ACKNOWLEDGED) {
            message_free(queue->unacked_messages[i].message);
        }
    }

    pthread_mutex_unlock(&queue->mutex);

    // Destroy synchronization primitives
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);

    // Free queue resources
    free(queue->name);
    free(queue->messages);
    free(queue->unacked_messages);
    free(queue);

    LOG_INFO("Queue freed");
}

int queue_enqueue(Queue* queue, Message* message) {
    if (!queue || !message) {
        return 0;
    }

    pthread_mutex_lock(&queue->mutex);

    // Wait until queue is not full
    while (queue->size == queue->capacity) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    // Add message to queue
    queue->messages[queue->tail] = message;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;

    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

// Helper function to add a message to the unacknowledged list
static int add_to_unacked(Queue* queue, Message* message, uint64_t* delivery_tag) {
    // Check if we need to resize the unacked messages array
    if (queue->unacked_count >= queue->unacked_capacity) {
        size_t new_capacity = queue->unacked_capacity * 2;
        DeliveryInfo* new_array = (DeliveryInfo*)realloc(queue->unacked_messages, 
                                                       new_capacity * sizeof(DeliveryInfo));
        if (!new_array) {
            return 0;
        }
        queue->unacked_messages = new_array;
        queue->unacked_capacity = new_capacity;
    }

    // Add the message to the unacked list
    DeliveryInfo* info = &queue->unacked_messages[queue->unacked_count++];
    info->message = message;
    info->delivery_tag = queue->next_delivery_tag++;
    info->delivery_time = time(NULL);
    info->status = MESSAGE_STATUS_DELIVERED;

    // Return the delivery tag to the caller
    if (delivery_tag) {
        *delivery_tag = info->delivery_tag;
    }

    return 1;
}

Message* queue_dequeue(Queue* queue, uint64_t* delivery_tag) {
    if (!queue) {
        return NULL;
    }

    pthread_mutex_lock(&queue->mutex);

    // Wait until queue is not empty
    while (queue->size == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    // Get message from queue
    Message* message = queue->messages[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;

    // Add to unacknowledged messages list
    if (!add_to_unacked(queue, message, delivery_tag)) {
        // If we can't track the message, put it back in the queue
        queue->head = (queue->head + queue->capacity - 1) % queue->capacity;
        queue->size++;
        pthread_mutex_unlock(&queue->mutex);
        LOG_ERROR("Failed to add message to unacknowledged list");
        return NULL;
    }

    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    return message;
}

int queue_try_enqueue(Queue* queue, Message* message) {
    if (!queue || !message) {
        return 0;
    }

    pthread_mutex_lock(&queue->mutex);

    // Check if queue is full
    if (queue->size == queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    // Add message to queue
    queue->messages[queue->tail] = message;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;

    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

Message* queue_try_dequeue(Queue* queue, uint64_t* delivery_tag) {
    if (!queue) {
        return NULL;
    }

    pthread_mutex_lock(&queue->mutex);

    // Check if queue is empty
    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    // Get message from queue
    Message* message = queue->messages[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;

    // Add to unacknowledged messages list
    if (!add_to_unacked(queue, message, delivery_tag)) {
        // If we can't track the message, put it back in the queue
        queue->head = (queue->head + queue->capacity - 1) % queue->capacity;
        queue->size++;
        pthread_mutex_unlock(&queue->mutex);
        LOG_ERROR("Failed to add message to unacknowledged list");
        return NULL;
    }

    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    return message;
}

int queue_ack(Queue* queue, uint64_t delivery_tag) {
    if (!queue) {
        return 0;
    }

    pthread_mutex_lock(&queue->mutex);
    int found = 0;

    // Find the message with the given delivery tag
    for (size_t i = 0; i < queue->unacked_count; i++) {
        if (queue->unacked_messages[i].delivery_tag == delivery_tag) {
            // Mark as acknowledged
            queue->unacked_messages[i].status = MESSAGE_STATUS_ACKNOWLEDGED;
            // Free the message
            message_free(queue->unacked_messages[i].message);
            found = 1;
            
            LOG_INFO("Message with delivery tag %lu acknowledged", delivery_tag);
            break;
        }
    }

    // Clean up acknowledged messages from the unacked list
    if (found) {
        size_t new_count = 0;
        for (size_t i = 0; i < queue->unacked_count; i++) {
            if (queue->unacked_messages[i].status != MESSAGE_STATUS_ACKNOWLEDGED) {
                if (i != new_count) {
                    queue->unacked_messages[new_count] = queue->unacked_messages[i];
                }
                new_count++;
            }
        }
        queue->unacked_count = new_count;
    }

    pthread_mutex_unlock(&queue->mutex);
    return found;
}

int queue_reject(Queue* queue, uint64_t delivery_tag, int requeue) {
    if (!queue) {
        return 0;
    }

    pthread_mutex_lock(&queue->mutex);
    int found = 0;
    Message* message = NULL;

    // Find the message with the given delivery tag
    for (size_t i = 0; i < queue->unacked_count; i++) {
        if (queue->unacked_messages[i].delivery_tag == delivery_tag) {
            if (requeue) {
                // Keep the message for requeuing
                message = queue->unacked_messages[i].message;
            } else {
                // Free the message if not requeuing
                message_free(queue->unacked_messages[i].message);
            }
            
            // Mark as acknowledged (to remove from unacked list)
            queue->unacked_messages[i].status = MESSAGE_STATUS_ACKNOWLEDGED;
            found = 1;
            
            LOG_INFO("Message with delivery tag %lu rejected (requeue: %d)", delivery_tag, requeue);
            break;
        }
    }

    // Clean up rejected message from the unacked list
    if (found) {
        size_t new_count = 0;
        for (size_t i = 0; i < queue->unacked_count; i++) {
            if (queue->unacked_messages[i].status != MESSAGE_STATUS_ACKNOWLEDGED) {
                if (i != new_count) {
                    queue->unacked_messages[new_count] = queue->unacked_messages[i];
                }
                new_count++;
            }
        }
        queue->unacked_count = new_count;

        // Requeue the message if requested
        if (requeue && message) {
            // Check if queue is full
            if (queue->size == queue->capacity) {
                LOG_ERROR("Cannot requeue message: queue is full");
                message_free(message);
            } else {
                // Add message back to queue (at the head)
                queue->head = (queue->head + queue->capacity - 1) % queue->capacity;
                queue->messages[queue->head] = message;
                queue->size++;
                LOG_INFO("Message requeued");
            }
        }
    }

    pthread_mutex_unlock(&queue->mutex);
    return found;
}

void queue_process_timeouts(Queue* queue, int timeout_seconds) {
    if (!queue || timeout_seconds <= 0) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    time_t current_time = time(NULL);
    int requeued_count = 0;

    // Check for timed out messages
    for (size_t i = 0; i < queue->unacked_count; i++) {
        if (queue->unacked_messages[i].status == MESSAGE_STATUS_DELIVERED) {
            time_t delivery_time = queue->unacked_messages[i].delivery_time;
            if (current_time - delivery_time > timeout_seconds) {
                // Message has timed out, requeue it
                Message* message = queue->unacked_messages[i].message;
                queue->unacked_messages[i].status = MESSAGE_STATUS_ACKNOWLEDGED; // Mark for removal

                // Check if queue is full
                if (queue->size < queue->capacity) {
                    // Add message back to queue (at the head for priority)
                    queue->head = (queue->head + queue->capacity - 1) % queue->capacity;
                    queue->messages[queue->head] = message;
                    queue->size++;
                    requeued_count++;
                } else {
                    // Queue is full, can't requeue
                    LOG_ERROR("Cannot requeue timed out message: queue is full");
                    message_free(message);
                }
            }
        }
    }

    // Clean up acknowledged messages
    if (requeued_count > 0) {
        size_t new_count = 0;
        for (size_t i = 0; i < queue->unacked_count; i++) {
            if (queue->unacked_messages[i].status != MESSAGE_STATUS_ACKNOWLEDGED) {
                if (i != new_count) {
                    queue->unacked_messages[new_count] = queue->unacked_messages[i];
                }
                new_count++;
            }
        }
        queue->unacked_count = new_count;
        LOG_INFO("Requeued %d timed out messages", requeued_count);
    }

    pthread_mutex_unlock(&queue->mutex);
}

size_t queue_size(Queue* queue) {
    if (!queue) {
        return 0;
    }

    pthread_mutex_lock(&queue->mutex);
    size_t size = queue->size;
    pthread_mutex_unlock(&queue->mutex);

    return size;
}

void queue_set_durable(Queue* queue, uint8_t durable) {
    if (queue) {
        pthread_mutex_lock(&queue->mutex);
        queue->durable = durable ? 1 : 0;
        pthread_mutex_unlock(&queue->mutex);
    }
}
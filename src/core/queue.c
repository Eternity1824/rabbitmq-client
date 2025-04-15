#include "core/queue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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

    // Initialize mutex and condition variables
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->messages);
        free(queue->name);
        free(queue);
        return NULL;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->messages);
        free(queue->name);
        free(queue);
        return NULL;
    }

    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->messages);
        free(queue->name);
        free(queue);
        return NULL;
    }

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

    pthread_mutex_unlock(&queue->mutex);

    // Destroy synchronization primitives
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);

    // Free queue resources
    free(queue->name);
    free(queue->messages);
    free(queue);
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

Message* queue_dequeue(Queue* queue) {
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

Message* queue_try_dequeue(Queue* queue) {
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

    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);

    return message;
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
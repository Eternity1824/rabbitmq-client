#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include "core/message.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Connection status
 */
typedef enum {
    CONNECTION_STATUS_DISCONNECTED,
    CONNECTION_STATUS_CONNECTING,
    CONNECTION_STATUS_CONNECTED,
    CONNECTION_STATUS_FAILED
} ConnectionStatus;

/**
 * Connection callback function types
 */
typedef void (*ConnectionCallback)(void* context);
typedef void (*MessageCallback)(void* context, Message* message);

/**
 * Connection structure
 */
typedef struct Connection {
    int socket_fd;
    char* host;
    uint16_t port;
    ConnectionStatus status;
    pthread_t receiver_thread;
    pthread_mutex_t mutex;
    uint8_t running;
    
    // Callbacks
    ConnectionCallback on_connect;
    ConnectionCallback on_disconnect;
    MessageCallback on_message;
    void* callback_context;
} Connection;

/**
 * Create a new connection
 */
Connection* connection_create(const char* host, uint16_t port);

/**
 * Free connection resources
 */
void connection_free(Connection* connection);

/**
 * Connect to remote host
 */
int connection_connect(Connection* connection);

/**
 * Disconnect from remote host
 */
int connection_disconnect(Connection* connection);

/**
 * Send a message over the connection
 */
int connection_send(Connection* connection, const Message* message);

/**
 * Set connection callbacks
 */
void connection_set_on_connect(Connection* connection, ConnectionCallback callback, void* context);
void connection_set_on_disconnect(Connection* connection, ConnectionCallback callback, void* context);
void connection_set_on_message(Connection* connection, MessageCallback callback, void* context);

/**
 * Get connection status
 */
ConnectionStatus connection_get_status(Connection* connection);

#ifdef __cplusplus
}
#endif

#endif /* CONNECTION_H */ 
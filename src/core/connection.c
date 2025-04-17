#include "core/connection.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

// Message header size and magic bytes
#define MESSAGE_HEADER_SIZE 16
#define MESSAGE_MAGIC 0x52414D51  // "RAMQ"

// Connection timeout in seconds
#define CONNECTION_TIMEOUT 10

// Forward declarations
static void* connection_receiver_thread(void* arg);
Message* receive_message_data(int socket_fd);
int send_message_data(int socket_fd, const Message* message);

Connection* connection_create(const char* host, uint16_t port) {
    if (!host) {
        LOG_ERROR("Invalid host for connection");
        return NULL;
    }

    Connection* conn = (Connection*)malloc(sizeof(Connection));
    if (!conn) {
        LOG_ERROR("Failed to allocate memory for connection");
        return NULL;
    }

    // Initialize connection properties
    conn->socket_fd = -1;
    conn->host = strdup(host);
    conn->port = port;
    conn->status = CONNECTION_STATUS_DISCONNECTED;
    conn->running = 0;
    conn->on_connect = NULL;
    conn->on_disconnect = NULL;
    conn->on_message = NULL;
    conn->callback_context = NULL;

    // Initialize mutex
    if (pthread_mutex_init(&conn->mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize connection mutex");
        free(conn->host);
        free(conn);
        return NULL;
    }

    LOG_INFO("Connection created for %s:%d", host, port);
    return conn;
}

void connection_free(Connection* conn) {
    if (!conn) {
        return;
    }

    // Disconnect if connected
    if (conn->status == CONNECTION_STATUS_CONNECTED) {
        connection_disconnect(conn);
    }

    pthread_mutex_destroy(&conn->mutex);
    free(conn->host);
    free(conn);

    LOG_INFO("Connection freed");
}

int connection_connect(Connection* conn) {
    if (!conn) {
        LOG_ERROR("Cannot connect NULL connection");
        return 0;
    }

    pthread_mutex_lock(&conn->mutex);

    // Check if already connected or connecting
    if (conn->status == CONNECTION_STATUS_CONNECTED || 
        conn->status == CONNECTION_STATUS_CONNECTING) {
        pthread_mutex_unlock(&conn->mutex);
        LOG_WARNING("Connection is already connected or connecting");
        return 1;
    }

    // Update status to connecting
    conn->status = CONNECTION_STATUS_CONNECTING;
    pthread_mutex_unlock(&conn->mutex);

    // Create socket
    conn->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->socket_fd < 0) {
        pthread_mutex_lock(&conn->mutex);
        conn->status = CONNECTION_STATUS_FAILED;
        pthread_mutex_unlock(&conn->mutex);
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return 0;
    }

    // Set socket to non-blocking
    int flags = fcntl(conn->socket_fd, F_GETFL, 0);
    fcntl(conn->socket_fd, F_SETFL, flags | O_NONBLOCK);

    // Resolve hostname
    struct hostent* server = gethostbyname(conn->host);
    if (!server) {
        close(conn->socket_fd);
        pthread_mutex_lock(&conn->mutex);
        conn->socket_fd = -1;
        conn->status = CONNECTION_STATUS_FAILED;
        pthread_mutex_unlock(&conn->mutex);
        LOG_ERROR("Failed to resolve hostname: %s", conn->host);
        return 0;
    }

    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(conn->port);

    // Connect to server (non-blocking)
    int result = connect(conn->socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0 && errno != EINPROGRESS) {
        close(conn->socket_fd);
        pthread_mutex_lock(&conn->mutex);
        conn->socket_fd = -1;
        conn->status = CONNECTION_STATUS_FAILED;
        pthread_mutex_unlock(&conn->mutex);
        LOG_ERROR("Failed to connect: %s", strerror(errno));
        return 0;
    }

    // Wait for connection to complete
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(conn->socket_fd, &fdset);
    struct timeval tv;
    tv.tv_sec = CONNECTION_TIMEOUT;
    tv.tv_usec = 0;

    result = select(conn->socket_fd + 1, NULL, &fdset, NULL, &tv);
    if (result <= 0) {
        close(conn->socket_fd);
        pthread_mutex_lock(&conn->mutex);
        conn->socket_fd = -1;
        conn->status = CONNECTION_STATUS_FAILED;
        pthread_mutex_unlock(&conn->mutex);
        LOG_ERROR("Connection timed out or select error");
        return 0;
    }

    // Check if connection succeeded
    int so_error;
    socklen_t len = sizeof(so_error);
    getsockopt(conn->socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
        close(conn->socket_fd);
        pthread_mutex_lock(&conn->mutex);
        conn->socket_fd = -1;
        conn->status = CONNECTION_STATUS_FAILED;
        pthread_mutex_unlock(&conn->mutex);
        LOG_ERROR("Connection failed: %s", strerror(so_error));
        return 0;
    }

    // Set socket back to blocking mode
    fcntl(conn->socket_fd, F_SETFL, flags);

    // Start receiver thread
    pthread_mutex_lock(&conn->mutex);
    conn->status = CONNECTION_STATUS_CONNECTED;
    conn->running = 1;
    pthread_mutex_unlock(&conn->mutex);

    if (pthread_create(&conn->receiver_thread, NULL, connection_receiver_thread, conn) != 0) {
        close(conn->socket_fd);
        pthread_mutex_lock(&conn->mutex);
        conn->socket_fd = -1;
        conn->status = CONNECTION_STATUS_FAILED;
        conn->running = 0;
        pthread_mutex_unlock(&conn->mutex);
        LOG_ERROR("Failed to create receiver thread");
        return 0;
    }

    // Call on_connect callback if set
    if (conn->on_connect) {
        conn->on_connect(conn->callback_context);
    }

    LOG_INFO("Connected to %s:%d", conn->host, conn->port);
    return 1;
}

int connection_disconnect(Connection* conn) {
    if (!conn) {
        LOG_ERROR("Cannot disconnect NULL connection");
        return 0;
    }

    pthread_mutex_lock(&conn->mutex);
    if (conn->status != CONNECTION_STATUS_CONNECTED) {
        pthread_mutex_unlock(&conn->mutex);
        LOG_WARNING("Connection is not connected");
        return 1;
    }

    // Stop receiver thread
    conn->running = 0;
    pthread_mutex_unlock(&conn->mutex);

    // Wait for receiver thread to terminate
    pthread_join(conn->receiver_thread, NULL);

    // Close socket
    if (conn->socket_fd >= 0) {
        close(conn->socket_fd);
    }

    pthread_mutex_lock(&conn->mutex);
    conn->socket_fd = -1;
    conn->status = CONNECTION_STATUS_DISCONNECTED;
    pthread_mutex_unlock(&conn->mutex);

    // Call on_disconnect callback if set
    if (conn->on_disconnect) {
        conn->on_disconnect(conn->callback_context);
    }

    LOG_INFO("Disconnected from %s:%d", conn->host, conn->port);
    return 1;
}

int connection_send(Connection* conn, const Message* message) {
    if (!conn || !message) {
        LOG_ERROR("Invalid parameters for connection_send");
        return 0;
    }

    pthread_mutex_lock(&conn->mutex);
    if (conn->status != CONNECTION_STATUS_CONNECTED) {
        pthread_mutex_unlock(&conn->mutex);
        LOG_ERROR("Cannot send message: Connection is not connected");
        return 0;
    }

    int socket_fd = conn->socket_fd;
    pthread_mutex_unlock(&conn->mutex);

    if (send_message_data(socket_fd, message)) {
        LOG_DEBUG("Message sent to %s:%d", conn->host, conn->port);
        return 1;
    } else {
        LOG_ERROR("Failed to send message to %s:%d", conn->host, conn->port);
        return 0;
    }
}

void connection_set_on_connect(Connection* conn, ConnectionCallback callback, void* context) {
    if (conn) {
        pthread_mutex_lock(&conn->mutex);
        conn->on_connect = callback;
        conn->callback_context = context;
        pthread_mutex_unlock(&conn->mutex);
    }
}

void connection_set_on_disconnect(Connection* conn, ConnectionCallback callback, void* context) {
    if (conn) {
        pthread_mutex_lock(&conn->mutex);
        conn->on_disconnect = callback;
        conn->callback_context = context;
        pthread_mutex_unlock(&conn->mutex);
    }
}

void connection_set_on_message(Connection* conn, MessageCallback callback, void* context) {
    if (conn) {
        pthread_mutex_lock(&conn->mutex);
        conn->on_message = callback;
        conn->callback_context = context;
        pthread_mutex_unlock(&conn->mutex);
    }
}

ConnectionStatus connection_get_status(Connection* conn) {
    if (!conn) {
        return CONNECTION_STATUS_DISCONNECTED;
    }

    pthread_mutex_lock(&conn->mutex);
    ConnectionStatus status = conn->status;
    pthread_mutex_unlock(&conn->mutex);

    return status;
}

// Receiver thread function
static void* connection_receiver_thread(void* arg) {
    Connection* conn = (Connection*)arg;
    LOG_INFO("Receiver thread started for %s:%d", conn->host, conn->port);

    while (1) {
        // Check if thread should exit
        pthread_mutex_lock(&conn->mutex);
        int running = conn->running;
        int socket_fd = conn->socket_fd;
        pthread_mutex_unlock(&conn->mutex);

        if (!running) {
            break;
        }

        // Try to receive a message
        Message* message = receive_message_data(socket_fd);
        if (message) {
            // Call on_message callback if set
            if (conn->on_message) {
                conn->on_message(conn->callback_context, message);
            } else {
                // Free message if no callback set
                message_free(message);
            }
        } else {
            // Check if connection is still valid
            if (errno == ECONNRESET || errno == ENOTCONN || errno == EPIPE) {
                // Connection lost, close and notify
                pthread_mutex_lock(&conn->mutex);
                conn->running = 0;
                conn->status = CONNECTION_STATUS_DISCONNECTED;
                pthread_mutex_unlock(&conn->mutex);

                if (conn->on_disconnect) {
                    conn->on_disconnect(conn->callback_context);
                }

                LOG_WARNING("Connection lost to %s:%d: %s", 
                           conn->host, conn->port, strerror(errno));
                break;
            }

            // Sleep briefly to avoid busy waiting
            usleep(10000);  // 10ms
        }
    }

    LOG_INFO("Receiver thread terminated for %s:%d", conn->host, conn->port);
    return NULL;
}

// Send a message over a socket
int send_message_data(int socket_fd, const Message* message) {
    if (socket_fd < 0 || !message) {
        return 0;
    }

    // Prepare header
    uint32_t magic = MESSAGE_MAGIC;
    uint32_t exchange_len = strlen(message->exchange);
    uint32_t routing_key_len = strlen(message->routing_key);
    uint32_t body_size = message->body_size;

    // Send magic
    if (send(socket_fd, &magic, sizeof(magic), 0) != sizeof(magic)) {
        return 0;
    }

    // Send exchange length and string
    if (send(socket_fd, &exchange_len, sizeof(exchange_len), 0) != sizeof(exchange_len)) {
        return 0;
    }
    if (send(socket_fd, message->exchange, exchange_len, 0) != exchange_len) {
        return 0;
    }

    // Send routing key length and string
    if (send(socket_fd, &routing_key_len, sizeof(routing_key_len), 0) != sizeof(routing_key_len)) {
        return 0;
    }
    if (send(socket_fd, message->routing_key, routing_key_len, 0) != routing_key_len) {
        return 0;
    }

    // Send body size and body
    if (send(socket_fd, &body_size, sizeof(body_size), 0) != sizeof(body_size)) {
        return 0;
    }
    if (body_size > 0) {
        if (send(socket_fd, message->body, body_size, 0) != body_size) {
            return 0;
        }
    }

    // Send properties
    if (send(socket_fd, &message->priority, sizeof(message->priority), 0) != sizeof(message->priority)) {
        return 0;
    }
    if (send(socket_fd, &message->expiration, sizeof(message->expiration), 0) != sizeof(message->expiration)) {
        return 0;
    }
    if (send(socket_fd, &message->persistent, sizeof(message->persistent), 0) != sizeof(message->persistent)) {
        return 0;
    }

    return 1;
}

// Receive a message from a socket
Message* receive_message_data(int socket_fd) {
    if (socket_fd < 0) {
        return NULL;
    }

    // Read magic
    uint32_t magic;
    if (recv(socket_fd, &magic, sizeof(magic), 0) != sizeof(magic)) {
        return NULL;
    }
    if (magic != MESSAGE_MAGIC) {
        LOG_ERROR("Invalid message magic: %08x", magic);
        return NULL;
    }

    // Read exchange
    uint32_t exchange_len;
    if (recv(socket_fd, &exchange_len, sizeof(exchange_len), 0) != sizeof(exchange_len)) {
        return NULL;
    }
    char* exchange = (char*)malloc(exchange_len + 1);
    if (!exchange) {
        return NULL;
    }
    if (recv(socket_fd, exchange, exchange_len, 0) != exchange_len) {
        free(exchange);
        return NULL;
    }
    exchange[exchange_len] = '\0';

    // Read routing key
    uint32_t routing_key_len;
    if (recv(socket_fd, &routing_key_len, sizeof(routing_key_len), 0) != sizeof(routing_key_len)) {
        free(exchange);
        return NULL;
    }
    char* routing_key = (char*)malloc(routing_key_len + 1);
    if (!routing_key) {
        free(exchange);
        return NULL;
    }
    if (recv(socket_fd, routing_key, routing_key_len, 0) != routing_key_len) {
        free(exchange);
        free(routing_key);
        return NULL;
    }
    routing_key[routing_key_len] = '\0';

    // Read body size
    uint32_t body_size;
    if (recv(socket_fd, &body_size, sizeof(body_size), 0) != sizeof(body_size)) {
        free(exchange);
        free(routing_key);
        return NULL;
    }

    // Read body if any
    void* body = NULL;
    if (body_size > 0) {
        body = malloc(body_size);
        if (!body) {
            free(exchange);
            free(routing_key);
            return NULL;
        }
        if (recv(socket_fd, body, body_size, 0) != body_size) {
            free(exchange);
            free(routing_key);
            free(body);
            return NULL;
        }
    }

    // Create message
    Message* message = message_create(exchange, routing_key, body, body_size);
    free(exchange);
    free(routing_key);
    if (body) {
        free(body);
    }

    if (!message) {
        return NULL;
    }

    // Read properties
    if (recv(socket_fd, &message->priority, sizeof(message->priority), 0) != sizeof(message->priority)) {
        message_free(message);
        return NULL;
    }
    if (recv(socket_fd, &message->expiration, sizeof(message->expiration), 0) != sizeof(message->expiration)) {
        message_free(message);
        return NULL;
    }
    if (recv(socket_fd, &message->persistent, sizeof(message->persistent), 0) != sizeof(message->persistent)) {
        message_free(message);
        return NULL;
    }

    return message;
} 
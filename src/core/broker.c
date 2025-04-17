#include "core/broker.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// func in connection.c
extern Message* receive_message_data(int socket_fd);
extern int send_message_data(int socket_fd, const Message* message);

// Event loop sleep time in milliseconds
#define EVENT_LOOP_SLEEP_MS 100

// Forward declarations
static void* broker_event_loop(void* arg);
static int broker_route_message(Broker* broker, Exchange* exchange, Message* message);
static int match_topic(const char* pattern, const char* topic);

#define BROKER_LISTEN_PORT 5672
#define BROKER_LISTEN_BACKLOG 16
#define MAX_CONNECTIONS 100  // max number of client connections

static void* broker_network_thread(void* arg);

int broker_listen_fd = -1;
pthread_t broker_network_tid;
int client_fds[MAX_CONNECTIONS];  // file descriptor array
int client_count = 0;  // current client count

// Client connection information structure
typedef struct {
    int fd;                 // Client socket file descriptor
    pthread_t thread;       // Handler thread ID
    int is_consumer;        // Whether it is a consumer
    char queue_name[64];    // Queue name subscribed by the consumer
    int running;            // Whether the thread is running
    Broker* broker;         // Broker reference
} ClientConnection;

// client connections array
ClientConnection client_connections[MAX_CONNECTIONS];

// consumer info structure
typedef struct {
    int client_index;       // client index in array
    char queue_name[64];    // consumer subscribed queue's name
} ConsumerInfo;

// consumers array
ConsumerInfo consumers[MAX_CONNECTIONS];
int consumer_count = 0;

// resend_message_to_consumers function
static void forward_message_to_consumers(const char* queue_name, Message* message) {
    if (!queue_name || !message) {
        return;
    }
    
    int forwarded = 0;
    
    // iterate over consumers
    for (int i = 0; i < consumer_count; i++) {
        if (strcmp(consumers[i].queue_name, queue_name) == 0) {
            int client_index = consumers[i].client_index;
            
            // ensure client is still connected
            if (client_connections[client_index].fd != -1 && 
                client_connections[client_index].running) {
                
                // send message to
                if (send_message_data(client_connections[client_index].fd, message)) {
                    LOG_INFO("Forwarded message to consumer at index %d (fd %d) for queue '%s'", 
                             client_index, client_connections[client_index].fd, queue_name);
                    forwarded++;
                } else {
                    LOG_WARNING("Failed to forward message to consumer at index %d (fd %d)", 
                                client_index, client_connections[client_index].fd);
                }
            }
        }
    }
    
    if (forwarded == 0) {
        LOG_WARNING("No consumers available for queue '%s', message not forwarded", queue_name);
    } else {
        LOG_INFO("Message forwarded to %d consumers for queue '%s'", forwarded, queue_name);
    }
}

// register_consumer function
static int register_consumer(int client_index, const char* queue_name) {
    if (client_index < 0 || client_index >= MAX_CONNECTIONS || !queue_name) {
        return 0;
    }
    
    // check if maximum number of consumers has been reached
    if (consumer_count >= MAX_CONNECTIONS) {
        LOG_WARNING("Maximum number of consumers reached");
        return 0;
    }
    
    // add info to consumers array
    strncpy(consumers[consumer_count].queue_name, queue_name, sizeof(consumers[consumer_count].queue_name) - 1);
    consumers[consumer_count].queue_name[sizeof(consumers[consumer_count].queue_name) - 1] = '\0';
    consumers[consumer_count].client_index = client_index;
    consumer_count++;
    
    // mark the client as a consumer
    client_connections[client_index].is_consumer = 1;
    strncpy(client_connections[client_index].queue_name, queue_name, sizeof(client_connections[client_index].queue_name) - 1);
    client_connections[client_index].queue_name[sizeof(client_connections[client_index].queue_name) - 1] = '\0';
    
    LOG_INFO("Registered consumer at index %d (fd %d) for queue '%s', total consumers: %d", 
             client_index, client_connections[client_index].fd, queue_name, consumer_count);
    return 1;
}

// client_handler_thread function
static void* client_handler_thread(void* arg) {
    ClientConnection* conn = (ClientConnection*)arg;
    int fd = conn->fd;
    Broker* broker = conn->broker;
    int client_index = -1;
    
    // find the client index
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (&client_connections[i] == conn) {
            client_index = i;
            break;
        }
    }
    
    if (client_index == -1) {
        LOG_ERROR("Could not find client index for fd %d", fd);
        return NULL;
    }
    
    LOG_INFO("Client handler thread started for fd %d (index %d)", fd, client_index);
    
    // deal with messages
    while (conn->running) {
        // try to receive message
        Message* message = receive_message_data(fd);
        if (!message) {
            // failed to receive message
            LOG_WARNING("Failed to receive message from client %d, closing connection", fd);
            break;
        }
        
        LOG_INFO("Received message from client %d, exchange: %s, routing key: %s", 
                fd, message->exchange, message->routing_key);
        
        // check for consumer registration
        // Simple protocol definition: If the message has exchange='consumer' and routing_key='register', then this is a consumer registration message.
        // message body is the queue name
        if (strcmp(message->exchange, "consumer") == 0 && 
            strcmp(message->routing_key, "register") == 0 && 
            message->body && message->body_size > 0) {
            
            // get queue name
            char queue_name[64] = {0};
            size_t copy_size = message->body_size < sizeof(queue_name) - 1 ? message->body_size : sizeof(queue_name) - 1;
            memcpy(queue_name, message->body, copy_size);
            queue_name[copy_size] = '\0';
            
            LOG_INFO("Consumer registration message received, queue: %s", queue_name);
            
            // register consumer
            if (register_consumer(client_index, queue_name)) {
                LOG_INFO("Consumer registered for queue '%s'", queue_name);
            } else {
                LOG_WARNING("Failed to register consumer for queue '%s'", queue_name);
            }
            
            message_free(message);
            continue;
        }
        
        // if it's not a consumer registration message
        if (!conn->is_consumer) {
            // get exchange
            Exchange* exchange = broker_get_exchange(broker, message->exchange);
            if (!exchange) {
                LOG_WARNING("Exchange '%s' not found, message dropped", message->exchange);
                message_free(message);
                continue;
            }
            
            // send message to exchange
            if (broker_publish(broker, exchange, message)) {
                LOG_INFO("Message published to exchange '%s'", message->exchange);
            } else {
                LOG_WARNING("Failed to publish message to exchange '%s'", message->exchange);
            }
            
            // broker_publish already frees the message
        } else {
            // if it's a consumer, forward the message
            LOG_WARNING("Consumer %d sent a message, ignoring", fd);
            message_free(message);
        }
    }
    
    LOG_INFO("Client handler thread exiting for fd %d", fd);
    return NULL;
}

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

    // Create network thread
    if (pthread_create(&broker_network_tid, NULL, broker_network_thread, broker) != 0) {
        pthread_mutex_lock(&broker->mutex);
        broker->running = 0;
        pthread_mutex_unlock(&broker->mutex);
        LOG_ERROR("Failed to create broker network thread");
        return 0;
    }

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
                            
                            // send message to consumer
                            forward_message_to_consumers(binding->queue->name, msg_clone);
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
                            
                            // send message to consumer
                            forward_message_to_consumers(binding->queue->name, msg_clone);
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
                            
                            // send message to consumer
                            forward_message_to_consumers(binding->queue->name, msg_clone);
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

    // Message acknowledgment timeout in seconds
    const int ACK_TIMEOUT_SECONDS = 30;
    time_t last_timeout_check = time(NULL);

    while (1) {
        // Check if broker is still running
        pthread_mutex_lock(&broker->mutex);
        int running = broker->running;
        pthread_mutex_unlock(&broker->mutex);

        if (!running) {
            break;
        }

        // Process message timeouts every 5 seconds
        time_t current_time = time(NULL);
        if (current_time - last_timeout_check >= 5) {
            // Process timeouts for all queues
            pthread_mutex_lock(&broker->mutex);
            for (size_t i = 0; i < broker->queue_count; i++) {
                queue_process_timeouts(broker->queues[i], ACK_TIMEOUT_SECONDS);
            }
            pthread_mutex_unlock(&broker->mutex);
            
            last_timeout_check = current_time;
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

// 网络监听线程实现
static void* broker_network_thread(void* arg) {
    Broker* broker = (Broker*)arg;
    struct sockaddr_in addr;
    broker_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (broker_listen_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return NULL;
    }
    int opt = 1;
    setsockopt(broker_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(BROKER_LISTEN_PORT);
    if (bind(broker_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind socket: %s", strerror(errno));
        close(broker_listen_fd);
        broker_listen_fd = -1;
        return NULL;
    }
    if (listen(broker_listen_fd, BROKER_LISTEN_BACKLOG) < 0) {
        LOG_ERROR("Failed to listen on socket: %s", strerror(errno));
        close(broker_listen_fd);
        broker_listen_fd = -1;
        return NULL;
    }
    LOG_INFO("Broker listening on 0.0.0.0:%d", BROKER_LISTEN_PORT);
    
    // 初始化客户端连接数组
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        client_fds[i] = -1;
        client_connections[i].fd = -1;
        client_connections[i].running = 0;
    }
    client_count = 0;
    
    while (1) {
        pthread_mutex_lock(&broker->mutex);
        int running = broker->running;
        pthread_mutex_unlock(&broker->mutex);
        if (!running) break;
        
        int client_fd = accept(broker_listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("Accept failed: %s", strerror(errno));
            continue;
        }
        
        LOG_INFO("Accepted client fd %d", client_fd);
        
        // 保存客户端连接，不立即关闭
        if (client_count < MAX_CONNECTIONS) {
            // 找一个空位置保存连接
            for (int i = 0; i < MAX_CONNECTIONS; i++) {
                if (client_connections[i].fd == -1) {
                    // 初始化客户端连接信息
                    client_connections[i].fd = client_fd;
                    client_connections[i].is_consumer = 0; // 默认为生产者
                    client_connections[i].running = 1;
                    client_connections[i].broker = broker;
                    memset(client_connections[i].queue_name, 0, sizeof(client_connections[i].queue_name));
                    
                    // 创建客户端处理线程
                    if (pthread_create(&client_connections[i].thread, NULL, client_handler_thread, &client_connections[i]) != 0) {
                        LOG_ERROR("Failed to create client handler thread for fd %d", client_fd);
                        close(client_fd);
                        client_connections[i].fd = -1;
                        client_connections[i].running = 0;
                    } else {
                        client_fds[i] = client_fd;
                        client_count++;
                        LOG_INFO("Client %d stored at slot %d, total clients: %d, handler thread created", 
                                client_fd, i, client_count);
                    }
                    break;
                }
            }
        } else {
            // 连接数已满，关闭新连接
            LOG_WARNING("Max connections reached, closing client %d", client_fd);
            close(client_fd);
        }
    }
    
    // 关闭所有客户端连接
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (client_connections[i].fd != -1) {
            // 停止客户端处理线程
            client_connections[i].running = 0;
            pthread_join(client_connections[i].thread, NULL);
            
            // 关闭连接
            close(client_connections[i].fd);
            client_connections[i].fd = -1;
            client_fds[i] = -1;
        }
    }
    client_count = 0;
    
    close(broker_listen_fd);
    broker_listen_fd = -1;
    LOG_INFO("Broker network thread exiting");
    return NULL;
}
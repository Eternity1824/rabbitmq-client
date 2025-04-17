# Simple RabbitMQ Client Implementation

This is a simplified RabbitMQ client implementation with basic message queue functionality, developed using a hybrid C/C++ structure.

## Project Structure

```
├── Makefile           # Project build script
├── README.md          # Project documentation
├── architecture.md    # Architecture diagram and description
│
├── include/           # Header files
│   ├── core/          # Core component headers
│   │   ├── broker.h   # Broker interface
│   │   ├── connection.h # Connection management interface
│   │   ├── log.h      # Logging system
│   │   ├── message.h  # Message structure definition
│   │   └── queue.h    # Thread-safe queue interface
│   └── wrapper/       # C++ wrapper headers
│       └── client.hpp # C++ client interface
│
├── src/               # Source code
│   ├── broker/        # Broker implementation
│   │   └── main.c     # Broker main program
│   ├── core/          # Core components implementation
│   │   ├── broker.c   # Broker component implementation
│   │   ├── connection.c # Connection management implementation
│   │   ├── log.c      # Logging system implementation
│   │   ├── message.c  # Message structure implementation
│   │   └── queue.c    # Thread-safe queue implementation
│   └── wrapper/       # C++ wrapper implementation
│       └── client.cpp # C++ client interface implementation
│
├── examples/          # Example code
│   ├── consumer/      # Consumer examples
│   │   └── ack_consumer.cpp # Consumer with acknowledgment
│   └── producer/      # Producer examples
│       └── main.cpp   # Producer example
│
├── bin/               # Compiled binaries
└── obj/               # Object files
```

## Features

- Message structure definition (C language)
- Thread-safe queue implementation (C language, using pthread)
- Broker management with TCP socket support (C language)
- Exchange types: Direct, Fanout, and Topic
- Message acknowledgment (ACK) support for reliable delivery
- C++ interface wrapper (providing modern C++ style API)
- Consumer registration protocol

## Compilation

Compile the project using make:

```bash
make all
```

Clean build artifacts:

```bash
make clean
```

## Usage Examples

### Start the Broker

```bash
# Start with default settings (port 5672)
./bin/broker

# Or specify a custom port
./bin/broker -p 5673

# For more options
./bin/broker --help
```

### Run a Consumer with Acknowledgment

```bash
# Connect to local broker on default port
./bin/ack_consumer

# Or specify host and port
./bin/ack_consumer localhost 5672 test
```

### Run a Producer

```bash
# Connect to local broker on default port
./bin/producer

# Or specify host and port
./bin/producer localhost 5672 amq.direct test
```

## Implemented Features

- **Message Passing**: Basic message sending and receiving functionality
- **Queue Management**: Thread-safe queue operations with mutex and condition variables
- **Exchange Types**: Support for Direct, Fanout, and Topic exchange types
- **Thread Safety**: Using mutexes and condition variables to ensure thread safety
- **Message Acknowledgment**: Support for manual and automatic message acknowledgment
- **Consumer Registration Protocol**: Simple protocol for consumers to register with queues
- **TCP Network Layer**: Socket-based network communication between components

## Unimplemented Features

- **Persistent Storage**: Message persistence to disk not implemented
- **Cluster Support**: Only single-node mode supported
- **Advanced Features**: Delayed queues, priority queues, and other advanced features not implemented
- **Complete AMQP Protocol**: Full AMQP protocol specification not implemented
- **Comprehensive Testing**: Unit and integration tests not fully implemented

## Contribution

Issues and Pull Requests are welcome to improve this project.
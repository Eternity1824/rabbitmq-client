# RabbitMQ Client Architecture

```mermaid
graph TD
    %% Main Components
    Client["Client Application"] 
    Producer["Producer"]
    Consumer["Consumer"]
    Broker["Broker"]
    
    %% Core Components
    subgraph Core Components
        Message["Message"]
        Queue["Thread-safe Queue"]
        Connection["TCP Connection"]
        Exchange["Exchange"]
    end
    
    %% Exchange Types
    subgraph Exchange Types
        DirectExchange["Direct Exchange"]
        FanoutExchange["Fanout Exchange"]
        TopicExchange["Topic Exchange"]
    end
    
    %% Thread Safety Mechanisms
    subgraph Thread Safety
        Mutex["Mutex"]
        CondVar["Condition Variables"]
        AtomicOps["Atomic Operations"]
    end
    
    %% Message Acknowledgment Mechanisms
    subgraph Message Acknowledgment
        Ack["ACK"]
        Nack["NACK"]
        Requeue["Requeue"]
    end
    
    %% Relationships
    Client --> Producer
    Client --> Consumer
    Producer --> Connection
    Consumer --> Connection
    Connection --> Broker
    Broker --> Exchange
    Exchange --> Queue
    Queue --> Message
    
    Exchange --> DirectExchange
    Exchange --> FanoutExchange
    Exchange --> TopicExchange
    
    Queue --> Mutex
    Queue --> CondVar
    Queue --> AtomicOps
    
    Consumer --> Ack
    Consumer --> Nack
    Nack --> Requeue
    
    %% Style Settings
    classDef core fill:#f9f,stroke:#333,stroke-width:2px;
    classDef exchange fill:#bbf,stroke:#33f,stroke-width:1px;
    classDef thread fill:#bfb,stroke:#3f3,stroke-width:1px;
    classDef ack fill:#fbb,stroke:#f33,stroke-width:1px;
    
    class Message,Queue,Connection,Exchange core;
    class DirectExchange,FanoutExchange,TopicExchange exchange;
    class Mutex,CondVar,AtomicOps thread;
    class Ack,Nack,Requeue ack;
```

## Component Description

### Core Components

1. **Message**: Message structure containing message content, routing key, exchange name, and other information
2. **Queue**: Thread-safe queue implementation using mutexes and condition variables to ensure thread safety
3. **Connection**: TCP connection management handling communication between clients and server
4. **Exchange**: Exchange responsible for message routing

### Exchange Types

1. **Direct Exchange**: Routes messages to queues based on exact routing key matching
2. **Fanout Exchange**: Broadcasts messages to all bound queues regardless of routing key
3. **Topic Exchange**: Routes messages to queues based on pattern-matching routing keys

### Thread Safety Mechanisms

1. **Mutex**: Ensures only one thread can access shared resources at a time
2. **Condition Variables**: Used for thread notification and waiting
3. **Atomic Operations**: Ensures operation atomicity

### Message Acknowledgment Mechanisms

1. **ACK**: Message acknowledgment indicating successful processing
2. **NACK**: Message rejection indicating processing failure
3. **Requeue**: Puts rejected messages back into the queue

## Data Flow

1. Producers send messages to the Broker via TCP connection
2. Broker routes messages to appropriate queues based on exchange and routing key
3. Consumers receive messages from the Broker via TCP connection
4. Consumers process messages and send acknowledgment or rejection
5. If a message is rejected with requeue option, it's put back into the queue

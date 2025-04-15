# RabbitMQ简易客户端实现

这是一个简化版的RabbitMQ客户端实现，包含基础的消息队列功能，使用C/C++混合结构开发。

## 项目结构

```
├── common/       # 公共定义
├── broker/       # Broker相关
├── network/      # 网络通信
├── storage/      # 持久化存储
├── src/          # 源代码
│   ├── common/   # 公共实现
│   ├── broker/   # Broker实现
│   ├── network/  # 网络实现
│   └── storage/  # 存储实现
├── examples/     # 示例代码
│   ├── producer/ # 生产者示例
│   └── consumer/ # 消费者示例
└── tests/        # 测试代码
    ├── unit/     # 单元测试
    └── integration/ # 集成测试
```

## 功能特性

- 消息结构体定义（C语言）
- 线程安全队列/链表（C语言，使用pthread）
- Broker管理主循环（C语言，select/epoll控制）
- CLI命令接口（C语言，基本交互）
- C++接口封装（提供现代C++风格API）
- 实现了最基本的消息发送和接收功能

## 编译方法

使用make编译项目：

```bash
make all
```

清理编译产物：

```bash
make clean
```

## 使用示例

### 启动Broker

```bash
./bin/broker -p 5672
```

### 运行生产者

```bash
./bin/producer localhost 5672 amq.direct test
```

### 运行消费者

```bash
./bin/consumer localhost 5672 test
```

## 实现的特性

- **消息传递**：基本的消息发送和接收功能
- **队列管理**：创建、绑定、解绑队列
- **交换机类型**：支持Direct、Fanout和Topic三种类型
- **线程安全**：使用互斥锁和条件变量保证线程安全

## 未实现的功能

- 持久化存储：未实现消息持久化到磁盘
- 集群支持：仅支持单机模式
- 高级特性：未实现延迟队列、优先队列等高级特性
- 完整的AMQP协议：未实现完整的AMQP协议规范

## 贡献

欢迎提交Issues和Pull Requests来改进这个项目。 
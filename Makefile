	# 编译器设置
CC = gcc
CXX = g++
CFLAGS = -Wall -g -I./include
CXXFLAGS = -Wall -g -std=c++11 -I./include
LDFLAGS = -lpthread

# 目录结构
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
EXAMPLES_DIR = examples

# 源文件
CORE_C_SOURCES = $(wildcard $(SRC_DIR)/core/*.c)
WRAPPER_CXX_SOURCES = $(wildcard $(SRC_DIR)/wrapper/*.cpp)
BROKER_MAIN = $(SRC_DIR)/broker/main.c
PRODUCER_SRC = $(EXAMPLES_DIR)/producer/main.cpp
CONSUMER_SRC = $(EXAMPLES_DIR)/consumer/ack_consumer.cpp

# 目标文件
CORE_C_OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(CORE_C_SOURCES))
WRAPPER_CXX_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(WRAPPER_CXX_SOURCES))
CORE_OBJECTS = $(CORE_C_OBJECTS) $(WRAPPER_CXX_OBJECTS)
BROKER_OBJ = $(OBJ_DIR)/broker/main.o
PRODUCER_OBJ = $(OBJ_DIR)/examples/producer/main.o
CONSUMER_OBJ = $(OBJ_DIR)/examples/consumer/ack_consumer.o

# 可执行文件
BROKER = $(BIN_DIR)/broker
PRODUCER = $(BIN_DIR)/producer
CONSUMER = $(BIN_DIR)/ack_consumer

# 默认目标
all: dirs $(BROKER) $(PRODUCER) $(CONSUMER)

# 创建必要的目录
dirs:
	mkdir -p $(BIN_DIR)
	mkdir -p $(OBJ_DIR)/core $(OBJ_DIR)/broker $(OBJ_DIR)/network $(OBJ_DIR)/storage $(OBJ_DIR)/wrapper
	mkdir -p $(OBJ_DIR)/examples/producer $(OBJ_DIR)/examples/consumer

# 编译C源文件
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 编译C++源文件
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译示例源文件
$(PRODUCER_OBJ): $(PRODUCER_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CONSUMER_OBJ): $(CONSUMER_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译broker main
$(BROKER_OBJ): $(BROKER_MAIN)
	$(CC) $(CFLAGS) -c $< -o $@

# 链接broker
$(BROKER): $(CORE_OBJECTS) $(BROKER_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

# 链接producer示例
$(PRODUCER): $(CORE_OBJECTS) $(PRODUCER_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

# 链接consumer示例
$(CONSUMER): $(CORE_OBJECTS) $(CONSUMER_OBJ)
	$(CXX) $^ -o $@ $(LDFLAGS)

# 清理
clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*

# 打印变量值（用于调试）
print-%:
	@echo $* = $($*)

.PHONY: all dirs clean
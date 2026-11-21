CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude -Isrc/server
LDFLAGS  := -pthread
LDLIBS   := -lsqlite3

BUILD_DIR := build
BIN_DIR   := bin
DATA_DIR  := data

SERVER_TARGET := $(BIN_DIR)/server
CLIENT_TARGET := $(BIN_DIR)/client

SHARED_SRCS := \
	src/session_manager.cpp \
	src/database.cpp

SERVER_SRCS := \
	$(SHARED_SRCS) \
	src/server/client_handler.cpp \
	src/server/server_main.cpp

CLIENT_SRCS := \
	src/client/client_main.cpp \
	src/client/receiver_thread.cpp

SERVER_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SERVER_SRCS))
CLIENT_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CLIENT_SRCS))

.PHONY: all server client run-server clean

all: server

server: $(SERVER_TARGET)

client: $(CLIENT_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS) | $(BIN_DIR) $(DATA_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(CLIENT_TARGET): $(CLIENT_OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR) $(DATA_DIR):
	@mkdir -p $@

run-server: server
	./$(SERVER_TARGET) 8080 $(DATA_DIR)/server.db

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

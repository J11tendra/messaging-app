CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude -pthread
LDFLAGS  := -pthread
LDLIBS   := -lsqlite3

BUILD_DIR := build
BIN_DIR   := bin
DATA_DIR  := data

SERVER_TARGET := $(BIN_DIR)/server
CLIENT_TARGET := $(BIN_DIR)/client
TEST_TARGET   := $(BIN_DIR)/test_db

COMMON_SRCS := src/common/protocol.cpp
DB_SRCS     := src/database/db.cpp

SERVER_SRCS := \
	$(COMMON_SRCS) \
	$(DB_SRCS) \
	src/server/session_manager.cpp \
	src/server/client_handler.cpp \
	src/server/server_main.cpp

CLIENT_SRCS := \
	$(COMMON_SRCS) \
	src/client/receiver_thread.cpp \
	src/client/client_main.cpp

TEST_SRCS := \
	$(COMMON_SRCS) \
	$(DB_SRCS) \
	tests/test_db.cpp

SERVER_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SERVER_SRCS))
CLIENT_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CLIENT_SRCS))
TEST_OBJS   := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(TEST_SRCS))

.PHONY: all server client test_db test clean run-server

all: server client test_db

server: $(SERVER_TARGET)

client: $(CLIENT_TARGET)

test_db: $(TEST_TARGET)

$(SERVER_TARGET): $(SERVER_OBJS) | $(BIN_DIR) $(DATA_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(CLIENT_TARGET): $(CLIENT_OBJS) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(TEST_TARGET): $(TEST_OBJS) | $(BIN_DIR) $(DATA_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR) $(DATA_DIR):
	@mkdir -p $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

run-server: server
	./$(SERVER_TARGET) 8080 $(DATA_DIR)/server.db

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(DATA_DIR)/*.db

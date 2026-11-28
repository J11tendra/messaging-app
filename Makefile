CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude -pthread
LDFLAGS  := -pthread
LDLIBS   := -lsqlite3

BUILD_DIR := build
BIN_DIR   := bin
DATA_DIR  := data

# ---- modules ---------------------------------------------------------------
COMMON_SRCS := src/common/protocol.cpp src/common/network.cpp src/common/console.cpp
DB_SRCS     := src/database/db.cpp
SERVER_LIB  := src/server/user_registry.cpp src/server/session_manager.cpp \
               src/server/connection_tracker.cpp src/server/command_handler.cpp \
               src/server/client_handler.cpp
CLIENT_LIB  := src/client/client_commands.cpp src/client/receiver_thread.cpp

SERVER_SRCS := $(COMMON_SRCS) $(DB_SRCS) $(SERVER_LIB) src/server/server_main.cpp
CLIENT_SRCS := $(COMMON_SRCS) $(CLIENT_LIB) src/client/client_main.cpp

UNIT_TESTS  := test_protocol test_db test_user_registry test_session_manager test_client_commands
TEST_BINS   := $(addprefix $(BIN_DIR)/,$(UNIT_TESTS))

obj = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(1))

.PHONY: all server client tests test unit-test integration-test run-server clean

all: server client tests

server: $(BIN_DIR)/server
client: $(BIN_DIR)/client
tests:  $(TEST_BINS)

$(BIN_DIR)/server: $(call obj,$(SERVER_SRCS)) | $(BIN_DIR) $(DATA_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BIN_DIR)/client: $(call obj,$(CLIENT_SRCS)) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/test_protocol: $(call obj,tests/test_protocol.cpp $(COMMON_SRCS)) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/test_db: $(call obj,tests/test_db.cpp $(DB_SRCS)) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BIN_DIR)/test_user_registry: $(call obj,tests/test_user_registry.cpp src/server/user_registry.cpp $(COMMON_SRCS)) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/test_session_manager: $(call obj,tests/test_session_manager.cpp src/server/session_manager.cpp $(COMMON_SRCS)) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/test_client_commands: $(call obj,tests/test_client_commands.cpp src/client/client_commands.cpp $(COMMON_SRCS)) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BIN_DIR) $(DATA_DIR):
	@mkdir -p $@

# ---- tests -----------------------------------------------------------------
unit-test: tests
	@set -e; for t in $(TEST_BINS); do ./$$t; done

integration-test: server client
	./tests/integration_test.sh

test: unit-test integration-test

run-server: server
	./$(BIN_DIR)/server 8080 $(DATA_DIR)/server.db config/users.txt

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(DATA_DIR)/*.db

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

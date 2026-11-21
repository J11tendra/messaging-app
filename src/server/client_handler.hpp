#pragma once

#include <atomic>

namespace chat {

class Database;
class SessionManager;

struct ClientHandlerContext {
    int client_fd = -1;
    SessionManager* sessions = nullptr;
    Database* db = nullptr;
    std::atomic<bool>* running = nullptr;
};

// Worker thread entry point for handling a single connected client over TCP.
void handle_client(ClientHandlerContext ctx);

}  // namespace chat

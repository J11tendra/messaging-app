#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include <atomic>

namespace chat {

class SessionManager;

struct ClientHandlerContext {
    int client_fd{-1};
    SessionManager *sessions{nullptr};
    std::atomic<bool> *running{nullptr};
};

// Worker thread routine to handle communications with a single connected client
void handle_client(ClientHandlerContext ctx);

}  // namespace chat

#endif // CLIENT_HANDLER_H

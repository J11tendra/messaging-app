/**
 * @file server_context.h
 * @brief Shared server state handed to every worker thread.
 */
#ifndef SERVER_CONTEXT_H
#define SERVER_CONTEXT_H

#include <atomic>
#include <cstddef>

namespace chat {

class SessionManager;
class UserRegistry;
class ConnectionTracker;

struct ServerContext {
    SessionManager *sessions{nullptr};       // online users (mutex protected)
    const UserRegistry *users{nullptr};      // valid users (read-only after start-up)
    ConnectionTracker *connections{nullptr}; // all sockets + worker threads
    std::atomic<bool> *running{nullptr};     // false once shutdown has begun
    size_t max_clients{64};                  // concurrent logged-in sessions allowed
};

}  // namespace chat

#endif  // SERVER_CONTEXT_H

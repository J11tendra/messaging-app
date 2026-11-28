/**
 * @file client_handler.h
 * @brief Server worker thread: one per connected client.
 */
#ifndef CLIENT_HANDLER_H
#define CLIENT_HANDLER_H

#include <string>

#include "server_context.h"

namespace chat {

/**
 * Thread entry point. Performs the connect handshake (authentication against
 * the valid-user list, duplicate-session rejection), delivers unread
 * messages, then runs the command loop until the client disconnects or the
 * server shuts down. Always removes the user from the online list and
 * releases the socket before returning.
 */
void handle_client(int client_fd, std::string peer, ServerContext ctx);

}  // namespace chat

#endif  // CLIENT_HANDLER_H

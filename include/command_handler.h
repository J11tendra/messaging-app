/**
 * @file command_handler.h
 * @brief Server-side handling of each client command (sendmessage, getmessages, ...).
 */
#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "protocol.h"
#include "server_context.h"
#include "session_manager.h"

namespace chat {

/**
 * Executes one command from a logged-in client and sends the reply.
 * @return false when the client asked to disconnect (shutdown), true otherwise.
 */
bool handle_command(const ServerContext &ctx, const SessionPtr &session, const Packet &request);

/** Sends queued offline messages to a user who just logged in, then marks them read. */
void deliver_unread_messages(const SessionPtr &session);

}  // namespace chat

#endif  // COMMAND_HANDLER_H

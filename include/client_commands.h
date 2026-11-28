/**
 * @file client_commands.h
 * @brief Client-side command parsing: turns a typed line into a request Packet.
 *        Pure functions (no I/O) so they can be unit tested.
 */
#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include <string>

#include "protocol.h"

namespace chat {

enum class ClientCommand {
    Empty,
    Help,
    Connect,         // connect <server IP> <user Name>
    SendMessage,     // sendmessage <user> <message>
    GetMessages,     // getmessages <user>
    DeleteMessages,  // deletemessages <user>
    GetUserList,     // getuserlist
    Shutdown,        // shutdown
    Unknown,
    Invalid          // known command, bad arguments (see error)
};

struct ParsedCommand {
    ClientCommand kind{ClientCommand::Empty};
    std::string arg1;   // IP / target user
    std::string arg2;   // user name / message text
    std::string error;  // usage message when kind == Invalid / Unknown
};

ParsedCommand parse_command(const std::string &line);

/** Builds the request for a server command. Returns false for local-only commands. */
bool build_request(const ParsedCommand &cmd, const std::string &current_user, Packet &out);

/** Text printed by the "help" command. */
std::string help_text();

}  // namespace chat

#endif  // CLIENT_COMMANDS_H

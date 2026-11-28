/**
 * @file client_commands.cpp
 * @brief Parsing and validation of CLI commands.
 */
#include "client_commands.h"

#include <sstream>

namespace chat {

namespace {
std::string trim(const std::string &s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

ParsedCommand invalid(const std::string &usage) {
    ParsedCommand c;
    c.kind = ClientCommand::Invalid;
    c.error = "Usage: " + usage;
    return c;
}
}  // namespace

ParsedCommand parse_command(const std::string &raw) {
    const std::string line = trim(raw);
    ParsedCommand cmd;
    if (line.empty()) return cmd;

    std::istringstream iss(line);
    std::string word;
    iss >> word;

    if (word == "help") {
        cmd.kind = ClientCommand::Help;
    } else if (word == "connect") {
        std::string extra;
        iss >> cmd.arg1 >> cmd.arg2 >> extra;
        if (cmd.arg1.empty() || cmd.arg2.empty() || !extra.empty())
            return invalid("connect <server IP> <user Name>");
        if (cmd.arg2.size() >= MAX_USERNAME_LEN)
            return invalid("connect <server IP> <user Name>  (user name max 31 characters)");
        cmd.kind = ClientCommand::Connect;
    } else if (word == "sendmessage") {
        iss >> cmd.arg1;
        std::string rest;
        std::getline(iss, rest);
        cmd.arg2 = trim(rest);
        if (cmd.arg1.empty() || cmd.arg2.empty()) return invalid("sendmessage <user> <message>");
        if (cmd.arg2.size() > MAX_MESSAGE_TEXT) {
            ParsedCommand c;
            c.kind = ClientCommand::Invalid;
            c.error = "Message too long (max " + std::to_string(MAX_MESSAGE_TEXT) + " characters).";
            return c;
        }
        cmd.kind = ClientCommand::SendMessage;
    } else if (word == "getmessages" || word == "deletemessages") {
        std::string extra;
        iss >> cmd.arg1 >> extra;
        if (cmd.arg1.empty() || !extra.empty()) return invalid(word + " <user>");
        cmd.kind = word == "getmessages" ? ClientCommand::GetMessages : ClientCommand::DeleteMessages;
    } else if (word == "getuserlist") {
        cmd.kind = ClientCommand::GetUserList;
    } else if (word == "shutdown") {
        cmd.kind = ClientCommand::Shutdown;
    } else {
        cmd.kind = ClientCommand::Unknown;
        cmd.error = "Unknown command '" + word + "'. Type 'help' for the list of commands.";
    }
    return cmd;
}

bool build_request(const ParsedCommand &cmd, const std::string &me, Packet &out) {
    switch (cmd.kind) {
        case ClientCommand::Connect:
            out = create_packet(CommandType::CMD_CONNECT, cmd.arg2);
            return true;
        case ClientCommand::SendMessage:
            out = create_packet(CommandType::CMD_SEND_MSG, me, cmd.arg1, cmd.arg2);
            return true;
        case ClientCommand::GetMessages:
            out = create_packet(CommandType::CMD_GET_MSGS, me, cmd.arg1);
            return true;
        case ClientCommand::DeleteMessages:
            out = create_packet(CommandType::CMD_DEL_MSGS, me, cmd.arg1);
            return true;
        case ClientCommand::GetUserList:
            out = create_packet(CommandType::CMD_GET_USERLIST, me);
            return true;
        case ClientCommand::Shutdown:
            out = create_packet(CommandType::CMD_SHUTDOWN_CLIENT, me);
            return true;
        default:
            return false;
    }
}

std::string help_text() {
    return "Commands:\n"
           "  connect <server IP> <user Name>  log in to the server\n"
           "  sendmessage <user> <message>     send a message (delivered live or at next login)\n"
           "  getmessages <user>               show all messages exchanged with <user>\n"
           "  deletemessages <user>            delete all messages to/from <user> on the server\n"
           "  getuserlist                      list users currently online\n"
           "  shutdown                         disconnect and exit\n"
           "  help                             show this list";
}

}  // namespace chat

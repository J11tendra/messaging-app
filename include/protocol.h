/**
 * @file protocol.h
 * @brief Wire protocol shared by client and server.
 *
 * Every message on the TCP stream is exactly one fixed-size Packet
 * (1,096 bytes). Fixed framing means the receiver always knows how many
 * bytes make up one message, so the protocol is unambiguous even though TCP
 * is a byte stream.
 *
 *   Client -> Server : CommandType  (what the user asked for)
 *   Server -> Client : ResponseType (replies, pushed messages, broadcasts)
 *
 * The packet layout is defined here; sending/receiving lives in network.h.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace chat {

constexpr size_t MAX_USERNAME_LEN = 32;    // including the terminating '\0'
constexpr size_t MAX_PAYLOAD_LEN  = 1024;  // including the terminating '\0'
constexpr size_t MAX_MESSAGE_TEXT = MAX_PAYLOAD_LEN - 1;
constexpr int    DEFAULT_PORT     = 8080;

/** Requests sent from client to server. */
enum class CommandType : uint16_t {
    CMD_CONNECT         = 1,  // connect <server IP> <user Name>  (sender = user name)
    CMD_SEND_MSG        = 2,  // sendmessage <user> <message>     (recipient, payload)
    CMD_GET_MSGS        = 3,  // getmessages <user>               (recipient)
    CMD_DEL_MSGS        = 4,  // deletemessages <user>            (recipient)
    CMD_GET_USERLIST    = 5,  // getuserlist
    CMD_SHUTDOWN_CLIENT = 6   // shutdown
};

/** Replies and notifications sent from server to client. */
enum class ResponseType : uint16_t {
    RESP_SUCCESS    = 100,  // command succeeded; payload = human readable text
    RESP_ERROR      = 101,  // command failed; status = StatusCode, payload = reason
    RESP_USERLIST   = 102,  // payload = list of online users
    RESP_HISTORY    = 103,  // one line of conversation history (sent once per message)
    RESP_UNREAD     = 104,  // one unread (offline) message, delivered at login
    MSG_REALTIME    = 105,  // message pushed to an online user (sender, payload)
    SERVER_SHUTDOWN = 106,  // broadcast: "Server shutting down..."
    RESP_INFO       = 107   // informational line (e.g. "You have 3 unread messages")
};

enum class StatusCode : uint16_t {
    STATUS_OK                  = 0,
    ERR_USER_NOT_FOUND         = 1,
    ERR_USER_ALREADY_CONNECTED = 2,
    ERR_INVALID_USER           = 3,
    ERR_DATABASE_FAILURE       = 4,
    ERR_GENERAL                = 5,
    ERR_SERVER_FULL            = 6,
    ERR_PROTOCOL               = 7
};

#pragma pack(push, 1)
/** One protocol frame. Integers travel in network byte order (see network.cpp). */
struct Packet {
    uint16_t type{0};                     // CommandType or ResponseType
    uint16_t status{0};                   // StatusCode
    char sender[MAX_USERNAME_LEN]{};      // '\0'-terminated
    char recipient[MAX_USERNAME_LEN]{};   // '\0'-terminated
    uint32_t payload_len{0};              // bytes used in payload (excluding '\0')
    char payload[MAX_PAYLOAD_LEN]{};      // '\0'-terminated

    void set_sender(const std::string &name);
    void set_recipient(const std::string &name);
    void set_payload(const std::string &text);
    std::string get_sender() const;
    std::string get_recipient() const;
    std::string get_payload() const;
};
#pragma pack(pop)

static_assert(sizeof(Packet) == 1096, "Packet must be exactly 1096 bytes");

Packet create_packet(uint16_t type,
                     const std::string &sender = "",
                     const std::string &recipient = "",
                     const std::string &payload = "",
                     StatusCode status = StatusCode::STATUS_OK);

Packet create_packet(CommandType type,
                     const std::string &sender = "",
                     const std::string &recipient = "",
                     const std::string &payload = "");

Packet create_packet(ResponseType type,
                     const std::string &sender = "",
                     const std::string &recipient = "",
                     const std::string &payload = "");

Packet create_error_packet(StatusCode code, const std::string &error_msg);

}  // namespace chat

#endif  // PROTOCOL_H

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <string>

namespace chat {

constexpr size_t MAX_USERNAME_LEN = 32;
constexpr size_t MAX_PAYLOAD_LEN  = 1024;
constexpr int DEFAULT_PORT        = 8080;

// Command Types (Sent from Client to Server)
enum class CommandType : uint16_t {
    CMD_CONNECT = 1,       // connect <server IP> <user Name>
    CMD_SEND_MSG = 2,      // sendmessage <user> <message>
    CMD_GET_MSGS = 3,      // getmessages <user>
    CMD_DEL_MSGS = 4,      // deletemessages <user>
    CMD_GET_USERLIST = 5,  // getuserlist
    CMD_SHUTDOWN_CLIENT = 6// shutdown
};

// Response / Notification Types (Sent from Server to Client)
enum class ResponseType : uint16_t {
    RESP_SUCCESS = 100,    // General operation success
    RESP_ERROR = 101,      // Operation failed; error message in payload
    RESP_USERLIST = 102,   // Active connected user list
    RESP_HISTORY = 103,    // Retrieved message history
    RESP_UNREAD = 104,     // Unread messages queued during offline period
    MSG_REALTIME = 105,    // Real-time incoming message from another user
    SERVER_SHUTDOWN = 106  // Broadcast notifying clients that server is shutting down
};

// Status Codes
enum class StatusCode : uint16_t {
    STATUS_OK = 0,
    ERR_USER_NOT_FOUND = 1,
    ERR_USER_ALREADY_CONNECTED = 2,
    ERR_INVALID_USER = 3,
    ERR_DATABASE_FAILURE = 4,
    ERR_GENERAL = 5
};

// Primary Network Wire Packet (1,096 bytes total)
#pragma pack(push, 1)
struct Packet {
    uint16_t type{0};                     // CommandType or ResponseType as uint16_t
    uint16_t status{0};                   // StatusCode as uint16_t
    char sender[MAX_USERNAME_LEN]{0};     // Sender username (null-terminated)
    char recipient[MAX_USERNAME_LEN]{0};  // Target username (null-terminated)
    uint32_t payload_len{0};              // Length of payload data
    char payload[MAX_PAYLOAD_LEN]{0};     // Message text / history / user list / error string

    void set_sender(const std::string &name);
    void set_recipient(const std::string &name);
    void set_payload(const std::string &text);
    std::string get_sender() const;
    std::string get_recipient() const;
    std::string get_payload() const;
};
#pragma pack(pop)

static_assert(sizeof(Packet) == 1096, "Packet struct must be exactly 1096 bytes");

// Safe socket transmission helpers (endian-safe: translates to/from network byte order)
bool send_packet(int socket_fd, const Packet &pkt);
bool recv_packet(int socket_fd, Packet &pkt);

// Factory helpers
Packet create_packet(uint16_t type,
                     const std::string &sender = "",
                     const std::string &recipient = "",
                     const std::string &payload = "",
                     StatusCode status = StatusCode::STATUS_OK);

Packet create_error_packet(StatusCode code, const std::string &error_msg);

}  // namespace chat

#endif // PROTOCOL_H

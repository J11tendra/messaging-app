#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstddef>
#include <string>

constexpr size_t MAX_USERNAME_LEN = 32;
constexpr size_t MAX_PAYLOAD_LEN  = 1024;
constexpr int DEFAULT_PORT        = 8080;

// Command Types (Sent from Client to Server)
enum class CommandType : uint16_t {
    CMD_CONNECT = 1,       // connect <server IP> <user Name>
    CMD_SEND_MSG,          // sendmessage <user> <message>
    CMD_GET_MSGS,          // getmessages <user>
    CMD_DEL_MSGS,          // deletemessages <user>
    CMD_GET_USERLIST,      // getuserlist
    CMD_SHUTDOWN_CLIENT    // shutdown
};

// Response / Notification Types (Sent from Server to Client)
enum class ResponseType : uint16_t {
    RESP_SUCCESS = 100,    // General operation success
    RESP_ERROR,            // Operation failed; error message in payload
    RESP_USERLIST,         // Active connected user list
    RESP_HISTORY,          // Retrieved message history
    RESP_UNREAD,           // Unread messages queued during offline period
    MSG_REALTIME,          // Real-time incoming message from another user
    SERVER_SHUTDOWN        // Broadcast notifying clients that server is shutting down
};

// Status Codes
enum class StatusCode : uint16_t {
    STATUS_OK = 0,
    ERR_USER_NOT_FOUND,
    ERR_USER_ALREADY_CONNECTED,
    ERR_INVALID_USER,
    ERR_DATABASE_FAILURE,
    ERR_GENERAL
};

// Primary Network Packet Structure
#pragma pack(push, 1)
struct Packet {
    uint16_t type{0};                     // CommandType or ResponseType as uint16_t
    uint16_t status{0};                   // StatusCode as uint16_t
    char sender[MAX_USERNAME_LEN]{0};     // Sender username
    char recipient[MAX_USERNAME_LEN]{0};  // Target username
    uint32_t payload_len{0};              // Length of actual payload data
    char payload[MAX_PAYLOAD_LEN]{0};     // Text / history / user list / error string

    // C++ Convenience Helpers
    void set_sender(const std::string &name);
    void set_recipient(const std::string &name);
    void set_payload(const std::string &text);
    std::string get_sender() const;
    std::string get_recipient() const;
    std::string get_payload() const;
};
#pragma pack(pop)

// Safe socket transmission helpers (return true on success, false on disconnect/error)
bool send_packet(int socket_fd, const Packet &pkt);
bool recv_packet(int socket_fd, Packet &pkt);

// Factory helpers
Packet create_packet(uint16_t type, const std::string &sender = "", const std::string &recipient = "", const std::string &payload = "");
Packet create_error_packet(StatusCode code, const std::string &error_msg);

#endif // PROTOCOL_H

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <arpa/inet.h>

namespace chat {

constexpr uint16_t DEFAULT_SERVER_PORT = 8080;
constexpr size_t MAX_USERNAME_LEN = 32;
constexpr size_t PAYLOAD_BUFFER_LEN = 1024;

// Fixed-size binary packet layout (1,096 bytes total)[cite: 1]:
//   uint32_t cmd         (4 bytes)
//   uint32_t status      (4 bytes)
//   char sender[32]      (32 bytes)
//   char recipient[32]   (32 bytes)
//   char payload[1024]   (1,024 bytes)
// Total: 4 + 4 + 32 + 32 + 1024 = 1,096 bytes.
constexpr size_t PACKET_SIZE_BYTES = 1096;

enum class CommandType : uint32_t {
    CONNECT = 1,
    SEND_MESSAGE = 2,
    GET_MESSAGES = 3,
    DELETE_MESSAGES = 4,
    GET_USER_LIST = 5,
    SHUTDOWN = 6,
    SERVER_NOTICE = 7
};

enum class StatusCode : uint32_t {
    OK = 0,
    ERR_INVALID_USER = 1,
    ERR_DUPLICATE_LOGIN = 2,
    ERR_DB_ERROR = 3,
    ERR_BAD_REQUEST = 4,
    SERVER_SHUTTING_DOWN = 5
};

#pragma pack(push, 1)
struct Packet {
    uint32_t cmd;                        // Network byte order (htonl / ntohl)[cite: 1]
    uint32_t status;                     // Network byte order (htonl / ntohl)[cite: 1]
    char sender[MAX_USERNAME_LEN];       // Null-padded / null-terminated
    char recipient[MAX_USERNAME_LEN];    // Null-padded / null-terminated
    char payload[PAYLOAD_BUFFER_LEN];    // Message text, user list, or history chunk
};
#pragma pack(pop)

static_assert(sizeof(Packet) == PACKET_SIZE_BYTES,
              "Packet struct must be exactly 1,096 bytes");

inline Packet make_packet(CommandType cmd,
                          StatusCode status,
                          const std::string& sender,
                          const std::string& recipient,
                          const std::string& payload) {
    Packet pkt{};
    pkt.cmd = htonl(static_cast<uint32_t>(cmd));
    pkt.status = htonl(static_cast<uint32_t>(status));

    const size_t copy_len = (payload.size() < PAYLOAD_BUFFER_LEN - 1)
                                ? payload.size()
                                : (PAYLOAD_BUFFER_LEN - 1);

    std::strncpy(pkt.sender, sender.c_str(), MAX_USERNAME_LEN - 1);
    pkt.sender[MAX_USERNAME_LEN - 1] = '\0';

    std::strncpy(pkt.recipient, recipient.c_str(), MAX_USERNAME_LEN - 1);
    pkt.recipient[MAX_USERNAME_LEN - 1] = '\0';

    if (copy_len > 0) {
        std::memcpy(pkt.payload, payload.data(), copy_len);
    }
    pkt.payload[copy_len] = '\0';

    return pkt;
}

inline CommandType get_command(const Packet& pkt) {
    return static_cast<CommandType>(ntohl(pkt.cmd));
}

inline StatusCode get_status(const Packet& pkt) {
    return static_cast<StatusCode>(ntohl(pkt.status));
}

inline std::string get_sender_str(const Packet& pkt) {
    char buf[MAX_USERNAME_LEN + 1];
    std::memcpy(buf, pkt.sender, MAX_USERNAME_LEN);
    buf[MAX_USERNAME_LEN] = '\0';
    return std::string(buf);
}

inline std::string get_recipient_str(const Packet& pkt) {
    char buf[MAX_USERNAME_LEN + 1];
    std::memcpy(buf, pkt.recipient, MAX_USERNAME_LEN);
    buf[MAX_USERNAME_LEN] = '\0';
    return std::string(buf);
}

inline std::string get_payload_str(const Packet& pkt) {
    char buf[PAYLOAD_BUFFER_LEN + 1];
    std::memcpy(buf, pkt.payload, PAYLOAD_BUFFER_LEN);
    buf[PAYLOAD_BUFFER_LEN] = '\0';
    return std::string(buf);
}

}  // namespace chat

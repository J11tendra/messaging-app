# Multi-User Client-Server Messaging Application

A robust, multi-threaded client-server messaging system implemented in **C++17 (Linux)** demonstrating core Operating System concepts:
- **Inter-Process Communication (IPC):** Reliable stream sockets (TCP).
- **Concurrency & Multi-Threading:** Worker thread dispatch for concurrent client handling.
- **Synchronization:** Mutexes (`std::mutex`, `std::lock_guard`) for shared data structures (Active User Sessions, SQLite database access).
- **Persistent Storage:** SQLite database for message history, offline queuing, and authorized user directory.
- **Signal Handling:** Graceful server/client shutdown handling (`SIGINT` broadcast).

---

## 📁 Repository Structure

```text
Client-Server/
├── README.md               # Complete architectural blueprint and handoff guide
├── Makefile                # Unified build file for Linux
├── protocol.h              # Shared network wire protocol, enums, Packet struct
├── protocol.cpp            # Socket send/recv loops, network byte-order translation
├── db.h                    # Database interface (SQLite)
├── db.cpp                  # Database implementation & SQL queries (Thread-safe)
├── session_manager.h       # [NEXT] In-memory active user directory (Thread-safe)
├── session_manager.cpp     # [NEXT] Session mapping: username -> socket_fd
├── server.cpp              # [NEXT] Server process entry, socket listener, thread dispatch
└── client.cpp              # [NEXT] Client CLI, dual-thread reader/writer
```

---

## 🚦 Project Status & Team Handoff Roadmap

| Component | Responsible | Status | Files |
| :--- | :--- | :--- | :--- |
| **1. Protocol & Packet Serialization** | Module Lead | ✅ **Complete** | `protocol.h`, `protocol.cpp` |
| **2. Storage & SQLite Persistence** | Module Lead | ✅ **Complete & Tested** | `db.h`, `db.cpp`, `test_db.cpp` |
| **3. In-Memory Session Management** | Teammate | ⏳ Planned | `session_manager.h`, `session_manager.cpp` |
| **4. Server Core & Worker Threads** | Teammate | ⏳ Planned | `server.cpp` |
| **5. Client CLI & Dual Threading** | Teammate | ⏳ Planned | `client.cpp` |
| **6. Integration Testing & Demo** | Full Group | ⏳ Planned | End-to-end multi-client test |

---

## 📡 1. Network Protocol Specification (Completed)

All communication between client and server uses a binary-packed structure `Packet` defined in `protocol.h`.

### Packet Anatomy (`sizeof(Packet) = 1064 bytes`)
```text
+----------------+----------------+-------------------------------+-------------------------------+
| type (uint16)  | status (uint16)| sender (char[32])             | recipient (char[32])          |
+----------------+----------------+-------------------------------+-------------------------------+
| payload_len (uint32)            | payload (char[1024])                                          |
+---------------------------------+---------------------------------------------------------------+
```

### Command & Response Codes

| Type Enum | Code | Origin $\rightarrow$ Dest | Purpose | Payload Content |
| :--- | :--- | :--- | :--- | :--- |
| `CMD_CONNECT` | 1 | Client $\rightarrow$ Server | Initial login handshake | Empty (Sender set to username) |
| `CMD_SEND_MSG` | 2 | Client $\rightarrow$ Server | Send chat to target user | Message text |
| `CMD_GET_MSGS` | 3 | Client $\rightarrow$ Server | Retrieve chat history | Empty (Recipient set to target) |
| `CMD_DEL_MSGS` | 4 | Client $\rightarrow$ Server | Delete chat history | Empty (Recipient set to target) |
| `CMD_GET_USERLIST`| 5 | Client $\rightarrow$ Server | Request active online users | Empty |
| `CMD_SHUTDOWN_CLIENT`| 6| Client $\rightarrow$ Server | Graceful client logout | Empty |
| `RESP_SUCCESS` | 100 | Server $\rightarrow$ Client | Operation succeeded | Informational message |
| `RESP_ERROR` | 101 | Server $\rightarrow$ Client | Operation failed | Error description |
| `RESP_USERLIST`| 102 | Server $\rightarrow$ Client | Online user list response | Comma/newline separated users |
| `RESP_HISTORY` | 103 | Server $\rightarrow$ Client | History response | Formatted conversation logs |
| `RESP_UNREAD` | 104 | Server $\rightarrow$ Client | Offline message playback | Unread message text |
| `MSG_REALTIME` | 105 | Server $\rightarrow$ Client | Real-time chat notification | Message text |
| `SERVER_SHUTDOWN`| 106 | Server $\rightarrow$ Client | Broadcast before server exits | `"Server shutting down..."` |

### Status Codes (`StatusCode`)
* `STATUS_OK = 0`
* `ERR_USER_NOT_FOUND = 1`
* `ERR_USER_ALREADY_CONNECTED = 2`
* `ERR_INVALID_USER = 3`
* `ERR_DATABASE_FAILURE = 4`
* `ERR_GENERAL = 5`

---

## 💾 2. Database Specification (`db.h` / `db.cpp`)

The server uses SQLite (`server.db`) to persist data. All operations are synchronized via a mutex.

### Schema
```sql
-- Valid authorized users (pre-seeded with: alice, bob, charlie, david, eve)
CREATE TABLE IF NOT EXISTS users (
    username TEXT PRIMARY KEY NOT NULL
);

-- Message storage & offline queue
CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    sender TEXT NOT NULL,
    recipient TEXT NOT NULL,
    content TEXT NOT NULL,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    is_read INTEGER DEFAULT 0 -- 0 = offline queued, 1 = delivered/read
);
```

### Core Database APIs
```cpp
bool db_init(const std::string &db_path = "server.db");
void db_close();
bool db_is_valid_user(const std::string &username);
bool db_save_message(const std::string &sender, const std::string &recipient, const std::string &content, bool is_read);
std::vector<Packet> db_get_unread_messages(const std::string &username);
std::string db_get_conversation(const std::string &user1, const std::string &user2);
bool db_delete_conversation(const std::string &user1, const std::string &user2);
```

---

## 🛠️ 3. Teammate Handoff: Blueprint for Remaining Modules

### A. In-Memory Session Manager (`session_manager.h`, `session_manager.cpp`)
> **OS Concept:** Shared Data Structure & Mutex Synchronization.

Must maintain live client socket mappings in RAM:
```cpp
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>

class SessionManager {
private:
    std::unordered_map<std::string, int> active_sessions; // username -> socket_fd
    std::mutex mtx;

public:
    bool add_session(const std::string &username, int socket_fd) {
        std::lock_guard<std::mutex> lock(mtx);
        if (active_sessions.count(username)) return false; // Duplicate session!
        active_sessions[username] = socket_fd;
        return true;
    }

    void remove_session(const std::string &username) {
        std::lock_guard<std::mutex> lock(mtx);
        active_sessions.erase(username);
    }

    int get_socket(const std::string &username) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = active_sessions.find(username);
        return (it != active_sessions.end()) ? it->second : -1;
    }

    std::vector<std::string> get_online_users() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<std::string> users;
        for (const auto &[name, fd] : active_sessions) users.push_back(name);
        return users;
    }

    std::vector<int> get_all_sockets() {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<int> fds;
        for (const auto &[name, fd] : active_sessions) fds.push_back(fd);
        return fds;
    }
};
```

---

### B. Server Core (`server.cpp`)
> **OS Concept:** Multi-Threading (`std::thread`), Socket IPC, Signal Handling.

1. **Initialization:**
   * Call `db_init("server.db")`.
   * Bind server socket to `0.0.0.0:8080`, call `listen()`.
   * Register signal handler for `SIGINT` (Ctrl+C).
2. **Main Accept Loop:**
   * Run `client_fd = accept(server_fd, ...)`.
   * Launch detached worker thread: `std::thread(client_handler, client_fd).detach();`.
3. **Worker Thread Logic (`client_handler(int client_fd)`):**
   * **Handshake:** Read first packet. Must be `CMD_CONNECT`.
     * Check `db_is_valid_user(username)`. If invalid $\rightarrow$ send `RESP_ERROR` and close.
     * Check `session_mgr.add_session(username, client_fd)`. If duplicate $\rightarrow$ send `RESP_ERROR` and close.
     * Send `RESP_SUCCESS`.
     * Fetch unread messages: `auto unread = db_get_unread_messages(username);`
     * Send each unread packet to client.
   * **Command Loop:** `while (recv_packet(client_fd, pkt))`
     * `CMD_SEND_MSG`:
       * If recipient is invalid user in DB $\rightarrow$ send error back.
       * Check if recipient is online with `session_mgr.get_socket(recipient)`.
       * If online $\rightarrow$ send `MSG_REALTIME` packet to recipient's socket AND `db_save_message(..., is_read=true)`.
       * If offline $\rightarrow$ `db_save_message(..., is_read=false)`.
       * Send `RESP_SUCCESS` to sender.
     * `CMD_GET_MSGS`:
       * Check if target user is valid.
       * Call `db_get_conversation(sender, recipient)`, send back `RESP_HISTORY`.
     * `CMD_DEL_MSGS`:
       * Call `db_delete_conversation(sender, recipient)`, send back `RESP_SUCCESS`.
     * `CMD_GET_USERLIST`:
       * Call `session_mgr.get_online_users()`, join with comma, send back `RESP_USERLIST`.
     * `CMD_SHUTDOWN_CLIENT`:
       * Break out of loop.
   * **Cleanup on Thread Exit:**
     * `session_mgr.remove_session(username);`
     * `close(client_fd);`
4. **Graceful Server Shutdown (`handle_sigint`):**
   * Loop through `session_mgr.get_all_sockets()`.
   * Send `Packet` with `type = SERVER_SHUTDOWN`, payload `"Server shutting down..."`.
   * Close all sockets and call `db_close()`.
   * Terminate process.

---

### C. Client Process (`client.cpp`)
> **OS Concept:** Dual-Threaded CLI (Simultaneous Non-Blocking Read & Write).

1. **Connection:**
   * Parse command line: `./client <server_ip> <username>`.
   * Connect socket to `server_ip:8080`.
   * Send `CMD_CONNECT` packet. Wait for response. If error $\rightarrow$ exit.
2. **Spawn Receiver Thread (`std::thread(receiver_loop, sock_fd).detach();`):**
   * Continuously calls `recv_packet(sock_fd, pkt)`.
   * If `type == MSG_REALTIME`:
     ```text
     \nMessage from <sender>: <payload>
     > [flush prompt]
     ```
   * If `type == RESP_UNREAD`:
     ```text
     \n[Unread] Message from <sender>: <payload>
     > [flush prompt]
     ```
   * If `type == SERVER_SHUTDOWN`:
     ```text
     \nServer shutting down...
     ```
     `exit(0);`
   * If `type == RESP_USERLIST` or `RESP_HISTORY` or `RESP_ERROR`:
     Print formatted payload to console.
3. **Main Thread (Interactive User Prompt):**
   * Loop `std::getline(std::cin, line)`:
   * Parse CLI commands:
     * `sendmessage <user> <message>` $\rightarrow$ sends `CMD_SEND_MSG`.
     * `getmessages <user>` $\rightarrow$ sends `CMD_GET_MSGS`.
     * `deletemessages <user>` $\rightarrow$ sends `CMD_DEL_MSGS`.
     * `getuserlist` $\rightarrow$ sends `CMD_GET_USERLIST`.
     * `shutdown` $\rightarrow$ sends `CMD_SHUTDOWN_CLIENT`, closes socket, terminates.

---

## 🔨 Compilation Instructions (Linux)

### Using `Makefile`
```bash
# Build both server and client
make

# Clean build artifacts
make clean
```

### Manual GCC Compilation
```bash
# Compile Protocol
g++ -std=c++17 -Wall -Wextra -c protocol.cpp -o protocol.o

# Compile Database
g++ -std=c++17 -Wall -Wextra -c db.cpp -o db.o

# Compile Server
g++ -std=c++17 -Wall -Wextra server.cpp protocol.o db.o -lsqlite3 -lpthread -o server

# Compile Client
g++ -std=c++17 -Wall -Wextra client.cpp protocol.o -lpthread -o client
```

---

## 🧪 Demo & Grading Rubric Checklist

When evaluating or presenting to the professor:
1. **Concurrency ($\ge 5$ clients):**
   * Launch `./server` in Terminal 1.
   * Launch 5 separate terminals:
     * `./client 127.0.0.1 alice`
     * `./client 127.0.0.1 bob`
     * `./client 127.0.0.1 charlie`
     * `./client 127.0.0.1 david`
     * `./client 127.0.0.1 eve`
2. **Duplicate Login Prevention:**
   * In a 6th terminal, try `./client 127.0.0.1 alice`.
   * Verify server rejects with duplicate session error.
3. **Real-time Messaging:**
   * Alice sends: `sendmessage bob Hello Bob!`.
   * Verify message appears immediately on Bob's console.
4. **Offline Queuing:**
   * Exit Bob's client (`shutdown`).
   * Alice sends: `sendmessage bob Are you there?`.
   * Re-launch Bob's client.
   * Verify Bob immediately sees the unread message upon login.
5. **History & Deletion:**
   * Bob runs `getmessages alice` $\rightarrow$ full history is displayed.
   * Bob runs `deletemessages alice` $\rightarrow$ conversation is wiped.
   * Bob runs `getmessages alice` $\rightarrow$ returns empty.
6. **Graceful Server Shutdown:**
   * Press `Ctrl+C` in the server terminal.
   * Verify all 5 clients display `"Server shutting down..."` and exit gracefully.

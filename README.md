# Multi-User Client-Server Messaging Application

A robust, multi-threaded client-server messaging system implemented in **C++17 (Linux)** demonstrating core Operating System concepts.

---

## 📋 Table of Contents
1. [Overview & Core OS Concepts](#-overview--core-os-concepts)
2. [System Architecture](#-system-architecture)
3. [Repository Structure](#-repository-structure)
4. [Wire Protocol & Packet Anatomy](#-wire-protocol--packet-anatomy)
5. [Commands & Responses](#-commands--responses)
6. [Compilation & Build Guide (Linux)](#-compilation--build-guide-linux)
7. [Step-by-Step Demonstration Guide (Evaluation Checklist)](#-step-by-step-demonstration-guide-evaluation-checklist)

---

## 🎯 Overview & Core OS Concepts

This project implements a multi-user messaging system meeting all project document requirements:

| OS Concept | Practical Application in Project | Source Location |
| :--- | :--- | :--- |
| **Process Management** | Client and server run as independent standalone processes. Server manages multiple simultaneous client processes. | `src/server/server_main.cpp`, `src/client/client_main.cpp` |
| **Concurrency & Threading** | **Server:** Worker thread dispatch per client (`std::thread`).<br>**Client:** Dual-threaded CLI (interactive UI thread + background receiver thread). | `src/server/client_handler.cpp`, `src/client/receiver_thread.cpp` |
| **Synchronization** | Thread-safe in-memory session management and synchronized SQLite database operations using `std::mutex` and `std::lock_guard`. | `src/server/session_manager.cpp`, `src/database/db.cpp` |
| **Inter-Process Communication (IPC)** | Reliable connection-oriented byte stream communication implemented using POSIX TCP Sockets (`AF_INET`, `SOCK_STREAM`). | `src/common/protocol.cpp` |
| **File System & Persistence** | Server-side persistence using **SQLite3** for message storage, offline queuing, conversation history, and authorized users. | `src/database/db.cpp` |
| **Signal Handling** | Graceful shutdown handling on both server (`SIGINT` broadcast to all clients) and client (`SIGINT` disconnect). | `src/server/server_main.cpp`, `src/client/client_main.cpp` |

---

## 🏗️ System Architecture

```text
+-------------------+           +---------------------------------------+           +-------------------+
|   Client Process  |           |             Server Process            |           |   Client Process  |
|      (Alice)      |           |                                       |           |       (Bob)       |
|  +-------------+  |   TCP     |  +---------------------------------+  |   TCP     |  +-------------+  |
|  | CLI Thread  | <==========> |  | Accept Loop & Worker Dispatcher |  | <==========> |  | CLI Thread  |  |
|  +-------------+  |  (Socket) |  +---------------------------------+  |  (Socket) |  +-------------+  |
|  +-------------+  |           |                  |                    |           |  +-------------+  |
|  | Receiver Th |  |           |  +---------------------------------+  |           |  | Receiver Th |  |
|  +-------------+  |           |  | Session Manager (Mutex Protected) |           |  +-------------+  |
+-------------------+           |  +---------------------------------+  |           +-------------------+
                                |                  |                    |
                                |  +---------------------------------+  |
                                |  | Persistent SQLite Database      |  |
                                |  | (users & messages tables)       |  |
                                |  +---------------------------------+  |
                                +---------------------------------------+
```

---

## 📁 Repository Structure

```text
messaging-app/
├── docs/
│   └── Design_Document.docx    # Formal group design document
├── include/                    # Clean modular header files
│   ├── protocol.h              # Binary network packet & IPC definitions
│   ├── db.h                    # Persistent SQLite database interface
│   ├── session_manager.h       # Thread-safe in-memory active sessions
│   ├── client_handler.h        # Server worker thread entry point
│   └── receiver_thread.h       # Client background listener thread
├── src/                        # Modular source code
│   ├── common/
│   │   └── protocol.cpp        # Socket send/recv loops, endian translation
│   ├── database/
│   │   └── db.cpp              # Database schemas, queries & thread synchronization
│   ├── server/
│   │   ├── session_manager.cpp # Session mapping (user <-> socket_fd)
│   │   ├── client_handler.cpp  # Command dispatch, message routing & queueing
│   │   └── server_main.cpp     # Server process entry, listener & SIGINT handler
│   └── client/
│       ├── receiver_thread.cpp # Asynchronous incoming packet handler
│       └── client_main.cpp     # Interactive CLI and connection management
├── tests/
│   ├── test_db.cpp             # Automated unit tests for database and persistence
│   └── run_tests.sh            # Automated test runner script
├── Makefile                    # Linux build file
└── README.md                   # Complete system documentation
```

---

## 📡 Wire Protocol & Packet Anatomy

All communication over TCP sockets uses a binary-packed structure `Packet` (exactly 1,096 bytes) defined in `include/protocol.h`:

```text
+---------------------+---------------------+-------------------------------+-------------------------------+
| type (uint16_t)     | status (uint16_t)   | sender (char[32])             | recipient (char[32])          |
| 2 bytes             | 2 bytes             | 32 bytes                      | 32 bytes                      |
+---------------------+---------------------+-------------------------------+-------------------------------+
| payload_len (uint32_t)                    | payload (char[1024])                                          |
| 4 bytes                                   | 1024 bytes (null-terminated)                                  |
+-------------------------------------------+---------------------------------------------------------------+
Total: 1,096 bytes
```

- Multi-byte integers (`type`, `status`, `payload_len`) are serialized using network byte order (`htons`/`ntohs`, `htonl`/`ntohl`).
- Socket transmission uses exact byte loops with `EINTR` handling and `MSG_NOSIGNAL` to avoid socket termination signals.

---

## 💬 Commands & Responses

### Client Commands
| Command | Syntax | Description |
| :--- | :--- | :--- |
| `connect` | `connect <server IP> <user Name>` | Connects to server, authenticates and registers the user into SQLite, and plays back unread messages. |
| `sendmessage` | `sendmessage <user> <message>` | Sends a message to `<user>`. Delivered immediately if online; queued in SQLite if offline. Displays an error if `<user>` is not registered. |
| `getmessages` | `getmessages <user>` | Retrieves full two-way chronological chat history exchanged with `<user>`. |
| `deletemessages` | `deletemessages <user>` | Permanently deletes all messages exchanged with `<user>` from the server database. |
| `getuserlist` | `getuserlist` | Lists all users currently connected to the server. |
| `shutdown` | `shutdown` | Disconnects from server and terminates client process gracefully. |

### Message & Notification Formats
- **Real-Time Incoming Message:**
  ```text
  Message from <sender>: <message text>
  ```
- **Offline Unread Message (Delivered at login):**
  ```text
  [Unread] Message from <sender>: <message text>
  ```
- **Server Shutdown Broadcast:**
  ```text
  Server shutting down...
  ```

---

## 🔨 Compilation & Build Guide (Linux)

### Prerequisites
- GCC / G++ (supporting C++17)
- SQLite3 development library (`libsqlite3-dev` on Ubuntu/Debian, `sqlite-devel` on Fedora/RHEL)
- GNU Make

To install dependencies on Debian/Ubuntu:
```bash
sudo apt-get update
sudo apt-get install -y build-essential libsqlite3-dev
```

### Build Commands
```bash
# Build server, client, and unit tests
make

# Build individual targets
make server
make client
make test_db

# Run automated unit tests
make test

# Clean all build artifacts and test databases
make clean
```

---

## 🧪 Step-by-Step Demonstration Guide (Evaluation Checklist)

This guide walks through every item required by the project evaluation rubric.

### 1. Launch the Server
In **Terminal 1**:
```bash
./bin/server
# Or specify custom port and db: ./bin/server 8080 data/server.db
```
The server starts, initializes `data/server.db` (clean, empty user directory), and listens on port 8080. It automatically registers each new user upon their first connection.

---

### 2. Connect At Least 5 Concurrent Clients
Launch 5 separate terminals and run:

- **Terminal 2 (Alice):**
  ```bash
  ./bin/client
  > connect 127.0.0.1 alice
  # Or directly: ./bin/client 127.0.0.1 alice
  ```
- **Terminal 3 (Bob):**
  ```bash
  ./bin/client 127.0.0.1 bob
  ```
- **Terminal 4 (Charlie):**
  ```bash
  ./bin/client 127.0.0.1 charlie
  ```
- **Terminal 5 (David):**
  ```bash
  ./bin/client 127.0.0.1 david
  ```
- **Terminal 6 (Eve):**
  ```bash
  ./bin/client 127.0.0.1 eve
  ```

**Verify:**
In Alice's terminal, type:
```text
> getuserlist
```
**Output:**
```text
Online users (5): alice, bob, charlie, david, eve
```

---

### 3. Duplicate Login Prevention Test
In **Terminal 7**, attempt to log in as `alice` while Alice is already connected:
```bash
./bin/client
> connect 127.0.0.1 alice
```
**Output:**
```text
[connect failed] User 'alice' is already connected from another session.
```
**Result:** Rejected as required by specification.

---

### 4. Real-Time Messaging Test
In **Terminal 2 (Alice)**:
```text
> sendmessage bob Hello Bob, are you online?
```
**Observe:**
- On Alice's console: `Message delivered to bob.`
- In **Terminal 3 (Bob)**, real-time message appears instantly:
  ```text
  Message from alice: Hello Bob, are you online?
  ```
In **Terminal 3 (Bob)**, reply:
```text
> sendmessage alice Yes Alice, I am here!
```
- In **Terminal 2 (Alice)**, message appears instantly:
  ```text
  Message from bob: Yes Alice, I am here!
  ```

---

### 5. Offline Message Queuing Test
1. Disconnect Bob in **Terminal 3**:
   ```text
   > shutdown
   ```
2. In **Terminal 2 (Alice)**, check user list:
   ```text
   > getuserlist
   ```
   Bob is no longer in the online list.
3. Alice sends a message to offline Bob:
   ```text
   > sendmessage bob Please read this when you log in tomorrow.
   ```
   Alice sees:
   ```text
   User 'bob' is offline. Message queued for delivery at next login.
   ```
4. Re-launch Bob in **Terminal 3**:
   ```bash
   ./bin/client 127.0.0.1 bob
   ```
   **Immediate Output on Bob's console at login:**
   ```text
   [ok] Connected successfully as bob.
   [Unread] Message from alice: Please read this when you log in tomorrow.
   ```
   The queued message was delivered automatically and marked read in SQLite.

---

### 6. Message History Retrieval & Deletion Test
1. In **Terminal 3 (Bob)**, retrieve conversation history with Alice:
   ```text
   > getmessages alice
   ```
   **Output:** Full chronological two-way history between Alice and Bob:
   ```text
   [2026-11-22 14:30:00] alice: Hello Bob, are you online?
   [2026-11-22 14:30:05] bob: Yes Alice, I am here!
   [2026-11-22 14:30:20] alice: Please read this when you log in tomorrow.
   ```
2. Delete the conversation:
   ```text
   > deletemessages alice
   ```
   **Output:**
   ```text
   All messages exchanged with 'alice' deleted successfully.
   ```
3. Verify deletion:
   ```text
   > getmessages alice
   ```
   **Output:**
   ```text
   No message history exchanged with alice.
   ```

---

### 7. Server Shutdown Broadcast Test
In **Terminal 1 (Server)**, press `Ctrl+C`:
```text
^C
[server] Shutting down... broadcasting notification to active clients.
[server] Clean shutdown completed successfully.
```
**Observe all connected client terminals:**
Each client immediately receives:
```text
Server shutting down...
```
and gracefully terminates back to shell prompt.

---

### 8. Run Automated Test Cases
Run the automated test suite:
```bash
make test
# Or: ./tests/run_tests.sh
```
All 8 automated unit tests pass successfully.

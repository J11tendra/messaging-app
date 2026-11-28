# Multi-User Client-Server Messaging Application

A multi-threaded client-server messaging system in **C++17 for Linux** that demonstrates core
Operating System concepts: processes, threads, synchronization, socket IPC, I/O multiplexing,
persistent storage and signal handling.

---

## Contents
1. [OS concepts used](#os-concepts-used)
2. [Architecture](#architecture)
3. [Repository structure](#repository-structure)
4. [Wire protocol](#wire-protocol)
5. [Commands](#commands)
6. [Concurrency and synchronization design](#concurrency-and-synchronization-design)
7. [Build and run](#build-and-run)
8. [Tests](#tests)
9. [Demonstration guide](#demonstration-guide)

---

## OS concepts used

| Concept | How it is used | Where |
| :--- | :--- | :--- |
| **Process management** | Server and every client are separate processes. | `server_main.cpp`, `client_main.cpp` |
| **Concurrency & threading** | Server: one worker thread per client, joined at shutdown. Client: CLI thread + receiver thread. | `client_handler.cpp`, `connection_tracker.cpp`, `receiver_thread.cpp` |
| **Synchronization** | `std::mutex` for the online-user table, one send lock per socket, one delivery lock per user, one database lock, one console lock. | `session_manager.*`, `db.cpp`, `console.cpp` |
| **IPC** | TCP sockets (`AF_INET`, `SOCK_STREAM`) with fixed-size framed packets. | `network.cpp`, `protocol.h` |
| **I/O management** | `poll()` multiplexes the listening socket, server console (stdin) and a signal pipe; the client `poll()`s stdin and a wake-up pipe. | `server_main.cpp`, `client_main.cpp` |
| **File system & persistence** | SQLite database for messages; valid users in a text file. | `db.cpp`, `config/users.txt` |
| **Signal handling** | `SIGINT`/`SIGTERM` handled with the self-pipe trick (handler only calls `write()`); `SIGPIPE` ignored. | `server_main.cpp`, `client_main.cpp` |

---

## Architecture

```text
   Client process (Rouchelle)                Server process                       Client process (Tanvi)
 +-------------------------+      +--------------------------------------+      +-------------------------+
 | main thread             |      | main thread: poll()                  |      | main thread             |
 |  poll(stdin, wake pipe) | TCP  |   listen socket -> accept + spawn    | TCP  |  poll(stdin, wake pipe) |
 |  parse cmd -> Packet ---+=====>|   stdin         -> console commands  |<=====+--- parse cmd -> Packet  |
 |                         |      |   signal pipe   -> graceful shutdown |      |                         |
 | receiver thread         |      |                                      |      | receiver thread         |
 |  recv() -> print <------+======+ worker (Rouchelle)   worker (Tanvi)  +======+-> recv() -> print       |
 +-------------------------+      |        |  handle_command()  |        |      +-------------------------+
                                  |        v                    v        |
                                  |  SessionManager (online users, mutex)|
                                  |  UserRegistry   (valid users, r/o)   |
                                  |  SQLite db      (messages, mutex)    |
                                  +--------------------------------------+
```

Clients never talk to each other directly: every message is routed through the server, and only
the server stores messages. Clients keep nothing on disk.

**Message flow for `sendmessage Tanvi hi`:** Rouchelle's CLI thread sends `CMD_SEND_MSG` → Rouchelle's
worker checks `Tanvi` is a valid user → takes Tanvi's delivery lock → if Tanvi is online, pushes
`MSG_REALTIME` on Tanvi's socket (under Tanvi's send lock) and stores the message as read; otherwise
stores it as unread → replies to Rouchelle. Tanvi's receiver thread prints `Message from Rouchelle: hi`.

---

## Repository structure

```text
messaging-app/
├── config/users.txt              List of valid users (one per line)
├── include/                      Headers
├── src/
│   ├── common/                   Shared by client and server
│   │   ├── protocol.cpp          Packet fields and factories
│   │   ├── network.cpp           Sockets: framing, connect, listen      (networking)
│   │   └── console.cpp           Thread-safe console output
│   ├── database/
│   │   └── db.cpp                SQLite message store                    (database)
│   ├── server/
│   │   ├── server_main.cpp       Start-up, poll() loop, shutdown
│   │   ├── client_handler.cpp    Worker thread: login + command loop     (threading)
│   │   ├── connection_tracker.cpp  Tracks sockets and joins worker threads (threading)
│   │   ├── command_handler.cpp   sendmessage / getmessages / ...         (command handling)
│   │   ├── session_manager.cpp   Online users, send and delivery locks   (synchronization)
│   │   └── user_registry.cpp     Valid-user list
│   └── client/
│       ├── client_main.cpp       CLI loop, connect/disconnect
│       ├── client_commands.cpp   Command parsing -> request packets      (command handling)
│       └── receiver_thread.cpp   Displays incoming messages              (threading)
├── tests/                        Unit tests + 5-client integration test
├── docs/Design_Document.docx
└── Makefile
```

---

## Wire protocol

Every message is exactly one 1,096-byte `Packet` (`include/protocol.h`), so the receiver always
knows where a message ends:

```text
| type u16 | status u16 | sender char[32] | recipient char[32] | payload_len u32 | payload char[1024] |
```

Integers are sent in network byte order. `send_packet`/`recv_packet` loop until the whole packet is
transferred (TCP may split it) and retry on `EINTR`. Received strings are always re-terminated and a
`payload_len` out of range is rejected.

| Client → Server | Fields used |
| :--- | :--- |
| `CMD_CONNECT` | sender = user name |
| `CMD_SEND_MSG` | recipient, payload = text |
| `CMD_GET_MSGS` / `CMD_DEL_MSGS` | recipient = other user |
| `CMD_GET_USERLIST`, `CMD_SHUTDOWN_CLIENT` | — |

| Server → Client | Meaning |
| :--- | :--- |
| `RESP_SUCCESS` / `RESP_ERROR` | Reply to a command (error carries a `StatusCode`) |
| `RESP_INFO` | Informational line (unread count, history header/footer) |
| `RESP_UNREAD` | One unread message, sent at login |
| `RESP_HISTORY` | One history line (one packet per message, so long histories are never cut off) |
| `RESP_USERLIST` | Online users |
| `MSG_REALTIME` | Message pushed to an online user |
| `SERVER_SHUTDOWN` | Broadcast `Server shutting down...` |

---

## Commands

| Command | Behaviour |
| :--- | :--- |
| `connect <server IP> <user Name>` | Logs in. The name must be in `config/users.txt`; otherwise the server rejects it, the client shows the reason and **exits**. A second login for a user who is already online is rejected the same way. On success, unread messages are shown. |
| `sendmessage <user> <message>` | Live delivery if `<user>` is online (`Message from <sender>: <text>`), otherwise stored and shown at their next login. Invalid user → error. |
| `getmessages <user>` | All messages exchanged with `<user>`, oldest first, with timestamps. Invalid user → error. |
| `deletemessages <user>` | Deletes all messages to/from `<user>` on the server and reports how many. Invalid user → error. |
| `getuserlist` | Users currently online. |
| `shutdown` | Disconnects and exits; the server removes the user from the online list. Ctrl+C does the same. |

**Server console** (type in the server terminal): `users` lists online users, `shutdown` stops the
server. Ctrl+C or `kill` (SIGTERM) also stop it. In every case the server broadcasts
`Server shutting down...`, every client prints it and exits, all worker threads are joined and the
database is closed.

---

## Concurrency and synchronization design

| Shared data | Protected by | Why |
| :--- | :--- | :--- |
| Online-user table (`username → Session`) | `SessionManager::mtx_` | Duplicate check and insert happen under one lock, so two simultaneous logins with the same name cannot both succeed. |
| Each client socket | `Session::send_mtx_` | Several threads can write to one client (its own worker, other users' workers forwarding messages, the shutdown broadcast). The lock keeps each packet whole on the stream. A `closed` flag under the same lock stops writes to an fd after it is released. |
| Online/offline delivery decision | `delivery_lock(user)` (one per user) | Held while a sender chooses "push live" or "store unread", and while that user logs in and receives unread messages. Without it, a message could be stored as unread just after the login check and not appear until the next login. |
| SQLite connection | `g_db_mtx` | Serializes all queries on the single connection. |
| Valid-user list | none needed | Loaded once before any thread starts, then read-only. |
| Socket set / worker threads | `ConnectionTracker` mutexes | Lets shutdown wake every blocked worker and `join()` it; no detached threads outlive the database. |
| stdout | console mutex | Keeps lines from different threads from interleaving. |

**Lock order:** `delivery_lock` → `SessionManager::mtx_` → `Session::send_mtx_`. No code path takes
them in the reverse order, so deadlock is impossible.

Unread messages are marked read only **after** they have been sent to the client, so a failed
delivery leaves them queued.

---

## Build and run

```bash
sudo apt-get install -y build-essential libsqlite3-dev   # Debian/Ubuntu

make                 # server, client and unit tests
make test            # unit tests + 5-client integration test
make clean
```

```bash
./bin/server [port] [db_path] [users_file]       # defaults: 8080 data/server.db config/users.txt
./bin/client [-p port] [<server IP> <user Name>] # or type "connect <server IP> <user Name>"
```

Valid users are `Rouchelle`, `Tanvi`, `Jitendra`, `Akshay` and `Prof.` (case-sensitive, and `Prof.`
includes the full stop); edit `config/users.txt` and restart
the server to change them. When clients run on other machines, use the server machine's IP address
and make sure the firewall allows the port (e.g. `sudo ufw allow 8080/tcp`).

---

## Tests

`make test` runs:

| Suite | What it checks |
| :--- | :--- |
| `test_protocol` | Field truncation, socket round trip, network byte order, reassembly of split packets, rejection of malformed frames and EOF. |
| `test_db` | Foreign keys, unread queue and marking, two-way history, delete scoped to a pair, 8 concurrent writer threads, persistence across reopen. |
| `test_user_registry` | File parsing, comments, invalid names, case sensitivity. |
| `test_session_manager` | Duplicate rejection, 8 threads racing to log in as the same user (exactly one wins), capacity limit, 6 threads writing to one socket without corrupting packets. |
| `test_client_commands` | Parsing and validation of every CLI command. |
| `integration_test.sh` | Real server + 5 concurrent clients: login, invalid and duplicate login, user list, live and concurrent messages, offline delivery, history, delete, client shutdown, SIGINT broadcast, persistence across restart, SIGTERM, case-sensitive names. 48 checks. |

The unit tests and the integration test also run clean under ThreadSanitizer:
```bash
make BUILD_DIR=/tmp/tsan BIN_DIR=/tmp/tsan_bin \
     CXXFLAGS="-std=c++17 -O1 -g -Iinclude -pthread -fsanitize=thread" \
     LDFLAGS="-pthread -fsanitize=thread" all
BIN=/tmp/tsan_bin ./tests/integration_test.sh
```

---

## Demonstration guide

1. **Start the server** (terminal 1): `./bin/server`
2. **Connect 5 clients** (terminals 2–6):
   `./bin/client 127.0.0.1 Rouchelle`, `... Tanvi`, `... Jitendra`, `... Akshay`, `... Prof.`
   (from another machine replace `127.0.0.1` with the server's IP). The server prints
   `User 'Rouchelle' connected ...` for each.
3. **User list**: in Rouchelle's terminal `getuserlist` → `Online users (5): Akshay Jitendra Prof. Rouchelle (you) Tanvi`.
4. **Invalid user**: `./bin/client 127.0.0.1 mallory` → `[login rejected] Invalid user 'mallory'...` and the client exits.
5. **Duplicate session**: `./bin/client 127.0.0.1 Rouchelle` while Rouchelle is online → rejected, client exits.
6. **Real-time messaging**: Rouchelle `sendmessage Tanvi Hello Tanvi` → Tanvi sees `Message from Rouchelle: Hello Tanvi`.
   Send from several clients at once to show concurrent handling.
7. **Client shutdown**: Prof. types `shutdown` → server logs `User 'Prof.' disconnected`; `getuserlist` no longer shows Prof.
8. **Offline messages**: Jitendra `sendmessage Prof. Please review our design` → `Prof. is offline...`.
   Start `./bin/client 127.0.0.1 Prof.` again → `You have 1 unread message(s):` followed by the message.
9. **History**: Tanvi `getmessages Rouchelle`; then `deletemessages Rouchelle`; then `getmessages Rouchelle` → `No messages exchanged with Rouchelle.`
10. **Server shutdown**: press Ctrl+C (or type `shutdown`) in the server terminal → every client prints
    `Server shutting down...` and exits.
11. **Run tests**: `make test`.

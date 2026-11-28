#!/usr/bin/env bash
# End-to-end test: real server + 5 concurrent clients over TCP.
# Covers every behaviour in the project brief's demonstration checklist.
# Run from the repository root:  make integration-test
set -u

BIN=${BIN:-./bin}
PORT=${PORT:-$((20000 + RANDOM % 20000))}
WORK=$(mktemp -d /tmp/msgapp_it.XXXXXX)
USERS="$WORK/users.txt"
DB="$WORK/server.db"
PASS=0
FAIL=0

printf "Rouchelle\nTanvi\nJitendra\nAkshay\nProf.\n" > "$USERS"

cleanup() {
    for fd in 11 12 13 14 15 16 17; do eval "exec $fd>&-" 2>/dev/null; done
    kill "$SERVER_PID" 2>/dev/null
    pkill -f "$BIN/client -p $PORT" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

check() {  # check <description> <file> <fixed string expected>
    if grep -qF -- "$3" "$2"; then
        echo "  [PASS] $1"; PASS=$((PASS + 1))
    else
        echo "  [FAIL] $1  (expected '$3' in $(basename "$2"))"; FAIL=$((FAIL + 1))
        sed 's/^/        | /' "$2" | tail -15
    fi
}
check_absent() {
    if grep -qF -- "$3" "$2"; then
        echo "  [FAIL] $1"; FAIL=$((FAIL + 1))
    else
        echo "  [PASS] $1"; PASS=$((PASS + 1))
    fi
}
pause() { sleep "${1:-0.4}"; }

# start_client <fd> <name>: client reads commands from a FIFO we keep open on <fd>
start_client() {
    local fd=$1 name=$2
    mkfifo "$WORK/$name.in"
    "$BIN/client" -p "$PORT" < "$WORK/$name.in" > "$WORK/$name.out" 2>&1 &
    eval "${name}_PID=$!"
    eval "exec $fd>\"$WORK/$name.in\""
}
say() { local fd=$1; shift; echo "$*" >&"$fd"; pause 0.25; }

echo "=== Integration test (port $PORT) ==="
"$BIN/server" "$PORT" "$DB" "$USERS" < /dev/null > "$WORK/server.out" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do grep -q "Listening on port" "$WORK/server.out" 2>/dev/null && break; sleep 0.1; done
check "server starts and loads valid users" "$WORK/server.out" "Loaded 5 valid users"

# --- 1. Connection management -------------------------------------------------
# start_client <fd> <tag>: <tag> names the output file; the user name is sent with connect
start_client 11 Rouchelle; say 11 "connect 127.0.0.1 Rouchelle"
start_client 12 Tanvi;     say 12 "connect 127.0.0.1 Tanvi"
start_client 13 Jitendra;  say 13 "connect 127.0.0.1 Jitendra"
start_client 14 Akshay;    say 14 "connect 127.0.0.1 Akshay"
start_client 15 Prof;      say 15 "connect 127.0.0.1 Prof."
pause
for u in Rouchelle Tanvi Jitendra Akshay; do check "$u logs in" "$WORK/$u.out" "Login successful. Welcome, $u!"; done
check "Prof. logs in" "$WORK/Prof.out" "Login successful. Welcome, Prof.!"
check "server logs connection events" "$WORK/server.out" "User 'Prof.' connected"

# invalid user: rejected, client process exits
"$BIN/client" -p "$PORT" 127.0.0.1 mallory < /dev/null > "$WORK/mallory.out" 2>&1
RC=$?
check "invalid user rejected with message" "$WORK/mallory.out" "Invalid user 'mallory'"
[ $RC -ne 0 ] && { echo "  [PASS] client exits after invalid-user rejection"; PASS=$((PASS+1)); } \
              || { echo "  [FAIL] client should exit non-zero after rejection"; FAIL=$((FAIL+1)); }

# names are case-sensitive
"$BIN/client" -p "$PORT" 127.0.0.1 rouchelle < /dev/null > "$WORK/lower.out" 2>&1
check "user names are case-sensitive" "$WORK/lower.out" "Invalid user 'rouchelle'"

# duplicate session: rejected
"$BIN/client" -p "$PORT" 127.0.0.1 Rouchelle < /dev/null > "$WORK/dup.out" 2>&1
check "duplicate session rejected" "$WORK/dup.out" "already connected"
check "server logs duplicate rejection" "$WORK/server.out" "'Rouchelle' already has a session"

# --- 2/4. User list -----------------------------------------------------------
say 11 "getuserlist"; pause
check "getuserlist shows 5 online users" "$WORK/Rouchelle.out" "Online users (5)"
check "getuserlist includes Prof." "$WORK/Rouchelle.out" "  Prof."

# --- 2. Messaging (real time, concurrent senders) -----------------------------
say 11 "sendmessage Tanvi Hello Tanvi, this is Rouchelle"
check "real-time message shown in required format" "$WORK/Tanvi.out" "Message from Rouchelle: Hello Tanvi, this is Rouchelle"
check "sender gets delivery confirmation" "$WORK/Rouchelle.out" "Message delivered to Tanvi."

for fd_user in "12 Tanvi" "13 Jitendra" "14 Akshay" "15 Prof."; do
    set -- $fd_user; echo "sendmessage Rouchelle hi from $2" >&"$1" &
done
pause 1
for u in Tanvi Jitendra Akshay Prof.; do check "concurrent message from $u reaches Rouchelle" "$WORK/Rouchelle.out" "Message from $u: hi from $u"; done

say 11 "sendmessage mallory are you there?"
check "sendmessage to invalid user -> error" "$WORK/Rouchelle.out" "Invalid user 'mallory'"

# --- client shutdown + offline delivery ----------------------------------------
say 15 "shutdown"; pause
check "client shutdown command" "$WORK/Prof.out" "Disconnected"
check "server removes user on client exit" "$WORK/server.out" "User 'Prof.' disconnected"
say 11 "getuserlist"; pause
check "user list updated after disconnect (4 online)" "$WORK/Rouchelle.out" "Online users (4)"

say 11 "sendmessage Prof. see you in class"
say 12 "sendmessage Prof. message from Tanvi"
check "offline recipient -> queued notice" "$WORK/Rouchelle.out" "Prof. is offline"
start_client 16 Prof2; say 16 "connect 127.0.0.1 Prof."; pause
check "unread count shown at login" "$WORK/Prof2.out" "You have 2 unread message(s)"
check "unread message 1 shown at login" "$WORK/Prof2.out" "Message from Rouchelle: see you in class"
check "unread message 2 shown at login" "$WORK/Prof2.out" "Message from Tanvi: message from Tanvi"
say 16 "shutdown"; pause
start_client 17 Prof3; say 17 "connect 127.0.0.1 Prof."; pause
check "unread messages are not repeated on next login" "$WORK/Prof3.out" "No unread messages."
say 17 "shutdown"

# --- 3. Storage and retrieval -------------------------------------------------
say 12 "getmessages Rouchelle"; pause
check "getmessages returns history header" "$WORK/Tanvi.out" "Conversation with Rouchelle (2 messages)"
check "getmessages includes Rouchelle's message" "$WORK/Tanvi.out" "Rouchelle -> Tanvi: Hello Tanvi, this is Rouchelle"
check "getmessages includes Tanvi's message" "$WORK/Tanvi.out" "Tanvi -> Rouchelle: hi from Tanvi"
say 12 "getmessages mallory"
check "getmessages invalid user -> error" "$WORK/Tanvi.out" "Invalid user 'mallory'"

say 12 "deletemessages Rouchelle"; pause
check "deletemessages reports deleted count" "$WORK/Tanvi.out" "Deleted 2 message(s) exchanged with Rouchelle"
say 11 "getmessages Tanvi"; pause
check "history empty after delete (seen by other side too)" "$WORK/Rouchelle.out" "No messages exchanged with Tanvi."
say 12 "deletemessages nobody"
check "deletemessages invalid user -> error" "$WORK/Tanvi.out" "Invalid user 'nobody'"

# --- 5. Shutdown handling -----------------------------------------------------
say 14 "shutdown"; pause
check "Akshay disconnects gracefully" "$WORK/Akshay.out" "Disconnected"

kill -INT "$SERVER_PID"
for _ in $(seq 1 50); do kill -0 "$SERVER_PID" 2>/dev/null || break; sleep 0.1; done
pause 0.5
kill -0 "$SERVER_PID" 2>/dev/null && { echo "  [FAIL] server did not exit"; FAIL=$((FAIL+1)); } \
                                   || { echo "  [PASS] server exits on Ctrl+C (SIGINT)"; PASS=$((PASS+1)); }
check "server broadcasts to remaining clients" "$WORK/server.out" "Shutdown broadcast sent to 3 client(s)"
check "server joins all threads" "$WORK/server.out" "All client threads joined"
for u in Rouchelle Tanvi Jitendra; do
    check "$u displays 'Server shutting down...'" "$WORK/$u.out" "Server shutting down..."
    pid_var="${u}_PID"
    kill -0 "${!pid_var}" 2>/dev/null && { echo "  [FAIL] $u client still running"; FAIL=$((FAIL+1)); } \
                                      || { echo "  [PASS] $u client terminated"; PASS=$((PASS+1)); }
done

# --- Persistence: restart server, history still there --------------------------
"$BIN/server" "$PORT" "$DB" "$USERS" < /dev/null > "$WORK/server2.out" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do grep -q "Listening on port" "$WORK/server2.out" 2>/dev/null && break; sleep 0.1; done
( echo "getmessages Jitendra"; sleep 0.6; echo "shutdown" ) | "$BIN/client" -p "$PORT" 127.0.0.1 Rouchelle > "$WORK/persist.out" 2>&1
check "messages persist across server restart" "$WORK/persist.out" "Jitendra -> Rouchelle: hi from Jitendra"
kill -TERM "$SERVER_PID"; wait "$SERVER_PID" 2>/dev/null
check "server also shuts down cleanly on SIGTERM" "$WORK/server2.out" "Received SIGTERM"

echo ""
echo "Integration: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

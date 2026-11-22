#ifndef DB_H
#define DB_H

#include <string>
#include <vector>
#include "protocol.h"

// Lifecycle
bool db_init(const std::string &db_path = "server.db");
void db_close();

// User Authorization
bool db_is_valid_user(const std::string &username);
bool db_add_user(const std::string &username, const std::string &password = "password123");

// Message Storage & Routing
// is_read = true if recipient is currently online (real-time delivered)
// is_read = false if recipient is offline (queued)
bool db_save_message(const std::string &sender, const std::string &recipient, const std::string &content, bool is_read);

// Offline Queue Delivery
// Fetches unread messages (is_read = 0) for recipient upon login and marks them as read (is_read = 1)
std::vector<Packet> db_get_unread_messages(const std::string &username);

// History & Deletion
// Returns formatted chronological message exchange between user1 and user2
std::string db_get_conversation(const std::string &user1, const std::string &user2);

// Permanently deletes all messages between user1 and user2
bool db_delete_conversation(const std::string &user1, const std::string &user2);

#endif // DB_H

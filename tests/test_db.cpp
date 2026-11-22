#include <iostream>
#include <cassert>
#include "db.h"
#include "protocol.h"

int main() {
    std::cout << "==========================================\n";
    std::cout << "       RUNNING DATABASE UNIT TESTS        \n";
    std::cout << "==========================================\n";

    // 1. Initialization
    std::cout << "[Test 1] Initializing SQLite database (data/test_suite.db)... ";
    assert(chat::db_init("data/test_suite.db") == true);
    std::cout << "PASSED\n";

    // 2. User Registration & Authentication
    std::cout << "[Test 2] Dynamically registering and validating users... ";
    assert(chat::db_is_valid_user("alice") == false);
    assert(chat::db_add_user("alice", "pass123") == true);
    assert(chat::db_is_valid_user("alice") == true);
    assert(chat::db_add_user("bob", "pass123") == true);
    assert(chat::db_is_valid_user("bob") == true);
    assert(chat::db_is_valid_user("unauthorized_hacker") == false);
    std::cout << "PASSED\n";

    // 3. Adding custom user
    std::cout << "[Test 3] Adding dynamic authorized user 'frank'... ";
    assert(chat::db_add_user("frank", "pass456") == true);
    assert(chat::db_is_valid_user("frank") == true);
    std::cout << "PASSED\n";

    // 4. Storing Real-time vs Offline Messages
    std::cout << "[Test 4] Storing real-time and offline messages... ";
    // Alice sends Bob a real-time message (Bob was online)
    assert(chat::db_save_message("alice", "bob", "Hello Bob in real-time!", true) == true);
    // Alice sends Bob an offline message (Bob was offline)
    assert(chat::db_save_message("alice", "bob", "Bob, check this when you log in!", false) == true);
    // Bob sends Alice a message
    assert(chat::db_save_message("bob", "alice", "Thanks Alice!", true) == true);
    std::cout << "PASSED\n";

    // 5. Offline Unread Queue & Automatic Read Acknowledgment
    std::cout << "[Test 5] Fetching offline unread messages for 'bob'... ";
    auto unread_bob = chat::db_get_unread_messages("bob");
    assert(unread_bob.size() == 1);
    assert(unread_bob[0].get_sender() == "alice");
    assert(unread_bob[0].get_payload() == "Bob, check this when you log in!");
    std::cout << "PASSED\n";
    std::cout << "         -> Received unread message: " << unread_bob[0].get_payload() << "\n";

    // Verifying it was marked as read (second query must return 0 unread)
    auto unread_bob_again = chat::db_get_unread_messages("bob");
    assert(unread_bob_again.size() == 0);
    std::cout << "         -> Second check returned 0 unread. PASSED\n";

    // 6. Two-Way Conversation History Retrieval
    std::cout << "[Test 6] Retrieving conversation history between alice and bob...\n";
    std::string history = chat::db_get_conversation("alice", "bob");
    std::cout << "--------- Conversation Log ---------\n" << history << "------------------------------------\n";
    assert(history.find("Hello Bob in real-time!") != std::string::npos);
    assert(history.find("Bob, check this when you log in!") != std::string::npos);
    assert(history.find("Thanks Alice!") != std::string::npos);
    std::cout << "         -> History retrieval PASSED\n";

    // 7. Conversation Deletion
    std::cout << "[Test 7] Deleting conversation between alice and bob... ";
    assert(chat::db_delete_conversation("alice", "bob") == true);
    std::string history_after = chat::db_get_conversation("alice", "bob");
    assert(history_after.find("No message history exchanged with bob.") != std::string::npos);
    std::cout << "PASSED\n";

    // 8. Cleanup
    chat::db_close();
    std::cout << "[Test 8] Clean database shutdown... PASSED\n";

    std::cout << "==========================================\n";
    std::cout << "      ALL DATABASE UNIT TESTS PASSED!     \n";
    std::cout << "==========================================\n";
    return 0;
}

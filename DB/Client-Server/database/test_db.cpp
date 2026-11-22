#include <iostream>
#include <cassert>
#include "db.h"
#include "protocol.h"

int main() {
    std::cout << "==========================================\n";
    std::cout << "       RUNNING DATABASE UNIT TESTS        \n";
    std::cout << "==========================================\n";

    // 1. Initialization
    std::cout << "[Test 1] Initializing SQLite database (test_suite.db)... ";
    assert(db_init("test_suite.db") == true);
    std::cout << "PASSED\n";

    // 2. User Authentication
    std::cout << "[Test 2] Validating pre-seeded users... ";
    assert(db_is_valid_user("alice") == true);
    assert(db_is_valid_user("bob") == true);
    assert(db_is_valid_user("charlie") == true);
    assert(db_is_valid_user("david") == true);
    assert(db_is_valid_user("eve") == true);
    assert(db_is_valid_user("hacker_unknown") == false);
    std::cout << "PASSED\n";

    // 3. Adding custom user
    std::cout << "[Test 3] Adding a dynamic user 'frank'... ";
    assert(db_add_user("frank", "pass456") == true);
    assert(db_is_valid_user("frank") == true);
    std::cout << "PASSED\n";

    // 4. Saving Real-time vs Offline Messages
    std::cout << "[Test 4] Storing real-time and offline messages... ";
    // Alice sends Bob a real-time message (Bob was online)
    assert(db_save_message("alice", "bob", "Hello Bob in real-time!", true) == true);
    // Alice sends Bob an offline message (Bob was offline)
    assert(db_save_message("alice", "bob", "Bob, check this when you log in!", false) == true);
    // Bob sends Alice a message
    assert(db_save_message("bob", "alice", "Thanks Alice!", true) == true);
    std::cout << "PASSED\n";

    // 5. Offline Unread Queue & Automatic Read Acknowledgment
    std::cout << "[Test 5] Fetching offline unread messages for 'bob'... ";
    auto unread_bob = db_get_unread_messages("bob");
    assert(unread_bob.size() == 1);
    assert(unread_bob[0].get_sender() == "alice");
    std::cout << "\n         -> Received unread: " << unread_bob[0].get_payload() << "\n";
    
    // Verifying it was marked as read (second query must return 0)
    auto unread_bob_again = db_get_unread_messages("bob");
    assert(unread_bob_again.size() == 0);
    std::cout << "         -> Second check returned 0 unread. PASSED\n";

    // 6. Two-Way Conversation History Retrieval
    std::cout << "[Test 6] Retrieving conversation history between alice and bob...\n";
    std::string history = db_get_conversation("alice", "bob");
    std::cout << "--------- Conversation Log ---------\n" << history << "------------------------------------\n";
    assert(history.find("Hello Bob in real-time!") != std::string::npos);
    assert(history.find("Bob, check this when you log in!") != std::string::npos);
    assert(history.find("Thanks Alice!") != std::string::npos);
    std::cout << "         -> History retrieval PASSED\n";

    // 7. Conversation Deletion
    std::cout << "[Test 7] Deleting conversation between alice and bob... ";
    assert(db_delete_conversation("alice", "bob") == true);
    std::string history_after = db_get_conversation("alice", "bob");
    assert(history_after.find("No message history exchanged with bob.") != std::string::npos);
    std::cout << "PASSED\n";

    // 8. Cleanup
    db_close();
    std::cout << "[Test 8] Clean database shutdown... PASSED\n";

    std::cout << "==========================================\n";
    std::cout << "      ALL DATABASE UNIT TESTS PASSED!     \n";
    std::cout << "==========================================\n";
    return 0;
}

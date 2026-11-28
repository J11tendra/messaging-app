/**
 * @file user_registry.h
 * @brief The list of valid users who may log in (file based: config/users.txt).
 *
 * File format: one user name per line; blank lines and lines starting with
 * '#' are ignored. The list is loaded once at server start-up and is
 * read-only afterwards, so worker threads can query it concurrently without
 * a lock (no writer exists after start-up).
 */
#ifndef USER_REGISTRY_H
#define USER_REGISTRY_H

#include <set>
#include <string>
#include <vector>

namespace chat {

class UserRegistry {
public:
    /** Loads (replaces) the list. Returns false and sets err on failure. */
    bool load_from_file(const std::string &path, std::string &err);

    /** For tests / programmatic setup. Returns false if the name is malformed. */
    bool add(const std::string &username);

    bool is_valid(const std::string &username) const;
    std::vector<std::string> all() const;
    size_t size() const { return users_.size(); }

    /** 1-31 chars of [A-Za-z0-9_.-]; "server" is reserved. */
    static bool is_well_formed(const std::string &username);

private:
    std::set<std::string> users_;
};

}  // namespace chat

#endif  // USER_REGISTRY_H

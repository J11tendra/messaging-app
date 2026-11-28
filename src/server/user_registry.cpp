/**
 * @file user_registry.cpp
 * @brief Loads and queries the valid-user list.
 */
#include "user_registry.h"

#include <cctype>
#include <fstream>

#include "protocol.h"

namespace chat {

namespace {
std::string trim(const std::string &s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
}  // namespace

bool UserRegistry::is_well_formed(const std::string &name) {
    if (name.empty() || name.size() >= MAX_USERNAME_LEN || name == "server") return false;
    for (unsigned char c : name) {
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.') return false;
    }
    return true;
}

bool UserRegistry::add(const std::string &username) {
    if (!is_well_formed(username)) return false;
    users_.insert(username);
    return true;
}

bool UserRegistry::load_from_file(const std::string &path, std::string &err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open user list file '" + path + "'";
        return false;
    }
    std::set<std::string> loaded;
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (!is_well_formed(line)) {
            err = path + ":" + std::to_string(line_no) + ": invalid user name '" + line + "'";
            return false;
        }
        loaded.insert(line);
    }
    if (loaded.empty()) {
        err = "user list file '" + path + "' contains no users";
        return false;
    }
    users_.swap(loaded);
    return true;
}

bool UserRegistry::is_valid(const std::string &username) const {
    return users_.count(username) > 0;
}

std::vector<std::string> UserRegistry::all() const {
    return std::vector<std::string>(users_.begin(), users_.end());
}

}  // namespace chat

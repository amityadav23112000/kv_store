#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <sstream>
#include "kvstore.h"

// ONE complete command line in (no '\n'), reply text out (ends with '\n').
inline std::string process_command(KVStore& store, const std::string& line) {
    std::istringstream iss(line);      // treat the line like a tiny input stream
    std::string cmd, key;
    iss >> cmd >> key;                 // >> pulls one whitespace-separated word each

    if (cmd == "SET") {
        std::string value;
        std::getline(iss, value);      // grab the REST of the line = the value
        if (!value.empty() && value[0] == ' ')
            value.erase(0, 1);         // drop the single space after key
        if (key.empty() || value.empty())
            return "ERR usage: SET key value\n";
        store.set(key, value);
        return "OK\n";
    }
    if (cmd == "GET") {
        if (key.empty()) return "ERR usage: GET key\n";
        auto v = store.get(key);
        return v ? (*v + "\n") : "(nil)\n";
    }
    if (cmd == "DEL") {
        if (key.empty()) return "ERR usage: DEL key\n";
        return store.del(key) ? "1\n" : "0\n";
    }
    return "ERR unknown command '" + cmd + "'\n";
}

#endif // PROTOCOL_H
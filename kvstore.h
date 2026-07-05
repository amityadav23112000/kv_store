#ifndef KVSTORE_H
#define KVSTORE_H

#include <string>
#include <unordered_map>
#include <optional>       // for "value or nothing"

class KVStore {
private:
    std::unordered_map<std::string, std::string> data_;
    // NOTE: not thread-safe yet — deliberately. M4 catches this red-handed.
public:
    // SET key value  → store (overwrite if exists)
    void set(const std::string& key, const std::string& value) {
        data_[key] = value;
    }

    // GET key → the value, or "nothing" if key absent
    std::optional<std::string> get(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) return std::nullopt;   // not found
        return it->second;                            // found: the value
    }

    // DEL key → true if something was actually deleted
    bool del(const std::string& key) {
        return data_.erase(key) > 0;    // erase returns how many it removed
    }


};

#endif // KVSTORE_H
#ifndef KVSTORE_H
#define KVSTORE_H

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>

class KVStore {
public:
    void set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(store_mutex);
        data[key] = value;
    }
    std::optional<std::string> get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(store_mutex);
        auto it = data.find(key);
        if (it == data.end()) return std::nullopt;
        return it->second;
    }
    bool del(const std::string& key) {
        std::lock_guard<std::mutex> lock(store_mutex);
        return data.erase(key) > 0;
    }
private:
    std::unordered_map<std::string, std::string> data;
    mutable std::mutex store_mutex;   // ONE door for everything
};

#endif
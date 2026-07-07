#ifndef KVSTORE_H
#define KVSTORE_H

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <functional>

class KVStore {
public:
    void set(const std::string& key, const std::string& value) {
        Shard& shard = pick_shard(key);
        std::lock_guard<std::mutex> lock(shard.shard_mutex);
        shard.data[key] = value;
    }
    std::optional<std::string> get(const std::string& key) const {
        const Shard& shard = pick_shard(key);
        std::lock_guard<std::mutex> lock(shard.shard_mutex);
        auto it = shard.data.find(key);
        if (it == shard.data.end()) return std::nullopt;
        return it->second;
    }
    bool del(const std::string& key) {
        Shard& shard = pick_shard(key);
        std::lock_guard<std::mutex> lock(shard.shard_mutex);
        return shard.data.erase(key) > 0;
    }
private:
    static const size_t NUM_SHARDS = 8;
    struct alignas(128) Shard {                      // own cache line (M6)
        std::unordered_map<std::string, std::string> data;
        mutable std::mutex shard_mutex;              // plain mutex = V2
    };
    Shard shards[NUM_SHARDS];

    Shard& pick_shard(const std::string& key) {
        return shards[std::hash<std::string>{}(key) % NUM_SHARDS];
    }
    const Shard& pick_shard(const std::string& key) const {
        return shards[std::hash<std::string>{}(key) % NUM_SHARDS];
    }
};

#endif
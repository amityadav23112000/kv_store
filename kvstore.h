#ifndef KVSTORE_H
#define KVSTORE_H

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <functional>   // std::hash

class KVStore {
private:
    static const size_t NUM_SHARDS = 8;   // a few x cores; power of 2

 struct alignas(128) Shard {              // "every Shard starts at a 128-multiple address"
    std::unordered_map<std::string, std::string> data;
    mutable std::mutex shard_mutex;
};

    Shard shards[NUM_SHARDS];             // the 8 doors, side by side in memory

    // Deterministic: same key -> same shard, every time, every thread.
    Shard& pick_shard(const std::string& key) {
        size_t hash_value = std::hash<std::string>{}(key);
        return shards[hash_value % NUM_SHARDS];
    }
    const Shard& pick_shard(const std::string& key) const {
        size_t hash_value = std::hash<std::string>{}(key);
        return shards[hash_value % NUM_SHARDS];
    }
public:
    void set(const std::string& key, const std::string& value) {
        Shard& shard = pick_shard(key);                 // which door?
        std::lock_guard<std::mutex> lock(shard.shard_mutex);
        shard.data[key] = value;
    }

    std::optional<std::string> get(const std::string& key) const {
        const Shard& shard = pick_shard(key);
        std::lock_guard<std::mutex> lock(shard.shard_mutex);
        auto it = shard.data.find(key);
        if (it == shard.data.end()) return std::nullopt;
        return it->second;                              // copy out (M4 lesson)
    }

    bool del(const std::string& key) {
        Shard& shard = pick_shard(key);
        std::lock_guard<std::mutex> lock(shard.shard_mutex);
        return shard.data.erase(key) > 0;
    }

};

#endif // KVSTORE_H
// Direct store benchmark: NO network. Threads hammer KVStore in-process.
// Isolates pure LOCK behavior -- where the 3 variants differ most.
// usage: ./bench_store <num_threads> <ops_per_thread>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include "kvstore.h"

int main(int argc, char* argv[]) {
    int num_threads    = (argc > 1) ? std::stoi(argv[1]) : 8;
    int ops_per_thread = (argc > 2) ? std::stoi(argv[2]) : 200000;

    KVStore store;
    auto start_time = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int id = 0; id < num_threads; ++id) {
        threads.emplace_back([id, ops_per_thread, &store] {
            for (int i = 0; i < ops_per_thread; ++i) {
                // i % 50: reuse 50 keys so we measure LOCKS, not map growth/rehash
                std::string key = "k" + std::to_string(id) + "_" + std::to_string(i % 50);
                if (i % 10 == 0) store.set(key, "value");   // 10% writes
                else             (void)store.get(key);      // 90% reads
            }
        });
    }
    for (std::thread& t : threads) t.join();

    auto end_time = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end_time - start_time).count();
    long total = static_cast<long>(num_threads) * ops_per_thread;

    std::cout << "threads: " << num_threads
              << "  ops: " << total
              << "  time: " << seconds << " s"
              << "  throughput: " << static_cast<long>(total / seconds) << " ops/sec\n";
    return 0;
}
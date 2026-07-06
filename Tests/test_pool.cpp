#include <iostream>
#include <chrono>
#include "../thread_pool.h"

std::mutex print_mutex;   // only so output lines don't garble (M4 lesson!)

int main() {
    ThreadPool pool(3);

    for (int task_id = 1; task_id <= 6; ++task_id) {
        pool.submit([task_id] {
            {
                std::lock_guard<std::mutex> lock(print_mutex);
                std::cout << "task " << task_id << " running on thread "
                          << std::this_thread::get_id() << "\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200)); // pretend work
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    std::cout << "main: done, pool destructor will wait for workers\n";
}   // ~ThreadPool runs here
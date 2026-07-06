#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

// A fixed team of worker threads sharing one to-do queue.
class ThreadPool {
public:
    // Hire 'number_of_workers' threads. They start immediately and wait for work.
    ThreadPool(size_t number_of_workers) {
        for (size_t i = 0; i < number_of_workers; ++i) {
            // Each worker runs worker_loop() forever (until shutdown).
            worker_threads.emplace_back(&ThreadPool::worker_loop, this);
        }
    }

    // Producer side: drop one task onto the queue and ring the bell.
    void submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);   // protect the shared queue
            task_queue.push(task);
        }                                                    // unlock BEFORE notifying
        work_available.notify_one();                         // wake ONE sleeping worker
    }

    // Shutdown: tell everyone to finish and wait for them to leave.
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop_requested = true;
        }
        work_available.notify_all();          // wake EVERY worker so they see the flag
        for (std::thread& worker : worker_threads) {
            worker.join();                    // wait for each worker to finish
        }
    }

private:
    // What every worker does for its whole life.
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);

                // Sleep until: there is work OR we are shutting down.
                work_available.wait(lock, [this] {
                    return !task_queue.empty() || stop_requested;
                });

                // Woke because of shutdown and nothing left to do -> go home.
                if (stop_requested && task_queue.empty()) {
                    return;
                }

                task = task_queue.front();    // take the oldest task
                task_queue.pop();
            }                                 // unlock HERE, before running the task
            task();                           // do the work WITHOUT holding the lock
        }
    }

    std::vector<std::thread> worker_threads;         // the fixed team
    std::queue<std::function<void()>> task_queue;    // the shared to-do board
    std::mutex queue_mutex;                          // guards task_queue + stop flag
    std::condition_variable work_available;          // the "work arrived" doorbell
    bool stop_requested = false;                     // shutdown signal
};

#endif // THREAD_POOL_H
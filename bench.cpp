// Benchmark client: N threads, each opens its own connection and fires
// SET+GET pairs as fast as the server answers. Prints total ops/second.
//
// usage: ./bench <num_clients> <ops_per_client>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t rc = send(fd, data + sent, n - sent, 0);
        if (rc == -1) return false;
        sent += static_cast<size_t>(rc);
    }
    return true;
}

// Read from fd until we have ONE complete '\n'-terminated reply.
// 'inbox' carries leftover bytes between calls (framing, M3 style).
bool read_one_reply(int fd, std::string& inbox) {
    while (inbox.find('\n') == std::string::npos) {
        char chunk[1024];
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;                     // server gone
        inbox.append(chunk, static_cast<size_t>(n));
    }
    inbox.erase(0, inbox.find('\n') + 1);             // consume exactly one reply
    return true;
}

std::atomic<long> total_ops_done{0};   // many threads increment -> atomic (M4!)

void client_worker(int client_id, int ops_per_client) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return;

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(6379);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&server_addr),
                sizeof(server_addr)) == -1) { close(fd); return; }

    std::string inbox;
    for (int i = 0; i < ops_per_client; ++i) {
        // Key includes client id: different clients spread across shards.
        std::string key = "k" + std::to_string(client_id) + "_" + std::to_string(i % 50);

        std::string set_cmd = "SET " + key + " value" + std::to_string(i) + "\n";
        if (!send_all(fd, set_cmd.data(), set_cmd.size())) break;
        if (!read_one_reply(fd, inbox)) break;
        total_ops_done++;

        std::string get_cmd = "GET " + key + "\n";
        if (!send_all(fd, get_cmd.data(), get_cmd.size())) break;
        if (!read_one_reply(fd, inbox)) break;
        total_ops_done++;
    }
    close(fd);
}

int main(int argc, char* argv[]) {
    int num_clients    = (argc > 1) ? std::stoi(argv[1]) : 8;
    int ops_per_client = (argc > 2) ? std::stoi(argv[2]) : 1000;

    auto start_time = std::chrono::steady_clock::now();   // steady: immune to clock changes

    std::vector<std::thread> clients;
    for (int id = 0; id < num_clients; ++id)
        clients.emplace_back(client_worker, id, ops_per_client);
    for (std::thread& t : clients) t.join();

    auto end_time = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end_time - start_time).count();

    long done = total_ops_done.load();
    std::cout << "clients: " << num_clients
              << "  ops completed: " << done
              << "  time: " << seconds << " s"
              << "  throughput: " << static_cast<long>(done / seconds) << " ops/sec\n";
    return 0;
}
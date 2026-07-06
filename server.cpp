#include <iostream>
#include <string>
#include <thread>          // std::thread::hardware_concurrency
#include <sys/socket.h>    // socket, setsockopt, bind, listen, accept, recv, send
#include <netinet/in.h>    // sockaddr_in, INADDR_ANY, htons
#include <arpa/inet.h>     // inet_ntop
#include <unistd.h>        // close
#include <cerrno>          // errno
#include <cstring>         // strerror
#include "kvstore.h"       // KVStore (sharded + cache-line aligned since M6)
#include "protocol.h"      // process_command
#include "thread_pool.h"   // ThreadPool (M5)
#include "socket_raii.h"   // Socket RAII wrapper (M8)

// send() may accept fewer bytes than asked; loop until all n are delivered.
bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t rc = send(fd, data + sent, n - sent, 0);
        if (rc == -1) return false;
        sent += static_cast<size_t>(rc);
    }
    return true;
}

// One worker serves one client: buffer bytes, cut complete lines, run, reply.
void handle_client(int raw_fd, KVStore& store) {
    Socket connection(raw_fd);   // RAII: fd closed automatically on EVERY exit path

    char chunk[1024];
    std::string inbox;           // bytes received but not yet processed

    while (true) {
        ssize_t n = recv(connection.fd(), chunk, sizeof(chunk), 0);
        if (n == 0)  { std::cout << "[fd " << connection.fd() << "] disconnected\n"; return; }
        if (n == -1) { std::cerr << "[fd " << connection.fd() << "] recv failed: "
                                 << strerror(errno) << "\n"; return; }

        inbox.append(chunk, static_cast<size_t>(n));

        // FRAMING LOOP: process every COMPLETE line sitting in the inbox
        size_t newline_pos;
        while ((newline_pos = inbox.find('\n')) != std::string::npos) {
            std::string line = inbox.substr(0, newline_pos);
            inbox.erase(0, newline_pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::string reply = process_command(store, line);
            if (!send_all(connection.fd(), reply.data(), reply.size())) {
                return;          // no close() needed -- destructor handles it
            }
        }
    }
}   // <- ~Socket() runs here or at any return above: close(fd), guaranteed

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return 1;
    }
    Socket listener(server_fd);   // RAII for the doorbell too

    int yes = 1;
    if (setsockopt(listener.fd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        std::cerr << "setsockopt() failed: " << strerror(errno) << "\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(6379);   // network byte order (M7!)

    if (bind(listener.fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::cerr << "bind() failed: " << strerror(errno) << "\n";
        return 1;
    }
    if (listen(listener.fd(), SOMAXCONN) == -1) {
        std::cerr << "listen() failed: " << strerror(errno) << "\n";
        return 1;
    }

    KVStore store;   // the one shared database (created ONCE, before the loop)

    size_t worker_count = std::thread::hardware_concurrency();
    if (worker_count == 0) worker_count = 4;
    ThreadPool pool(worker_count);
    std::cout << "Thread pool started with " << worker_count << " workers\n";
    std::cout << "KV server on port 6379... (Ctrl+C to stop)\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        int client_fd = accept(listener.fd(),
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd == -1) {
            if (errno == EINTR) continue;   // transient: retry
            std::cerr << "accept() failed: " << strerror(errno) << "\n";
            break;                          // permanent: stop, don't spin
        }

        char ip_text[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_text, sizeof(ip_text));
        std::cout << "New client " << ip_text << ":" << ntohs(client_addr.sin_port)
                  << " -> fd " << client_fd << "\n";

        // raw fd handed to the task; Socket is constructed INSIDE handle_client
        pool.submit([client_fd, &store] {
            handle_client(client_fd, store);
        });
    }

    return 0;   // ~ThreadPool joins workers, ~Socket closes listener -- all automatic
}
#include <iostream>
#include <string>
#include <thread>          // std::thread::hardware_concurrency
#include <sys/socket.h>    // socket, setsockopt, bind, listen, accept, recv, send
#include <netinet/in.h>    // sockaddr_in, INADDR_ANY, htons
#include <arpa/inet.h>     // inet_ntop
#include <unistd.h>        // close
#include <cerrno>          // errno
#include <cstring>         // strerror
#include <csignal>         // sigaction, SIGINT, SIGPIPE   (M8)
#include <atomic>          // std::atomic<bool>            (M8)
#include "kvstore.h"       // KVStore (sharded + cache-line aligned, M6)
#include "protocol.h"      // process_command (M3)
#include "thread_pool.h"   // ThreadPool (M5)
#include "socket_raii.h"   // Socket RAII wrapper (M8)

// Raised by the Ctrl+C handler, read by the accept loop.
// atomic: written from a signal handler while main reads it -- no race allowed.
std::atomic<bool> shutdown_requested{false};

// Runs when Ctrl+C arrives. Rule: do NOTHING here except raise the flag.
void handle_sigint(int) {
    shutdown_requested = true;
}

// send() may accept fewer bytes than asked; loop until all n are delivered.
bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t rc = send(fd, data + sent, n - sent, 0);
        if (rc == -1) return false;                 // connection broken
        sent += static_cast<size_t>(rc);
    }
    return true;
}

// One worker serves one client: buffer bytes, cut complete lines, run, reply.
void handle_client(int raw_fd, KVStore& store) {
    Socket connection(raw_fd);   // RAII: fd closed automatically on EVERY exit path

    char chunk[1024];            // raw bytes from one recv
    std::string inbox;           // bytes received but not yet processed

    while (true) {
        ssize_t n = recv(connection.fd(), chunk, sizeof(chunk), 0);   // blocks
        if (n == 0)  { std::cout << "[fd " << connection.fd() << "] disconnected\n"; return; }
        if (n == -1) { std::cerr << "[fd " << connection.fd() << "] recv failed: "
                                 << strerror(errno) << "\n"; return; }

        inbox.append(chunk, static_cast<size_t>(n));   // exactly n bytes

        // FRAMING LOOP: process every COMPLETE line sitting in the inbox
        size_t newline_pos;
        while ((newline_pos = inbox.find('\n')) != std::string::npos) {
            std::string line = inbox.substr(0, newline_pos);   // one command
            inbox.erase(0, newline_pos + 1);                   // remove it + '\n'
            if (!line.empty() && line.back() == '\r') line.pop_back();  // nc adds \r
            if (line.empty()) continue;

            std::string reply = process_command(store, line);  // protocol layer
            if (!send_all(connection.fd(), reply.data(), reply.size())) {
                return;      // no close() needed -- Socket destructor handles it
            }
        }
        // leftover partial command stays in inbox, waits for more bytes
    }
}   // <- ~Socket() runs here or at any return above: close(fd), guaranteed

int main() {
    // ---- 0. Signal setup (M8) -------------------------------------------
    // Ctrl+C: run our handler; no SA_RESTART, so it WAKES a blocked accept()
    // (which then returns -1 with errno == EINTR, letting us check the flag).
    struct sigaction sigint_action{};
    sigint_action.sa_handler = handle_sigint;
    sigaction(SIGINT, &sigint_action, nullptr);

    // A dead client mid-send raises SIGPIPE, which by default KILLS us.
    // Ignore it: send() then just returns -1 (EPIPE), which we already handle.
    signal(SIGPIPE, SIG_IGN);

    // ---- 1. Create the TCP socket ----------------------------------------
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return 1;
    }
    Socket listener(server_fd);   // RAII for the doorbell too

    // ---- 2. Allow quick restarts (skip the TIME_WAIT bind block) ---------
    int yes = 1;
    if (setsockopt(listener.fd(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        std::cerr << "setsockopt() failed: " << strerror(errno) << "\n";
        return 1;
    }

    // ---- 3. Our address: any local IP, port 6379 -------------------------
    sockaddr_in addr{};                    // zero every byte first
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(6379);   // network byte order (M7!)

    // ---- 4. Attach socket to that address --------------------------------
    if (bind(listener.fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::cerr << "bind() failed: " << strerror(errno) << "\n";
        return 1;
    }

    // ---- 5. Open the lobby ------------------------------------------------
    if (listen(listener.fd(), SOMAXCONN) == -1) {
        std::cerr << "listen() failed: " << strerror(errno) << "\n";
        return 1;
    }

    // ---- 6. Long-lived shared objects: created ONCE, before the loop -----
    KVStore store;                          // the one shared database

    size_t worker_count = std::thread::hardware_concurrency();
    if (worker_count == 0) worker_count = 4;   // fallback if the OS won't say
    ThreadPool pool(worker_count);
    std::cout << "Thread pool started with " << worker_count << " workers\n";
    std::cout << "KV server on port 6379... (Ctrl+C to stop)\n";

    // ---- 7. Accept loop: runs until Ctrl+C --------------------------------
    while (!shutdown_requested) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);   // in-out param, reset each time
        int client_fd = accept(listener.fd(),
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd == -1) {
            if (errno == EINTR) {
                if (shutdown_requested) break;   // woken BY Ctrl+C: close shop
                continue;                        // woken by something else: retry
            }
            std::cerr << "accept() failed: " << strerror(errno) << "\n";
            break;                               // permanent error: stop, don't spin
        }

        char ip_text[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_text, sizeof(ip_text));
        std::cout << "New client " << ip_text << ":" << ntohs(client_addr.sin_port)
                  << " -> fd " << client_fd << "\n";

        // Raw fd handed to the task; Socket ownership is constructed
        // INSIDE handle_client (wrapping it here would die at this '}').
        pool.submit([client_fd, &store] {
            handle_client(client_fd, store);
        });
    }

    std::cout << "\nShutting down: no new clients; waiting for active ones...\n";
    return 0;   // ~ThreadPool drains + joins workers, ~Socket closes listener
}
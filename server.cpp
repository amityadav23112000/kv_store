#include <iostream>
#include <sys/socket.h>   // socket, setsockopt, bind
#include <netinet/in.h>   // sockaddr_in, INADDR_ANY, htons
#include <unistd.h>       // close
#include <cerrno>
#include <cstring>        // strerror
#include <arpa/inet.h>    // inet_ntop
#include <thread>
#include "protocol.h"      // proce
// #include "kvstore.h"      // KVStore
// Send ALL n bytes, looping because send() may accept fewer than asked.
bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t rc = send(fd, data + sent, n - sent, 0);  // resume where we left off
        if (rc == -1) return false;                       // connection broken
        sent += static_cast<size_t>(rc);
    }
    return true;
}

void handle_client(int client_fd, KVStore& store) {
    char chunk[1024];
    std::string inbox;                       // bytes arrived, not yet processed

    while (true) {
        ssize_t n = recv(client_fd, chunk, sizeof(chunk), 0);
        if (n == 0)  { std::cout << "[fd " << client_fd << "] disconnected\n"; break; }
        if (n == -1) { std::cerr << "[fd " << client_fd << "] recv failed\n"; break; }

        inbox.append(chunk, static_cast<size_t>(n));   // exactly n bytes, no C-string games

        size_t nl;
        while ((nl = inbox.find('\n')) != std::string::npos) {  // a full line exists?
            std::string line = inbox.substr(0, nl);    // take it (without '\n')
            inbox.erase(0, nl + 1);                    // remove it from inbox
            if (!line.empty() && line.back() == '\r')  // nc/telnet send \r\n sometimes
                line.pop_back();
            if (line.empty()) continue;

            std::string reply = process_command(store, line);   // ← protocol layer
            if (!send_all(client_fd, reply.data(), reply.size())) { close(client_fd); return; }
        }
    }
    close(client_fd);
}

int main() {
    // ---- 1. Create the TCP socket -------------------------------------
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return 1;
    }

    // ---- 2. Allow quick restarts (opt out of TIME_WAIT bind block) ----
    int yes = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        std::cerr << "setsockopt() failed: " << strerror(errno) << "\n";
        return 1;
    }

    // ---- 3. Describe our address: any local IP, port 6379 -------------
    sockaddr_in addr{};                    // zero every byte first
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(6379);   // network byte order!

    // ---- 4. Attach socket to that address -----------------------------
    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::cerr << "bind() failed: " << strerror(errno) << "\n";
        return 1;
    }

    // ---- 5. Open the lobby: kernel now completes handshakes for us ----
    if (listen(server_fd, SOMAXCONN) == -1) {
        std::cerr << "listen() failed: " << strerror(errno) << "\n";
        return 1;
    }
    std::cout << "Echo server listening on port 6379... (Ctrl+C to stop)\n";

    // ---- 6. Outer loop: one iteration per CLIENT -----------------------
    while (true) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);   // in-out param, reset each time
        int client_fd = accept(server_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd == -1) {
            if (errno == EINTR) continue;   // transient (signal): retry
            std::cerr << "accept() failed: " << strerror(errno) << "\n";
            break;                          // permanent: die loudly, don't spin
        }

        char ip_text[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_text, sizeof(ip_text));
        std::cout << "Client connected from " << ip_text
                  << ":" << ntohs(client_addr.sin_port) << "\n";

        KVStore store;                                            // ONE store for everyone
        std::thread(handle_client, client_fd, std::ref(store)).detach();
    }

    close(server_fd);
    return 0;
}
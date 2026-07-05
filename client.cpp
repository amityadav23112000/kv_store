#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>    // inet_pton
#include <unistd.h>
#include <cerrno>
#include <cstring>

// Same helper as the server — in M3 this moves to a shared file.
bool send_all(int fd, const char* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t rc = send(fd, data + sent, n - sent, 0);
        if (rc == -1) return false;
        sent += static_cast<size_t>(rc);
    }
    return true;
}

int main() {
    // 1. Same first step as the server — a generic TCP endpoint
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return 1;
    }

    // 2. WHO we're calling (server side filled in WHO WE ARE — note the mirror)
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(6379);              // network byte order, always
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) != 1) {
        std::cerr << "inet_pton failed: bad address text\n";
        return 1;
    }

    // 3. Handshake happens HERE — first real packets; blocks until done
    if (connect(fd, reinterpret_cast<sockaddr*>(&server_addr),
                sizeof(server_addr)) == -1) {
        std::cerr << "connect() failed: " << strerror(errno) << "\n";
        return 1;
    }
    std::cout << "Connected to 127.0.0.1:6379. Type lines ('quit' to exit).\n";

    // 4. The request -> response loop
    std::string line;
    char buf[1024];
    while (std::getline(std::cin, line)) {   // read one line from the human
        if (line == "quit") break;
        line += '\n';                        // getline STRIPS '\n'; restore it
        if (!send_all(fd, line.data(), line.size())) {
            std::cerr << "send() failed: " << strerror(errno) << "\n";
            break;
        }
        ssize_t n = recv(fd, buf, sizeof(buf), 0);   // wait for the echo
        if (n == 0)  { std::cout << "Server closed the connection.\n"; break; }
        if (n == -1) { std::cerr << "recv() failed: " << strerror(errno) << "\n"; break; }
        std::cout << "echo> " << std::string(buf, static_cast<size_t>(n));
    }

    close(fd);
    return 0;
}
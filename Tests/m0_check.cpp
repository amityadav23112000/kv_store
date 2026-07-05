#include <iostream>      // std::cout
#include <sys/socket.h>  // socket() — the networking syscall wrapper
#include <netinet/in.h>  // sockaddr_in, htons (we'll use these from M1)
#include <unistd.h>      // close() — POSIX syscalls live here
#include <cerrno>        // errno — the "what went wrong" global
#include <cstring>       // strerror — errno number -> human text

int main() {
    // Ask the KERNEL to create a TCP socket. Returns a file descriptor.
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        std::cout << "socket() failed: " << strerror(errno) << "\n";
        return 1;
    }

    std::cout << "Kernel gave us socket file descriptor: " << fd << "\n";
    close(fd);   // give the resource back to the kernel
    std::cout << "Environment OK: compiler, headers, and syscalls all work.\n";
    return 0;
}
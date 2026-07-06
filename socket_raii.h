#ifndef SOCKET_RAII_H
#define SOCKET_RAII_H

#include <unistd.h>    // close

// RAII wrapper: owning a Socket object == owning the fd.
// When the object dies (any exit path), the fd is closed. Leaks impossible.
class Socket {
public:
    // Take ownership of an already-created fd (from socket() or accept()).
    explicit Socket(int fd) : fd_(fd) {}

    // Destructor: the whole point. Runs at '}' no matter HOW we leave.
    ~Socket() {
        if (fd_ != -1) close(fd_);
    }

    // COPYING IS FORBIDDEN: two owners would close the same fd twice.
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // MOVING IS ALLOWED: ownership transfers, the old object gives it up.
    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;                 // old owner now owns nothing
    }

    int fd() const { return fd_; }      // read access for recv/send

private:
    int fd_;                            // -1 means "owns nothing"
};

#endif // SOCKET_RAII_H
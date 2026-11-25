#pragma once
#include "net/phone_link.h"
#ifdef ESP_PLATFORM
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace phone {
inline sockaddr_in address(const char* ip) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kPort);
    inet_pton(AF_INET, ip, &a.sin_addr);
    return a;
}
inline Endpoint endpoint(const sockaddr_in& a) {
    Endpoint e;
    memcpy(e.id, &a.sin_addr, 4);
    memcpy(e.id + 4, &a.sin_port, 2);
    return e;
}

class Socket : public Link {
public:
    int fd = -1;
    void stop() {
        if (fd >= 0) close(fd);
        fd = -1;
        reset(false);
    }
protected:
    int read(Endpoint& src, uint8_t* data, size_t capacity) override {
        sockaddr_in a{};
        socklen_t alen = sizeof(a);
        int n = recvfrom(fd, data, capacity, MSG_DONTWAIT, (sockaddr*)&a, &alen);
        src = endpoint(a);
        return n;
    }
    void write(const Endpoint& dst, const uint8_t* data, size_t n) override {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        memcpy(&a.sin_addr, dst.id, 4);
        memcpy(&a.sin_port, dst.id + 4, 2);
        sendto(fd, data, n, MSG_DONTWAIT, (const sockaddr*)&a, sizeof(a));
    }
};
}

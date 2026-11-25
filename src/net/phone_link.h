#pragma once
#include "core/wire.h"
#include "net/net_session.h"

// One UDP endpoint per device. Discovery is unicast on this two-station link.
// IPv4 and port (network byte order) form the six-byte peer ID expected by Session.
namespace phone {
constexpr uint16_t kPort = 39731;
constexpr char kGateway[] = "192.168.73.1";
struct Endpoint {
    uint8_t id[6] = {};
    bool operator==(const Endpoint& other) const { return memcmp(id, other.id, 6) == 0; }
};
class Link : public net::Transport {
public:
    void reset(bool accessPoint, Endpoint gateway = {}) {
        ap_ = accessPoint;
        peer_ = gateway;
        confirmed_ = false;
        probed_ = false;
        clearInbox();
    }
    void clearInbox() { read_ = count_ = 0; }
    bool connected(uint32_t now) const { return confirmed_ && now - lastSeen_ < 2500; }
    void service(uint32_t now) {
        if (!connected(now)) confirmed_ = false;
        clearInbox();
        if (!ap_ && (!probed_ || now - lastProbe_ >= 500)) {
            write(peer_, probe_, sizeof(probe_));
            lastProbe_ = now;
            probed_ = true;
        }
        // A busy socket must not monopolize the game loop. One extra byte detects truncation.
        for (int i = 0; i < 16; ++i) {
            uint8_t data[arcade::kMaxPacket + 1];
            Endpoint from;
            int n = read(from, data, sizeof(data));
            if (n < 0) break;
            if (!n || size_t(n) > arcade::kMaxPacket) continue;
            if (ap_ && n == sizeof(probe_) && !memcmp(data, probe_, sizeof(probe_))) {
                if (connected(now) && !(from == peer_)) continue;
                if (!(from == peer_)) clearInbox();
                peer_ = from;
                confirmed_ = true;
                lastSeen_ = now;
                write(peer_, reply_, sizeof(reply_));
            } else if (from == peer_) {
                if (!ap_ && n == sizeof(reply_) && !memcmp(data, reply_, sizeof(reply_))) {
                    confirmed_ = true;
                    lastSeen_ = now;
                } else if (connected(now) && data[0] == 0xd7) {
                    Packet& p = packets_[count_++];
                    memcpy(p.data, data, n);
                    p.size = uint8_t(n);
                }
            }
        }
    }
    bool poll(uint8_t id[6], uint8_t* data, size_t* len) {
        if (read_ == count_) return false;
        const Packet& p = packets_[read_++];
        memcpy(id, peer_.id, 6);
        memcpy(data, p.data, p.size);
        *len = p.size;
        return true;
    }
    void send(const uint8_t* id, const uint8_t* data, size_t n) override {
        if (confirmed_ && n && n <= arcade::kMaxPacket && (!id || !memcmp(id, peer_.id, 6)))
            write(peer_, data, n);
    }
protected:
    virtual int read(Endpoint&, uint8_t*, size_t) = 0;
    virtual void write(const Endpoint&, const uint8_t*, size_t) = 0;
private:
    const uint8_t probe_[5] = {'S','A','P',1,'?'};
    const uint8_t reply_[5] = {'S','A','P',1,'!'};
    struct Packet { uint8_t size, data[arcade::kMaxPacket]; };
    Packet packets_[16];
    int count_ = 0, read_ = 0;
    Endpoint peer_;
    bool ap_ = false, confirmed_ = false, probed_ = false;
    uint32_t lastSeen_ = 0, lastProbe_ = 0;
};
}

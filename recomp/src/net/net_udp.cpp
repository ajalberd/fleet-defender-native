#include "net/net_udp.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <chrono>
#include <cstring>

namespace f14 {

namespace {
uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
} // namespace

NetUdp::NetUdp() = default;

NetUdp::~NetUdp() {
    shutdown();
}

bool NetUdp::init() {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        status_ = "WSAStartup failed";
        return false;
    }
    wsa_ready_ = true;
#endif
    return true;
}

void NetUdp::shutdown() {
    disconnect();
#ifdef _WIN32
    if (wsa_ready_) {
        WSACleanup();
        wsa_ready_ = false;
    }
#endif
}

void NetUdp::close_socket() {
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = INVALID_SOCKET;
    }
#endif
}

bool NetUdp::start_host(uint16_t port) {
    disconnect();
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        status_ = "Failed to create UDP socket";
        return false;
    }
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        status_ = "Failed to bind UDP port " + std::to_string(port);
        return false;
    }
    sock_ = static_cast<uintptr_t>(s);
    local_port_ = port;
    mode_ = NetMode::Host;
    local_node_id_[5] = 0x01;
    status_ = "Hosting UDP on port " + std::to_string(port);
    return true;
#else
    return false;
#endif
}

bool NetUdp::join_host(const std::string& host_ip, uint16_t port) {
    disconnect();
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        status_ = "Failed to create UDP socket";
        return false;
    }
    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0; // ephemeral port
    bind(s, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr));

    sock_ = static_cast<uintptr_t>(s);
    local_port_ = port;
    mode_ = NetMode::Client;
    local_node_id_[5] = static_cast<uint8_t>((now_ms() & 0xFE) | 0x02);
    register_or_update_peer(host_ip, port);
    status_ = "Connected to " + host_ip + ":" + std::to_string(port);
    send_ping();
    return true;
#else
    return false;
#endif
}

void NetUdp::disconnect() {
    close_socket();
    mode_ = NetMode::Offline;
    peers_.clear();
    rx_queue_.clear();
    status_ = "Offline";
}

void NetUdp::register_or_update_peer(const std::string& ip, uint16_t port) {
    uint64_t t = now_ms();
    for (auto& p : peers_) {
        if (p.ip == ip && p.port == port) {
            p.last_seen_ms = t;
            return;
        }
    }
    peers_.push_back({ip, port, t});
}

bool NetUdp::send_payload(const uint8_t* data, size_t len) {
#ifdef _WIN32
    if (sock_ == INVALID_SOCKET || mode_ == NetMode::Offline || peers_.empty()) return false;
    bool sent_any = false;
    for (const auto& peer : peers_) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(peer.port);
        if (inet_pton(AF_INET, peer.ip.c_str(), &dst.sin_addr) == 1) {
            int n = sendto(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(data),
                           static_cast<int>(len), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
            if (n > 0) {
                tx_packets_++;
                sent_any = true;
            }
        }
    }
    return sent_any;
#else
    (void)data; (void)len;
    return false;
#endif
}

void NetUdp::send_ping() {
    const uint8_t ping[8] = {'F', '3', 'P', 'I', 'N', 'G', 0, 1};
    send_payload(ping, sizeof(ping));
}

void NetUdp::poll() {
#ifdef _WIN32
    if (sock_ == INVALID_SOCKET || mode_ == NetMode::Offline) return;
    uint8_t buf[2048];
    for (int iter = 0; iter < 32; ++iter) {
        sockaddr_in from{};
        int from_len = sizeof(from);
        int n = recvfrom(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf), sizeof(buf),
                         0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n <= 0) break;

        char ip_str[64]{};
        inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
        uint16_t port = ntohs(from.sin_port);
        register_or_update_peer(ip_str, port);
        rx_packets_++;

        // Ignore internal ping packets in game IPX queue
        if (n == 8 && std::memcmp(buf, "F3PING", 6) == 0) {
            continue;
        }
        NetPacket pkt;
        pkt.sender_ip = ip_str;
        pkt.sender_port = port;
        pkt.data.assign(buf, buf + n);
        rx_queue_.push_back(std::move(pkt));
        if (rx_queue_.size() > 128) rx_queue_.pop_front();
    }
#endif
}

bool NetUdp::pop_packet(NetPacket& out) {
    if (rx_queue_.empty()) return false;
    out = std::move(rx_queue_.front());
    rx_queue_.pop_front();
    return true;
}

} // namespace f14

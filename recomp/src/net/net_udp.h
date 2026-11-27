#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace f14 {

enum class NetMode {
    Offline = 0,
    Host = 1,
    Client = 2
};

struct PeerEndpoint {
    std::string ip;
    uint16_t port = 0;
    uint64_t last_seen_ms = 0;
};

struct NetPacket {
    std::string sender_ip;
    uint16_t sender_port = 0;
    std::vector<uint8_t> data;
};

class NetUdp {
public:
    NetUdp();
    ~NetUdp();

    bool init();
    void shutdown();

    bool start_host(uint16_t port = 19920);
    bool join_host(const std::string& host_ip, uint16_t port = 19920);
    void disconnect();

    // Send raw payload to all connected peers (Host) or to the Host (Client)
    bool send_payload(const uint8_t* data, size_t len);
    void send_ping();

    // Poll non-blocking UDP socket into rx_queue_
    void poll();
    bool pop_packet(NetPacket& out);

    NetMode mode() const { return mode_; }
    const std::string& status() const { return status_; }
    uint16_t local_port() const { return local_port_; }
    const std::vector<PeerEndpoint>& peers() const { return peers_; }
    uint64_t tx_packets() const { return tx_packets_; }
    uint64_t rx_packets() const { return rx_packets_; }
    const std::array<uint8_t, 6>& local_node_id() const { return local_node_id_; }

private:
    uintptr_t sock_ = static_cast<uintptr_t>(~0ULL);
    bool wsa_ready_ = false;
    NetMode mode_ = NetMode::Offline;
    uint16_t local_port_ = 19920;
    std::string status_ = "Offline";
    std::vector<PeerEndpoint> peers_;
    std::deque<NetPacket> rx_queue_;
    uint64_t tx_packets_ = 0;
    uint64_t rx_packets_ = 0;
    std::array<uint8_t, 6> local_node_id_{0xFA, 0x1C, 0x03, 0x00, 0x00, 0x01};

    void close_socket();
    void register_or_update_peer(const std::string& ip, uint16_t port);
};

} // namespace f14

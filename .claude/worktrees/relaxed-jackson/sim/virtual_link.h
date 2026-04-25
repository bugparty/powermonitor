#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

namespace sim {

struct LinkConfig {
    size_t min_chunk = 1;
    size_t max_chunk = 16;
    uint32_t min_delay_us = 0;
    uint32_t max_delay_us = 2000;
    double drop_prob = 0.0;
    double flip_prob = 0.0;
};

class VirtualLink;

class VirtualLinkEndpoint {
public:
    VirtualLinkEndpoint() = default;
    VirtualLinkEndpoint(VirtualLink *link, int direction);

    void write(const std::vector<uint8_t> &bytes, uint64_t now_us);
    size_t available() const;
    std::vector<uint8_t> read(size_t max_bytes);
    
    // Get the receive time of the most recently delivered data
    // Returns 0 if no data has been received yet
    uint64_t last_receive_time() const { return last_receive_time_; }

private:
    VirtualLink *link_ = nullptr;
    int direction_ = 0;
    std::vector<uint8_t> rx_buffer_;
    uint64_t last_receive_time_ = 0;  // Timestamp of most recent data delivery

    friend class VirtualLink;
};

class VirtualLink {
public:
    VirtualLink();

    VirtualLinkEndpoint &pc();
    VirtualLinkEndpoint &device();

    void set_pc_to_dev_config(const LinkConfig &config);
    void set_dev_to_pc_config(const LinkConfig &config);

    void pump(uint64_t now_us);

private:
    struct PendingChunk {
        uint64_t deliver_time = 0;
        std::vector<uint8_t> bytes;
        int direction = 0;
    };

    void send(int direction, const std::vector<uint8_t> &bytes, uint64_t now_us);
    void deliver(PendingChunk &chunk);

    LinkConfig pc_to_dev_{};
    LinkConfig dev_to_pc_{};
    VirtualLinkEndpoint pc_;
    VirtualLinkEndpoint dev_;
    std::deque<PendingChunk> pending_;
    std::mt19937 rng_;
    uint64_t last_delivery_[2] = {}; // Last delivery timestamp for each direction: [0]=PC-to-device, [1]=device-to-PC

    friend class VirtualLinkEndpoint;
};

} // namespace sim

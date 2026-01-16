#include "sim/virtual_link.h"

#include <algorithm>
#include <chrono>

namespace sim {

VirtualLinkEndpoint::VirtualLinkEndpoint(VirtualLink *link, int direction)
    : link_(link), direction_(direction) {}

void VirtualLinkEndpoint::write(const std::vector<uint8_t> &bytes, uint64_t now_us) {
    if (!link_ || bytes.empty()) {
        return;
    }
    link_->send(direction_, bytes, now_us);
}

size_t VirtualLinkEndpoint::available() const {
    return rx_buffer_.size();
}

std::vector<uint8_t> VirtualLinkEndpoint::read(size_t max_bytes) {
    const size_t count = std::min(max_bytes, rx_buffer_.size());
    std::vector<uint8_t> out(rx_buffer_.begin(), rx_buffer_.begin() + count);
    rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + count);
    return out;
}

VirtualLink::VirtualLink()
    : pc_(this, 0), dev_(this, 1), rng_(static_cast<uint32_t>(
                                            std::chrono::steady_clock::now().time_since_epoch().count()))
    {}

VirtualLinkEndpoint &VirtualLink::pc() { return pc_; }
VirtualLinkEndpoint &VirtualLink::device() { return dev_; }

void VirtualLink::set_pc_to_dev_config(const LinkConfig &config) { pc_to_dev_ = config; }
void VirtualLink::set_dev_to_pc_config(const LinkConfig &config) { dev_to_pc_ = config; }

void VirtualLink::send(int direction, const std::vector<uint8_t> &bytes, uint64_t now_us) {
    const LinkConfig &config = direction == 0 ? pc_to_dev_ : dev_to_pc_;
    std::uniform_int_distribution<size_t> chunk_dist(config.min_chunk, config.max_chunk);
    std::uniform_int_distribution<uint32_t> delay_dist(config.min_delay_us, config.max_delay_us);
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<int> bit_dist(0, 7);

    size_t offset = 0;
    while (offset < bytes.size()) {
        size_t chunk_size = chunk_dist(rng_);
        chunk_size = std::min(chunk_size, bytes.size() - offset);
        std::vector<uint8_t> chunk(bytes.begin() + offset, bytes.begin() + offset + chunk_size);
        offset += chunk_size;

        if (prob_dist(rng_) < config.drop_prob) {
            continue;
        }

        for (auto &byte : chunk) {
            if (prob_dist(rng_) < config.flip_prob) {
                byte ^= static_cast<uint8_t>(1U << bit_dist(rng_));
            }
        }

        PendingChunk pending;
        pending.direction = direction;
        uint64_t deliver_time = now_us + delay_dist(rng_);
        if (deliver_time <= last_delivery_[direction]) {
            deliver_time = last_delivery_[direction] + 1;
        }
        pending.deliver_time = deliver_time;
        last_delivery_[direction] = deliver_time;
        pending.bytes = std::move(chunk);
        pending_.push_back(std::move(pending));
    }
}

void VirtualLink::pump(uint64_t now_us) {
    auto it = pending_.begin();
    while (it != pending_.end()) {
        if (it->deliver_time <= now_us) {
            deliver(*it);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void VirtualLink::deliver(PendingChunk &chunk) {
    if (chunk.direction == 0) {
        dev_.rx_buffer_.insert(dev_.rx_buffer_.end(), chunk.bytes.begin(), chunk.bytes.end());
    } else {
        pc_.rx_buffer_.insert(pc_.rx_buffer_.end(), chunk.bytes.begin(), chunk.bytes.end());
    }
}

} // namespace sim

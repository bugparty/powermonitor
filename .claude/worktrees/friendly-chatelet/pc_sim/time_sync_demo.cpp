#include <iostream>

#include "node/device_node.h"
#include "node/pc_node.h"
#include "sim/event_loop.h"
#include "sim/virtual_link.h"

// Simple standalone demo to observe time synchronization behavior in pc_sim.
// Build this as a normal executable and run it to see TIME_SYNC exchanges,
// calculated delay/offset, and how they behave under link delay/jitter.

int main() {
    sim::VirtualLink link;

    // Configure link with realistic delay/jitter and no packet loss/bit flips.
    sim::LinkConfig config;
    config.min_chunk = 1;
    config.max_chunk = 32;
    config.min_delay_us = 100;    // 100 us minimum one-way delay
    config.max_delay_us = 5000;   // up to 5 ms one-way delay (jitter)
    config.drop_prob = 0.0;       // no drops for this demo
    config.flip_prob = 0.0;       // no bit flips
    link.set_pc_to_dev_config(config);
    link.set_dev_to_pc_config(config);

    node::PCNode pc(&link.pc());
    node::DeviceNode device(&link.device());
    sim::EventLoop loop;

    // Optional: set an initial absolute time on the device so TIME_SET path is exercised.
    const uint64_t initial_unix_time_us = 1'600'000'000'000'000ULL; // arbitrary demo value
    pc.send_time_set(initial_unix_time_us, loop.now_us());

    // Track next sync time for periodic TIME_SYNC every 1 second
    uint64_t next_sync_time_us = 0;
    const uint64_t sync_interval_us = 1'000'000;  // 1 second

    // Run simulation for 20 seconds, tick every 500 us.
    loop.run_for(20'000'000, 500, [&](uint64_t now_us) {
        link.pump(now_us);
        pc.tick(now_us);
        device.tick(now_us);

        // Send TIME_SYNC periodically
        if (now_us >= next_sync_time_us) {
            std::cout << "\n=== TIME_SYNC at now_us=" << now_us << " ===\n";
            pc.send_time_sync(now_us);
            next_sync_time_us = now_us + sync_interval_us;
        }
    });

    std::cout << "\nSummary:\n";
    std::cout << "  crc_fail_count = " << pc.crc_fail_count() << "\n";
    std::cout << "  data_drop_count = " << pc.data_drop_count() << "\n";
    std::cout << "  timeout_count   = " << pc.timeout_count() << "\n";
    std::cout << "  retransmit_count= " << pc.retransmit_count() << "\n";

    return 0;
}


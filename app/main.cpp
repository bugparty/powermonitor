#include <iostream>

#include "node/device_node.h"
#include "node/pc_node.h"
#include "sim/event_loop.h"
#include "sim/virtual_link.h"

int main() {
    sim::VirtualLink link;
    sim::LinkConfig config;
    config.min_chunk = 1;
    config.max_chunk = 16;
    config.min_delay_us = 0;
    config.max_delay_us = 2000;
    config.drop_prob = 0.0;
    config.flip_prob = 0.0;
    link.set_pc_to_dev_config(config);
    link.set_dev_to_pc_config(config);

    node::PCNode pc(&link.pc());
    node::DeviceNode device(&link.device());

    sim::EventLoop loop;

    pc.send_ping(loop.now_us());
    pc.send_set_cfg(0x0000, 0x0000, 0x1000, 0x0000, loop.now_us());
    pc.send_stream_start(1000, 0x000F, loop.now_us());

    loop.run_for(10'000'000, 500, [&](uint64_t now_us) {
        link.pump(now_us);
        pc.tick(now_us);
        device.tick(now_us);
    });

    pc.send_stream_stop(loop.now_us());
    loop.run_for(500'000, 500, [&](uint64_t now_us) {
        link.pump(now_us);
        pc.tick(now_us);
        device.tick(now_us);
    });

    std::cout << "Summary: crc_fail=" << pc.crc_fail_count() << " data_drop="
              << pc.data_drop_count() << " timeouts=" << pc.timeout_count()
              << " retries=" << pc.retransmit_count() << "\n";
    return 0;
}

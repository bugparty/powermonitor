#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace sim {

class EventLoop {
public:
    using Task = std::function<void(uint64_t)>;

    uint64_t now_us() const { return now_us_; }

    void schedule_in(uint64_t delay_us, Task task);
    void run_for(uint64_t duration_us, uint64_t tick_us, const Task &tick_task);

private:
    struct Timer {
        uint64_t due = 0;
        size_t id = 0;
        Task task;

        bool operator>(const Timer &other) const { return due > other.due; }
    };

    uint64_t now_us_ = 0;
    size_t next_id_ = 0;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers_;
};

} // namespace sim

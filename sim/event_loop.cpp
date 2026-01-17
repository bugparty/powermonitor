#include "sim/event_loop.h"

namespace sim {

void EventLoop::schedule_in(uint64_t delay_us, Task task) {
    Timer timer;
    timer.due = now_us_ + delay_us;
    timer.id = next_id_++;
    timer.task = std::move(task);
    timers_.push(std::move(timer));
}

void EventLoop::run_for(uint64_t duration_us, uint64_t tick_us, const Task &tick_task) {
    const uint64_t end_time = now_us_ + duration_us;
    while (now_us_ < end_time) {
        while (!timers_.empty() && timers_.top().due <= now_us_) {
            auto timer = timers_.top();
            timers_.pop();
            if (timer.task) {
                timer.task(now_us_);
            }
        }
        if (tick_task) {
            tick_task(now_us_);
        }
        now_us_ += tick_us;
    }
}

} // namespace sim

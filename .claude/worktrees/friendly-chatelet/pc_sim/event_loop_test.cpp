#include <gtest/gtest.h>
#include "sim/event_loop.h"
#include <vector>

namespace sim {

class EventLoopTest : public ::testing::Test {
protected:
    EventLoop loop;
};

TEST_F(EventLoopTest, ScheduleInFuture) {
    bool executed = false;
    uint64_t execution_time = 0;

    loop.schedule_in(100, [&](uint64_t now) {
        executed = true;
        execution_time = now;
    });

    loop.run_for(50, 10, nullptr);
    EXPECT_FALSE(executed);

    loop.run_for(60, 10, nullptr);
    EXPECT_TRUE(executed);
    EXPECT_EQ(execution_time, 100);
}

TEST_F(EventLoopTest, MultipleTasksOrder) {
    std::vector<int> order;

    loop.schedule_in(200, [&](uint64_t) { order.push_back(2); });
    loop.schedule_in(100, [&](uint64_t) { order.push_back(1); });
    loop.schedule_in(300, [&](uint64_t) { order.push_back(3); });

    loop.run_for(400, 10, nullptr);

    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST_F(EventLoopTest, MultipleTasksSameTime) {
    int count = 0;
    loop.schedule_in(100, [&](uint64_t) { count++; });
    loop.schedule_in(100, [&](uint64_t) { count++; });

    loop.run_for(150, 10, nullptr);
    EXPECT_EQ(count, 2);
}

TEST_F(EventLoopTest, TickTask) {
    int ticks = 0;
    loop.run_for(100, 10, [&](uint64_t) { ticks++; });

    // 0, 10, 20, 30, 40, 50, 60, 70, 80, 90 (10 ticks)
    // The loop condition is while (now_us_ < end_time)
    // now_us_ starts at 0, end_time is 100.
    // Iterations:
    // now=0: run tick, now+=10 (now=10)
    // now=10: run tick, now+=10 (now=20)
    // ...
    // now=90: run tick, now+=10 (now=100)
    // now=100: exit loop
    EXPECT_EQ(ticks, 10);
}

TEST_F(EventLoopTest, RunForUpdatesTime) {
    EXPECT_EQ(loop.now_us(), 0);
    loop.run_for(100, 10, nullptr);
    EXPECT_EQ(loop.now_us(), 100);
    loop.run_for(50, 5, nullptr);
    EXPECT_EQ(loop.now_us(), 150);
}

TEST_F(EventLoopTest, TaskSchedulesAnotherTask) {
    bool task2_executed = false;
    loop.schedule_in(100, [&](uint64_t) {
        loop.schedule_in(100, [&](uint64_t) {
            task2_executed = true;
        });
    });

    loop.run_for(150, 10, nullptr);
    EXPECT_FALSE(task2_executed);

    loop.run_for(100, 10, nullptr);
    EXPECT_TRUE(task2_executed);
    EXPECT_EQ(loop.now_us(), 250);
}

TEST_F(EventLoopTest, TaskWithZeroDelayRunsImmediatelyInRunFor) {
    bool executed = false;
    loop.schedule_in(0, [&](uint64_t) {
        executed = true;
    });

    // Run for 0 duration with some tick?
    // If duration is 0, end_time = now_us_, while (now_us_ < end_time) won't even start.
    loop.run_for(1, 1, nullptr);
    EXPECT_TRUE(executed);
}

} // namespace sim

#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/wait.h>
#include "shm_power_ring_buffer.h"

using namespace powermonitor::client;

TEST(ShmPowerSpscTest, CreateAndPush) {
    const char* test_shm = "/test_power_spsc_basic";
    shm_unlink(test_shm);  // Clean up if exists

    ShmPowerRingBuffer buffer(test_shm);
    ASSERT_TRUE(buffer.valid());
    ASSERT_TRUE(buffer.is_creator());

    RealtimePowerSample sample{};
    sample.power_w = 10.5;
    sample.sequence_num = 1;

    buffer.push(sample);
    // No assertion - just verify it doesn't crash

    shm_unlink(test_shm);
}

TEST(ShmPowerSpscTest, OverflowCount) {
    const char* test_shm = "/test_power_spsc_overflow";
    shm_unlink(test_shm);

    ShmPowerRingBuffer buffer(test_shm);
    ASSERT_TRUE(buffer.valid());

    // Push more than capacity to trigger overflow
    for (size_t i = 0; i < kPowerMetricsRingCapacity + 100; ++i) {
        RealtimePowerSample sample{};
        sample.sequence_num = i;
        buffer.push(sample);
    }

    EXPECT_GT(buffer.overflow_count(), 0);

    shm_unlink(test_shm);
}

TEST(ShmPowerSpscTest, CrossProcessPushPop) {
    const char* test_shm = "/test_power_spsc_ipc";
    shm_unlink(test_shm);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // Child: writer
        ShmPowerRingBuffer writer(test_shm);
        if (!writer.valid()) exit(1);

        for (int i = 0; i < 10; ++i) {
            RealtimePowerSample sample{};
            sample.sequence_num = i;
            sample.power_w = static_cast<double>(i) * 1.5;
            writer.push(sample);
        }
        exit(0);
    } else {
        // Parent: reader (use SPSC directly)
        usleep(10000);  // Wait for writer to start

        using PowerSpscBuffer = buffers::spsc_ring_buffer<
            RealtimePowerSample,
            kPowerMetricsRingCapacity,
            buffers::ShmStorage
        >;

        PowerSpscBuffer reader(test_shm, kPowerMetricsVersion, buffers::ShmOpenMode::open);
        ASSERT_TRUE(reader.valid());

        int expected = 0;
        int attempts = 0;
        while (expected < 10 && attempts < 1000) {
            RealtimePowerSample sample;
            if (reader.try_pop(sample)) {
                EXPECT_EQ(sample.sequence_num, expected);
                EXPECT_DOUBLE_EQ(sample.power_w, static_cast<double>(expected) * 1.5);
                ++expected;
            }
            ++attempts;
            if (attempts % 100 == 0) usleep(100);
        }

        EXPECT_EQ(expected, 10);

        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }

    shm_unlink(test_shm);
}

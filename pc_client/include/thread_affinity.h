#pragma once

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <string>

namespace powermonitor {
namespace client {

class ThreadAffinity {
public:
    // Set CPU affinity for the calling thread
    // Returns true on success, false on failure
    static bool SetCpuAffinity(int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);

        pthread_t thread = pthread_self();
        int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

        return ret == 0;
    }

    // Set real-time scheduling priority for the calling thread
    // prio: 1-99 (higher = more important)
    // Returns true on success, false on failure
    static bool SetRealtimePriority(int prio) {
        struct sched_param param;
        param.sched_priority = prio;

        int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        return ret == 0;
    }

    // Combine affinity + RT priority in one call
    // Returns true if both succeed, false otherwise
    static bool SetRealtimeWithAffinity(int core_id, int prio) {
        bool affinity_ok = SetCpuAffinity(core_id);
        bool rt_ok = SetRealtimePriority(prio);
        return affinity_ok && rt_ok;
    }

    // Get current CPU affinity (returns core ID, -1 if not bound)
    static int GetCurrentCore() {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);

        pthread_t thread = pthread_self();
        int ret = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

        if (ret != 0) {
            return -1;
        }

        // Check if only one core is set
        int core_count = 0;
        int last_core = -1;
        for (int i = 0; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &cpuset)) {
                core_count++;
                last_core = i;
            }
        }

        return (core_count == 1) ? last_core : -1;
    }

    // Get number of CPU cores
    static int GetNumCores() {
        return sysconf(_SC_NPROCESSORS_ONLN);
    }

    // Print current thread's affinity and priority info
    static void PrintThreadInfo(const std::string& thread_name) {
        int core = GetCurrentCore();
        int policy;
        struct sched_param param;

        pthread_getschedparam(pthread_self(), &policy, &param);

        const char* policy_str;
        switch (policy) {
            case SCHED_FIFO: policy_str = "SCHED_FIFO"; break;
            case SCHED_RR:   policy_str = "SCHED_RR"; break;
            default:         policy_str = "SCHED_OTHER"; break;
        }

        printf("[ThreadAffinity] %s: core=%d policy=%s prio=%d\n",
               thread_name.c_str(), core, policy_str, param.sched_priority);
    }
};

} // namespace client
} // namespace powermonitor

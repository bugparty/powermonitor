#pragma once

#include <cstdio>
#include <string>

#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace powermonitor {
namespace client {

class ThreadAffinity {
public:
    // Set CPU affinity for the calling thread.
    // Returns true on success; no-op and returns false on Windows.
    static bool SetCpuAffinity(int core_id) {
#ifndef _WIN32
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
        (void)core_id;
        return false;
#endif
    }

    // Set real-time scheduling priority (SCHED_FIFO).
    // prio: 1-99. No-op and returns false on Windows.
    static bool SetRealtimePriority(int prio) {
#ifndef _WIN32
        struct sched_param param;
        param.sched_priority = prio;
        return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#else
        (void)prio;
        return false;
#endif
    }

    // Combine affinity + RT priority in one call.
    static bool SetRealtimeWithAffinity(int core_id, int prio) {
        return SetCpuAffinity(core_id) & SetRealtimePriority(prio);
    }

    // Returns the single pinned core ID, or -1 if not bound (or on Windows).
    static int GetCurrentCore() {
#ifndef _WIN32
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
            return -1;
        int count = 0, last = -1;
        for (int i = 0; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &cpuset)) { ++count; last = i; }
        }
        return (count == 1) ? last : -1;
#else
        return -1;
#endif
    }

    // Returns the number of online CPUs (1 on Windows stub).
    static int GetNumCores() {
#ifndef _WIN32
        return static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
#else
        return 1;
#endif
    }

    // Print current thread's affinity and scheduling policy info.
    static void PrintThreadInfo(const std::string& thread_name) {
#ifndef _WIN32
        int policy;
        struct sched_param param;
        pthread_getschedparam(pthread_self(), &policy, &param);
        const char* policy_str =
            (policy == SCHED_FIFO) ? "SCHED_FIFO" :
            (policy == SCHED_RR)   ? "SCHED_RR"   : "SCHED_OTHER";
        printf("[ThreadAffinity] %s: core=%d policy=%s prio=%d\n",
               thread_name.c_str(), GetCurrentCore(), policy_str, param.sched_priority);
#else
        printf("[ThreadAffinity] %s: (not supported on Windows)\n", thread_name.c_str());
#endif
    }
};

} // namespace client
} // namespace powermonitor

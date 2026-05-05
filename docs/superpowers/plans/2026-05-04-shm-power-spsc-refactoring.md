# ShmPowerRingBuffer SPSC Refactoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hand-rolled atomic ring buffer with production-grade SPSC queue from RingBuffer library.

**Architecture:** C++ writer uses `spsc_ring_buffer<..., ShmStorage>` with `push_overwrite()`, Python reader consumes via `try_pop()` through C extension. Cross-process via POSIX shared memory.

**Tech Stack:** C++17, Python 3 C Extension, RingBuffer library (spsc_ring_buffer + ShmStorage policy), POSIX shm_open/mmap

---

## Task 1: Update Schema Version

**Files:**
- Modify: `pc_client/include/realtime_power_metrics.h:12`

- [ ] **Step 1: Update kPowerMetricsVersion constant**

Change version from 2 to 3 to signal SHM layout incompatibility.

```cpp
// Before
constexpr uint32_t kPowerMetricsVersion = 2;

// After
constexpr uint32_t kPowerMetricsVersion = 3;
```

- [ ] **Step 2: Commit version bump**

```bash
git add pc_client/include/realtime_power_metrics.h
git commit -m "refactor(power): bump kPowerMetricsVersion to 3 for SPSC migration"
```

---

## Task 2: Rewrite ShmPowerRingBuffer Header

**Files:**
- Modify: `pc_client/include/shm_power_ring_buffer.h`

- [ ] **Step 1: Replace implementation with spsc_ring_buffer**

Replace entire file content with SPSC implementation:

```cpp
#pragma once

#include <spsc_ring_buffer.hpp>
#include "realtime_power_metrics.h"

namespace powermonitor {
namespace client {

class ShmPowerRingBuffer {
public:
    ShmPowerRingBuffer()
        : buffer_(POWER_METRICS_SHM_NAME,
                  kPowerMetricsVersion,
                  buffers::ShmOpenMode::create_or_open) {}

    explicit ShmPowerRingBuffer(const char* shm_name)
        : buffer_(shm_name,
                  kPowerMetricsVersion,
                  buffers::ShmOpenMode::create_or_open) {}

    void push(const RealtimePowerSample& sample) {
        buffer_.push_overwrite(sample);
    }

    uint64_t overflow_count() const {
        return buffer_.overflow_count();
    }

    bool valid() const { return buffer_.valid(); }
    bool is_creator() const { return buffer_.is_creator(); }

private:
    using PowerSpscBuffer = buffers::spsc_ring_buffer<
        RealtimePowerSample,
        kPowerMetricsRingCapacity,
        buffers::ShmStorage
    >;

    PowerSpscBuffer buffer_;
};

}  // namespace client
}  // namespace powermonitor
```

- [ ] **Step 2: Commit ShmPowerRingBuffer refactor**

```bash
git add pc_client/include/shm_power_ring_buffer.h
git commit -m "refactor(power): replace ShmPowerRingBuffer with spsc_ring_buffer"
```

---

## Task 3: Write C++ Unit Tests

**Files:**
- Create: `pc_client/tests/shm_spsc_test.cpp`

- [ ] **Step 1: Create test file with basic SPSC test**

```cpp
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
```

- [ ] **Step 2: Add test to CMakeLists.txt**

Update `pc_client/CMakeLists.txt` to add new test executable:

```cmake
# After pc_client_tests definition (around line 152)

add_executable(shm_spsc_test
    tests/shm_spsc_test.cpp
)

target_link_libraries(shm_spsc_test PRIVATE
    pc_client_lib
    gtest_main
    RingBuffer::RingBuffer
)

if(NOT MSVC AND UNIX)
    target_link_libraries(shm_spsc_test PRIVATE pthread rt)
endif()

target_include_directories(shm_spsc_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../protocol
)

target_compile_definitions(shm_spsc_test PRIVATE ASIO_STANDALONE)

gtest_discover_tests(shm_spsc_test)
```

- [ ] **Step 3: Build and run tests**

```bash
cd /home/bowmanhan/Code/boxr_research/powermonitor
cmake --build build_linux --target shm_spsc_test
./build_linux/bin/shm_spsc_test
```

Expected: All tests PASS

- [ ] **Step 4: Commit tests**

```bash
git add pc_client/tests/shm_spsc_test.cpp pc_client/CMakeLists.txt
git commit -m "test(power): add SPSC unit tests for ShmPowerRingBuffer"
```

---

## Task 4: Rewrite Python C Extension Reader

**Files:**
- Modify: `scheduler/power_shm_reader_ext.cpp`

- [ ] **Step 1: Replace implementation with SPSC reader**

Replace entire file with new SPSC-based implementation:

```cpp
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <spsc_ring_buffer.hpp>
#include "realtime_power_metrics.h"

namespace {

using namespace powermonitor::client;
using PowerSpscBuffer = buffers::spsc_ring_buffer<
    RealtimePowerSample,
    kPowerMetricsRingCapacity,
    buffers::ShmStorage
>;

struct ReaderObject {
    PyObject_HEAD
    PowerSpscBuffer* buffer;
};

void Reader_dealloc(ReaderObject* self) {
    delete self->buffer;
    self->buffer = nullptr;
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

int Reader_init(ReaderObject* self, PyObject* args, PyObject* kwargs) {
    const char* shm_name = POWER_METRICS_SHM_NAME;
    static const char* kwlist[] = {"shm_name", nullptr};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|s", const_cast<char**>(kwlist), &shm_name)) {
        return -1;
    }

    self->buffer = new PowerSpscBuffer(
        shm_name,
        kPowerMetricsVersion,
        buffers::ShmOpenMode::open
    );

    if (!self->buffer->valid()) {
        PyErr_Format(PyExc_RuntimeError, "Failed to open SPSC buffer: %s", shm_name);
        delete self->buffer;
        self->buffer = nullptr;
        return -1;
    }

    return 0;
}

PyObject* sample_to_dict(const RealtimePowerSample& sample) {
    PyObject* d = PyDict_New();
    auto set_u64 = [&](const char* key, uint64_t value) {
        PyObject* obj = PyLong_FromUnsignedLongLong(value);
        PyDict_SetItemString(d, key, obj);
        Py_DECREF(obj);
    };
    auto set_u32 = [&](const char* key, uint32_t value) {
        PyObject* obj = PyLong_FromUnsignedLong(value);
        PyDict_SetItemString(d, key, obj);
        Py_DECREF(obj);
    };
    auto set_f64 = [&](const char* key, double value) {
        PyObject* obj = PyFloat_FromDouble(value);
        PyDict_SetItemString(d, key, obj);
        Py_DECREF(obj);
    };

    set_u64("sequence_num", sample.sequence_num);
    set_u32("source", sample.source);
    set_u32("flags", sample.flags);
    set_u64("host_timestamp_us", sample.host_timestamp_us);
    set_u64("unix_timestamp_us", sample.unix_timestamp_us);
    set_u64("device_timestamp_us", sample.device_timestamp_us);
    set_u64("device_timestamp_unix_us", sample.device_timestamp_unix_us);
    set_f64("power_w", sample.power_w);
    set_f64("voltage_v", sample.voltage_v);
    set_f64("current_a", sample.current_a);
    set_f64("temp_c", sample.temp_c);
    set_f64("energy_j", sample.energy_j);
    set_u64("gpu_freq_hz", sample.gpu_freq_hz);
    set_u64("cpu_cluster0_freq_hz", sample.cpu_cluster0_freq_hz);
    set_u64("cpu_cluster1_freq_hz", sample.cpu_cluster1_freq_hz);
    set_u64("emc_freq_hz", sample.emc_freq_hz);
    set_f64("cpu_temp_c", sample.cpu_temp_c);
    set_f64("gpu_temp_c", sample.gpu_temp_c);
    return d;
}

PyObject* Reader_try_pop(ReaderObject* self, PyObject*) {
    RealtimePowerSample sample;
    if (self->buffer && self->buffer->try_pop(sample)) {
        return sample_to_dict(sample);
    }
    Py_RETURN_NONE;
}

PyObject* Reader_overflow_count(ReaderObject* self, PyObject*) {
    if (!self->buffer) {
        return PyLong_FromUnsignedLongLong(0);
    }
    return PyLong_FromUnsignedLongLong(self->buffer->overflow_count());
}

PyObject* Reader_size(ReaderObject* self, PyObject*) {
    if (!self->buffer) {
        return PyLong_FromSize_t(0);
    }
    return PyLong_FromSize_t(self->buffer->size());
}

PyObject* Reader_valid(ReaderObject* self, PyObject*) {
    return PyBool_FromLong(self->buffer && self->buffer->valid());
}

PyMethodDef Reader_methods[] = {
    {"try_pop", reinterpret_cast<PyCFunction>(Reader_try_pop), METH_NOARGS,
     "Pop one sample from the queue. Returns dict or None if empty."},
    {"overflow_count", reinterpret_cast<PyCFunction>(Reader_overflow_count), METH_NOARGS,
     "Get overflow count."},
    {"size", reinterpret_cast<PyCFunction>(Reader_size), METH_NOARGS,
     "Get current queue size."},
    {"valid", reinterpret_cast<PyCFunction>(Reader_valid), METH_NOARGS,
     "Check if buffer is valid."},
    {nullptr, nullptr, 0, nullptr}
};

PyTypeObject ReaderType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    .tp_name = "power_shm_reader_ext.SharedPowerReader",
    .tp_basicsize = sizeof(ReaderObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "SPSC ring buffer reader for power samples",
    .tp_methods = Reader_methods,
    .tp_init = reinterpret_cast<initproc>(Reader_init),
    .tp_dealloc = reinterpret_cast<destructor>(Reader_dealloc),
};

PyModuleDef powermodule = {
    PyModuleDef_HEAD_INIT,
    "power_shm_reader_ext",
    "Power SHM SPSC reader module",
    -1,
    nullptr, nullptr, nullptr, nullptr, nullptr
};

PyMODINIT_FUNC PyInit_power_shm_reader_ext(void) {
    PyObject* m;

    if (PyType_Ready(&ReaderType) < 0)
        return nullptr;

    m = PyModule_Create(&powermodule);
    if (!m)
        return nullptr;

    Py_INCREF(&ReaderType);
    if (PyModule_AddObject(m, "SharedPowerReader", reinterpret_cast<PyObject*>(&ReaderType)) < 0) {
        Py_DECREF(&ReaderType);
        Py_DECREF(m);
        return nullptr;
    }

    return m;
}

}  // namespace
```

- [ ] **Step 2: Update scheduler CMakeLists.txt to link RingBuffer**

Add RingBuffer dependency to `scheduler/CMakeLists.txt`:

```cmake
# Find the RingBuffer include directory from powermonitor build
target_include_directories(power_shm_reader_ext PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../powermonitor/build_linux/_deps/ringbuffer-src
)

# Link against RingBuffer (header-only, just need includes)
```

- [ ] **Step 3: Build Python extension**

```bash
cd /home/bowmanhan/Code/boxr_research/scheduler
python3 setup.py build_ext --inplace
```

Expected: Extension builds successfully

- [ ] **Step 4: Commit Python extension rewrite**

```bash
git add scheduler/power_shm_reader_ext.cpp scheduler/CMakeLists.txt
git commit -m "refactor(scheduler): rewrite Python C extension with SPSC consumer"
```

---

## Task 5: Update Mock SHM Server

**Files:**
- Modify: `scheduler/mock_shm_server.py`

- [ ] **Step 1: Update mock server to use new SPSC layout**

The mock server uses `write_power_shm_test` binary, which needs to be updated to use the new SPSC layout. Since we refactored `ShmPowerRingBuffer`, the existing `write_power_shm_test` will automatically use the new SPSC implementation.

However, verify that `scheduler/write_power_shm_test.cpp` still compiles:

```bash
cd /home/bowmanhan/Code/boxr_research/scheduler
cmake --build build --target write_power_shm_test
```

If it fails, update `write_power_shm_test.cpp` to use the new SPSC API (should be minimal change).

- [ ] **Step 2: Commit mock server update (if changes were needed)**

```bash
git add scheduler/write_power_shm_test.cpp
git commit -m "fix(scheduler): update write_power_shm_test for SPSC layout"
```

---

## Task 6: Integration Testing

**Files:**
- Test: End-to-end integration

- [ ] **Step 1: Start pc_client writer**

In terminal 1:
```bash
cd /home/bowmanhan/Code/boxr_research/powermonitor
./build_linux/bin/host_pc_client --port /dev/ttyUSB0 --onboard
```

Expected: Starts writing to SHM

- [ ] **Step 2: Run Python reader**

In terminal 2 (Python):
```python
import sys
sys.path.insert(0, '/home/bowmanhan/Code/boxr_research/scheduler')
import power_shm_reader_ext

reader = power_shm_reader_ext.SharedPowerReader()
print(f"Valid: {reader.valid()}")

for i in range(10):
    sample = reader.try_pop()
    if sample:
        print(f"Sample {i}: power={sample['power_w']:.2f}W")
    else:
        print(f"Buffer empty, waiting...")
    import time
    time.sleep(0.1)
```

Expected: Samples consumed successfully

- [ ] **Step 3: Test overflow scenario**

Modify Python reader to consume slowly and check overflow:

```python
import time
reader = power_shm_reader_ext.SharedPowerReader()
time.sleep(5)  # Let buffer fill up
print(f"Overflow count: {reader.overflow_count()}")
print(f"Current size: {reader.size()}")
```

Expected: `overflow_count() > 0` if writer is fast enough

---

## Task 7: Documentation Update

**Files:**
- Modify: `powermonitor/README.md`
- Modify: `scheduler/README.md`

- [ ] **Step 1: Update powermonitor README**

Add note about SPSC queue usage:

```markdown
### Power Ring Buffer

The `ShmPowerRingBuffer` uses a single-producer single-consumer (SPSC) queue 
from the RingBuffer library. The writer (pc_client) pushes samples with 
`push_overwrite()`, which overwrites oldest samples if the buffer is full.

Readers consume samples via `try_pop()`. This is a true queue - once consumed, 
samples are removed from the buffer.
```

- [ ] **Step 2: Update scheduler README**

Document new Python API:

```python
# Old API (removed)
# sample = reader.latest(source=SOURCE_PICO)  # Query only

# New API
sample = reader.try_pop()  # Consume one sample
if sample:
    process(sample)

# Monitor overflow
overflow = reader.overflow_count()
size = reader.size()
```

- [ ] **Step 3: Commit documentation**

```bash
git add powermonitor/README.md scheduler/README.md
git commit -m "docs: update README with SPSC queue documentation"
```

---

## Deployment Checklist

After all tasks complete:

- [ ] Stop all pc_client processes
- [ ] Stop all scheduler processes
- [ ] Unlink old SHM: `sudo rm /dev/shm/powermonitor_power_metrics`
- [ ] Deploy new pc_client binary
- [ ] Deploy new Python extension
- [ ] Start pc_client
- [ ] Start scheduler
- [ ] Monitor logs for errors
- [ ] Verify samples flowing correctly

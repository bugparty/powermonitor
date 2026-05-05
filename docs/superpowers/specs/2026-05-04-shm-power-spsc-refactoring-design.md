# ShmPowerRingBuffer SPSC Refactoring Design

**Date**: 2026-05-04
**Author**: Claude
**Status**: Draft

## Summary

Refactor `ShmPowerRingBuffer` to use the `spsc_ring_buffer` from the RingBufferCpp library, replacing the hand-rolled atomic ring buffer with a production-grade SPSC (Single-Producer Single-Consumer) queue implementation.

## Motivation

### Current Problems

1. **Hand-rolled atomic operations**: Current implementation manually manages `write_index` and `sequence_guard`, which is error-prone
2. **Query-only pattern**: Python reader queries latest sample without consuming, which doesn't scale for stream processing
3. **No true SPSC semantics**: Current design is a broadcast-style ring buffer, not a proper queue

### Goals

1. Use a battle-tested SPSC queue library instead of hand-rolled atomics
2. Enable true consumption pattern for Python reader
3. Maintain cross-process communication via shared memory
4. Ensure data integrity with proper memory ordering

## Design

### Architecture Change

**Before (Query Pattern)**:
```
pc_client (C++)                scheduler (Python)
     │                               │
ShmPowerRingBuffer            power_shm_reader_ext
     │                               │
  push()  ──────────────────>  read_latest_sample()
(write_index++)                   (no consumption)
```

**After (SPSC Queue Pattern)**:
```
pc_client (C++)                scheduler (Python)
     │                               │
spsc_ring_buffer               power_shm_reader_ext
     │                               │
push_overwrite()  ──────────>  try_pop()
   (SPSC)                      (true consumption)
```

### Component Changes

#### 1. C++ Writer (`pc_client/include/shm_power_ring_buffer.h`)

**Current Implementation**:
```cpp
class ShmPowerRingBuffer {
    void push(const RealtimePowerSample& input_sample) {
        const uint64_t write_index = region_->header.write_index.fetch_add(1, std::memory_order_acq_rel);
        RealtimePowerRingSlot& slot = region_->slots[write_index % region_->header.capacity];

        const uint64_t guard_begin = (write_index << 1U) | 1U;
        const uint64_t guard_end = (write_index << 1U);

        slot.sequence_guard.store(guard_begin, std::memory_order_release);
        slot.sample = input_sample;
        slot.sample.sequence_num = write_index;
        slot.sequence_guard.store(guard_end, std::memory_order_release);
    }
};
```

**Refactored Implementation**:
```cpp
#include <spsc_ring_buffer.hpp>

class ShmPowerRingBuffer {
    using PowerSpscBuffer = buffers::spsc_ring_buffer<
        RealtimePowerSample,
        kPowerMetricsRingCapacity,  // 4096
        buffers::ShmStorage
    >;

    PowerSpscBuffer buffer_;

public:
    ShmPowerRingBuffer()
        : buffer_(POWER_METRICS_SHM_NAME,
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
};
```

**Key Decisions**:
- Use `ShmStorage` policy for cross-process support
- Use `push_overwrite()` to guarantee real-time data (overwrites oldest if full)
- Schema version set to `kPowerMetricsVersion` for compatibility checking

#### 2. Python C Extension Reader (`scheduler/power_shm_reader_ext.cpp`)

**Current Implementation**:
```cpp
// Query mode: iterate backwards from write_index
bool read_latest_sample(const RealtimePowerMetricsRegion* region,
                        uint32_t source_filter,
                        RealtimePowerSample* out) {
    const uint64_t write_index = region->header.write_index.load(std::memory_order_acquire);
    // ... iterate and check sequence_guard ...
    // Does NOT advance tail, only reads
}
```

**Refactored Implementation**:
```cpp
#include <spsc_ring_buffer.hpp>

struct ReaderObject {
    PyObject_HEAD
    using PowerSpscBuffer = buffers::spsc_ring_buffer<
        RealtimePowerSample,
        kPowerMetricsRingCapacity,
        buffers::ShmStorage
    >;
    PowerSpscBuffer* buffer;
};

int Reader_init(ReaderObject* self, PyObject* args, PyObject* kwargs) {
    const char* shm_name = POWER_METRICS_SHM_NAME;
    // ... parse args ...

    // Open existing SHM (created by writer)
    self->buffer = new PowerSpscBuffer(
        shm_name,
        kPowerMetricsVersion,
        buffers::ShmOpenMode::open
    );

    if (!self->buffer->valid()) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to open SPSC buffer");
        return -1;
    }
    return 0;
}

// New API: consume one sample
PyObject* Reader_try_pop(ReaderObject* self, PyObject*) {
    RealtimePowerSample sample;
    if (self->buffer->try_pop(sample)) {
        return sample_to_dict(sample);
    }
    Py_RETURN_NONE;  // Buffer empty
}

// Statistics
PyObject* Reader_overflow_count(ReaderObject* self, PyObject*) {
    return PyLong_FromUnsignedLongLong(self->buffer->overflow_count());
}

PyObject* Reader_size(ReaderObject* self, PyObject*) {
    return PyLong_FromSize_t(self->buffer->size());
}
```

**Python API Change**:
```python
# Before (query mode)
reader = SharedPowerReader()
sample = reader.latest(source=SOURCE_PICO)  # Query only, no consumption

# After (consume mode)
reader = SharedPowerReader()
sample = reader.try_pop()  # Consume one sample
if sample:
    process(sample)
```

**Old API removed**: No `latest()` method, only `try_pop()` for true consumption.

#### 3. SHM Layout Change

**Old Layout** (`realtime_power_metrics.h`):
```cpp
struct RealtimePowerRingHeader {
    uint32_t version;
    uint32_t capacity;
    std::atomic<uint64_t> write_index;
    std::atomic<uint64_t> dropped_records;
    std::atomic<uint64_t> writer_pid;
};

struct RealtimePowerMetricsRegion {
    RealtimePowerRingHeader header;
    std::array<RealtimePowerRingSlot, 4096> slots;
};
```

**New Layout** (`spsc_ring_buffer.hpp`):
```cpp
struct ring_buffer_header {
    uint32_t schema_version;
    std::atomic<uint32_t> capacity;
    std::atomic<uint64_t> writer_pid;
    alignas(64) std::atomic<uint64_t> head;   // Producer writes
    alignas(64) std::atomic<uint64_t> tail;   // Consumer reads
    alignas(64) std::atomic<uint64_t> overflow_count;
};

template<typename T, size_t N>
struct ring_buffer_region {
    ring_buffer_header header;
    std::atomic<uint64_t> sequence[N];
    typename std::aligned_storage<sizeof(T), alignof(T)>::type slots[N];
};
```

**Incompatibility**: Layouts are fundamentally different. Old and new processes cannot share the same SHM segment.

**Migration Strategy**:
- Update `kPowerMetricsVersion` from 2 to 3
- Old readers will fail with version mismatch error (safe failure)
- Clean migration: stop old processes, unlink old SHM, start new processes

### Data Flow

1. **Writer (pc_client)**:
   ```cpp
   ShmPowerRingBuffer buffer;
   RealtimePowerSample sample = get_pico_sample();
   buffer.push(sample);  // push_overwrite internally
   ```

2. **Reader (Python scheduler)**:
   ```python
   reader = SharedPowerReader()
   while running:
       sample = reader.try_pop()
       if sample:
           process(sample)
       else:
           sleep(0.001)  # Buffer empty, wait
   ```

3. **Overflow Handling**:
   - If writer is faster than reader, `push_overwrite` overwrites oldest samples
   - Reader may skip samples (detected via sequence number in `try_pop`)
   - Monitor `overflow_count()` to detect sustained imbalance

### Error Handling

**Writer Side**:
- `valid() == false`: SHM creation failed (permissions, etc.)
- `push_overwrite()` is no-op on invalid buffer (safe)

**Reader Side**:
- Constructor throws exception if SHM open fails or version mismatch
- `try_pop()` returns `None` if buffer empty or invalid

### Threading Model

- **Single Writer**: One pc_client process (producer)
- **Single Reader**: One Python scheduler process (consumer)
- **SPSC Guarantee**: Exactly one producer, exactly one consumer
- **No Locks**: Lock-free implementation via atomic operations

### Memory Ordering

RingBuffer library uses correct acquire/release semantics:
- Producer: `head.store(new_head, std::memory_order_release)`
- Consumer: `head.load(std::memory_order_acquire)`

No manual memory ordering needed.

### Cache Line Alignment

RingBuffer library handles cache line padding:
```cpp
alignas(64) std::atomic<uint64_t> head;  // Producer cache line
alignas(64) std::atomic<uint64_t> tail;  // Consumer cache line
```

Prevents false sharing between producer and consumer.

## Migration Plan

### Phase 1: Update C++ Writer

1. Update `shm_power_ring_buffer.h` to use `spsc_ring_buffer`
2. Update `kPowerMetricsVersion` to 3
3. Update CMakeLists.txt (RingBuffer already linked)
4. Build and test locally

### Phase 2: Update Python Reader

1. Update `power_shm_reader_ext.cpp` to use `spsc_ring_buffer`
2. Remove `latest()` API, add `try_pop()` and `overflow_count()`
3. Update Python bindings
4. Update `scheduler.py` to use new API

### Phase 3: Update Tests

1. Update `mock_shm_server.py` to use new SHM layout
2. Update integration tests
3. Add overflow scenario tests

### Phase 4: Deployment

1. Stop all pc_client and scheduler processes
2. Unlink old SHM: `sudo rm /dev/shm/powermonitor_power_metrics`
3. Deploy new binaries
4. Start processes

## Testing Strategy

### Unit Tests

- **Writer**: Test `push_overwrite()` behavior when full
- **Reader**: Test `try_pop()` returns samples in order
- **Both**: Test overflow_count tracking

### Integration Tests

1. Start writer process
2. Start reader process
3. Verify samples flow correctly
4. Kill writer, verify reader handles gracefully
5. Restart writer, verify recovery

### Cross-Process Tests

Use RingBuffer library's `spsc_shm_tests.cpp` as reference:
- Fork process
- Writer in child, reader in parent
- Verify data integrity

### Performance Tests

- Measure throughput: samples/second
- Measure latency: push to pop delay
- Compare with old implementation

## Risks and Mitigations

### Risk 1: Layout Incompatibility

**Impact**: Old and new processes cannot coexist
**Mitigation**: Version check prevents silent corruption, clean migration required

### Risk 2: Python API Break

**Impact**: Existing scheduler code needs update
**Mitigation**: Simple migration: replace `latest()` with `try_pop()` loop

### Risk 3: Overflow Behavior Change

**Impact**: `push_overwrite` overwrites old data silently
**Mitigation**: Monitor `overflow_count()` to detect sustained producer-consumer imbalance

## Open Questions

None. All key decisions have been made:
- ✅ Use `spsc_ring_buffer` with `ShmStorage`
- ✅ Use `push_overwrite` for writer
- ✅ Use `try_pop` for reader
- ✅ Remove `latest()` query API
- ✅ Update schema version to 3

## References

- RingBuffer Library: `powermonitor/build_linux/_deps/ringbuffer-src/`
- SPSC Patterns: `docs/spsc-ring-buffer-patterns.md`
- Current Implementation: `pc_client/include/shm_power_ring_buffer.h`

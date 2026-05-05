# Code Quality Fix Plan

## Overview
Fix DRY violations and code organization issues in the powermonitor codebase protocol layer.

## Tasks

### Task 1: Extract Protocol Constants to Shared Header
**Problem:** Protocol constants (kSof0, kSof1, kProtoVersion, FrameType, MsgId, Status) are defined in two places:
- `device/protocol/frame_defs.hpp`
- `protocol/frame_builder.h`

**Solution:**
1. Create new header `protocol/protocol_constants.h` with shared protocol constants and enums
2. Update `device/protocol/frame_defs.hpp` to include and use the shared constants
3. Update `protocol/frame_builder.h` to include and use the shared constants
4. Ensure both device and PC code compile after changes
5. Run tests to verify no behavior change

**Files affected:**
- `protocol/protocol_constants.h` (NEW)
- `device/protocol/frame_defs.hpp` (MODIFY)
- `protocol/frame_builder.h` (MODIFY)

**Constraints:**
- Device-side must remain allocation-free
- PC-side must use std::vector
- No behavior changes, only code organization

### Task 2: Consolidate Serialization Functions
**Problem:** Serialization functions are duplicated across three locations:
- `protocol/serialization.h` - comprehensive pack/unpack functions
- `pc_client/include/protocol_helpers.h` - duplicates unpack_u16, unpack_u32, unpack_u64, pack_u16, pack_u32
- `pc_client/src/time_sync_demo.cpp` - implements pack_u64_le, read_u64_le

**Solution:**
1. Verify all needed functions exist in `protocol/serialization.h`
2. Remove duplicates from `pc_client/include/protocol_helpers.h`
3. Update `pc_client/include/protocol_helpers.h` to include and use `protocol/serialization.h`
4. Remove duplicates from `pc_client/src/time_sync_demo.cpp`
5. Update `time_sync_demo.cpp` to use functions from `protocol/serialization.h`
6. Build and test to verify no behavior change

**Files affected:**
- `pc_client/include/protocol_helpers.h` (MODIFY)
- `pc_client/src/time_sync_demo.cpp` (MODIFY)

**Constraints:**
- All existing callers must compile without changes
- Function signatures should be compatible or updated systematically
- No behavior changes

### Task 3: Document Duplicate Protocol Implementations
**Problem:** Codebase has two complete protocol implementations (device-side and PC-side) without clear documentation of why both exist.

**Solution:**
1. Add documentation comment in `device/protocol/` explaining device-side constraints (no dynamic allocation, fixed buffers)
2. Add documentation comment in `protocol/` explaining PC-side design (STL containers, flexibility)
3. Add note about CRC implementation differences (bit-by-bit vs table-based) and why
4. Add note about parser differences (fixed buffer vs deque) and why
5. Consider adding architecture decision record (ADR) if appropriate

**Files affected:**
- `device/protocol/crc16.hpp` (ADD COMMENT)
- `protocol/crc16_ccitt_false.h` (ADD COMMENT)
- `device/protocol/parser.hpp` (ADD COMMENT)
- `protocol/parser.h` (ADD COMMENT)

**Constraints:**
- Documentation only, no code changes
- Clear explanation of design rationale
- Help future maintainers understand the dual-implementation pattern

### Task 4: Namespace Frame Structs Clearly
**Problem:** Two different `Frame` structs with same name but different layouts:
- `device/protocol/frame_defs.hpp` - `Frame` with `uint8_t data[kMaxPayloadLen]`
- `protocol/frame_builder.h` - `Frame` with `std::vector<uint8_t> data`

**Solution:**
1. Rename device-side `Frame` to `FixedFrame` to clarify fixed-size buffer
2. Rename PC-side `Frame` to `DynamicFrame` to clarify dynamic allocation
3. Update all usages throughout the codebase
4. Build and test to verify no behavior change

**Files affected:**
- `device/protocol/frame_defs.hpp` (MODIFY)
- `device/protocol/parser.hpp` (MODIFY - uses Frame)
- `device/command_handler.hpp` (MODIFY - likely uses Frame)
- `protocol/frame_builder.h` (MODIFY)
- `protocol/parser.h` (MODIFY - uses Frame)
- `node/pc_node.cpp` (MODIFY - likely uses Frame)
- `pc_client/src/*.cpp` (MODIFY - likely use Frame)

**Constraints:**
- All references must be updated systematically
- Build must succeed after renaming
- Tests must pass
- No behavior changes

## Success Criteria
- All builds pass (device and PC)
- All tests pass
- No behavior changes
- Code is clearer and more maintainable
- DRY violations resolved

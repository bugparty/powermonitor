#pragma once

#include <cstdint>
#include "protocol/serialization.h"

namespace powermonitor {
namespace client {

// Re-export protocol serialization functions for backward compatibility.
// All implementations now come from protocol:: namespace.

using protocol::pack_u16;
using protocol::pack_u32;
using protocol::unpack_u16;
using protocol::unpack_u32;
using protocol::unpack_u64;
using protocol::unpack_s16;
using protocol::unpack_s32;

}  // namespace client
}  // namespace powermonitor

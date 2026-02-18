#ifndef DEVICE_SAMPLER_HPP
#define DEVICE_SAMPLER_HPP

#include "core/shared_context.hpp"

// Forward declaration
class INA228;

namespace device {

// Initialize sampler (call from Core 0 before launching Core 1)
void sampler_init(core::SharedContext* shared, INA228* ina228);

// Launch Core 1 (call from Core 0 after sampler_init)
void sampler_launch_core1();

// Send start command to Core 1 (call from Core 0)
void sampler_start();

// Send stop command to Core 1 (call from Core 0)
void sampler_stop();

} // namespace device

#endif // DEVICE_SAMPLER_HPP

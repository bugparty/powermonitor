## Onboard Power Sampler

This directory stores the Jetson Nano onboard power sampler used in the XR benchmark workflow.

- Source provenance: pulled back from the Nano host on 2026-03-16.
- Primary source file: `onboard_power_sampler.cpp`
- Output format: CSV with `mono_ns`, `unix_ns`, per-rail power in mW, and total power in mW.

The sampler reads Nano hwmon rails from `/sys/class/hwmon/...` and is separate from the Pico-based INA228 power monitor in this repo.

Typical build command on Nano:

```bash
g++ -O2 -std=c++17 -o onboard_power_sampler onboard_power_sampler.cpp
```

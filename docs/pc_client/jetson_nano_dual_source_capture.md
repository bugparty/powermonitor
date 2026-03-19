# Jetson Nano Integrated Dual-Source Capture

This document defines the target Jetson Nano workflow after onboard rail capture is integrated directly into `host_pc_client`.

The intended result is:

- one executable: `host_pc_client`
- one primary output JSON containing both `pico` and `onboard_cpp`
- separate logs may still exist for Pico/client and onboard capture internals
- onboard capture is enabled by default on Jetson Nano and can be explicitly disabled

Related docs:

- `docs/pc_client/overview.md`
- `docs/pc_client/power_bundle_schema.md`
- `onboard_power_sampler/README.md`

## Design Goals

- Keep the Pico capture path as the primary transport and control path.
- Start Nano onboard rail capture from inside `host_pc_client` rather than from a separate user-visible workflow.
- Write a single multi-source JSON output directly from `host_pc_client`.
- Preserve source-specific logs for debugging.
- Make onboard capture default-on for Jetson Nano usage while allowing opt-out.

## Execution Model

`host_pc_client` becomes the orchestrator for both capture sources.

At startup:

1. initialize the Pico / INA228 device as usual
2. initialize onboard capture if enabled
3. start Pico streaming
4. start onboard sampling
5. collect both sources until stop / timeout
6. write one JSON file containing both source payloads

At shutdown:

1. stop Pico streaming
2. stop onboard sampling
3. finalize summaries for both sources
4. write one merged output JSON
5. flush logs

## Output Artifacts

For a capture named `power_boxr_mh02_low_clk1_gpu_low_510000000`, the intended outputs are:

```text
power_boxr_mh02_low_clk1_gpu_low_510000000.json
power_boxr_mh02_low_clk1_gpu_low_510000000.log
power_boxr_mh02_low_clk1_gpu_low_510000000_onboard_cpp.log
```

Meaning:

- `.json`: single primary output with both `pico` and `onboard_cpp`
- `.log`: main client log
- `_onboard_cpp.log`: optional onboard-specific debug log

The old split raw artifacts:

- `power_<capture>.csv`
- `power_<capture>.bundle.json`

should be treated as legacy or transitional outputs, not the target steady-state workflow.

## JSON Model

The integrated output JSON should use the multi-source schema from `docs/pc_client/power_bundle_schema.md`.

That means `host_pc_client` writes:

- top-level capture metadata
- `sources.pico`
- `sources.onboard_cpp` when onboard capture is enabled

The JSON file is therefore already the merged artifact. No post-run conversion step is required in the normal Nano workflow.

## CLI Design

### Default Behavior On Jetson Nano

Recommended default behavior:

- Pico capture: enabled
- onboard capture: enabled

So a plain command such as:

```bash
~/powermonitor/build_linux/bin/host_pc_client --duration-s 30 --output power_demo.json
```

should produce a JSON file with both sources when onboard sampling is available.

### Proposed Switches

Recommended CLI switches:

| Switch | Meaning | Default on Nano |
|--------|---------|-----------------|
| `--onboard` | Enable onboard Nano rail capture | On |
| `--no-onboard` | Disable onboard Nano rail capture | Off |
| `--onboard-period-us <N>` | Onboard sampling period | `1000` |
| `--onboard-log <PATH>` | Path for onboard-specific log | Auto |
| `--onboard-strict` | Fail the run if onboard capture cannot start | Off |

Recommended behavior:

- `--no-onboard` removes `sources.onboard_cpp` from output
- if onboard capture is enabled but unavailable:
  - default mode: continue with Pico-only capture and emit a warning in the main log / JSON metadata
  - strict mode: fail startup

## Internal Integration Options

Two implementation strategies are acceptable:

1. link onboard sampler logic directly into `host_pc_client`
2. spawn and supervise the existing onboard sampler implementation internally

From the user point of view, both still count as a single-executable workflow as long as:

- the user runs only `host_pc_client`
- the primary output is one JSON
- the CLI exposes onboard control directly

## Web Viewer Expectations

The web viewer should continue to treat the output as a multi-source file:

- `pico` rendered in the upper panel
- `onboard_cpp` rendered in the lower panel
- synchronized visible time range

No special Nano-only viewer path should be needed beyond reading the unified JSON.

## Migration Notes

- Existing Pico-only JSON remains valid.
- Existing bundle JSON remains useful for older benchmark archives.
- New Jetson Nano runs should prefer direct integrated JSON output from `host_pc_client`.
- Documentation and scripts should gradually shift from “run two programs then merge” to “run one program with onboard enabled by default”.

# Multi-Source Power Bundle Schema

This document defines a bundle format for combining multiple power capture sources for the same run.

The goal is to keep existing Pico `powermonitor` JSON files unchanged while providing a normalized output that can also carry Jetson onboard power samples.

## Scope

- One bundle file represents one capture label, for example `power_boxr_mh02_low_clk1_gpu_low_510000000`.
- The bundle may contain one or more sources.
- Each source keeps its own metadata, summary statistics, and raw samples.

## Top-Level Structure

```json
{
  "schema_version": "power-bundle/v1",
  "capture_name": "power_boxr_mh02_low_clk1_gpu_low_510000000",
  "created_at": "2026-03-17T00:00:00Z",
  "sources": {
    "pico": {
      "format": "powermonitor_json/v1",
      "meta": {},
      "summary": {},
      "artifacts": {},
      "samples": []
    },
    "onboard_cpp": {
      "format": "onboard_csv/v1",
      "meta": {},
      "summary": {},
      "artifacts": {},
      "samples": []
    }
  }
}
```

## Source Model

Each source entry under `sources` must use the same shape:

```json
{
  "format": "powermonitor_json/v1",
  "meta": {},
  "summary": {
    "sample_count": 37053,
    "mean_w": 8.33,
    "p50_w": 8.21,
    "p95_w": 9.82,
    "energy_j": 975.1
  },
  "artifacts": {
    "primary_path": "power_boxr_mh02_low_clk1_gpu_low_510000000.json",
    "log_path": "power_boxr_mh02_low_clk1_gpu_low_510000000.log"
  },
  "samples": []
}
```

### Required fields

- `format`: source-specific payload format identifier
- `meta`: original source metadata or source-level normalized metadata
- `summary.sample_count`
- `summary.mean_w`
- `summary.p50_w`
- `summary.p95_w`
- `summary.energy_j`
- `samples`

### Source naming

Recommended source ids:

- `pico`: USB INA228 `powermonitor` capture
- `onboard_cpp`: Jetson onboard hwmon sampler capture

Do not use positional names such as `samples2` or `source_b`. Source ids must remain stable across tools and documents.

## Pico Source

`pico` is derived from the existing `powermonitor` JSON output.

```json
{
  "format": "powermonitor_json/v1",
  "meta": {
    "config": {},
    "device": {},
    "protocol_version": 1,
    "run": {},
    "schema_version": "1.0",
    "session": {},
    "stats": {}
  },
  "summary": {
    "sample_count": 37053,
    "mean_w": 8.33,
    "p50_w": 8.21,
    "p95_w": 9.82,
    "energy_j": 975.1
  },
  "artifacts": {
    "primary_path": "power_boxr_mh02_low_clk1_gpu_low_510000000.json",
    "log_path": "power_boxr_mh02_low_clk1_gpu_low_510000000.log"
  },
  "samples": [
    {
      "device_timestamp_unix_us": 1773365657253274,
      "device_timestamp_us": 81801120,
      "engineering": {
        "current_a": 0.414459432,
        "energy_j": 164524.31576924163,
        "power_w": 7.880136768000001,
        "temp_c": 28.15625,
        "vbus_v": 19.0130859375,
        "vshunt_v": 0.006219062500000001
      },
      "host_timestamp_us": 33784294390,
      "raw": {},
      "seq": 204,
      "timestamp_us": 33784294390
    }
  ]
}
```

Notes:

- Keep Pico samples unchanged when bundling.
- `summary.energy_j` is the run-level energy estimate derived from the time series.

## Onboard Source

`onboard_cpp` is derived from the Nano onboard sampler CSV.

```json
{
  "format": "onboard_csv/v1",
  "meta": {
    "source": "onboard_cpp",
    "columns": [
      "mono_ns",
      "unix_ns",
      "vdd_in_mw",
      "vdd_cpu_gpu_cv_mw",
      "vdd_soc_mw",
      "total_mw"
    ]
  },
  "summary": {
    "sample_count": 116800,
    "mean_w": 9.00,
    "p50_w": 8.79,
    "p95_w": 9.78,
    "energy_j": 1095.3
  },
  "artifacts": {
    "primary_path": "power_boxr_mh02_low_clk1_gpu_low_510000000.csv",
    "log_path": "power_boxr_mh02_low_clk1_gpu_low_510000000_onboard_cpp.log"
  },
  "samples": [
    {
      "mono_ns": 33784233110347,
      "unix_ns": 1773387132090858769,
      "rails": {
        "vdd_in_w": 6.448,
        "vdd_cpu_gpu_cv_w": 1.629,
        "vdd_soc_w": 1.472
      },
      "power_w": 9.549
    }
  ]
}
```

Notes:

- Keep rail names explicit.
- Use watts in normalized sample fields.
- Preserve original CSV semantics through `meta.columns`.

## Compatibility Rules

- Existing Pico JSON files remain valid raw artifacts and do not need schema changes.
- Bundles are additive companion files, recommended name: `power_<capture>.bundle.json`.
- Tools that only understand legacy Pico JSON may continue reading `power_<capture>.json`.
- New tools should prefer the bundle when present.

## Conversion Rules

When converting from current benchmark artifacts:

1. Read Pico samples from `power_<capture>.json`.
2. Read onboard samples from `power_<capture>.csv`.
3. Read onboard process logs from `power_<capture>_onboard_cpp.log` when present.
4. Write `power_<capture>.bundle.json` in the same directory.

Reference converter:

- `scripts/build_power_bundles.py`

If only one source exists, still emit the bundle with a single entry under `sources`.

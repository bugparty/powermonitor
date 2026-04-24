# Multi-Source Power JSON Schema

This document defines the multi-source JSON format for power capture runs that combine Pico / INA228 data with Jetson Nano onboard rail data.

The primary target is the integrated Jetson Nano workflow where `host_pc_client` writes one JSON file directly. Legacy bundle conversion remains supported for older archives.

## Scope

- One JSON file represents one capture label, for example `power_boxr_mh02_low_clk1_gpu_low_510000000`.
- The file may contain one or more sources.
- Each source keeps its own metadata, summary statistics, and raw samples.
- Source-specific sidecar logs may still exist.

## Top-Level Structure

```json
{
  "schema_version": "power-bundle/v1",
  "capture_name": "power_boxr_mh02_low_clk1_gpu_low_510000000",
  "created_at": "2026-03-17T00:00:00Z",
  "producer": {
    "name": "host_pc_client",
    "mode": "integrated_dual_source"
  },
  "sources": {
    "pico": {
      "format": "powermonitor_json/v1",
      "enabled": true,
      "meta": {},
      "summary": {},
      "artifacts": {},
      "samples": []
    },
    "onboard_cpp": {
      "format": "onboard_csv/v1",
      "enabled": true,
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
  "enabled": true,
  "meta": {},
  "summary": {
    "sample_count": 37053,
    "mean_w": 8.33,
    "p50_w": 8.21,
    "p95_w": 9.82,
    "energy_j": 975.1
  },
  "artifacts": {
    "log_path": "power_boxr_mh02_low_clk1_gpu_low_510000000.log"
  },
  "samples": []
}
```

### Required fields

- `format`: source-specific payload format identifier
- `enabled`: whether the source was enabled for this run
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

### Artifact fields

Recommended `artifacts` fields:

- `log_path`: sidecar log for this source
- `raw_path`: optional legacy raw sidecar path when the source was also persisted separately

In the integrated Nano workflow, the primary persisted data is the enclosing multi-source JSON file itself, not a per-source primary file.

## Pico Source

`pico` is derived from the existing `powermonitor` JSON output.

```json
{
  "format": "powermonitor_json/v1",
  "enabled": true,
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

- Keep Pico samples unchanged when emitting the multi-source JSON.
- `summary.energy_j` is the run-level energy estimate derived from the time series.

## Onboard Source

`onboard_cpp` contains Jetson Orin Nano onboard telemetry data collected by `OnboardSampler`.

```json
{
  "format": "onboard_csv/v1",
  "enabled": true,
  "meta": {
    "source": "onboard_cpp",
    "hwmon_path": "/sys/class/hwmon/hwmon1",
    "columns": "mono_ns,unix_ns,vdd_in_mw,vdd_cpu_gpu_cv_mw,vdd_soc_mw,total_mw,gpu_freq_hz,cpu_cluster0_freq_hz,cpu_cluster1_freq_hz,emc_freq_hz,temp_cpu_mc,temp_gpu_mc,temp_soc0_mc,temp_soc1_mc,temp_soc2_mc,temp_tj_mc,fan_rpm"
  },
  "summary": {
    "sample_count": 116800,
    "mean_w": 9.00,
    "p50_w": 8.79,
    "p95_w": 9.78,
    "energy_j": 1095.3
  },
  "artifacts": {
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
      "power_w": 9.549,
      "freqs": {
        "gpu_hz": 306000000,
        "cpu_cluster0_hz": 1190400,
        "cpu_cluster1_hz": 729600,
        "emc_hz": 204000000
      },
      "temps": {
        "cpu_mc": 55718,
        "gpu_mc": 56062,
        "soc0_mc": 54750,
        "soc1_mc": 55062,
        "soc2_mc": 54187,
        "tj_mc": 56062
      },
      "fan_rpm": 2163
    }
  ]
}
```

### Artifact Fields

| Field | Description |
|-------|-------------|
| `raw_path` | Legacy: path to original CSV file (conversion workflow only) |
| `log_path` | Path to source-specific log file |

In the integrated Nano workflow, `raw_path` is omitted since data is written directly to JSON.

### Sample Fields

| Field | Unit | Description |
|-------|------|-------------|
| `mono_ns` | ns | Monotonic clock timestamp |
| `unix_ns` | ns | Unix epoch timestamp |
| `rails.vdd_in_w` | W | Total input power |
| `rails.vdd_cpu_gpu_cv_w` | W | CPU+GPU+CV rail power |
| `rails.vdd_soc_w` | W | SoC rail power |
| `power_w` | W | Total power (sum of rails) |
| `freqs.gpu_hz` | Hz | GPU core frequency |
| `freqs.cpu_cluster0_hz` | Hz | CPU cluster 0 (little cores) frequency |
| `freqs.cpu_cluster1_hz` | Hz | CPU cluster 1 (big cores) frequency |
| `freqs.emc_hz` | Hz | External Memory Controller frequency |
| `temps.cpu_mc` | m°C | CPU temperature |
| `temps.gpu_mc` | m°C | GPU temperature |
| `temps.soc0_mc` | m°C | SoC zone 0 temperature |
| `temps.soc1_mc` | m°C | SoC zone 1 temperature |
| `temps.soc2_mc` | m°C | SoC zone 2 temperature |
| `temps.tj_mc` | m°C | Junction temperature |
| `fan_rpm` | RPM | Fan speed |

Fields set to `-1` indicate unavailable data (e.g., missing sysfs path or permission denied).

See `docs/pc_client/onboard_sampler_design.md` for architecture details.

## Compatibility Rules

- Existing Pico JSON files remain valid raw artifacts and do not need schema changes.
- In the integrated Jetson Nano workflow, this schema is the primary output format written directly to `power_<capture>.json`.
- In legacy workflows, the same schema may be written as an additive companion file named `power_<capture>.bundle.json`.
- Tools that only understand legacy Pico JSON may continue reading `power_<capture>.json`.
- New tools should prefer the multi-source form when present, whether it is stored as `.bundle.json` or directly as the primary `.json`.

## Conversion Rules

When converting from legacy benchmark artifacts:

1. Read Pico samples from `power_<capture>.json`.
2. Read onboard samples from `power_<capture>.csv`.
3. Read onboard process logs from `power_<capture>_onboard_cpp.log` when present.
4. Write `power_<capture>.bundle.json` in the same directory.

Reference converter:

- `scripts/build_power_bundles.py`

If only one source exists, still emit the bundle with a single entry under `sources`.

## Integrated Jetson Nano Output

For the target Jetson Nano design:

- `host_pc_client` writes this schema directly
- `sources.pico` is always present when Pico capture is enabled
- `sources.onboard_cpp` is present by default on Nano unless disabled with CLI
- source-specific logs may still be written as sidecar files
- recommended default CLI behavior is onboard enabled unless `--no-onboard` is passed
- if onboard capture is unavailable and strict mode is off, the run may continue with `sources.onboard_cpp` omitted and a warning recorded in metadata

This removes the normal need for a post-run merge step.

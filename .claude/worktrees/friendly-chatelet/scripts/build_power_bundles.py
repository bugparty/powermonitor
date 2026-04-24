#!/usr/bin/env python3
"""
Build multi-source power bundle JSON files from benchmark artifacts.

This script scans a directory for:
- power_<capture>.json          Pico powermonitor capture
- power_<capture>.csv           onboard Nano capture
- power_<capture>.log           Pico process log
- power_<capture>_onboard_cpp.log

For each capture prefix it writes:
- power_<capture>.bundle.json
"""

from __future__ import annotations

import argparse
import csv
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def percentile(sorted_values: list[float], q: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    pos = (len(sorted_values) - 1) * q
    lower = int(pos)
    upper = min(lower + 1, len(sorted_values) - 1)
    weight = pos - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight


def summarize_power_series(powers_w: list[float], timestamps_s: list[float] | None = None) -> dict[str, Any]:
    if not powers_w:
        return {
            "sample_count": 0,
            "mean_w": 0.0,
            "p50_w": 0.0,
            "p95_w": 0.0,
            "energy_j": 0.0,
        }

    ordered = sorted(powers_w)
    summary = {
        "sample_count": len(powers_w),
        "mean_w": sum(powers_w) / len(powers_w),
        "p50_w": percentile(ordered, 0.50),
        "p95_w": percentile(ordered, 0.95),
        "energy_j": 0.0,
    }
    if timestamps_s and len(timestamps_s) == len(powers_w) and len(timestamps_s) >= 2:
        energy_j = 0.0
        for i in range(1, len(powers_w)):
            dt_s = timestamps_s[i] - timestamps_s[i - 1]
            if dt_s < 0:
                continue
            energy_j += 0.5 * (powers_w[i - 1] + powers_w[i]) * dt_s
        summary["energy_j"] = energy_j
    return summary


def build_pico_source(json_path: Path) -> dict[str, Any]:
    with json_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    samples = data.get("samples", [])
    powers_w = []
    for sample in samples:
        engineering = sample.get("engineering", {})
        power_w = engineering.get("power_w")
        if isinstance(power_w, (int, float)):
            powers_w.append(float(power_w))

    summary = summarize_power_series(powers_w)
    if samples:
        first_energy = samples[0].get("engineering", {}).get("energy_j")
        last_energy = samples[-1].get("engineering", {}).get("energy_j")
        if isinstance(first_energy, (int, float)) and isinstance(last_energy, (int, float)) and last_energy >= first_energy:
            summary["energy_j"] = float(last_energy - first_energy)

    log_path = json_path.with_suffix(".log")
    artifacts = {"primary_path": json_path.name}
    if log_path.exists():
        artifacts["log_path"] = log_path.name

    return {
        "format": "powermonitor_json/v1",
        "meta": data.get("meta", {}),
        "summary": summary,
        "artifacts": artifacts,
        "samples": samples,
    }


def build_onboard_source(csv_path: Path) -> dict[str, Any]:
    samples = []
    powers_w = []
    timestamps_s = []
    columns = []

    with csv_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames:
            columns = list(reader.fieldnames)
        for row in reader:
            mono_ns = int(row["mono_ns"])
            unix_ns = int(row["unix_ns"])
            vdd_in_w = int(row["vdd_in_mw"]) / 1000.0
            vdd_cpu_gpu_cv_w = int(row["vdd_cpu_gpu_cv_mw"]) / 1000.0
            vdd_soc_w = int(row["vdd_soc_mw"]) / 1000.0
            total_w = int(row["total_mw"]) / 1000.0
            samples.append(
                {
                    "mono_ns": mono_ns,
                    "unix_ns": unix_ns,
                    "rails": {
                        "vdd_in_w": vdd_in_w,
                        "vdd_cpu_gpu_cv_w": vdd_cpu_gpu_cv_w,
                        "vdd_soc_w": vdd_soc_w,
                    },
                    "power_w": total_w,
                }
            )
            powers_w.append(total_w)
            timestamps_s.append(mono_ns / 1_000_000_000.0)

    summary = summarize_power_series(powers_w, timestamps_s)
    log_path = csv_path.with_name(f"{csv_path.stem}_onboard_cpp.log")
    artifacts = {"primary_path": csv_path.name}
    if log_path.exists():
        artifacts["log_path"] = log_path.name

    return {
        "format": "onboard_csv/v1",
        "meta": {
            "source": "onboard_cpp",
            "columns": columns,
        },
        "summary": summary,
        "artifacts": artifacts,
        "samples": samples,
    }


def bundle_for_capture(capture_base: str, directory: Path) -> dict[str, Any]:
    json_path = directory / f"{capture_base}.json"
    csv_path = directory / f"{capture_base}.csv"

    sources: dict[str, Any] = {}
    if json_path.exists():
        sources["pico"] = build_pico_source(json_path)
    if csv_path.exists():
        sources["onboard_cpp"] = build_onboard_source(csv_path)

    if not sources:
        raise FileNotFoundError(f"no sources found for {capture_base}")

    return {
        "schema_version": "power-bundle/v1",
        "capture_name": capture_base,
        "created_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "sources": sources,
    }


def discover_capture_bases(directory: Path) -> list[str]:
    bases = set()
    for path in directory.glob("power_*.json"):
        if path.name.endswith(".bundle.json"):
            continue
        bases.add(path.stem)
    for path in directory.glob("power_*.csv"):
        bases.add(path.stem)
    return sorted(bases)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build multi-source power bundle JSON files")
    parser.add_argument("directory", help="Directory containing power_*.json and power_*.csv files")
    args = parser.parse_args()

    directory = Path(args.directory).resolve()
    if not directory.is_dir():
        raise SystemExit(f"not a directory: {directory}")

    capture_bases = discover_capture_bases(directory)
    if not capture_bases:
        raise SystemExit(f"no power capture files found in {directory}")

    written = 0
    for capture_base in capture_bases:
        bundle = bundle_for_capture(capture_base, directory)
        out_path = directory / f"{capture_base}.bundle.json"
        with out_path.open("w", encoding="utf-8") as handle:
            json.dump(bundle, handle, indent=2)
            handle.write("\n")
        written += 1

    print(f"Wrote {written} bundle files in {directory}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

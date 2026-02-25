#!/usr/bin/env python3
"""
PowerMonitor Data Analysis Script

Usage:
    python3 analyze_data.py <json_file>
    python3 analyze_data.py <json_file> --check-seq      # Check sequence gaps
    python3 analyze_data.py <json_file> --check-time    # Check timestamp intervals
    python3 analyze_data.py <json_file> --check-all     # Full analysis
"""

import json
import sys
import argparse
from pathlib import Path


def check_sequence_gaps(samples):
    """Check for missing packets based on sequence number"""
    seq_gaps = []
    for i in range(1, len(samples)):
        curr_seq = samples[i]['seq']
        prev_seq = samples[i-1]['seq']
        diff = (curr_seq - prev_seq) & 0xFF
        if diff != 1:
            seq_gaps.append((i, prev_seq, curr_seq, diff))

    print(f"\n=== Sequence Gap Analysis ===")
    print(f"Total samples: {len(samples)}")
    print(f"Sequence gaps: {len(seq_gaps)}")

    if seq_gaps:
        print(f"Loss rate: {len(seq_gaps)/len(samples)*100:.2f}%")
        print("\nGap details (first 10):")
        for idx, prev, curr, diff in seq_gaps[:10]:
            rel_time = samples[idx]['device_timestamp_us']
            print(f"  Sample {idx}: seq {prev} -> {curr} (+{diff}), time={rel_time/1e6:.2f}s")
    else:
        print("No sequence gaps!")

    return seq_gaps


def check_timestamp_intervals(samples):
    """Check timestamp intervals"""
    unix_intervals = []
    for i in range(1, len(samples)):
        delta = samples[i]['device_timestamp_unix_us'] - samples[i-1]['device_timestamp_unix_us']
        unix_intervals.append(delta)

    print(f"\n=== Timestamp Interval Analysis ===")
    print(f"Total intervals: {len(unix_intervals)}")
    print(f"Average: {sum(unix_intervals)/len(unix_intervals):.1f} us")
    print(f"Max: {max(unix_intervals)} us")
    print(f"Min: {min(unix_intervals)} us")
    print(f"Expected: 1000 us (1kHz)")

    # Check for regressions (negative intervals)
    regressions = [(i+1, unix_intervals[i]) for i in range(len(unix_intervals)) if unix_intervals[i] < 0]
    if regressions:
        print(f"\nTimestamp regressions: {len(regressions)}")
        for idx, val in regressions[:5]:
            print(f"  Sample {idx}: {val} us")

    # Check for outliers
    outliers = [x for x in unix_intervals if x > 1100 or x < 900]
    if outliers:
        print(f"\nOutliers (>1100 or <900 us): {len(outliers)} ({len(outliers)/len(unix_intervals)*100:.2f}%)")

    return unix_intervals


def check_absolute_timestamps(samples):
    """Check absolute timestamps"""
    import datetime

    first = samples[0]
    last = samples[-1]

    first_unix = first['device_timestamp_unix_us']
    last_unix = last['device_timestamp_unix_us']
    first_ts = first_unix / 1e6
    last_ts = last_unix / 1e6

    print(f"\n=== Absolute Timestamp Analysis ===")
    print(f"First: {first_unix:,} ({datetime.datetime.utcfromtimestamp(first_ts)})")
    print(f"Last:  {last_unix:,} ({datetime.datetime.utcfromtimestamp(last_ts)})")
    print(f"Duration: {(last['device_timestamp_us'] - first['device_timestamp_us'])/1e6:.2f} seconds")

    # Check for reasonable Unix time
    import time
    now_unix = int(time.time() * 1_000_000)
    diff_sec = (now_unix - first_unix) / 1e6
    print(f"Time diff from now: {diff_sec:.2f} seconds")

    if abs(diff_sec) < 3600:  # Within 1 hour
        print("✓ Unix timestamp looks correct!")
    else:
        print(f"⚠ Unix timestamp off by {abs(diff_sec)/3600:.1f} hours")


def check_host_timestamps(samples):
    """Check host timestamps"""
    if 'host_timestamp_us' not in samples[0]:
        print("\n=== Host Timestamps ===")
        print("No host_timestamp_us in data")
        return

    # Check PC receive time vs device time
    print(f"\n=== Host vs Device Timestamp ===")
    diffs = []
    for i in range(min(100, len(samples))):
        host = samples[i]['host_timestamp_us']
        device = samples[i]['device_timestamp_unix_us']
        diffs.append(host - device)

    avg_diff = sum(diffs) / len(diffs)
    print(f"Avg (host - device): {avg_diff/1000:.2f} ms")


def main():
    parser = argparse.ArgumentParser(description='Analyze PowerMonitor data')
    parser.add_argument('file', help='JSON file to analyze')
    parser.add_argument('--check-seq', action='store_true', help='Check sequence gaps')
    parser.add_argument('--check-time', action='store_true', help='Check timestamps')
    parser.add_argument('--check-all', action='store_true', help='Full analysis')
    args = parser.parse_args()

    filepath = Path(args.file)
    if not filepath.exists():
        print(f"File not found: {args.file}")
        sys.exit(1)

    with open(filepath) as f:
        data = json.load(f)

    samples = data.get('samples', [])
    if not samples:
        print("No samples in data file")
        sys.exit(1)

    print(f"Loaded {len(samples)} samples from {filepath.name}")

    # Run requested checks
    if args.check_all:
        check_sequence_gaps(samples)
        check_timestamp_intervals(samples)
        check_absolute_timestamps(samples)
        check_host_timestamps(samples)
    else:
        if args.check_seq:
            check_sequence_gaps(samples)
        if args.check_time:
            check_timestamp_intervals(samples)
            check_absolute_timestamps(samples)
        if not args.check_seq and not args.check_time:
            # Default: full analysis
            check_sequence_gaps(samples)
            check_timestamp_intervals(samples)
            check_absolute_timestamps(samples)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Analyze idle vs wake HID captures and report candidate wake signatures.

Input CSV format must match capture_hid_reports.py:
  timestamp_ms,len,report_hex
"""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Report:
    timestamp_ms: int
    length: int
    data: bytes


def load_reports(path: Path) -> list[Report]:
    reports: list[Report] = []
    with path.open("r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            ts = int(row["timestamp_ms"])
            ln = int(row["len"])
            hx = row["report_hex"].strip()
            if not hx:
                continue
            data = bytes.fromhex(hx)
            reports.append(Report(ts, ln, data))
    return reports


def top_reports(reports: list[Report], limit: int = 10) -> list[tuple[bytes, int]]:
    ctr = Counter(r.data for r in reports)
    return ctr.most_common(limit)


def byte_activity(reports: list[Report], base: bytes) -> Counter:
    activity = Counter()
    for r in reports:
        n = min(len(base), len(r.data))
        for i in range(n):
            if r.data[i] != base[i]:
                activity[i] += 1
    return activity


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare idle vs wake HID captures")
    parser.add_argument("--idle", type=Path, required=True, help="Idle capture CSV")
    parser.add_argument("--wake", type=Path, required=True, help="Wake capture CSV")
    parser.add_argument("--top", type=int, default=10, help="Top unique reports to print")
    args = parser.parse_args()

    idle = load_reports(args.idle)
    wake = load_reports(args.wake)

    if not wake:
        print("Wake capture has no reports.")
        return 2

    if not idle:
        print("Idle capture has no reports. Proceeding with wake-only analysis.\n")

    idle_set = {r.data for r in idle}
    wake_set = {r.data for r in wake}

    wake_only = wake_set - idle_set
    shared = wake_set & idle_set

    print(f"Idle reports: {len(idle)} samples, {len(idle_set)} unique")
    print(f"Wake reports: {len(wake)} samples, {len(wake_set)} unique")
    print(f"Shared unique reports: {len(shared)}")
    print(f"Wake-only unique reports: {len(wake_only)}")
    print()

    if idle:
        print("Top idle reports:")
        for data, n in top_reports(idle, args.top):
            print(f"  {n:5d}  {data.hex()}")
    else:
        print("Top idle reports:")
        print("  (none)")

    print("\nTop wake reports:")
    for data, n in top_reports(wake, args.top):
        print(f"  {n:5d}  {data.hex()}")

    if wake_only:
        print("\nCandidate wake signatures (present in wake, absent in idle):")
        for hx in sorted(d.hex() for d in wake_only)[: args.top]:
            print(f"  {hx}")
    else:
        print("\nNo wake-only report found. Check byte-level activity below.")

    if idle:
        base_idle = top_reports(idle, 1)[0][0]
        wake_activity = byte_activity(wake, base_idle)

        print("\nMost active byte positions vs top idle report:")
        for idx, cnt in wake_activity.most_common(args.top):
            print(f"  byte[{idx:02d}] changed in {cnt} wake samples")
    else:
        print("\nMost active byte positions vs top idle report:")
        print("  (skipped: no idle baseline available)")

    print("\nNext step:")
    print("  Pick one stable wake signature or byte-mask rule and port it into ESP32 wake detection logic.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

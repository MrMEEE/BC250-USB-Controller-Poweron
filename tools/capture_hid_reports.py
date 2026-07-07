#!/usr/bin/env python3
"""Capture HID input reports to CSV for wake-pattern analysis.

Requires a HID backend, for example:
  pip install hidapi

Examples:
  python tools/capture_hid_reports.py --list
  python tools/capture_hid_reports.py --vid 0x28de --pid 0x1102 --output tools/captures/idle.csv --duration 30
  python tools/capture_hid_reports.py --vid 0x28de --pid 0x1102 --output tools/captures/wake.csv --duration 30
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from pathlib import Path


try:
    import hid  # type: ignore
except Exception as exc:  # pragma: no cover
    print("Error: failed to import 'hid'. Install with: pip install hidapi", file=sys.stderr)
    print(f"Detail: {exc}", file=sys.stderr)
    sys.exit(1)


def _decode_str(value) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value or "")


def list_devices() -> int:
    devices = hid.enumerate()
    if not devices:
        print("No HID devices found.")
        return 0

    print("HID devices:")
    for dev in devices:
        vid = dev.get("vendor_id", 0)
        pid = dev.get("product_id", 0)
        path = _decode_str(dev.get("path"))
        mfr = _decode_str(dev.get("manufacturer_string"))
        product = _decode_str(dev.get("product_string"))
        iface = dev.get("interface_number", "n/a")
        usage_page = dev.get("usage_page", "n/a")
        usage = dev.get("usage", "n/a")
        print(
            f"  VID:PID={vid:04x}:{pid:04x} iface={iface} usage={usage_page}:{usage} "
            f"product='{product}' manufacturer='{mfr}' path='{path}'"
        )
    return 0


def find_device(vid: int, pid: int):
    matches = [d for d in hid.enumerate() if d.get("vendor_id") == vid and d.get("product_id") == pid]
    if not matches:
        return None

    # Prefer non-zero interface first (often carries report traffic), then first match.
    matches.sort(key=lambda d: (d.get("interface_number", 0) == 0, d.get("interface_number", 0)))
    return matches[0]


def capture(vid: int, pid: int, output: Path, duration: float, report_len: int, timeout_ms: int) -> int:
    dev_info = find_device(vid, pid)
    if dev_info is None:
        print(f"Error: HID device {vid:04x}:{pid:04x} not found.", file=sys.stderr)
        return 2

    output.parent.mkdir(parents=True, exist_ok=True)

    path = dev_info.get("path")
    path_print = _decode_str(path)
    mfr = _decode_str(dev_info.get("manufacturer_string"))
    product = _decode_str(dev_info.get("product_string"))
    iface = dev_info.get("interface_number", "n/a")

    print(f"Capturing from {vid:04x}:{pid:04x} iface={iface} product='{product}' manufacturer='{mfr}'")
    print(f"Device path: {path_print}")
    print(f"Duration: {duration:.1f}s")

    handle = hid.device()
    if path is not None:
        handle.open_path(path)
    else:
        handle.open(vid, pid)

    start = time.monotonic()
    count = 0

    with output.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(["timestamp_ms", "len", "report_hex"])

        while (time.monotonic() - start) < duration:
            data = handle.read(report_len, timeout_ms)
            if not data:
                continue

            t_ms = int((time.monotonic() - start) * 1000)
            report_hex = "".join(f"{b:02x}" for b in data)
            writer.writerow([t_ms, len(data), report_hex])
            count += 1

    handle.close()

    meta = {
        "vendor_id": vid,
        "product_id": pid,
        "manufacturer": mfr,
        "product": product,
        "interface": iface,
        "path": path_print,
        "duration_s": duration,
        "report_len": report_len,
        "timeout_ms": timeout_ms,
        "report_count": count,
    }
    meta_path = output.with_suffix(output.suffix + ".meta.json")
    meta_path.write_text(json.dumps(meta, indent=2), encoding="utf-8")

    print(f"Wrote {count} reports to: {output}")
    print(f"Wrote metadata to:   {meta_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture HID input reports to CSV")
    parser.add_argument("--list", action="store_true", help="List HID devices and exit")
    parser.add_argument("--vid", type=lambda x: int(x, 0), help="USB vendor ID (e.g. 0x28de)")
    parser.add_argument("--pid", type=lambda x: int(x, 0), help="USB product ID (e.g. 0x1102)")
    parser.add_argument("--output", type=Path, default=Path("tools/captures/capture.csv"), help="CSV output path")
    parser.add_argument("--duration", type=float, default=30.0, help="Capture duration in seconds")
    parser.add_argument("--report-len", type=int, default=64, help="Max HID report length to read")
    parser.add_argument("--timeout-ms", type=int, default=50, help="Read timeout in milliseconds")

    args = parser.parse_args()

    if args.list:
        return list_devices()

    if args.vid is None or args.pid is None:
        parser.error("--vid and --pid are required unless --list is used")

    return capture(args.vid, args.pid, args.output, args.duration, args.report_len, args.timeout_ms)


if __name__ == "__main__":
    raise SystemExit(main())

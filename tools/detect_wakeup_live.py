#!/usr/bin/env python3
"""Live wake detector for Steam Controller dongle HID traffic.

Usage examples:
  python3 tools/detect_wakeup_live.py --list
  python3 tools/detect_wakeup_live.py --vid 0x28de --pid 0x1142
  python3 tools/detect_wakeup_live.py --vid 0x28de --pid 0x1142 --iface 1

Detection logic (default):
- Consider stream "idle" after 800 ms with no matching packets.
- Trigger wake when at least 3 matching packets arrive within 200 ms.
- After a wake trigger, remain latched until a re-arm silence period passes.

This is designed for always-powered dongles where VBUS does not indicate wake.
"""

from __future__ import annotations

import argparse
import sys
import time
from collections import deque


DEFAULT_PREFIXES = [
    bytes.fromhex("0100013c"),
    bytes.fromhex("0100040b"),
    bytes.fromhex("01000301"),
]


def decode_str(value) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value or "")


def get_hid():
    try:
        import hid  # type: ignore
        return hid
    except Exception as exc:
        print("Error: failed to import hid. Install with: python3 -m pip install --user hidapi", file=sys.stderr)
        print(f"Detail: {exc}", file=sys.stderr)
        raise SystemExit(1)


def list_devices() -> int:
    hid = get_hid()
    devices = hid.enumerate()
    if not devices:
        print("No HID devices found.")
        return 0

    print("HID devices:")
    for dev in devices:
        vid = dev.get("vendor_id", 0)
        pid = dev.get("product_id", 0)
        path = decode_str(dev.get("path"))
        mfr = decode_str(dev.get("manufacturer_string"))
        product = decode_str(dev.get("product_string"))
        iface = dev.get("interface_number", "n/a")
        usage_page = dev.get("usage_page", "n/a")
        usage = dev.get("usage", "n/a")
        print(
            f"  VID:PID={vid:04x}:{pid:04x} iface={iface} usage={usage_page}:{usage} "
            f"product='{product}' manufacturer='{mfr}' path='{path}'"
        )
    return 0


def find_device(vid: int, pid: int, iface: int | None):
    hid = get_hid()
    matches = [d for d in hid.enumerate() if d.get("vendor_id") == vid and d.get("product_id") == pid]
    if iface is not None:
        matches = [d for d in matches if d.get("interface_number") == iface]
    if not matches:
        return None

    # Prefer interface 1 for this dongle family unless caller forced iface.
    if iface is None:
        matches.sort(key=lambda d: (d.get("interface_number", 99) != 1, d.get("interface_number", 99)))
    return matches[0]


def parse_prefixes(raw: str | None) -> list[bytes]:
    if not raw:
        return DEFAULT_PREFIXES
    out = []
    for p in raw.split(","):
        p = p.strip().lower()
        if not p:
            continue
        out.append(bytes.fromhex(p))
    return out


def matches_prefix(data: bytes, prefixes: list[bytes]) -> bool:
    return any(data.startswith(p) for p in prefixes)


def main() -> int:
    parser = argparse.ArgumentParser(description="Detect wake-up pattern in live HID traffic")
    parser.add_argument("--list", action="store_true", help="List HID devices and exit")
    parser.add_argument("--vid", type=lambda x: int(x, 0), help="USB vendor ID, e.g. 0x28de")
    parser.add_argument("--pid", type=lambda x: int(x, 0), help="USB product ID, e.g. 0x1142")
    parser.add_argument("--iface", type=int, default=None, help="Optional HID interface number")
    parser.add_argument("--report-len", type=int, default=64, help="Max HID report length")
    parser.add_argument("--timeout-ms", type=int, default=50, help="Read timeout in ms")
    parser.add_argument("--min-silence-ms", type=int, default=800, help="Idle gap required before wake burst")
    parser.add_argument("--burst-ms", type=int, default=200, help="Burst window in ms")
    parser.add_argument("--burst-count", type=int, default=3, help="Packets required in burst window")
    parser.add_argument(
        "--rearm-silence-ms",
        type=int,
        default=1500,
        help="Silence required after trigger before detector re-arms",
    )
    parser.add_argument(
        "--prefixes",
        type=str,
        default=None,
        help="Comma-separated hex prefixes for matching packets (default: 0100013c,0100040b,01000301)",
    )

    args = parser.parse_args()

    if args.list:
        return list_devices()

    if args.vid is None or args.pid is None:
        parser.error("--vid and --pid are required unless --list is used")

    prefixes = parse_prefixes(args.prefixes)
    hid = get_hid()
    dev_info = find_device(args.vid, args.pid, args.iface)
    if dev_info is None:
        print(f"Error: HID device {args.vid:04x}:{args.pid:04x} not found.", file=sys.stderr)
        return 2

    path = dev_info.get("path")
    path_print = decode_str(path)
    product = decode_str(dev_info.get("product_string"))
    mfr = decode_str(dev_info.get("manufacturer_string"))
    iface = dev_info.get("interface_number", "n/a")

    print(f"Monitoring {args.vid:04x}:{args.pid:04x} iface={iface} product='{product}' manufacturer='{mfr}'")
    print(f"Path: {path_print}")
    print(
        f"Rule: idle_gap>={args.min_silence_ms}ms then >= {args.burst_count} packets within {args.burst_ms}ms"
    )
    print(f"Re-arm: requires {args.rearm_silence_ms}ms of silence after a wake trigger")
    print("Press Ctrl+C to stop.\n")

    h = hid.device()
    if path is not None:
        h.open_path(path)
    else:
        h.open(args.vid, args.pid)

    burst = deque()
    last_match_at = None
    armed = True

    try:
        while True:
            data = h.read(args.report_len, args.timeout_ms)
            now_ms = int(time.monotonic() * 1000)

            # Re-arm only after enough silence since the last matching packet.
            if not armed and last_match_at is not None and (now_ms - last_match_at) >= args.rearm_silence_ms:
                armed = True
                burst.clear()
                print(f"Detector re-armed at {now_ms} ms")

            if not data:
                continue

            b = bytes(data)
            if not matches_prefix(b, prefixes):
                continue

            # Start a new burst if we had enough silence.
            if last_match_at is None or (now_ms - last_match_at) >= args.min_silence_ms:
                burst.clear()

            last_match_at = now_ms
            burst.append(now_ms)

            # Keep only events in the burst window.
            while burst and (now_ms - burst[0]) > args.burst_ms:
                burst.popleft()

            if armed and len(burst) >= args.burst_count:
                print(f"WAKE DETECTED at {now_ms} ms, packet={b[:8].hex()}")
                burst.clear()
                armed = False

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        h.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

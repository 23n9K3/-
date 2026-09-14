#!/usr/bin/env python3
"""Send finite 0x88B5 broadcasts for the lwIP/ENC28J60 RX stress test."""

import argparse
import struct
import sys
import time

try:
    from scapy.all import Ether, Raw, get_if_list, sendp
except ImportError:
    print("Scapy is required: py -m pip install scapy", file=sys.stderr)
    raise SystemExit(2)


ETHERTYPE = 0x88B5
MAGIC = b"IOTPROJECT-LWIP"


def list_interfaces() -> None:
    for index, name in enumerate(get_if_list()):
        print(f"{index}: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Send bounded Ethernet broadcast traffic to IOTproject")
    parser.add_argument("--list", action="store_true", help="list interfaces")
    parser.add_argument("--iface", help="Scapy interface name")
    parser.add_argument("--count", type=int, default=10000,
                        help="frame count (default: 10000)")
    parser.add_argument("--interval", type=float, default=0.001,
                        help="seconds between frames (default: 0.001)")
    parser.add_argument("--payload-size", type=int, default=64,
                        help="payload bytes, 20..1400 (default: 64)")
    args = parser.parse_args()

    if args.list:
        list_interfaces()
        return 0
    if not args.iface:
        parser.error("--iface is required unless --list is used")
    if args.count <= 0:
        parser.error("--count must be positive")
    if args.interval < 0:
        parser.error("--interval must not be negative")
    if not 20 <= args.payload_size <= 1400:
        parser.error("--payload-size must be between 20 and 1400")

    started = time.monotonic()
    sent = 0
    for sequence in range(args.count):
        header = MAGIC + struct.pack("!I", sequence)
        payload = (header + bytes((i & 0xFF for i in range(args.payload_size))))[
            :args.payload_size]
        frame = Ether(dst="ff:ff:ff:ff:ff:ff", type=ETHERTYPE) / Raw(payload)
        sendp(frame, iface=args.iface, verbose=False)
        sent += 1
        if args.interval:
            time.sleep(args.interval)

    elapsed = time.monotonic() - started
    print(f"sent={sent} requested={args.count} elapsed={elapsed:.3f}s "
          f"rate={sent / elapsed if elapsed else 0:.1f} frame/s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

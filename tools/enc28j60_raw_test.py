#!/usr/bin/env python3
"""Send/capture IOTproject EtherType 0x88B5 frames with Scapy."""

import argparse
import struct
import time

from scapy.all import Ether, Raw, get_if_hwaddr, get_if_list, sendp, sniff

ETHERTYPE = 0x88B5
MAGIC = b"IOTPROJECT-ENC28J60"
TEST_DATA = bytes.fromhex("00112233445566778899AABBCCDDEEFF")


def crc32_mpeg2(data: bytes) -> int:
    value = 0xFFFFFFFF
    for byte in data:
        value ^= byte << 24
        for _ in range(8):
            value = ((value << 1) ^ 0x04C11DB7) & 0xFFFFFFFF \
                if value & 0x80000000 else (value << 1) & 0xFFFFFFFF
    return value


def payload(sequence: int, response: bool = False) -> bytes:
    body = (MAGIC + struct.pack("!IH", sequence, len(TEST_DATA)) +
            bytes([1 if response else 0]) + TEST_DATA)
    return body + struct.pack("!I", crc32_mpeg2(body))


def decode(packet):
    raw = bytes(packet[Raw].load) if Raw in packet else b""
    expected_length = len(MAGIC) + 4 + 2 + 1 + len(TEST_DATA) + 4
    if len(raw) < expected_length or not raw.startswith(MAGIC):
        return None
    body, received_crc = raw[:-4], struct.unpack("!I", raw[-4:])[0]
    if crc32_mpeg2(body) != received_crc:
        return None
    sequence, data_length = struct.unpack("!IH", raw[len(MAGIC):len(MAGIC) + 6])
    flags = raw[len(MAGIC) + 6]
    data = raw[len(MAGIC) + 7:-4]
    if data_length != len(data) or data != TEST_DATA:
        return None
    return sequence, flags


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--list", action="store_true", help="list interfaces")
    parser.add_argument("--iface", help="Npcap interface name")
    parser.add_argument("--sequence", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=8.0)
    args = parser.parse_args()

    if args.list:
        for interface in get_if_list():
            print(interface)
        return 0
    if not args.iface:
        parser.error("--iface is required unless --list is used")

    source = get_if_hwaddr(args.iface)
    request = Ether(dst="ff:ff:ff:ff:ff:ff", src=source,
                    type=ETHERTYPE) / Raw(payload(args.sequence))
    sendp(request, iface=args.iface, verbose=False)
    print(f"TX request: PASS seq={args.sequence}")

    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        packets = sniff(iface=args.iface, timeout=1.0, count=1,
                        lfilter=lambda p: Ether in p and
                        p[Ether].type == ETHERTYPE and p[Ether].src != source)
        for packet in packets:
            result = decode(packet)
            if result is not None:
                sequence, flags = result
                print(f"RX valid: PASS seq={sequence} flags=0x{flags:02X} "
                      f"src={packet[Ether].src}")
                if sequence == args.sequence and flags & 1:
                    print("Echo test: PASS")
                    return 0
    print("Echo test: TIMEOUT")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

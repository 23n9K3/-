#!/usr/bin/env python3
"""Bounded PC acceptance test for IOTproject ICMP/UDP/TCP RAW services."""

import argparse
import os
import socket
import subprocess
import sys
import time


UDP_PORT = 5000
TCP_PORT = 5001


def ping(ip: str, timeout: float) -> bool:
    if os.name == "nt":
        command = ["ping", "-n", "1", "-w", str(max(1, int(timeout * 1000))), ip]
    else:
        command = ["ping", "-c", "1", "-W", str(max(1, int(timeout))), ip]
    return subprocess.run(command, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, check=False).returncode == 0


def udp_echo(ip: str, timeout: float, repeat: int) -> None:
    sizes = (8, 64, 512, 1000)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        for sequence in range(repeat):
            size = sizes[sequence % len(sizes)]
            prefix = sequence.to_bytes(4, "big")
            payload = (prefix + bytes((i & 0xFF for i in range(size))))[:size]
            sock.sendto(payload, (ip, UDP_PORT))
            received, peer = sock.recvfrom(2048)
            if peer[0] != ip or received != payload:
                raise RuntimeError(f"UDP mismatch at sequence {sequence}")


def recv_exact(sock: socket.socket, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        data = sock.recv(length - len(chunks))
        if not data:
            raise RuntimeError("TCP connection closed before complete echo")
        chunks.extend(data)
    return bytes(chunks)


def tcp_echo(ip: str, timeout: float, repeat: int) -> None:
    sizes = (1, 17, 512, 1000, 1800)
    with socket.create_connection((ip, TCP_PORT), timeout=timeout) as sock:
        sock.settimeout(timeout)
        for sequence in range(repeat):
            size = sizes[sequence % len(sizes)]
            prefix = sequence.to_bytes(4, "big")
            payload = (prefix + bytes(((sequence + i) & 0xFF
                                       for i in range(size))))[:size]
            sock.sendall(payload)
            if recv_exact(sock, len(payload)) != payload:
                raise RuntimeError(f"TCP mismatch at sequence {sequence}")


def dual_tcp_echo(ip: str, timeout: float) -> None:
    sockets = []
    try:
        sockets = [socket.create_connection((ip, TCP_PORT), timeout=timeout)
                   for _ in range(2)]
        payloads = [b"client-0-" + bytes(range(64)),
                    b"client-1-" + bytes(range(96))]
        for sock, payload in zip(sockets, payloads):
            sock.settimeout(timeout)
            sock.sendall(payload)
        for sock, payload in zip(sockets, payloads):
            if recv_exact(sock, len(payload)) != payload:
                raise RuntimeError("two-client TCP echo mismatch")
    finally:
        for sock in sockets:
            sock.close()


def wait_for_ping(ip: str, expected: bool, deadline: float,
                  probe_timeout: float) -> bool:
    while time.monotonic() < deadline:
        if ping(ip, probe_timeout) == expected:
            return True
        time.sleep(1.0)
    return False


def recovery_test(ip: str, timeout: float, recovery_timeout: float) -> None:
    input("Unplug the Ethernet cable, then press Enter...")
    if not wait_for_ping(ip, False, time.monotonic() + recovery_timeout, timeout):
        raise RuntimeError("device did not go offline before timeout")
    print("Link down detected")
    input("Reconnect the Ethernet cable, then press Enter...")
    if not wait_for_ping(ip, True, time.monotonic() + recovery_timeout, timeout):
        raise RuntimeError("device did not recover before timeout")
    udp_echo(ip, timeout, 4)
    tcp_echo(ip, timeout, 4)
    print("Cable recovery: OK")


def main() -> int:
    parser = argparse.ArgumentParser(description="IOTproject network acceptance")
    parser.add_argument("--ip", default="192.168.1.50")
    parser.add_argument("--repeat", type=int, default=4)
    parser.add_argument("--connections", type=int, default=1,
                        help="number of sequential TCP connections")
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--recovery", action="store_true")
    parser.add_argument("--recovery-timeout", type=float, default=45.0)
    args = parser.parse_args()
    if (args.repeat <= 0 or args.connections <= 0 or args.timeout <= 0 or
            args.recovery_timeout <= 0):
        parser.error("repeat, connections and timeouts must be positive")

    try:
        if not ping(args.ip, args.timeout):
            raise RuntimeError("ping failed")
        print("Link: UP")
        print(f"IP: {args.ip}")
        print("Ping: OK")
        udp_echo(args.ip, args.timeout, args.repeat)
        print("UDP echo: OK")
        for _ in range(args.connections):
            tcp_echo(args.ip, args.timeout, args.repeat)
        dual_tcp_echo(args.ip, args.timeout)
        print("TCP echo: OK")
        if args.recovery:
            recovery_test(args.ip, args.timeout, args.recovery_timeout)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

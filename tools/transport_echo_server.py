#!/usr/bin/env python3
"""TCP/UDP echo peer for the IOTproject RAW transport bridge."""

import argparse
import socket
import threading
import time


def send_fragmented(sock, data, fragment_size, fragment_delay, address=None):
    if address is not None:
        sock.sendto(data, address)
        return
    offset = 0
    while offset < len(data):
        end = min(offset + fragment_size, len(data))
        sock.sendall(data[offset:end])
        offset = end
        if fragment_delay > 0 and offset < len(data):
            time.sleep(fragment_delay)


def tcp_client(conn, peer, args):
    total = 0
    print(f"[TCP] connected: {peer[0]}:{peer[1]}")
    conn.settimeout(1.0)
    try:
        if args.pause_read > 0:
            print(f"[TCP] pausing reads for {args.pause_read:.3f}s")
            time.sleep(args.pause_read)
        while True:
            try:
                data = conn.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            total += len(data)
            print(f"[TCP] rx={len(data)} total={total} from={peer[0]}:{peer[1]}")
            send_fragmented(conn, data, args.fragment_size,
                            args.fragment_delay)
            if args.close_after and total >= args.close_after:
                print(f"[TCP] active close after {total} bytes")
                break
    except (ConnectionError, OSError) as exc:
        print(f"[TCP] client error: {exc}")
    finally:
        try:
            conn.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        conn.close()
        print(f"[TCP] disconnected: {peer[0]}:{peer[1]} total={total}")


def tcp_server(args, stop):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.settimeout(1.0)
    server.bind((args.bind, args.tcp_port))
    server.listen(4)
    print(f"[TCP] listening on {args.bind}:{args.tcp_port}")
    try:
        while not stop.is_set():
            try:
                conn, peer = server.accept()
            except socket.timeout:
                continue
            threading.Thread(target=tcp_client, args=(conn, peer, args),
                             daemon=True).start()
    finally:
        server.close()


def udp_server(args, stop):
    server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server.settimeout(1.0)
    server.bind((args.bind, args.udp_port))
    print(f"[UDP] listening on {args.bind}:{args.udp_port}")
    try:
        while not stop.is_set():
            try:
                data, peer = server.recvfrom(65535)
            except socket.timeout:
                continue
            print(f"[UDP] datagram={len(data)} from={peer[0]}:{peer[1]}")
            send_fragmented(server, data, args.fragment_size,
                            args.fragment_delay, peer)
    finally:
        server.close()


def parse_args():
    parser = argparse.ArgumentParser(
        description="IOTproject TCP/UDP transport echo server")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--tcp-port", type=int, default=6000)
    parser.add_argument("--udp-port", type=int, default=6001)
    parser.add_argument("--fragment-size", type=int, default=257,
                        help="TCP echo fragment size")
    parser.add_argument("--fragment-delay", type=float, default=0.0,
                        help="delay between TCP fragments in seconds")
    parser.add_argument("--pause-read", type=float, default=0.0,
                        help="pause after TCP accept to provoke backpressure")
    parser.add_argument("--close-after", type=int, default=0,
                        help="actively close TCP after receiving N bytes")
    args = parser.parse_args()
    if not 1 <= args.tcp_port <= 65535 or not 1 <= args.udp_port <= 65535:
        parser.error("ports must be in 1..65535")
    if (args.fragment_size <= 0 or args.fragment_delay < 0 or
            args.pause_read < 0 or args.close_after < 0):
        parser.error("sizes and delays must be non-negative")
    return args


def main():
    args = parse_args()
    stop = threading.Event()
    threads = [
        threading.Thread(target=tcp_server, args=(args, stop), daemon=True),
        threading.Thread(target=udp_server, args=(args, stop), daemon=True),
    ]
    for thread in threads:
        thread.start()
    print("Press Ctrl+C to stop")
    try:
        while all(thread.is_alive() for thread in threads):
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("Stopping...")
    finally:
        stop.set()
        for thread in threads:
            thread.join(timeout=2.0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

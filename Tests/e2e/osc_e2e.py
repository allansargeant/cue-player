#!/usr/bin/env python3
"""End-to-end check of SimpleCue's OSC surface.

Drives the running app over a real UDP socket and reads its status feed back, so this
exercises the whole path -- socket, parser, action dispatch, engine, feedback -- rather
than the address parser the unit tests already cover.
"""
import socket
import struct
import sys
import time

APP_PORT = 53000
FEEDBACK_PORT = 53099


def pad(b):
    return b + b"\0" * (4 - len(b) % 4)


def encode(address, *args):
    out = pad(address.encode())
    tags = ","
    body = b""
    for a in args:
        if isinstance(a, int):
            tags += "i"
            body += struct.pack(">i", a)
        elif isinstance(a, float):
            tags += "f"
            body += struct.pack(">f", a)
        else:
            tags += "s"
            body += pad(str(a).encode())
    return out + pad(tags.encode()) + body


def decode(data):
    """Returns (address, [args]) for a simple (non-bundle) OSC message."""
    end = data.index(b"\0")
    address = data[:end].decode()
    offset = (end // 4 + 1) * 4
    if offset >= len(data) or data[offset:offset + 1] != b",":
        return address, []
    tend = data.index(b"\0", offset)
    tags = data[offset + 1:tend].decode()
    offset = (tend // 4 + 1) * 4
    args = []
    for t in tags:
        if t == "i":
            args.append(struct.pack(">i", 0) and struct.unpack(">i", data[offset:offset + 4])[0])
            offset += 4
        elif t == "f":
            args.append(round(struct.unpack(">f", data[offset:offset + 4])[0], 3))
            offset += 4
        elif t == "s":
            e = data.index(b"\0", offset)
            args.append(data[offset:e].decode())
            offset = (e // 4 + 1) * 4
    return address, args


def collect(sock, seconds=1.0):
    """Everything that arrives within the window, as {address: args}."""
    received = {}
    deadline = time.time() + seconds
    while time.time() < deadline:
        sock.settimeout(max(0.01, deadline - time.time()))
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            break
        try:
            address, args = decode(data)
            received[address] = args
        except Exception:
            pass
    return received


failures = []


def expect(condition, description, detail=""):
    if condition:
        print(f"  ok    {description}")
    else:
        failures.append(description)
        print(f"  FAIL  {description}  {detail}")


def main():
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("127.0.0.1", FEEDBACK_PORT))

    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = ("127.0.0.1", APP_PORT)

    print("OSC end-to-end")

    # 1. A status query should produce a full state dump.
    collect(rx, 0.3)
    tx.sendto(encode("/status/query"), dest)
    status = collect(rx, 1.5)

    expect("/status/standby" in status, "the app answers /status/query")
    expect("/status/master" in status, "the status feed includes the master level")
    expect("/status/playing" in status, "the status feed includes a playing count")

    if not status:
        print("\nNo reply at all -- is the app running with OSC input enabled on 53000?")
        return 1

    # 2. Setting the master level should be reflected back in the feed.
    tx.sendto(encode("/master/level", -12.0), dest)
    time.sleep(0.4)
    tx.sendto(encode("/status/query"), dest)
    status = collect(rx, 1.5)
    master = status.get("/status/master", [None])[0]
    expect(master is not None and abs(master + 12.0) < 0.2,
           "/master/level changes the master and the change is reported back",
           f"(got {master})")

    # 3. And back again, to prove it is not a one-way latch.
    tx.sendto(encode("/master/level", 0.0), dest)
    time.sleep(0.4)
    tx.sendto(encode("/status/query"), dest)
    status = collect(rx, 1.5)
    master = status.get("/status/master", [None])[0]
    expect(master is not None and abs(master) < 0.2,
           "the master level can be moved back", f"(got {master})")

    # 4. Transport commands must be accepted without the app falling over.
    for address in ("/panic", "/pause", "/resume", "/releasevamp", "/standby/next"):
        tx.sendto(encode(address), dest)
        time.sleep(0.1)

    tx.sendto(encode("/status/query"), dest)
    status = collect(rx, 1.5)
    expect("/status/standby" in status,
           "the app is still responding after a run of transport commands")
    expect(status.get("/status/paused", [1])[0] == 0,
           "/resume left the app unpaused", f"(got {status.get('/status/paused')})")

    # 5. An unknown address must be ignored, not crash or corrupt state.
    tx.sendto(encode("/not/a/real/address", 1, 2, 3), dest)
    time.sleep(0.2)
    tx.sendto(encode("/status/query"), dest)
    status = collect(rx, 1.5)
    expect("/status/standby" in status, "an unknown address is ignored safely")

    print(f"\n{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

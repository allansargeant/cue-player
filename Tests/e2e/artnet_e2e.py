#!/usr/bin/env python3
"""End-to-end check of the Art-Net path: real UDP packets in, OSC status out.

The master-level channel is the observable: it is continuous, so its value proves the
frame was received, parsed, mapped and applied, not merely that something arrived.
"""
import socket, struct, sys, time
sys.path.insert(0, __import__('os').path.dirname(__file__))
from osc_e2e import encode, collect  # reuse the OSC helpers

ART_PORT, FEEDBACK_PORT, APP_PORT = 6454, 53099, 53000
START_ADDRESS = 1
MASTER_OFFSET = 5          # DmxSettings::offsetMasterLevel
GO_OFFSET = 0

failures = []
def expect(cond, desc, detail=""):
    print(("  ok    " if cond else "  FAIL  ") + desc + ("  " + detail if detail and not cond else ""))
    if not cond: failures.append(desc)

def artnet(universe, levels):
    slots = bytearray(512)
    for addr, val in levels.items():
        slots[addr - 1] = val
    p = bytearray(18)
    p[0:8] = b"Art-Net\0"
    p[8], p[9] = 0x00, 0x50                      # OpDmx, little-endian
    p[10], p[11] = 0x00, 14                      # protocol version
    p[14] = universe & 0xff
    p[15] = (universe >> 8) & 0x7f
    p[16] = (len(slots) >> 8) & 0xff
    p[17] = len(slots) & 0xff
    return bytes(p) + bytes(slots)

def main():
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("127.0.0.1", FEEDBACK_PORT))
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    osc = ("127.0.0.1", APP_PORT)
    art = ("127.0.0.1", ART_PORT)

    print("Art-Net end-to-end")

    def master_now():
        collect(rx, 0.2)
        tx.sendto(encode("/status/query"), osc)
        return collect(rx, 1.5).get("/status/master", [None])[0]

    # Arming frame: the first frame after connecting must not act on anything.
    tx.sendto(artnet(1, {START_ADDRESS + MASTER_OFFSET: 255, START_ADDRESS + GO_OFFSET: 255}), art)
    time.sleep(0.5)
    baseline = master_now()
    expect(baseline is not None, "the app is reachable over OSC")

    # Move the master channel; the level should follow.
    tx.sendto(artnet(1, {START_ADDRESS + MASTER_OFFSET: 128}), art)
    time.sleep(0.5)
    level = master_now()
    expected = -60.0 + 128.0 / 255.0 * 60.0
    expect(level is not None and abs(level - expected) < 0.5,
           "an Art-Net frame drives the master level", f"(got {level}, expected {expected:.2f})")

    # Back to full.
    tx.sendto(artnet(1, {START_ADDRESS + MASTER_OFFSET: 255}), art)
    time.sleep(0.5)
    level = master_now()
    expect(level is not None and abs(level) < 0.3,
           "the master channel returns to 0 dB", f"(got {level})")

    # A frame on a universe we are not listening to must be ignored.
    tx.sendto(artnet(9, {START_ADDRESS + MASTER_OFFSET: 0}), art)
    time.sleep(0.5)
    level = master_now()
    expect(level is not None and abs(level) < 0.3,
           "a frame from another universe is ignored", f"(got {level})")

    # Garbage on the port must not disturb anything.
    tx.sendto(b"not an art-net packet at all", art)
    time.sleep(0.3)
    expect(master_now() is not None, "malformed traffic on the port is survived")

    print(f"\n{len(failures)} failures")
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())

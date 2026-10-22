#!/usr/bin/env python3
"""Analyze a raw GSA200 byte stream captured from the UART2 DMA ring.

No firmware changes; purely offline analysis of tools/ring_a.bin (+ ring_b.bin).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

FRAME_SIZE = 32
HEADER = b"\xAA\x55\x01\x20"


def crc8(data, poly, init=0x00, refin=False, refout=False, xorout=0x00):
    crc = init
    for byte in data:
        b = byte
        if refin:
            b = int(f"{b:08b}"[::-1], 2)
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFF if (crc & 0x80) else ((crc << 1) & 0xFF)
    if refout:
        crc = int(f"{crc:08b}"[::-1], 2)
    return crc ^ xorout


def crc16_modbus(data, init=0xFFFF):
    crc = init
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xA001) if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def find_frames(buf):
    """Return (offset, frame) for every AA 55 01 20 header followed by 32 bytes."""
    out = []
    i = 0
    while i + FRAME_SIZE <= len(buf):
        if buf[i:i + 4] == HEADER:
            out.append((i, bytes(buf[i:i + FRAME_SIZE])))
            i += FRAME_SIZE
        else:
            i += 1
    return out


def candidate_rules(frame):
    """Map rule name -> checksum value computed over the relevant bytes."""
    f = frame
    s_all = sum(f[0:31]) & 0xFF
    s_no_hdr = sum(f[2:31]) & 0xFF
    s_payload = sum(f[4:31]) & 0xFF
    s_0_29 = sum(f[0:30]) & 0xFF
    s_2_30 = sum(f[2:30]) & 0xFF
    s_4_30 = sum(f[4:30]) & 0xFF
    x_all = 0
    for b in f[0:31]:
        x_all ^= b
    x_2_30 = 0
    for b in f[2:30]:
        x_2_30 ^= b
    return {
        "sum[0:31]": s_all,
        "sum[2:31]": s_no_hdr,
        "sum[4:31]": s_payload,
        "sum[0:30]": s_0_29,
        "sum[2:30]": s_2_30,
        "sum[4:30]": s_4_30,
        "xor[0:31]": x_all,
        "xor[2:30]": x_2_30,
        "~sum[0:31]": (~s_all) & 0xFF,
        "-sum[0:31]": (-s_all) & 0xFF,
        "crc8_maxim[0:31]": crc8(f[0:31], 0x31, 0x00, True, True),
        "crc8_atm[0:31]": crc8(f[0:31], 0x07, 0x00, False, False),
        "crc8_maxim[2:31]": crc8(f[2:31], 0x31, 0x00, True, True),
        "crc8_maxim[4:31]": crc8(f[4:31], 0x31, 0x00, True, True),
        "crc8_atm[2:31]": crc8(f[2:31], 0x07, 0x00, False, False),
        "crc8_atm[4:31]": crc8(f[4:31], 0x07, 0x00, False, False),
    }


def main():
    chunks = []
    for name in ("ring_a.bin", "ring_b.bin"):
        path = os.path.join(HERE, name)
        if os.path.exists(path):
            with open(path, "rb") as fh:
                data = fh.read()
            chunks.append(data)
            print(f"{name}: {len(data)} bytes")
    if not chunks:
        print("No ring_a.bin / ring_b.bin found.")
        return 1
    buf = b"".join(chunks)
    print(f"stream: {len(buf)} bytes")
    print("hex:", buf.hex(" "))
    print()

    frames = find_frames(buf)
    offsets = [o for o, _ in frames]
    print(f"frames found: {len(frames)}")
    if offsets:
        gaps = [b - a for a, b in zip(offsets, offsets[1:])]
        print(f"header offsets: {offsets}")
        print(f"header spacing: {gaps}")
    print()

    for n, (off, f) in enumerate(frames):
        gx = f[4:8].hex()
        gy = f[8:12].hex()
        gz = f[12:16].hex()
        ax = f[16:20].hex()
        ay = f[20:24].hex()
        az = f[24:28].hex()
        print(f"[{n:02d}] off={off:4d} {f.hex(' ')}")
        print(f"     gyro={gx} {gy} {gz} accel={ax} {ay} {az} "
              f"temp={f[28]:02X}{f[29]:02X} b30={f[30]:02X} b31={f[31]:02X}")
        import struct
        try:
            vals = [struct.unpack(">f", f[4 + 4 * i:8 + 4 * i])[0] for i in range(6)]
            print("     BE: gyro=(%.6f, %.6f, %.6f) accel=(%.6f, %.6f, %.6f) temp=%.2f" % (
                vals[0], vals[1], vals[2], vals[3], vals[4], vals[5],
                struct.unpack(">h", f[28:30])[0] * 0.01))
        except Exception as exc:
            print("     BE decode failed:", exc)
    print()

    if len(frames) < 2:
        print("Not enough complete frames for statistics; rerun capture.")
        return 0

    # Which byte increments by 1 from frame to frame?
    print("increment-by-1 candidates (position: matches/total):")
    scores = []
    for pos in range(FRAME_SIZE):
        hit = 0
        for (_, a), (_, b) in zip(frames, frames[1:]):
            if ((b[pos] - a[pos]) & 0xFF) == 1:
                hit += 1
        scores.append((hit, pos))
    for hit, pos in sorted(scores, reverse=True)[:6]:
        print(f"  byte[{pos:2d}]: {hit}/{len(frames)-1}")

    # Count how many frames each rule matches, against byte 31 and byte 30.
    matches31 = {}
    matches30 = {}
    for _, f in frames:
        rules = candidate_rules(f)
        for name, value in rules.items():
            matches31[name] = matches31.get(name, 0) + (value == f[31])
            matches30[name] = matches30.get(name, 0) + (value == f[30])

    print()
    print(f"checksum candidates vs byte[31] (matches/{len(frames)}):")
    for name, hit in sorted(matches31.items(), key=lambda kv: kv[1], reverse=True):
        print(f"  {name:22s} {hit}")
    print()
    print(f"checksum candidates vs byte[30] (matches/{len(frames)}):")
    for name, hit in sorted(matches30.items(), key=lambda kv: kv[1], reverse=True):
        print(f"  {name:22s} {hit}")

    # Two-byte CRC16 candidates.
    crc16_lo = crc16_hi = 0
    for _, f in frames:
        c = crc16_modbus(f[0:30])
        crc16_lo += (c & 0xFF) == f[30] and ((c >> 8) & 0xFF) == f[31]
        crc16_hi += ((c >> 8) & 0xFF) == f[30] and (c & 0xFF) == f[31]
    print()
    print(f"CRC16/MODBUS over [0:30]: lo-hi match={crc16_lo}, hi-lo match={crc16_hi}, total={len(frames)}")

    # Delayed checksum: byte31 of frame n vs sum of frame n-1.
    delayed = 0
    for (_, a), (_, b) in zip(frames, frames[1:]):
        if b[31] == (sum(a[0:31]) & 0xFF):
            delayed += 1
    print(f"delayed sum (prev frame) match byte[31]: {delayed}/{len(frames)-1}")

    # Constant trailing bytes?
    print()
    print("byte[30] values:", [f[30] for _, f in frames])
    print("byte[31] values:", [f[31] for _, f in frames])
    return 0


if __name__ == "__main__":
    sys.exit(main())

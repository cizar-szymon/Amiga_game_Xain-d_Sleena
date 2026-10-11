#!/usr/bin/env python3
"""Show a reference snapshot (frame_NNNNN.bin from mame_capture.py).

  python tools/snapinfo.py build/ref/try1/frame_01400.bin [--hex share1:0:100]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mame_capture import read_snapshot  # noqa: E402


def main():
    p = argparse.ArgumentParser()
    p.add_argument("file")
    p.add_argument("--hex", action="append", default=[], help="chunk:offset:length (hex)")
    args = p.parse_args()
    c = read_snapshot(args.file)
    for name, data in c.items():
        if name != "regs":
            print(f"{name:12s} {len(data):5d} B")
    print(c["regs"].decode(), end="")
    io = c["io_main"]
    print("scroll BG1 x=%03X y=%03X  BG2 x=%03X y=%03X  pri=%d bank=%d flip=%d latch=%02X  sub bank=%d" % (
        io[0] | io[1] << 8, io[2] | io[3] << 8, io[4] | io[5] << 8, io[6] | io[7] << 8,
        io[15] & 7, io[15] >> 3 & 1, io[13] & 1, io[8], c["sub_bank"][0] & 1))
    for spec in args.hex:
        name, off, length = spec.split(":")
        data = c[name][int(off, 16):int(off, 16) + int(length, 16)]
        for i in range(0, len(data), 16):
            print("%s %04X: %s" % (name, int(off, 16) + i, " ".join("%02X" % b for b in data[i:i + 16])))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare memory dumps of the port (-dump) with MAME snapshots (mame_capture.py), chunk by chunk.

  python tools/compare_dumps.py build/ref/play1 build/port_out/play1 [--detail FRAME] [--chunks share1,...]
"""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mame_capture import read_snapshot  # noqa: E402

CHUNKS = ["share1", "charram", "bgram0", "bgram1", "spriteram", "palette", "palette_ext",
          "sound_ram", "mcu_ram", "io_main", "ym1", "ym2"]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ref")
    p.add_argument("port")
    p.add_argument("--detail", type=int, help="list differing addresses at this frame")
    p.add_argument("--chunks", default=",".join(CHUNKS))
    args = p.parse_args()
    chunks = args.chunks.split(",")
    frames = sorted(int(re.search(r"(\d+)", os.path.basename(f)).group(1))
                    for f in glob.glob(os.path.join(args.ref, "frame_*.bin")))
    for f in frames:
        pf = os.path.join(args.port, "frame_%05d.bin" % f)
        if not os.path.exists(pf):
            continue
        a = read_snapshot(os.path.join(args.ref, "frame_%05d.bin" % f))
        b = read_snapshot(pf)
        parts = []
        for c in chunks:
            if c not in a or c not in b:
                continue
            n = min(len(a[c]), len(b[c]))
            d = [i for i in range(n) if a[c][i] != b[c][i]]
            if c == "mcu_ram":
                d = [i for i in d if i >= 0x10]
            if d:
                parts.append(f"{c}:{len(d)}")
            if args.detail == f and d:
                print(f"frame {f} {c}: " + " ".join(f"{i:04X}={a[c][i]:02X}/{b[c][i]:02X}" for i in d[:64]))
        print(f"frame {f:5d}: " + (" ".join(parts) if parts else "identical"))


if __name__ == "__main__":
    main()

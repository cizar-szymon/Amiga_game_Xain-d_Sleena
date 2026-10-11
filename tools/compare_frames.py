#!/usr/bin/env python3
"""Compare screenshots of the port (frame_NNNNN.bmp) with MAME reference shots (frame_NNNNN.png).

  python tools/compare_frames.py build/ref/play1 build/port_out/play1 [--montage OUT.png FRAMES]

Prints per frame the number of differing pixels; --montage writes MAME | port | difference
rows for the given frames (e.g. 1400,3500).
"""
import argparse
import glob
import os
import re

from PIL import Image, ImageChops


def load_pair(ref_dir, port_dir, frame):
    ref = os.path.join(ref_dir, "frame_%05d.png" % frame)
    port = os.path.join(port_dir, "frame_%05d.bmp" % frame)
    if not (os.path.exists(ref) and os.path.exists(port)):
        return None, None
    return Image.open(ref).convert("RGB"), Image.open(port).convert("RGB")


def diff_count(a, b):
    d = ImageChops.difference(a, b).convert("L")
    return sum(1 for v in d.getdata() if v)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ref")
    p.add_argument("port")
    p.add_argument("--montage", nargs=2, metavar=("OUT", "FRAMES"))
    args = p.parse_args()

    frames = sorted(int(re.search(r"(\d+)", os.path.basename(f)).group(1))
                    for f in glob.glob(os.path.join(args.ref, "frame_*.png")))
    same = 0
    for f in frames:
        a, b = load_pair(args.ref, args.port, f)
        if a is None:
            continue
        n = diff_count(a, b)
        same += n == 0
        print(f"frame {f:5d}: {n:6d} pixels differ")
    print(f"{same} of {len(frames)} frames identical")

    if args.montage:
        out, lst = args.montage
        rows = []
        for f in [int(v) for v in lst.split(",")]:
            a, b = load_pair(args.ref, args.port, f)
            if a is None:
                continue
            d = ImageChops.difference(a, b).point(lambda v: 255 if v else 0)
            rows.append((a, b, d))
        w, h = rows[0][0].size
        m = Image.new("RGB", (w * 3 + 8, h * len(rows) + 4 * (len(rows) - 1)), (255, 255, 255))
        for i, (a, b, d) in enumerate(rows):
            for j, im in enumerate((a, b, d)):
                m.paste(im, (j * (w + 4), i * (h + 4)))
        m.save(out)


if __name__ == "__main__":
    main()

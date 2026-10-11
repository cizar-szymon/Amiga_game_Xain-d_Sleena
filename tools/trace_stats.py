#!/usr/bin/env python3
"""Statistics of MAME traces (trace_<cpu>.txt from mame_capture.py --trace).

  python tools/trace_stats.py build/ref/trace_play/trace_*.txt [--frames 10] [--top 12]

For each trace: instructions and cycles (from T=), and the most executed addresses
(idle loops show up at the top).
"""
import argparse
import collections
import re

LINE = re.compile(r"T=(\d+) +([0-9A-F]+): +(.*)$")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("files", nargs="+")
    p.add_argument("--frames", type=float, default=1.0, help="number of frames the traces cover")
    p.add_argument("--top", type=int, default=12)
    args = p.parse_args()
    for path in args.files:
        pcs = collections.Counter()
        text = {}
        t_first = t_last = None
        n = 0
        for line in open(path, encoding="latin-1"):
            m = LINE.search(line)
            if not m:
                continue
            t, pc, ins = int(m.group(1)), m.group(2), m.group(3).strip()
            t_first = t if t_first is None else t_first
            t_last = t
            pcs[pc] += 1
            text[pc] = ins
            n += 1
        cycles = (t_last - t_first) if n else 0
        print(f"== {path}: {n} instructions, {cycles} cycles, "
              f"{n / args.frames:.0f} instr/frame, {cycles / max(n, 1):.2f} cycles/instr")
        for pc, cnt in pcs.most_common(args.top):
            print(f"  {pc:>5}  {cnt:7d}  {100.0 * cnt / n:5.1f}%  {text[pc]}")


if __name__ == "__main__":
    main()

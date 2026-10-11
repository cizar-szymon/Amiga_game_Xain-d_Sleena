#!/usr/bin/env python3
"""Opcode coverage of 6809 MAME traces: which opcodes and indexed modes were executed.

  python tools/trace_coverage.py build/ref/*/trace_maincpu.txt build/ref/*/trace_sub.txt ...
"""
import collections
import re
import sys

LINE = re.compile(r"T=\d+ +([0-9A-F]+): +(\S+)\s*(.*)$")


def main():
    mnem = collections.Counter()
    modes = collections.Counter()
    for path in sys.argv[1:]:
        for line in open(path, encoding="latin-1"):
            m = LINE.search(line)
            if not m:
                continue
            op, arg = m.group(2), m.group(3).strip()
            if arg.startswith("#"):
                mode = "imm"
            elif arg.startswith("<"):
                mode = "dir"
            elif arg.startswith("$") and op[0] not in "BL" or (op in ("JMP", "JSR") and arg.startswith("$")):
                mode = "ext"
            elif "[" in arg:
                mode = "ind " + re.sub(r"\$[0-9A-F]+", "n", arg)
            elif "," in arg:
                mode = "idx " + re.sub(r"\$[0-9A-F]+", "n", arg)
                mode = re.sub(r"-?\d+,", "n,", mode)
            else:
                mode = "inh/rel"
            mnem[op] += 1
            modes[mode] += 1
    print(f"{len(mnem)} mnemonics:")
    print(" ".join(f"{k}:{v}" for k, v in sorted(mnem.items())))
    print(f"{len(modes)} addressing forms:")
    for k, v in sorted(modes.items(), key=lambda kv: -kv[1]):
        print(f"  {v:8d}  {k}")


if __name__ == "__main__":
    main()

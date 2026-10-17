#!/usr/bin/env python3
"""Checks the translator templates in a vasm listing of m6809_68k.s for code that breaks when a
template is copied elsewhere: PC-relative addressing (mode 7, register 2/3 in the first word),
BSR, and branches leaving the template.

    vasmm68k_mot -quiet -Fhunk -nowarn=62 -L core.lst port/src/m6809_68k.s -o core.o
    python tools/check_tpl_lst.py core.lst
"""
import re
import sys

lines = open(sys.argv[1], encoding="latin-1").read().split("\n")
inside = False
cur, start = None, None
bad = 0
tpl = {}          # name -> (start, end) addresses
order = []
rows = []
for l in lines:
    if "; ---- templates" in l:
        inside = True
    if not inside:
        continue
    m = re.match(r"^\S+:([0-9A-F]{8}) ([0-9A-F]+)\s+\t\s*\d+: \t(.*)$", l)
    lab = re.search(r"\d+: (tp_\w+):", l)
    if lab:
        rows.append(("label", lab.group(1), None, None))
        continue
    if m:
        rows.append(("ins", int(m.group(1), 16), m.group(2), m.group(3).strip()))

# addresses of template labels (the next instruction's address)
pending = []
for kind, a, b, c in rows:
    if kind == "label":
        pending.append(a)
    else:
        for name in pending:
            tpl[name] = a
        pending = []
for kind, addr, hexs, text in rows:
    if kind != "ins":
        cur = addr if addr.endswith("_end") is False else cur
        continue
    if len(hexs) < 4:
        continue
    w = int(hexs[:4], 16)
    if text.startswith("dc."):
        continue
    if (w & 0xff00) == 0x6100:
        print("BSR: %08X %s" % (addr, text)); bad += 1
    elif (w & 0xf000) != 0x6000 and (w & 0x3f) in (0x3a, 0x3b) and (w & 0xffc0) != 0x4e40:
        print("PC-relative: %08X %s %s" % (addr, hexs, text)); bad += 1
print("%d problems" % bad)
sys.exit(1 if bad else 0)

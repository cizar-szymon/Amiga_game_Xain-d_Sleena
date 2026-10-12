"""Sound commands named in the game code: LDA #imm shortly before a call of a sound routine.

    python tools/snd_calls.py

Reads the unidasm listings in build/snddis (main.dis, sub.dis, sb0.dis, sb1.dis): main CPU routine $9F81
(direct write to the latch), sub CPU routine $4741 or its vector $FE45 (queue $80, sent by the main CPU at $9D04).
Calls with another value than an immediate load (tables, registers) are listed separately.
"""
import re

ROUTINES = {"main.dis": ["$9F81"], "sub.dis": ["$4741", "$FE45"], "sb0.dis": ["$4741", "$FE45"],
            "sb1.dis": ["$FE45"]}
found = {}
other = []
for fn, targets in ROUTINES.items():
    lines = open("build/snddis/" + fn).read().splitlines()
    for i, l in enumerate(lines):
        if not re.search(r"\b(JSR|JMP|LBSR|BSR|LBRA)\s+(%s)\b" % "|".join(re.escape(t) for t in targets), l, re.I):
            continue
        val = None
        for back in range(1, 4):
            m = re.search(r"LDA\s+#\$([0-9A-F]{2})", lines[i - back], re.I)
            if m:
                val = m.group(1).upper()
                break
            if re.search(r"\b(LDA|JSR|BSR|RTS|PULS|TFR\s+\w,A)\b", lines[i - back], re.I):
                break
        addr = l.split(":")[0]
        if val:
            found.setdefault(val, []).append("%s:%s" % (fn[:-4], addr))
        else:
            other.append("%s:%s  %s" % (fn[:-4], addr, " | ".join(x.split(":", 1)[1].strip() for x in lines[i - 3:i + 1])))
for v in sorted(found):
    print("%s  x%-3d %s" % (v, len(found[v]), " ".join(found[v][:6])))
print("\ncalls without an immediate value:")
for o in other:
    print(" ", o)

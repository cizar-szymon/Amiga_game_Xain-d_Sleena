"""Analyse a sndrip DUMP file (512 bytes of sound RAM + 4 byte sample position (C long on Windows) per tick).

    python tools/snd_state.py DUMP.bin [SKIP_TICKS]

Prints, for every byte that changes: the number of changes and distinct values. Then it looks
for the first tick whose bytes (all changing ones except those named in EXCLUDE env, hex list)
equal an earlier tick.
"""
import os
import sys
import numpy as np

raw = np.fromfile(sys.argv[1], dtype=np.uint8)
rec = 512 + 4
n = len(raw) // rec
a = raw[:n * rec].reshape(n, rec)
ram = a[:, :512]
pos = a[:, 512:].copy().view("<i4").ravel()
skip = int(sys.argv[2]) if len(sys.argv) > 2 else 0
print("%d ticks, %.1f ticks per second of sound" % (n, n / (pos[-1] / 15977.0)))
changes = (ram[1:] != ram[:-1]).sum(axis=0)
for i in np.nonzero(changes)[0]:
    vals = np.unique(ram[skip:, i])
    print("%03X changes %7d distinct %3d" % (i, changes[i], len(vals)))

excl = set(int(x, 16) for x in os.environ.get("EXCLUDE", "").split(",") if x)
cols = [i for i in np.nonzero(changes)[0] if i not in excl]
key = ram[:, cols]
seen = {}
for t in range(skip, n):
    k = key[t].tobytes()
    if k in seen:
        t0 = seen[k]
        print("repeat: tick %d = tick %d, samples %d .. %d (%.2f s .. %.2f s)" %
              (t, t0, pos[t0], pos[t], pos[t0] / 15977.0, pos[t] / 15977.0))
        diff = np.nonzero(ram[t] != ram[t0])[0]
        print("  bytes differing in all 512:", " ".join("%03X" % d for d in diff))
        break
    seen[k] = t
else:
    print("no repeat")

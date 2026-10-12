"""Repeats of the sequence pointers ($27-$3E, 2 bytes per channel) in a sndrip DUMP file.

    python tools/snd_ptr.py DUMP.bin [SKIP_TICKS]

For each repeat of the pointers (only at the moment a pointer changes), the gap in ticks and
seconds and the other bytes that differ.
"""
import sys
import numpy as np

raw = np.fromfile(sys.argv[1], dtype=np.uint8)
rec = 512 + 4
n = len(raw) // rec
a = raw[:n * rec].reshape(n, rec)
ram = a[:, :512]
pos = a[:, 512:].copy().view("<i4").ravel()
skip = int(sys.argv[2]) if len(sys.argv) > 2 else 0
ptr = ram[:, 0x27:0x3f]
chg = np.nonzero((ptr[1:] != ptr[:-1]).any(axis=1))[0] + 1
seen = {}
shown = 0
for t in chg:
    if t < skip:
        continue
    k = ptr[t].tobytes()
    if k in seen:
        t0 = seen[k]
        diff = np.nonzero(ram[t, :0x200] != ram[t0, :0x200])[0]
        print("tick %d = %d  gap %d ticks %.2f s  differ: %s" % (t, t0, t - t0, (pos[t] - pos[t0]) / 15977.0,
              " ".join("%03X" % d for d in diff)))
        shown += 1
        if shown >= 12:
            break
    else:
        seen[k] = t

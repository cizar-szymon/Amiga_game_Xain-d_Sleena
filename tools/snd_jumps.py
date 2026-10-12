"""Backward jumps of the sequence pointer of each channel in a sndrip state file.

    python tools/snd_jumps.py DIR XX

Pointer of channel c = base ($0F + 2c) + offset ($27 + 2c), big endian. Prints for each active
channel the times of its backward jumps and the gaps between them.
"""
import os
import sys

import numpy as np

RATE = 15977
REC = 516
d, cmd = sys.argv[1], sys.argv[2]
raw = np.fromfile(os.path.join(d, "cmd_%s.state" % cmd), dtype=np.uint8)
n = len(raw) // REC
a = raw[:n * REC].reshape(n, REC)
ram = a[:, :512].astype(np.int64)
pos = a[:, 512:].copy().view("<i4").ravel()
for c in range(12):
    if not ram[:, 2 + c].any():
        continue
    off = ram[:, 0x27 + 2 * c] * 256 + ram[:, 0x28 + 2 * c]
    base = ram[:, 0x0f + 2 * c] * 256 + ram[:, 0x10 + 2 * c]
    j = np.nonzero(off[1:] < off[:-1])[0] + 1
    times = pos[j] / RATE
    print("ch %2d cmd %02X base %04X: %d jumps back, at %s" % (c, ram[-1, 2 + c], base[-1], len(j),
          " ".join("%.2f" % t for t in times[:8])))
    if len(times) > 1:
        print("        gaps %s" % " ".join("%.2f" % g for g in np.diff(times)[:8]))

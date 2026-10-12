"""Best repeat period of a recording by the energy of 20 ms blocks (no state needed).

    python tools/snd_period.py DIR XX [MIN_S] [MAX_S]

For start s and period p: error of the energy after s against after s + p, up to the end. Prints
the best periods and, for the best one, the earliest start with a small error.
"""
import os
import sys
import wave

import numpy as np

RATE = 15977
d, cmd = sys.argv[1], sys.argv[2]
lo = float(sys.argv[3]) if len(sys.argv) > 3 else 3
hi = float(sys.argv[4]) if len(sys.argv) > 4 else 140
w = wave.open(os.path.join(d, "cmd_%s.wav" % cmd))
s = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
blk = RATE // 50
n = len(s) // blk
e = s[:n * blk].reshape(n, blk)
e = np.sqrt(((e - e.mean(axis=1, keepdims=True)) ** 2).mean(axis=1))
res = []
for p in range(int(lo * 50), int(hi * 50)):
    x, y = e[:-p], e[p:]
    # error over the second half of the recording (after any intro)
    h = len(x) // 2
    err = np.abs(x[h:] - y[h:]).sum() / max(1.0, (x[h:] + y[h:]).sum() / 2)
    res.append((err, p))
res.sort()
for err, p in res[:8]:
    print("period %.2f s  err %.4f" % (p / 50.0, err))
err, p = res[0]
x, y = e[:-p], e[p:]
for st in range(0, len(x)):
    seg = slice(st, len(x))
    er = np.abs(x[seg] - y[seg]).sum() / max(1.0, (x[seg] + y[seg]).sum() / 2)
    if er < err * 1.5 + 0.01:
        print("earliest start ca. %.2f s (err %.4f)" % (st / 50.0, er))
        break

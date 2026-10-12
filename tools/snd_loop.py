"""Find the loops of the recordings from tools/sndrip (cmd_XX.wav + cmd_XX.state).

    python tools/snd_loop.py DIR [XX ...]      (no XX: every cmd_XX.state that sounds up to the end)

From the structure of the music, not from the sound (the noise drums make sound comparisons
unreliable):
1. Channels that still play at the end of the recording (their command in $02 + c). Pointer of a
   channel: $0F + 2c (base) + $27 + 2c (offset); a backward jump of the offset ends a pass.
2. For each channel the period of its jumps: from the earliest jump i0 possible, the smallest
   number m of jumps after which the gaps repeat (to 30 ms) up to the end; the pattern starts one
   period before its first periodic jump.
3. The loop period: the smallest multiple of the period of the channel with the longest one (the
   melody) that is within 0.15 s of a multiple of every other channel's period, at most MAX_S
   (channels with patterns of other lengths: bass, drums). Otherwise the melody's period.
4. The loop starts at the latest pattern start; its length in samples is measured between a jump
   of the melody channel and its jump one loop period later.
Prints the loop and the error of the sound energy (20 ms blocks) after both points over one
period (for information), writes DIR/loops.txt (cmd start end).
"""
import glob
import os
import re
import sys
import wave

import numpy as np

RATE = 15977              # replaced by the rate of the recordings (sndrip.txt)
REC = 512 + 4
MAX_S = 130
TOL = 0.03                 # s, gaps equal
ALIGN = 0.15               # s, channels aligned at the loop end


def load(d, cmd):
    w = wave.open(os.path.join(d, "cmd_%s.wav" % cmd))
    s = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    raw = np.fromfile(os.path.join(d, "cmd_%s.state" % cmd), dtype=np.uint8)
    n = len(raw) // REC
    a = raw[:n * REC].reshape(n, REC)
    pos = a[:, 512:].copy().view("<i4").ravel()
    return s, a[:, :512].astype(np.int64), pos


def energy(s, blk):
    n = len(s) // blk
    e = s[:n * blk].reshape(n, blk)
    e = e - e.mean(axis=1, keepdims=True)
    return np.sqrt((e * e).mean(axis=1))


def channel_period(jt):
    """(period, pattern start) of the jump times jt in seconds, or None"""
    g = np.diff(jt)
    if len(g) < 2:
        return None
    # the earliest pattern start first (a short pattern would else match only the last gaps)
    for i0 in range(0, len(g) - 1):
        for m in range(1, (len(g) - i0) // 3 + 1):     # at least 3 passes of the pattern
            if np.all(np.abs(g[i0 + m:] - g[i0:len(g) - m]) < TOL):
                p = g[i0:i0 + m].sum()
                return p, jt[i0 + 1] - p if i0 + 1 < len(jt) else jt[i0] - p
    return None


def find(d, cmd):
    s, ram, pos = load(d, cmd)
    t = pos / RATE
    chans = {}
    for c in range(12):
        if not ram[-1, 2 + c]:                        # silent at the end: not part of the loop
            continue
        off = ram[:, 0x27 + 2 * c] * 256 + ram[:, 0x28 + 2 * c]
        j = np.nonzero(off[1:] < off[:-1])[0] + 1
        cp = channel_period(t[j]) if len(j) >= 3 else None
        if cp:
            chans[c] = (cp[0], max(0.0, cp[1]), j)
    if not chans:
        return None
    mel = max(chans, key=lambda c: (round(chans[c][0], 2), -c))
    pm = chans[mel][0]
    period = pm
    for k in range(1, int(MAX_S / pm) + 1):
        ok = True
        for c, (pc, _, _) in chans.items():
            r = k * pm / pc
            if abs(r - round(r)) * pc > ALIGN:
                ok = False
                break
        if ok:
            period = k * pm
            break
    start = max(st for _, st, _ in chans.values())
    jm = chans[mel][2]
    i0 = next((i for i in jm if t[i] >= start - TOL), None)
    if i0 is None:
        return None
    i1 = min(jm, key=lambda i: abs(t[i] - t[i0] - period))
    if abs(t[i1] - t[i0] - period) > 0.1:
        return None
    # the loop may start anywhere once every pattern runs: at the latest pattern start, with the
    # period measured between the two jumps of the melody
    plen = int(pos[i1]) - int(pos[i0])
    p0 = int(pos[np.argmax(t >= start)]) if start > 0 else 0
    p1 = p0 + plen
    blk = RATE // 50
    e = energy(s, blk)
    b0, b1 = p0 // blk, p1 // blk
    m = min(b1 - b0, len(e) - b1)
    err = np.abs(e[b0:b0 + m] - e[b1:b1 + m]).sum() / max(1.0, (e[b0:b0 + m] + e[b1:b1 + m]).sum() / 2)
    desc = " ".join("ch%d %.2f@%.1f" % (c, p, st) for c, (p, st, _) in sorted(chans.items()))
    return p0, p1, err, m / 50.0, desc


def find_energy(d, cmd):
    """fallback: the period with the smallest energy error (3 s .. MAX_S, over the second half),
    then the earliest start whose error over one period is close to the best"""
    s, _, _ = load(d, cmd)
    blk = RATE // 50
    e = energy(s, blk)
    perr = []
    for p in range(3 * 50, min(MAX_S * 50, len(e) // 2)):
        x, y = e[:-p], e[p:]
        h = len(x) // 2
        perr.append((np.abs(x[h:] - y[h:]).sum() / max(1.0, (x[h:] + y[h:]).sum() / 2), p))
    if not perr or min(perr)[0] > 0.3:
        return None
    # the shortest period close to the best (a held tone repeats with almost any period)
    emin = min(perr)[0]
    p = min(pp for er, pp in perr if er <= emin * 1.3 + 0.005)
    errs = []
    for st in range(0, len(e) - 2 * p, 5):
        x, y = e[st:st + p], e[st + p:st + 2 * p]
        errs.append((np.abs(x - y).sum() / max(1.0, (x + y).sum() / 2), st))
    if not errs:
        return None
    emin = min(er for er, _ in errs)
    # an early start is worth some error (memory): up to 0.2
    lim = max(emin * 1.2 + 0.01, 0.2)
    err, st = next(c for c in errs if c[0] <= lim)
    return st * blk, (st + p) * blk, err, p / 50.0, "energy"


def rate_of(d):
    """the sample rate of the recordings (header line of sndrip.txt)"""
    for line in open(os.path.join(d, "sndrip.txt")):
        m = re.search(r"rate (\d+) Hz", line)
        if m:
            return int(m.group(1))
    return 15977


def main():
    d = sys.argv[1]
    global RATE
    RATE = rate_of(d)
    cmds = sys.argv[2:]
    if not cmds:
        for line in open(os.path.join(d, "sndrip.txt")):
            f = line.split()
            if f and f[0] != "#" and f[-1] == "1":
                cmds.append(f[0])
    out = []
    for c in cmds:
        r = find(d, c)
        if not r or r[1] > 150 * RATE:          # no structure found or a very late loop
            r = find_energy(d, c) or r
        if r:
            p0, p1, err, chk, desc = r
            print("%s loop %8d .. %8d  (%.2f s .. %.2f s, period %.2f s) err %.3f over %.0f s  [%s]" %
                  (c, p0, p1, p0 / RATE, p1 / RATE, (p1 - p0) / RATE, err, chk, desc))
            out.append("%s %d %d\n" % (c, p0, p1))
        else:
            print("%s no loop" % c)
    with open(os.path.join(d, "loops.txt"), "w") as f:
        f.writelines(out)


main()

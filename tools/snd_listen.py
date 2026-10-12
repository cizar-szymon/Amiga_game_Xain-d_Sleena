"""Listening copies of the sndrip WAVs: DC removed, normalised to -1 dBFS, 44.1 kHz.

    python tools/snd_listen.py SRC_DIR DST_DIR [MAX_SECONDS]

Music is cut to MAX_SECONDS (default 90). Only for listening on the PC; the Amiga samples are
made from the originals.
"""
import glob
import os
import sys
import wave

import numpy as np

src, dst = sys.argv[1], sys.argv[2]
maxs = float(sys.argv[3]) if len(sys.argv) > 3 else 90
os.makedirs(dst, exist_ok=True)
for p in sorted(glob.glob(os.path.join(src, "cmd_*.wav"))):
    w = wave.open(p)
    rate = w.getframerate()
    s = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    s = s[:int(maxs * rate)]
    s -= s.mean()
    peak = np.abs(s).max()
    if peak > 0:
        s *= 0.89 * 32767 / peak
    t = np.arange(int(len(s) * 44100 / rate)) * (rate / 44100.0)
    out = np.interp(t, np.arange(len(s)), s).astype("<i2")
    o = wave.open(os.path.join(dst, os.path.basename(p)), "wb")
    o.setnchannels(1)
    o.setsampwidth(2)
    o.setframerate(44100)
    o.writeframes(out.tobytes())
    o.close()
print("done:", dst)

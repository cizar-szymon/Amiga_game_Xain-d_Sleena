"""A/B files: 16 bit recordings against the 8 bit Paula output of the sample player (PC only).

    python tools/snd_compare8.py SND_DIR PACK OUT_DIR [XX ...]

For each command: NN_XX_16bit.wav (the recording from tools/sndrip, DC removed as in the pack)
and NN_XX_8bit.wav (the data from the pack through the Amiga path: sample * volume * 2, gain
SMP_GAIN, >> 8, clipped to 8 bit, as samples_fill in main_amiga.c). Both at the same level,
music cut to 30 s, resampled the same way to 44.1 kHz (some players do not take 15977 Hz).
"""
import os
import struct
import sys
import wave

import numpy as np

RATE = 15977
SMP_GAIN = 3
DEFAULT = ["09", "02", "07", "0F", "8A", "88", "89", "8F", "A8", "97"]


def highpass(x, r=0.995):
    y = np.empty_like(x)
    px = py = 0.0
    for i in range(len(x)):
        py = x[i] - px + r * py
        px = x[i]
        y[i] = py
    return y


def write(path, s):
    t = np.arange(int(len(s) * 44100 / RATE)) * (RATE / 44100.0)
    out = np.clip(np.interp(t, np.arange(len(s)), s), -32768, 32767).astype("<i2")
    w = wave.open(path, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(44100)
    w.writeframes(out.tobytes())
    w.close()


def main():
    snd, pack, out = sys.argv[1], sys.argv[2], sys.argv[3]
    cmds = sys.argv[4:] or DEFAULT
    os.makedirs(out, exist_ok=True)
    data = open(pack, "rb").read()
    n = struct.unpack(">H", data[8:10])[0]
    base = 12 + n * 20
    table = {}
    for i in range(n):
        cmd, flags, mask, vol, _, _, off, length, loop = struct.unpack(">BBHBBHIII", data[12 + i * 20:32 + i * 20])
        table["%02X" % cmd] = (vol, np.frombuffer(data[base + off:base + off + length], dtype=np.int8).astype(np.int64))
    for k, c in enumerate(cmds, 1):
        w = wave.open(os.path.join(snd, "cmd_%s.wav" % c))
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
        vol, d = table[c]
        m = min(len(d), len(x), 30 * RATE)
        ref = highpass(x[:m])
        peak = max(1.0, np.abs(ref).max())
        # the pack stores round(ref * 127 / peak) with vol = round(64 * peak / 16384)
        s16 = np.clip(ref * (127.0 / peak) * (64.0 * peak / 16384.0) * 2 * SMP_GAIN, -32768, 32767)
        s8 = np.clip((d[:m] * vol * 2 * SMP_GAIN) >> 8, -128, 127) * 256
        kind = "music" if int(c, 16) < 0x80 else "sfx"
        write(os.path.join(out, "%02d_%s_%s_16bit.wav" % (k, c, kind)), s16)
        write(os.path.join(out, "%02d_%s_%s_8bit.wav" % (k, c, kind)), s8.astype(np.float64))
        used = np.abs(s8 / 256).max()
        print("%s %s %5.1f s  vol %2d  8 bit peak %3d of 127" % (c, kind, m / RATE, vol, used))
    print("written to", out)


main()

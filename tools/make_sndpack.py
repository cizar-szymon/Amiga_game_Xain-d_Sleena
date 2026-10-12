"""Build the sample file XainSleena.snd from the tools/sndrip recordings (PC tool).

    python tools/make_sndpack.py SND_DIR OUT_FILE

SND_DIR: cmd_XX.wav, cmd_XX.state, sndrip.txt and loops.txt (tools/snd_loop.py).
The samples come from the user's ROMs: the file must not be distributed (as the ROMs).

Contents: the commands the game sends (docs/PORTING.md, chapter 13) and the other one-shot
commands up to 10 s (cheap, in case the code computes one). Per command: DC removed (one pole
high pass, ca. 13 Hz), loops cross-faded over 32 ms before their end into the samples before
their start, one-shots faded out over 4 ms; 8 bit signed, normalised to the peak, with a Paula
volume 0..64 that gives back the original level (vol = 64 * peak / 16384; the player outputs
sample * vol * 2). Commands with identical sound share the data.

Format (big endian):
  0  "XSND", u16 version (1), u16 rate (Hz of the recordings, e.g. 22030), u16 count, u16 0
  12 count x 20 bytes: u8 cmd, u8 flags (1 = loops), u16 YM channels used (bit c = channel c,
     from $02 + c), u8 volume, u8 0, u16 0, u32 data offset (from the end of the table),
     u32 length (samples; a loop ends here), u32 loop start
  then the data (int8)
"""
import os
import re
import struct
import sys
import wave

import numpy as np

RATE = 15977              # replaced by the rate of the recordings (sndrip.txt)
USED_MUSIC = "01 02 03 04 05 06 07 08 09 0B 0C 0D 0F 12 15 16 17 19 1A 1C 1D".split()
USED_SFX = ("81 82 85 88 89 8A 8B 8C 8D 8E 8F 90 91 94 96 97 98 99 9F A1 A2 A3 A5 A8 AC B4 B5 "
            "B8 BC").split()
XFADE = 512
REF = 16384.0


def read_wav(path):
    w = wave.open(path)
    return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)


def highpass(x, r=0.995):
    y = np.empty_like(x)
    prev_x = prev_y = 0.0
    for i in range(len(x)):                  # small files; plain loop is fine
        prev_y = x[i] - prev_x + r * prev_y
        prev_x = x[i]
        y[i] = prev_y
    return y


def channels(path):
    raw = np.fromfile(path, dtype=np.uint8)
    a = raw[:len(raw) // 516 * 516].reshape(-1, 516)
    mask = 0
    for c in range(12):
        if a[:, 2 + c].any():
            mask |= 1 << c
    return mask


def rate_of(d):
    """the sample rate of the recordings (header line of sndrip.txt)"""
    for line in open(os.path.join(d, "sndrip.txt")):
        m = re.search(r"rate (\d+) Hz", line)
        if m:
            return int(m.group(1))
    return 15977


def main():
    d, out = sys.argv[1], sys.argv[2]
    global RATE
    RATE = rate_of(d)
    info = {}
    for line in open(os.path.join(d, "sndrip.txt")):
        f = line.split()
        if f and f[0] != "#":
            info[f[0]] = (int(f[1]), int(f[4]))           # samples, still sounding at the end
    loops = {}
    for line in open(os.path.join(d, "loops.txt")):
        c, a, b = line.split()
        loops[c] = (int(a), int(b))
    used = set(USED_MUSIC + USED_SFX)
    cmds = []
    for c in sorted(info):
        n, at_max = info[c]
        if c in used or (not at_max and n <= 10 * RATE):
            cmds.append(c)
    missing = sorted(used - set(cmds))
    if missing:
        print("not recorded (silent?):", " ".join(missing))

    entries, blobs, index = [], [], {}
    total = 0
    for c in cmds:
        x = highpass(read_wav(os.path.join(d, "cmd_%s.wav" % c)))
        n, at_max = info[c]
        flags, ls = 0, 0
        if c in loops:
            ls, le = loops[c]
            if ls < XFADE:                                # room for the cross-fade before the start
                ls, le = ls + XFADE, le + XFADE
            x = x[:le].copy()
            w = np.arange(XFADE) / float(XFADE)
            x[le - XFADE:le] = x[le - XFADE:le] * (1 - w) + x[ls - XFADE:ls] * w
            flags = 1
        elif at_max:
            print("%s sounds to the end but has no loop: cut" % c)
        else:
            k = min(64, len(x))
            x[len(x) - k:] *= np.linspace(1, 0, k)
        peak = max(1.0, np.abs(x).max())
        vol = int(max(1, min(64, round(64 * peak / REF))))
        data = np.clip(np.round(x * 127 / peak), -128, 127).astype(np.int8).tobytes()
        key = (data, ls, flags)
        if key in index:
            off = index[key]
        else:
            off = total
            index[key] = off
            blobs.append(data)
            total += len(data)
        mask = channels(os.path.join(d, "cmd_%s.state" % c))
        entries.append(struct.pack(">BBHBBHIII", int(c, 16), flags, mask, vol, 0, 0, off, len(data), ls))
        print("%s %s %6.2f s%s vol %2d ch %03X%s" % (c, "music" if int(c, 16) < 0x80 else "sfx  ",
              len(data) / RATE, " loop from %.2f" % (ls / RATE) if flags else "", vol, mask,
              "" if off == total - len(data) or off == index[key] and blobs and key not in index else ""))
    with open(out, "wb") as f:
        f.write(b"XSND" + struct.pack(">HHHH", 1, RATE, len(entries), 0))
        f.writelines(entries)
        f.writelines(blobs)
    print("%d commands, %d bytes of samples (%.1f MB), %s" % (len(entries), total, total / 1048576.0, out))


main()

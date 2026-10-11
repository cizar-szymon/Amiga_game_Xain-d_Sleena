#!/usr/bin/env python3
"""Compare the port's sound with the MAME reference (both mono 16-bit WAV, same sample rate).

  python tools/compare_wav.py REF.wav PORT.wav [--from S] [--to S] [--png OUT.png]

Prints the best time offset (cross-correlation of 10 ms RMS envelopes), the envelope correlation
and the level ratio per 5-second block; --png writes both spectrograms (MAME above, port below).
"""
import argparse
import wave

import numpy as np


def load(path):
    w = wave.open(path)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    return a, w.getframerate()


def envelope(a, rate, ms=10):
    n = rate * ms // 1000
    m = len(a) // n
    return np.sqrt((a[:m * n].reshape(m, n) ** 2).mean(axis=1))


def spectrogram(a, rate, nfft=1024, hop=512):
    win = np.hanning(nfft)
    frames = [np.abs(np.fft.rfft(a[i:i + nfft] * win)) for i in range(0, len(a) - nfft, hop)]
    s = 20 * np.log10(np.array(frames).T + 1e-3)
    return s[: nfft // 4]   # up to rate/4


def main():
    p = argparse.ArgumentParser()
    p.add_argument("ref")
    p.add_argument("port")
    p.add_argument("--from", dest="t0", type=float, default=0)
    p.add_argument("--to", dest="t1", type=float, default=1e9)
    p.add_argument("--png")
    args = p.parse_args()
    a, ra = load(args.ref)
    b, rb = load(args.port)
    if ra != rb:
        raise SystemExit(f"different rates {ra} {rb}")
    r = ra
    a = a[int(args.t0 * r):int(min(args.t1, len(a) / r) * r)]
    b = b[int(args.t0 * r):int(min(args.t1, len(b) / r) * r)]
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    ea, eb = envelope(a, r), envelope(b, r)
    best, best_c = 0, -1
    for lag in range(-50, 51):        # +-0.5 s in 10 ms steps
        x = ea[max(0, lag):len(ea) + min(0, lag)]
        y = eb[max(0, -lag):len(eb) + min(0, -lag)]
        if x.std() > 0 and y.std() > 0:
            c = np.corrcoef(x, y)[0, 1]
            if c > best_c:
                best, best_c = lag, c
    print(f"best offset {best * 10} ms (port later if > 0), envelope correlation {best_c:.3f}")
    block = 5 * r
    for i in range(0, n - block + 1, block):
        ra_ = np.sqrt((a[i:i + block] ** 2).mean())
        rb_ = np.sqrt((b[i:i + block] ** 2).mean())
        ratio = rb_ / ra_ if ra_ > 0 else float("nan")
        print(f"  {args.t0 + i / r:6.1f} s: RMS MAME {ra_:7.0f}  port {rb_:7.0f}  ratio {ratio:5.2f}")
    if args.png:
        from PIL import Image
        sa, sb = spectrogram(a, r), spectrogram(b, r)
        lo, hi = 0, max(sa.max(), sb.max())
        img = np.concatenate([sa, np.full((4, sa.shape[1]), hi), sb])[::-1]
        img = np.clip((img - lo) / (hi - lo) * 255, 0, 255).astype(np.uint8)
        Image.fromarray(img).save(args.png)


if __name__ == "__main__":
    main()

"""Print the envelope of a mono 16 bit WAV: peak and mean per block (default 0.25 s).

    python tools/wavenv.py FILE.wav [BLOCK_SECONDS] [MAX_BLOCKS]
"""
import struct
import sys
import wave

w = wave.open(sys.argv[1])
rate = w.getframerate()
data = w.readframes(w.getnframes())
s = struct.unpack("<%dh" % (len(data) // 2), data)
blk = int(rate * (float(sys.argv[2]) if len(sys.argv) > 2 else 0.25))
maxb = int(sys.argv[3]) if len(sys.argv) > 3 else 80
for i in range(0, min(len(s), blk * maxb), blk):
    b = s[i:i + blk]
    mean = sum(b) / len(b)
    peak = max(abs(v - mean) for v in b)
    print("%7.2f s  mean %7.1f  peak(ac) %5d" % (i / rate, mean, peak))

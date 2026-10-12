#!/bin/sh
# Build tools/sndrip.c with the emulator core (MSYS2 UCRT64 gcc) -> build/sndrip.exe
DIR="$(cd "$(dirname "$0")/.." && pwd)"
MSYSTEM=UCRT64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash -lc 'cd "$0/port" && gcc -std=c99 -O2 -Wall -Isrc src/m6809.c src/m6805.c src/mcu68705.c src/ym2203.c src/xain.c src/video.c src/romload.c ../tools/sndrip.c -o ../build/sndrip.exe' "$DIR"

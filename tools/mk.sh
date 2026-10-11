#!/bin/sh
# Run make in port/ inside MSYS2 UCRT64 (gcc, SDL2): sh tools/mk.sh [make arguments]
#   sh tools/mk.sh                     PC build
#   sh tools/mk.sh -f Makefile.amiga   Amiga build (vbcc)
DIR="$(cd "$(dirname "$0")/../port" && pwd)"
MSYSTEM=UCRT64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash -lc 'cd "$0" && make "$@"' "$DIR" "$@"

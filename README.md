# Xain'd Sleena – Amiga 1200 (68060, RTG) & PC port

Emulator of the Technos arcade game **Xain'd Sleena** (1986; 3× 6809, MC68705 MCU, 2× YM2203),
written from scratch in C99 with 68k assembler cores, for the **Amiga 1200 with a 68060 and a
graphics card** (AmigaOS 3.x, Picasso96, Paula/AHI) and for **Windows** (SDL2). It runs the
original ROMs of the MAME set `xsleena`; the result matches MAME 0.289 (CPU traces: 0 differences).

**The ROMs are not included** – you need your own copy of the MAME `xsleena` set.

| | |
|---|---|
| Release | 0.9.5 (12.10.2026) – shown on the start screen as `RELEASE x.y.z` |
| Speed | 68060 @ 100 MHz: full arcade speed (57.44 Hz) |
| Player / developer docs | [`docs/release/amiga`](docs/release/amiga), [`docs/release/pc`](docs/release/pc) (PL / EN) |
| Release notes | `docs/release/*/XainSleena_release.txt` |
| Port details (PL) | [`port/README.md`](port/README.md), [`docs/PORTING.md`](docs/PORTING.md) |

Build: `sh tools/mk.sh` (PC, MSYS2 UCRT64) and `sh tools/mk.sh -f Makefile.amiga` (Amiga, vbcc).

Opis po polsku: [`port/README.md`](port/README.md).

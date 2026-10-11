# Xain'd Sleena (Technos 1986) – port na PC i Amigę 1200 (68060 + RTG)

Emulator automatu Xain'd Sleena (zestaw MAME `xsleena`, wersja światowa z MCU) napisany od
zera w C99 z wstawkami w asemblerze 68k: gra działa z oryginalnych ROM-ów, wynik jest zgodny
z MAME 0.289 (wzorzec). Szczegóły, decyzje, pomiary i historia: [`docs/PORTING.md`](../docs/PORTING.md).

## Stan (11.10.2026)

| Faza | Stan |
|---|---|
| 0. Rozpoznanie, decyzja | ✅ sposób B – emulacja sprzętu (3× 6809, MCU 68705, 2× YM2203) |
| 1. Wzorzec MAME (ślady instrukcji, zrzuty, dźwięk) | ✅ |
| 2. Port na PC (SDL2) | ✅ grywalny; rdzenie CPU 0 różnic ze śladami MAME, obraz identyczny, dźwięk zgodny (korelacja 0,996) |
| 3. Amiga (Intuition, Picasso96, Paula/AHI, joystick) | ✅ działa na A1200, wynik identyczny z PC co do bajtu |
| 4. Optymalizacja | ⏳ 2,3 → ok. 25 kl./s bez dźwięku, 16–20 z dźwiękiem (68060/50, okno `SCALE=2`); cel 57,44 kl./s |
| 5. Wydanie (ikona, Tool Types, opisy) | ⏳ |

## Budowanie

- PC (MSYS2 UCRT64, gcc, SDL2): `sh tools/mk.sh` → `build/port/xainsleena.exe` i programy testowe.
- Amiga (vbcc `+aos68k_win`, NDK 3.2, AHI, P96): `sh tools/mk.sh -f Makefile.amiga` →
  `build/amiga/XainSleena` (gra) i programy testowe. Pliki `src/m6809_68k.s` i `src/video_68k.s`
  generują `tools/gen_6809_68k.py` i `tools/gen_video_68k.py`.
- ROM-y nie są dołączone: zestaw MAME `xsleena` (zip albo katalog) w `ROM/`, na Amidze
  `PROGDIR:rom/xsleena` lub `ROMPATH=`.

## Uruchamianie na Amidze

`XainSleena [SCALE=1|2|3] [SCREEN=FULL] [NOSOUND] [SOUND=PAULA|AHI] [FRAMESKIP=AUTO|n] [STATS|STATS=FULL] ...`
(pełna lista na początku `src/main_amiga.c`; te same opcje działają jako Tool Types).
Klawisze: strzałki, Ctrl/Z/spacja – strzał, Alt/X – skok, 5/6 – moneta, 1/2 – start,
P – pauza, Esc – koniec; joystick / pad CD32.

## Budowa programu

| Plik | Zawartość |
|---|---|
| `src/xain.c` | model automatu: mapa pamięci, banki, przerwania, przeplot CPU co linię (96 cykli), pomijanie pętli oczekiwania |
| `src/m6809.c` | rdzeń 6809 w C (wzorcowy, zgodny z MAME co do cykla) |
| `src/m6809_68k.s` | rdzeń 6809 w asemblerze 68k (generowany), rzadkie instrukcje przez rdzeń C |
| `src/m6805.c`, `src/mcu68705.c` | MCU 68705P5 z interfejsem Taito |
| `src/ym2203.c`, `src/ym2203_68k.s` | YM2203 (FM + SSG), port ymfm w liczbach całkowitych; wyjście FM w asm na Amidze |
| `src/video.c`, `src/video_68k.s` | grafika: renderer wzorcowy (linia po linii jak MAME) i szybki (cała klatka; linie w asm na Amidze) |
| `src/main_sdl.c`, `src/main_amiga.c` | interfejsy PC i Amigi |

## Testy (wszystkie porównują szybką ścieżkę z wolną, wzorcową)

| Program | Co sprawdza |
|---|---|
| `trace6809`, `trace6805` (PC) | rdzenie CPU instrukcja po instrukcji ze śladami MAME |
| `xain_test` / `XainTest` | przebieg bez okna: zrzuty, dźwięk, `XAIN_CHECK` (renderer szybki = wzorcowy), `XAIN_NOIDLE`, `XAIN_ASM`, dziennik YM |
| `Lockstep`, `LockstepT` (Amiga) | rdzeń 6809 w asm = rdzeń C (rejestry, cykle, cała pamięć co klatkę) |
| `CpuTest6809` (Amiga) | losowe instrukcje i ciągi: asm = C |
| `YmBench` | YM2203 osobno (czas, suma kontrolna) |
| `RenderBench` | renderer osobno (czas, suma kontrolna, warstwy) |

Narzędzia w `tools/`: wzorzec MAME (`mame_capture.py`), zdalne sterowanie Amigą
(`amiga_remote.py`, AmiAgent), porównania klatek, zrzutów i dźwięku.

# Xain'd Sleena: port na PC i Amigę (68060 + RTG)

Stan na 2026-10-11: **faza 2 (port na PC) w toku**: procesory, MCU, model automatu i obraz zgodne z MAME, ⏳ dźwięk. Cel: Xain'd Sleena (Technos, 1986) na Amidze 1200
z 68060, Picasso96 i Paulą/AHI, katalog docelowy na prawdziwej Amidze: `Work:XainSleena`.
Plan ogólny: `C:\Workprojects\AmigaGAMES\PORTING_PLAN.md`.

## 1. Stan prac

| Krok | Zakres | Stan |
|---|---|---|
| 0 | Rozpoznanie sprzętu, decyzja o architekturze (ten dokument) | ✅ sposób B i zestaw `xsleena` zaakceptowane 11.10.2026 |
| 1 | Wzorzec: MAME (zestaw `xsleena`) ze sterowaniem z Lua, ślady instrukcji, zrzuty ekranu/RAM, nagranie WAV (`tools/mame_capture.py`) | ✅ przebieg `play1` (rozdział 8) |
| 2 | Emulator 6809 w C (`port/src/m6809.c`), test lockstep ze śladami MAME (`tests/trace6809.c`) | ✅ 1 156 604 instrukcje, 1972 przerwania, 0 różnic rejestrów, cykli i zapisów (rozdział 10) |
| 3 | Model automatu: 3× 6809, mapa pamięci, banki ROM, przerwania (NMI/FIRQ/IRQ), zatrzask dźwięku (`xain.c`) | ✅ obraz i RAM jak w MAME do pierwszego wyścigu CPU A/B (klatka ~2850–3050 `play1`) |
| 3a | MCU 68705P5 (`m6805.c`, `mcu68705.c`: porty, licznik, interfejs Taito), test `tests/trace6805.c` | ✅ 157 132 instrukcje, 124 przerwania, 0 różnic |
| 4 | ROM-y: katalog `ROM/xsleena` lub zip MAME (`romload.c` z Vastar, nazwy zastępcze `pb-0.ic24\|pb-01.ic24`) | ✅ |
| 5 | Obraz: tekst 8×8, 2 tła 16×16 z przewijaniem, sprite'y, 8 trybów priorytetu, paleta 512 × 12 bit (`video.c`, renderer wzorcowy linia po linii) | ✅ 62 ze 101 klatek `play1` identyczne co do piksela (wszystkie do 3050); później różnice tylko z rozjazdu logiki |
| 6 | Dźwięk: 2× YM2203 (FM 3 kanały × 4 operatory + SSG), liczby całkowite (`ym2203.c`: port ymfm z MAME) | ✅ `play1` 5–48 s: obwiednia skorelowana z MAME w 0,996, przesunięcie 0 ms, poziom 96–99%, spektrogramy zgodne; ⏳ odsłuch przez użytkownika |
| 7 | Wersja na PC (SDL2, `main_sdl.c`): od tytułu do game over, zgodna ze wzorcem | ✅ obraz i dźwięk; ok. 1,4 ms na klatkę; ⏳ gra przez użytkownika |
| 8 | Wersja na Amigę (`main_amiga.c` z Vastar) | ✅ działa na A1200, wynik identyczny z PC co do bajtu (rozdział 11); 2,3 kl./s przy 68060/50 – punkt wyjścia fazy 4; ⏳ joystick i dźwięk do oceny po przyspieszeniu |
| 9 | Optymalizacja: rdzeń 6809 w asm 68k (generator wzorem `gen_z80_68k.py`), renderer przyrostowy, FM | ⏳ 2,3 → 13–17 kl./s z dźwiękiem, ok. 20 bez (rozdział 12); rdzeń asm: Lockstep 5000 klatek identyczny |
| 10 | Wydanie: ikona, Tool Types, opisy PL/EN | ⏳ |

## 2. Materiały (skąd co jest)

| Co | Gdzie |
|---|---|
| Rdzeń FPGA MiSTer (RndMnkIII, odtworzony ze schematów bramka po bramce) | `rtl/` (`XSleenaCore*.sv`), `releases/*.rbf`, `.mra` |
| Sterownik MAME (wersja ze zmianami autora rdzenia: mapa `bootleg_map` z komentarzami) | `doc/xain.cpp`, `doc/xain_v.cpp`, `doc/xain.h` |
| Format sprite'ów, przebiegi czasowe, zrzuty z PCB | `doc/sprite_format.xlsx`, `doc/video_timings.txt`, `doc/real_pcb_captures/`, `doc/captures/` |
| Karty katalogowe: MC6809E, MC68A09, YM2203, MC68705P5, TTL | `doc/*.pdf` |
| Instrukcja automatu (Solar Warrior) | `doc/SolarWarrior.man.pdf` |
| Zrzuty RAM obrazu do symulacji (tytuł, pustynia, dżungla, rekordy) | `xs_*_vmem.txt` w katalogu głównym |
| Rdzeń YM2203 w Verilogu (JT12/jt03, jotego) – specyfikacja FM i SSG | `rtl/jt12/` |
| ROM-y | `ROM/xsleena/`: **zestaw MAME `xsleena` (World), komplet**, wszystkie CRC zgodne z MAME (`pb-01.ic24` = `pb-0.ic24`, CRC `83c00dd8`), z kodem MCU `pz-0.113` i `bootstrap.bin` (wewnętrzny ROM startowy 68705P5, 115 B, niepotrzebny do gry). ROM-ów bootlegów (`xs87b-10.7d`, `xs87b-11.7c`, `1.rom`) **nie ma** |
| **MAME 0.289** (wzorzec; oficjalne wydanie x64, SHA256 sprawdzone) | `C:\amiga-dev\mame\mame.exe`; ROM-y w `C:\amiga-dev\mame\roms\xsleena` (kopia `ROM/xsleena` + `pb-0.ic24`, bo MAME w katalogu szuka po nazwie). `-verifyroms xsleena`: good. Przebieg bez obrazu: `mame xsleena -video none -sound none -nothrottle -seconds_to_run 40 -snapshot_directory DIR` → demo w dżungli, ok. 560% tempa |
| Emulator 68705 z MAME (`m6805.cpp`, `m68705.cpp`, `6805ops.hxx`) i VHDL 6805 | `doc/MAME_MC68705P5/`, `doc/6805.vhd` |

## 3. Sprzęt źródłowy

Zegar główny 12 MHz.

| Element | Opis |
|---|---|
| CPU A (główny) | HD68B09EP, **1,5 MHz** (12/8). Logika gry, wejścia, obraz, paleta |
| CPU B (pomocniczy) | HD68B09EP, **1,5 MHz**. Dzieli z A 8 KB RAM (`0000-1FFF`) |
| CPU dźwięku | HD68A09P, 1,5 MHz (MAME: MC6809 z zegarem 6 MHz, wewnętrznie /4) |
| MCU | MC68705P5 (3 MHz) – tylko w zestawach oryginalnych (ochrona); **bootlegi `xsleenab`/`xsleenaba` go nie mają** |
| Dźwięk | 2× YM2203 (OPN) 3 MHz: każdy 3 kanały FM (4 operatory) + SSG (3 kanały prostokątne, szum, obwiednia; zgodny z AY-3-8910). Przerwanie YM #1 → FIRQ CPU dźwięku. Mono |
| Obraz | piksel 6 MHz, 384 × 272 całkowicie, **256 × 240 widoczne** (linie 8–247), **57,44 Hz** (zmierzone na PCB 57,41 Hz), 15,62 kHz |
| Paleta | 512 kolorów × 12 bit (xBGR 444): `3C00-3DFF` R i G, `3E00-3FFF` B. Klasy: 0–127 tekst, 128–255 sprite'y, 256–383 BG1, 384–511 BG2 |

### Mapa pamięci

**CPU A** (bootleg, `bootleg_map`):

| Adres | Zapis | Odczyt |
|---|---|---|
| `0000-1FFF` | RAM wspólny z CPU B | |
| `2000-27FF` | RAM tekstu (MAP): 32×32 kafelki 8×8; `+400` atrybut: bity 0–1 nr kafelka 9–8, bity 5–7 kolor | |
| `2800-2FFF` | RAM tła BG1 (BACK1), 32×32 kafelki 16×16 (512×512 px); `+400` atrybut: bity 0–2 nr kafelka, 4–6 kolor, 7 odbicie X | |
| `3000-37FF` | RAM tła BG2 (BACK2), jak BG1 | |
| `3800-39FF` | RAM sprite'ów: po 4 bajty (Y, atrybut, kafelek, X). MAME rysuje 96 (`0x180` B), RTL 128 (`0x200` B) – do sprawdzenia | |
| `3A00-3A01` | przewinięcie X BG1 | `3A00` P1, `3A01` P2 + monety |
| `3A02-3A03` | przewinięcie Y BG1 | `3A02` DSW1, `3A03` DSW2 |
| `3A04-3A05` | przewinięcie X BG2 | `3A05`: bit 5 VBLANK, bity 3–4 stan MCU, bit 2 moneta 3 |
| `3A06-3A07` | przewinięcie Y BG2 | |
| `3A08` | zatrzask dźwięku (→ IRQ CPU dźwięku) | |
| `3A09` / `3A0A` / `3A0B` | kasowanie NMI / FIRQ / IRQ CPU A | |
| `3A0C` | IRQ dla CPU B | |
| `3A0D` | odwrócenie ekranu | |
| `3A0F` | bity 0–2 tryb priorytetu, bit 3 bank ROM `4000-7FFF` | |
| `3C00-3FFF` | paleta | |
| `4000-7FFF` | | ROM w bankach (2 × 16 KB) |
| `8000-FFFF` | | ROM |

W zestawach z MCU dochodzą `3A04`/`3A06` (odczyt) i `3A0E` (zapis) – komunikacja z 68705.

**CPU B:** `0000-1FFF` RAM wspólny, `2000` zapis = IRQ dla CPU A, `2800` zapis = kasowanie
własnego IRQ, `3000` bit 0 = bank ROM `4000-7FFF`, `8000-FFFF` ROM.

**CPU dźwięku:** `0000-07FF` RAM, `1000` odczyt zatrzasku, `2800-2801` YM #1, `3000-3001` YM #2,
`4000-FFFF` ROM.

### Przerwania i synchronizacja

- CPU A, **NMI**: linia 248 (początek VBLANK), zatrzaskiwane, kasowane zapisem `3A09`.
- CPU A, **FIRQ** (sygnał IMS): przy każdym przejściu bitu 3 licznika linii z 0 na 1, czyli co
  16 linii; kasowane zapisem `3A0A`.
- CPU A ↔ CPU B: wzajemne IRQ (`3A0C` → B, `2000` → A) i wspólny RAM. MAME synchronizuje
  procesory po każdej instrukcji CPU A (`set_perfect_quantum`) – **do zmierzenia**, jaki
  przeplot wystarczy (instrukcja / linia / 16 linii). Ma to duży wpływ na wydajność na Amidze.
- Bit VBLANK (`3A05`) jest w MAME podnoszony linię wcześniej: gra w `A26B` czeka na niego
  w ciasnej pętli, a NMI na początku gry kończy się po opadnięciu VBLANK. Trzeba to odtworzyć.
- Licznik linii: `08…FF`, potem `E8…FF` (272 linie), VBLANK od `F8`.

### Obraz

- **Warstwy:** tekst (8×8, bez przewijania), BG1 i BG2 (16×16, mapa 32×32 kafelki, przewijanie
  X/Y o pełne 9 bitów), sprite'y 16×16 (opcjonalnie podwójna wysokość, odbicie X).
  Kolor 0 przezroczysty, 16 kolorów na kafelek.
- **Priorytet** (PROM `pt-0.ic59`, rejestr `3A0F`): 8 trybów, np. tryb 0: tekst > sprite'y > BG1
  > BG2; tryb 2: BG1 > sprite'y > BG2 > tekst. Pełna lista w `doc/xain_v.cpp`. W porcie: tabela
  z PROM-u (wierniej) albo 8 kolejności rysowania jak w MAME.
- **Grafika:** tekst 32 KB (4 bpp), BG1 256 KB (2048 kafelków 16×16), BG2 192 KB (z dziurą),
  sprite'y 256 KB (2048 × 16×16). Razem ok. 740 KB po rozpakowaniu do 4 bpp, w formacie 8 bpp
  ok. 1,5 MB. Mieści się w Fast RAM A1200.
- **Kolory:** MAME przelicza 4 bity liniowo (×17). RTL używa krzywej z symulacji drabinki
  rezystorów (`XSleenaCore_RGB4bitLUT.sv`). Do porównania i wyboru.

## 4. Decyzja: sposób B – emulacja sprzętu (jak Vastar)

**Uzasadnienie:**
- nie ma disassembly ani symboli, a kodu jest dużo: 2 × 48 KB ROM programu (CPU A i B) + 48 KB
  dźwięku; przepisanie na C (sposób A) to wielomiesięczna analiza bez wzorca procedur;
- trzy procesory komunikują się przez wspólny RAM i przerwania, a gra zależy od czasów
  (pętla na VBLANK, FIRQ co 16 linii, ograniczona moc CPU – MiSTer ma opcję „Turbo” właśnie
  dlatego, że oryginał zwalnia);
- jest bardzo dobra specyfikacja: sterownik MAME i rdzeń FPGA odtworzony ze schematów;
- 6809 na 1,5 MHz to mała moc do emulacji: łącznie 4,5 MHz 6809, czyli ok. 1–1,2 mln instrukcji
  na sekundę (średnio ok. 4 cykle na instrukcję).

**Zestaw ROM-ów:** **`xsleena` (World)**, bo tylko on jest w `ROM/xsleena/`. Wymaga emulacji
MCU **MC68705P5** (2 KB kodu `pz-0.113`, zegar 3 MHz, czyli 0,75 mln cykli/s; układ ochrony
Taito, który wymienia dane z CPU A przez `3A04`/`3A06`/`3A0E` i flagi semaforów w `3A05`).
To mały koszt: rdzeń 6805 jest prosty, a MAME ma gotowy wzorzec (`doc/MAME_MC68705P5/`,
`taito68705_mcu_device`). Ten sam model obsłuży `xsleenaj` i `solrwarr`. Bootlegi `xsleenab`/
`xsleenaba` (bez MCU, mapa `bootleg_map`) jako opcja, jeśli użytkownik je dostarczy.

Mapowanie ROM-ów CPU A w `xsleena`: `p9-08.ic66` → `8000-FFFF`; `pa-09.ic65` pierwsze 16 KB →
bank 0 (`4000-7FFF`), drugie 16 KB → bank 1. CPU B tak samo z `p1-0.ic29` i `p0-0.ic15`.

**Elementy do napisania / skopiowania:**

| Element | Skąd |
|---|---|
| Rdzeń 6809 w C (wzorcowy) | nowy, według karty MC6809E i MAME `m6809`; testy instrukcji + ślad z MAME (`trace`) instrukcja po instrukcji |
| Rdzeń 6809 w asm 68k | nowy generator wzorem `Wastar/tools/gen_z80_68k.py`; weryfikacja lockstep z rdzeniem C |
| YM2203 FM | nowy, w liczbach całkowitych, według ymfm (MAME, BSD) i `rtl/jt12` |
| YM2203 SSG | `Wastar/port/src/ay8910.c` (SSG = AY z zegarem 3 MHz / 2; do potwierdzenia) |
| `romload.c`, `main_sdl.c`, `main_amiga.c`, `amiga_remote.py`, `make_icon.py`, `icon_tooltypes.py` | Vastar, dopasować |

## 5. Tempo i system TV

- Automat chodzi z prędkością **57,44 Hz** i nie ma wersji PAL, więc logika (3 CPU i dźwięk) chodzi
  w tempie oryginału: `timer.device` na Amidze, licznik czasu na PC.
- Obraz dla ekranu PAL 50 Hz albo okna na RTG: pomijanie klatek (`FRAMESKIP=AUTO`), logika
  i dźwięk zawsze w pełnym tempie.
- Wysokość obrazu 240 linii mieści się w PAL 256 linii bez skalowania (pełny ekran PAL lowres
  320×256: później, jeśli w ogóle; 512 kolorów palety trzeba by zmieścić w 256 kolorach AGA).
- Dźwięk: Paula z okresem 161 (22 030 Hz) albo AHI z pierścieniem buforów. FM YM2203 liczy
  natywnie 3 MHz / 72 = 41,7 kHz – do przeliczenia na 22 030 Hz (albo liczenie FM od razu
  w niższej częstotliwości, do odsłuchu).

## 6. Ryzyka wydajności (do zmierzenia, nie zgadywać)

1. **FM:** 6 kanałów × 4 operatory na każdą próbkę to prawdopodobnie największy koszt na 68060.
   Możliwe: niższa częstotliwość FM, pomijanie wyciszonych kanałów i operatorów, obwiednie
   partiami, asm.
2. **Przeplot CPU A/B:** jeśli gra wymaga synchronizacji co instrukcję, to rdzeń trzeba
   przełączać bardzo często. Najpierw zmierzyć na PC najmniejszy przeplot, który daje grę
   identyczną z przebiegiem wzorcowym.
3. **Obraz:** 2 przewijane warstwy w obu osiach + tekst + do 96/128 sprite'ów (w tym podwójnej
   wysokości). Renderer przyrostowy w blokach 8×8 jak w Vastar, składanie całymi wierszami
   przy przewijaniu.
4. **Pętle oczekiwania** (CPU A na VBLANK w `A26B`, CPU B i dźwięk na przerwania): wykryć
   i przeskakiwać czas.

Dla porównania: w Vastar 2× Z80 3 MHz + AY na 68060/50 zajmowały ok. 7 ms + 6 ms na klatkę.

## 7. Sterowanie (plan)

Joystick 8 kierunków, 2 przyciski (strzał, skok): pad CD32 albo joystick z drugim przyciskiem;
klawiatura: strzałki, Ctrl = strzał, Alt = skok, 5 = moneta, 1/2 = start, P = pauza, Esc = wyjście.
DIP-y jak w MAME (domyślnie `DSW1=3F`, `DSW2=FF` z `.mra`), opcje `LIVES=`, Free Play, start
joystickiem – jak w Vastar.

## 8. Wzorzec z MAME (faza 1)

**`tools/mame_capture.py`** uruchamia MAME 0.289 (`C:\amiga-dev\mame`) bez obrazu, z wygenerowanym
skryptem Lua (`tools/mame_capture.lua` wklejany do `capture_run.lua`; MAME uruchamia skrypt
startowy we własnym środowisku, więc `dofile` nie widzi konfiguracji).

```
python tools/mame_capture.py --out build/ref/play1 --scenario play1 --shots 0-5000/50 --wav build/ref/play1/play1.wav
python tools/mame_capture.py --out build/ref/t --scenario play1 --trace maincpu:3500:3510 --trace sub:3500:3510 --trace audiocpu:3500:3510 --trace mcu:mcu:3500:3510
python tools/snapinfo.py build/ref/play1/frame_03500.bin [--hex share1:0:100]
python tools/trace_stats.py build/ref/t/trace_*.txt --frames 10
```

- **Klatka N** = stan po N pełnych klatkach (powiadomienie końca klatki). Wejścia ze skryptu
  (`"1200:+coin1,1206:-coin1"`, nazwy: `coin1 coin2 start1 start2 up down left right fire jump`,
  `p2...`) ustawiane są na końcu podanej klatki. Port na PC ma odtwarzać te same skrypty.
- **Zrzut** `frame_NNNNN.png` (256×240, kolory MAME: 4 bity × 17) + `frame_NNNNN.bin`: `"XSNP"`
  i fragmenty (nazwa 16 B, rozmiar u32 big endian, dane): `share1` (8 KB RAM wspólny), `charram`,
  `bgram0` (BG2, `3000`), `bgram1` (BG1, `2800`), `spriteram` (384 B), `palette`, `palette_ext`,
  `sound_ram` (2 KB), `mcu_ram` (128 B przestrzeni 68705), `io_main` (ostatnie zapisy
  `3A00-3A0F`: przewinięcia, zatrzask, priorytet/bank), `sub_bank`, `ym1`/`ym2` (rejestry YM2203),
  `regs` (tekst: rejestry 4 procesorów). Przewinięcia, priorytet, bank i YM2203 nie dają się
  odczytać, więc Lua łapie zapisy (`install_write_tap`). Odczyt: `read_snapshot()`.
- **Ślad** `trace_<cpu>.txt`: każda instrukcja z rejestrami **przed** jej wykonaniem i licznikiem
  cykli `T=` (debugger MAME, `-debug -debugger none`). `tracelog` liczy rejestry CPU widocznego
  w debuggerze, więc jeden śledzony CPU na przebieg MAME; przy kilku `--trace` skrypt uruchamia MAME
  kilka razy (przebiegi są powtarzalne). Do porównania z rdzeniem 6809 instrukcja po instrukcji.
- **WAV:** `-wavwrite`, 48 kHz mono 16 bit.
- Szybkość: ok. 470–560% tempa bez śladu, ok. 380% ze śladem.

**Scenariusz `play1`** (`SCENARIOS` w `mame_capture.py`): moneta 1200, start 1300, wybór planety
ogniem 1700, od 1800 bieg w prawo, strzał co 40 klatek, skok co 120, do 4500. Przebieg: test/demo,
tytuł z „PLEASE INSERT COIN” (600), „CREDIT 1” (1250), misja (1400), wybór planety (1750–2500),
przelot statku (3000), dżungla z przewijaniem, punktami i wrogami (3500–4500), utrata życia
i „GET READY!” (5000). Wynik: `build/ref/play1/` (101 zrzutów co 50 klatek + `play1.wav`, 87 s).

**Obciążenie procesorów** (rozgrywka, klatki 3500–3510, `trace_stats.py`):

| CPU | instr./klatkę | cykli/instr. | pętle oczekiwania |
|---|---|---|---|
| główny 6809 | 6587 | 3,96 | ok. 49%: `8727-872B` i `8749-8752` (czeka na zmienne z przerwań; w drugiej pętli zapis `3A09`) |
| pomocniczy 6809 | 6226 | 4,19 | 55%: `8014 JMP $8014` (czeka na IRQ) |
| dźwiękowy 6809 | 6058 | 4,31 | ok. 9%: pętla opóźnienia `8810 DECA / BNE` |
| MCU 68705 | 2621 | 4,98 | ok. 94%: `096-0A0` |

Razem ok. 21,5 tys. instrukcji na klatkę (1,24 mln/s), bez pętli oczekiwania ok. 12 tys.
(0,7 mln/s). Liczniki cykli zgadzają się z zegarami: 26 113 cykli/klatkę dla 6809 (1,5 MHz /
57,44 Hz), 13 055 dla MCU (0,75 MHz).

W demo (klatka 1400) główny CPU czeka na VBLANK w `A25D-A262` (`LDB $3A05 / ANDB #$20 / BNE`).

## 9. Otwarte sprawy

- Zegar 68060 na A1200 (Warp 1260X, `SYS:C/WarpTool`): 11.10.2026 `WarpTool Diag` pokazuje
  **50 MHz**. Zmiana: `WarpTool CPUFreqHz=100M`, **nie więcej niż 100 MHz** (powyżej to
  podkręcanie, Amiga może się zawiesić). L2 karty: 96 KB, 6-drożna, linie 128 B (`WarpTool L2Info`).
  Pomiary wydajności podawać z zegarem; cel główny pozostaje 50 MHz.
- Liczba sprite'ów: 96 (MAME) czy 128 (RTL).
- Przeliczenie kolorów: liniowe (MAME) czy krzywa z RTL.
- Testy procesora 6809 (odpowiednik zexall dla Z80): podstawą będzie porównanie z śladami MAME
  instrukcja po instrukcji (wszystkie 4 procesory, z licznikiem cykli), uzupełnione własnymi
  testami instrukcji (flagi, tryby adresowania, DAA, przerwania) w fazie 2.

## 10. Port na PC (faza 2): budowa, weryfikacja, ustalenia

**Budowa:** `sh tools/mk.sh` (MSYS2 UCRT64, `make` w `port/`) → `build/port/xainsleena.exe`,
`trace6809.exe`, `trace6805.exe`. Kod: `m6809.c`, `m6805.c`, `mcu68705.c`, `ym2203.c`, `xain.c`,
`video.c`, `romload.c` (C99, bez FPU i 64 bitów) + `main_sdl.c`.

```
build\port\xainsleena.exe -rompath ROM                         # gra (okno, klawiatura, pad)
build\port\xainsleena.exe -headless -rompath ROM -frames 5001 -shots DIR 0-5000/50 -dump DIR 0-5000/50 -script "..."
python tools/compare_frames.py build/ref/play1 DIR [--montage OUT.png 1400,3500]
python tools/compare_dumps.py build/ref/play1 DIR [--detail FRAME]
build\port\trace6809.exe main|sub|sound ROM\xsleena build\ref\...\trace_maincpu.txt
build\port\trace6805.exe ROM\xsleena build\ref\...\trace_mcu_mcu.txt
```

**Testy procesorów (lockstep ze śladem MAME):** test ustawia rejestry z pierwszego wiersza śladu
i wykonuje instrukcję po instrukcji; przed każdą porównuje wszystkie rejestry i licznik cykli `T=`.
ROM czyta z plików, a każdy inny odczyt bierze z dziennika dostępów MAME (`trace_*_mem.txt`,
podsłuch Lua na całej przestrzeni), więc wejścia/wyjścia i RAM zmieniany przez inne procesory są
dokładnie takie jak w MAME; zapisy muszą się zgadzać z dziennikiem po kolei. Przerwania: wiersze
`(interrupted at XXXX, IRQ n)` śladu (6809: IRQ 0 = IRQ, 1 = FIRQ, 64 = NMI; 6805: 0 = /INT,
1 = licznik). Dziennik zawiera też odczyty debuggera (deasemblacja śladu), dlatego odczyty są
szukane po adresie, a początek dziennika wyrównany do pierwszego odczytu adresu pierwszej
instrukcji z bieżącym PC równym temu adresowi (MAME potrafi zacząć ślad w środku instrukcji).
Wynik: 81 okien (demo i `play1` co 200 klatek) dla 3× 6809 i 27 dla MCU, **0 różnic**. Pokrycie:
98 mnemoników 6809 i 82 formy adresowania (`tools/trace_coverage.py`); gra nie użyła w tych oknach
m.in. `EXG`, `CWAI`, `SYNC`, `SWI`, skoków ze znakiem (`BGE`…), `ASR`/`ROL` w pamięci.

**Ustalenia o czasie (MAME):**
- Czas zero (włączenie) i zrzut „klatki N” w MAME wypadają na **linii 248** (początek VBLANK):
  w śladzie NMI (linia 240) jest 264 linie po starcie. Klatka portu biegnie więc od linii 248 do
  247, a linia 247 jest rysowana na końcu klatki (jak `update_partial` przed powiadomieniem).
  Klatka N = stan po N+1 klatkach.
- Wejścia ustawione w Lua w klatce N działają od klatki N+1 (MAME przelicza porty przed
  wywołaniem skryptu); `-script` portu ma to samo opóźnienie, więc skrypty są wymienne.
- FIRQ (IMS) co 16 linii przy przejściu bitu 3 licznika `vcount` z 0 na 1 (16 na klatkę), NMI na
  `vcount` F8, bit VBLANK od `vcount` 247.
- Kwant przeplotu procesorów (`-slice`, domyślnie 24 cykle) nie zmienia wyniku `play1` (próby 2–96).

**Znana różnica: wyścig CPU A/B o wspólny RAM.** Licznik `1C72` zmniejsza CPU A (`DD27 DEC A,U`)
i zwiększa CPU B (`4BE5 INC A,Y`), wiele razy na klatkę. MAME przeplata procesory co cykl
magistrali (`set_perfect_quantum`, wstrzymanie w środku instrukcji), więc czasem traci jedną
zmianę (odczyt jednego CPU między odczytem a zapisem drugiego); port wykonuje instrukcję w całości.
W `play1` po ok. 2850 klatkach licznik ma 16 (MAME) zamiast 17 i dalej gra toczy się inaczej
(inny wynik, inny moment śmierci), choć obraz i zachowanie są poprawne. Wierne odtworzenie
wymagałoby rdzenia wstrzymywanego w środku instrukcji (tryb tylko na PC, do rozważenia); na
prawdziwym automacie wynik takiego wyścigu też zależy od fazy zegarów.

**Dźwięk (`ym2203.c`):** przeniesienie ymfm (MAME 0.289, BSD) do C99 na liczbach całkowitych:
operatory (tablice sinusa i potęg z układu, obwiednia ADSR z dzielnikiem 3, SSG-EG, detune, KSR),
8 algorytmów, sprzężenie zwrotne operatora 1, tryb 3 częstotliwości i CSM kanału 3, przetwornik
10.3 (`roundtrip_fp`), SSG (3 tony, szum, obwiednia, tablica amplitud ymfm). FM: próbka co 72 takty
(41 667 Hz), SSG: krok co 16 taktów, uśredniany na próbkę FM. Miks jak w sterowniku MAME:
SSG A/B/C × 0,50, FM × 0,40 (wyjścia ymfm w MAME: 0–2 = SSG, 3 = FM). Oba układy piszą do
pierścieni próbek w `Xain`; `xain_take_audio` przelicza je liniowo na częstotliwość wyjścia
(PC: 44 100 Hz, `-rate`). Porównanie: `-wav PLIK -rate 48000` i
`python tools/compare_wav.py build/ref/play1/play1.wav PLIK --from 5 --to 48 --png spec.png`.

## 11. Amiga (faza 3)

**Budowanie:** `sh tools/mk.sh -f Makefile.amiga` (vbcc `+aos68k_win`, `-cpu=68060 -O3 -speed`) →
`build/amiga/XainSleena` (gra) i `build/amiga/XainTest` (przebieg bez okna do porównań).
Na A1200: `Work:XainSleena/XainSleena`, ROM-y w `Work:XainSleena/rom/xsleena` (szukane też w
`ROMPATH`, `PROGDIR:`, `PROGDIR:rom`, `PROGDIR:roms`, `PROGDIR:ROM`; zip, szuflada lub pliki).

**`main_amiga.c`** (na wzór Vastar): okno Workbencha albo `SCREEN=FULL`, Picasso96 `BLIT`
(domyślnie na 16 bitach: zmienione bloki 8×8 przeliczane przez tablicę 512 kolorów do bufora
w Fast RAM, do VRAM tylko zapisy `copy_rows`, jeden blit) lub `WPA`, bez RTG / na 8 bitach `WCP`
z piórami (kolory przybliżone). Zmiana palety → nowa tablica kolorów i całe okno od nowa.
Zmienione bloki: porównanie bufora renderera (indeksy 9-bitowe) z kopią tego, co pokazuje okno.
Tempo 57,44 Hz z `timer.device`, `FRAMESKIP=AUTO` (klatka nierysowana nie jest też renderowana),
Paula z okresem 161 (22 030 Hz PAL) albo AHI, joystick/pad CD32 przez `lowlevel.library` raz na
klatkę, Tool Types, `STATS`/`STATS=FULL`, `VERIFY`, strażnik zawieszeń (`RAM:XainSleena-hang.txt`),
Ctrl-C. Wszystko w Fast RAM, w Chip RAM tylko bufory Pauli i pusty wskaźnik myszy.

**Zgodność Amiga = PC:** `XainTest ROMDIR OUTDIR 1400 100,600,1250,1399 SKRYPT` na A1200 i
`build/port/xain_test.exe` na PC (moneta 1200, start 1300) dają identyczne co do bajtu zrzuty
ekranu, pamięci i dźwięk (1 MB, 22 050 Hz) – 11.10.2026.

**Pierwszy pomiar (A1200, 68060/50, okno 16 bit, `SCALE=2`, demo):**

| Wariant | kl./s | CPU | Uwagi |
|---|---|---|---|
| `STATS=FULL` (Paula) | 2,3 | 96% | emu+render 94%, wysyłanie 2% |
| `NOSOUND` | 2,6 | 95% | synteza YM2203 ≈ 12% czasu |
| `NOSOUND FRAMESKIP=5` | 4,4 | 96% | stąd: renderer ≈ 190 ms/kl., emulacja ≈ 195 ms/kl. |
| `XainTest` (bez okna) | 2,3 | – | 1300 klatek w 568 s |

Do pełnego tempa (17,4 ms na klatkę) brakuje ok. 23×. Sterowania i dźwięku nie da się ocenić przy
2 kl./s – sprawdzenie po przyspieszeniu w fazie 4.

## 12. Optymalizacja (faza 4)

Zmiany (każda sprawdzona porównaniem z wolną ścieżką):

- **Pomijanie pętli oczekiwania (dokładne):** w obrębie przebiegu CPU (96 cykli) nic poza CPU się
  nie zmienia, więc gdy po jednym obrocie pętli rejestry są takie same, można przeskoczyć całe
  obroty. Pętle: główny `8727`, `8749`, `A25D`, `A268`; pomocniczy `8014`, `4B91`; MCU `096`
  (warunki: `$3C`=`$28`=0, Z=1 N=0, do najbliższego zdarzenia licznika). Wynik identyczny co do
  bajtu z przebiegiem bez pomijania (`XAIN_NOIDLE`).
- **Renderer szybki** (cała klatka na koniec, odcinki kafelków + typy wierszy): 0 różnic względem
  wzorcowego wywołanego na koniec klatki (`XAIN_CHECK`).
- **YM2203:** SSG liczony partiami do najbliższego „istotnego” zdarzenia (suma kontrolna dźwięku
  3000 klatek demo bez zmian, `04455656`); wyłączone operatory pomijane; `FMDIV=2` (domyślnie na
  Amidze) – wyjście FM co drugą próbkę, fazy i obwiednie w pełnym tempie.
- **Rdzeń 6809 w asemblerze 68k** (`tools/gen_6809_68k.py` → `port/src/m6809_68k.s`): A, B, X, CC
  i licznik cykli w rejestrach 68k, flagi z CCR (H osobno), skok przez tablicę, strony pamięci
  przez tablice `rbase`/`wbase` (ten sam układ struktury co rdzeń C, sprawdzany
  `m6809_layout_ok()`). Rzadkie instrukcje, przerwania i pętle oczekiwania przez rdzeń C.
  Weryfikacja: `Lockstep` (dwie maszyny, rdzeń C i asm, porównanie rejestrów, cykli i wszystkich
  pamięci po każdej klatce). `CPU=C` wybiera rdzeń C.

**Koszty po stronie CPU (sonda `build/probe/cpustat.c`, na klatkę, demo / rozgrywka):** główny
2658 / 4238 instrukcji, pomocniczy 544 / 3060, dźwiękowy 6048 / 6213; wywołań C przez rzadkie
kody (`TFR`, `RTI`) 18–65; dojść do pętli oczekiwania ok. 970 (główny 426, pomocniczy 544) –
każde było wywołaniem C (`idle_check`). Główny czyta `3A05` 1472 razy na klatkę (pętle czekania
na VBLANK `A25D`/`A268`: w każdym przebiegu 2–3 obroty przed i po pominięciu, każdy z odczytem
przez funkcję C).

- **MCU:** pominięcie pętli oczekiwania z każdej z jej 4 instrukcji, razem z częścią ostatniego
  obrotu (PC, cykle, licznik liczone, bez wykonywania), `div20` zamiast dzieleń, kod pętli
  sprawdzany raz: kroków rdzenia C 1178 → 142 na klatkę (wynik identyczny z `XAIN_NOIDLE`,
  4500 klatek obrazu i dźwięku). Na A1200 MCU 18% → 7%.
- **Pętle oczekiwania 6809 w asemblerze** (`idle_hit` w `gen_6809_68k.py`): te same reguły co
  `idle_check` w C (kod pętli, brak przerwań, rejestry jak przy poprzednim dojściu, pominięcie
  `(icount-1)/obrót` obrotów), bez wywołania C. Pomocniczy 15% → 7%.

| Wersja (A1200 060/50, okno 16 bit, `STATS=FULL NOSOUND`, demo) | kl./s | Podział (A/B/S/YM/MCU, render, present) |
|---|---|---|
| rdzeń asm, przed zmianami poniżej | 14,8 | 19/14/12/5/18, 14, 5 |
| MCU | 16,3 | 20/15/13/6/7, 14, 12 |
| pętle oczekiwania 6809 w asm | 20,5 | 19/7/16/7/9, 18, 7 |
| to samo z dźwiękiem (Paula, FMDIV=2, `STATS`) | 13,1–17,1 | było 6,0; YM2203 ok. 24% |
| renderer: linie w asm, listy kafelków | 19,0–25,6 | render 14–18% → 6–9%; z dźwiękiem 14,4 |

**Przy 68060/100 MHz** (`WarpTool CPUFreqHz=100M`, ta sama wersja): bez dźwięku 28,8–34,1 kl./s
(wysyłanie obrazu przy przewijaniu do 26%), z dźwiękiem 21,3–22,1 kl./s.

**Wysyłanie obrazu (BLIT, 16 bit, `SCALE=2`)** – czasy na narysowaną klatkę z `STATS=FULL`
(`[us c v w b]`: porównanie, przeliczenie, zapis do VRAM, blit); `REDRAW` wysyła cały obraz:

| Wersja (A1200 060/50, `REDRAW`) | c | v | w | b |
|---|---|---|---|---|
| C: porównanie z kopią `shown`, `convert_rect`, `CopyMem`, długie słowa | 17,6 ms | 20,7 | 28,4 | 1,2 |
| MOVE16 (po `CacheClearE`) | 17,4 | 24,5 | 16,2 | 1,2 |
| porównanie w asm bez kopiowania (dwa bufory na zmianę), przeliczenie w asm | 0,3 (porównanie ok. 5 ms bez `REDRAW`) | 23,7 | 16,1 | 1,2 |
| wiersz przeliczany raz (podwojony w poziomie), MOVE16 dwa razy | 0,3 | 17,5 (w tym 4,3 opróżnianie cache) | 16,2 | 1,2 |
| to samo, `SCALE=1` | 0,2 | 15,2 | 4,7 | 0,5 |

Przy przewijaniu zmienia się ok. 70% bloków (licznik `blocks` jest średnią ze wszystkich klatek,
też nierysowanych). Ograniczenia: odczyt Fast RAM (ok. 50 MB/s – porównanie i przeliczenie),
zapis do VRAM (długie słowa ok. 17 MB/s, MOVE16 ok. 30 MB/s); `SCALE=2` to 4× więcej danych.
`VERIFY`: 0 złych bloków. Opcje: `MOVE16=OFF`, `REDRAW` (pomiary).
Gra (demo): bez dźwięku 24,9–26,5 kl./s, z dźwiękiem 16,2–19,9 kl./s (`SCALE=2`); `SCALE=1`
bez dźwięku 25,9–26,7.

**Rdzeń 6809: TFR, EXG, RTI w asm; odczyt `3A05` bez wywołania C; bez dźwięku bez procesora
dźwiękowego (11.10.2026, 23:01):**
- `TFR` (6 cykli), `EXG` (8), `RTI` (6 / 15) w `gen_6809_68k.py` (`tfr_get`/`tfr_set` jak
  `tfr_read`/`tfr_write` w C, `RTI` przez wspólną część `PULS`); po nich sprawdzenie przerwań.
  W C zostały: `SYNC`, `CWAI`, `SWI*`, `DAA`, kody nieudokumentowane, reszta stron 10/11, wejście
  w przerwanie i I/O.
- `M6809.fast_addr/fast_val/fast_ok` (offset 12416): wartość `3A05` (VBLANK, moneta 3, flagi
  MCU) ustawiana przez maszynę przed każdym przebiegiem procesora głównego i po każdym jego
  dostępie do I/O przez C (`3A04`/`3A06`/`3A0E` zmieniają flagi MCU); `rd_slow` w asm zwraca
  ją bez wywołania C (ok. 1500 odczytów na klatkę w pętlach czekania na VBLANK). Rdzeń C wciąż
  woła funkcję obsługi, więc `Lockstep` porównuje obie drogi.
- `Xain.sound_cpu = 0` (Amiga: `NOSOUND`; `xain_test`: `XAIN_NOSNDCPU`): procesor dźwiękowy
  i YM2203 nie są emulowane – nic w automacie ich nie czyta (zatrzask dźwięku działa w jedną
  stronę). Zrzuty klatek 1600–4400 `play1` identyczne z wersją z procesorem dźwiękowym.
- Sprawdzenie: ślady MAME 0 różnic (C bez zmian), PC: klatki 1600–2900 jak poprzednio;
  A1200: `CpuTest6809` 30 000 pojedynczych (test nie losuje już PC > FFF0 – przejście przez
  FFFF/0000 to znane założenie rdzenia asm) i 10 000 ciągów bez różnic, `Lockstep` 5000 klatek
  identycznych.
- Pomiar (A1200 060/50, `SCALE=2`, demo): bez dźwięku **28,8–36,5 kl./s** (było 25–26,5), z
  dźwiękiem 16,5–20,3 (było 16,2–19,9; sam TFR/EXG/RTI/`3A05` to 1–2%).

**Szybkość rdzeni 6809 (`CpuBench [MLN_CYKLI]`):** pętla typowego kodu (kopiowanie ze zmianą,
arytmetyka 16-bit, `JSR`/`RTS`, `PSHS`/`PULS`, `TFR`, skoki; 3,54 cyklu na instrukcję) w płaskiej
pamięci 64 KB, bez pomijania pętli. A1200 060/50: rdzeń asm **5,38 MHz** emulowanego 6809
(1,52 mln instrukcji/s, ok. 33 cykle 68060 na instrukcję) = **3,6×** prawdziwy 6809 1,5 MHz
(jak w automacie), 5,4× przy 1 MHz; rdzeń C (vbcc) 0,49 MHz (11× wolniej niż asm); PC (gcc,
rdzeń C) 284 MHz. Automat to 3 × 1,5 MHz = 4,5 MHz – bez pomijania pętli oczekiwania rdzeń
asm zająłby ok. 84% czasu 68060/50.

Mikrotesty (`CpuBench`, 32 jednakowe instrukcje + `BRA`, rdzeń asm, 68060/50), cykle 68060 na
instrukcję 6809: `NOP` 14,7 (sam rozdział: `subq`/`ble`, pobranie kodu, tablica, `jmp (a0)`),
`INCA` 17,6, `ANDA #` 19,8, `ADDA #` 23,9 (flaga H), `BRA`/`BNE` 22 (sprawdzenie mapy pętli
oczekiwania), `LDA ext` 29,8, `STA ext` 30,1, `LDA <dp` 31,5, `LDD ext` 33,9, `LDA ,X` / `,X+` /
`5,X` ok. 50 (tryb indeksowy przez `jsr` do procedury z tablicy i `rts`), `TFR A,B` 80 (dwie
procedury przez tablice i `check_ints`). Wniosek: najwięcej kosztują skoki obliczane (rozdział
ok. 15 cykli na każdą instrukcję, tryb indeksowy kolejne ok. 20), dostęp do pamięci przez
tablicę stron ok. 12.

Renderer na A1200 sprawdzony `XAIN_CHECK` (porównanie z wzorcowym w każdej klatce): 0 różnic
w 3000 klatkach `play1`, sumy kontrolne klatek 1600–2900 identyczne z PC.

(Podział z `STATS=FULL`: części mierzone co 4. klatkę, bez kosztu samego odczytu czasu.)

**Renderer – pomiar osobno (`RenderBench ROMDIR [POWT [KLATKI...]]`):** maszyna do podanych
klatek (wejścia jak w `Lockstep`), każda rysowana `POWT` razy; czas i suma kontrolna obrazu (ta
sama na PC i Amidze, w C i asm). `XAIN_SKIP=n` pomija warstwy (bit 0 tekst, 1 BG1, 2 BG2,
3 sprite'y). Na A1200 (klatki 400/900/2000/2400):

| Wersja | ms na klatkę obrazu |
|---|---|
| renderer szybki w C (vbcc) | 29–34 |
| linie BG i tekstu w asm 68k, kafelki szukane w każdej linii | 19–22 |
| kafelki szukane raz na wiersz kafelków (listy `VTile` z C), tekst: tylko niepuste komórki; sprite'y: listy na linię, typy wierszy | 14,4–17,2 |

Podział (klatka 2000, tryb 0): BG2 (dolna, kryjąca) 8,7 ms, sprite'y 3,3, BG1 2,7, tekst 1,3,
reszta 0,7. Rysowanie wszystkich linii do jednej (bufor w pamięci podręcznej): 12 ms zamiast
15 – zapis bufora obrazu to ok. 3 ms, reszta głównie chybienia pamięci podręcznej przy czytaniu
grafiki kafelków (każda linia kafelka to nowa linia 16 B; ok. 4000 na warstwę na klatkę).
Próba z pikselami jako słowa (dwa piksele na długie słowo) była wolniejsza – dwa razy więcej
chybień. Linie w asm generuje `tools/gen_video_68k.py` (`video_68k.s`); `video_asm = 0` – C.
Sprawdzenie: sumy kontrolne `RenderBench`, `XAIN_CHECK` (porównanie z rendererem wzorcowym
w każdej klatce) na PC i A1200.

**YM2203 – pomiar osobno (`YmBench`):** `xain_test` z `XAIN_YMLOG=PLIK` zapisuje dziennik wywołań
YM2203 (zapisy rejestrów, `advance`), `YmBench PLIK [FMDIV [DEBUG [POWT [OD]]]]` odtwarza go na
dwóch układach z syntezą i podaje czas i sumę kontrolną próbek (ta sama na PC i Amidze).
`DEBUG`: bit 0 bez SSG, bit 1 bez FM, bit 2 wyjście FM w C zamiast asm. `OD` – klatki tylko
z licznikami (pomija ciche początki). Dziennik `play4500` (scenariusz `play1`, 4500 klatek),
pomiar od klatki 1400 (54 s dźwięku):

| Wersja (A1200 060/50, FMDIV=2) | % czasu rzeczywistego | Uwagi |
|---|---|---|
| wyjściowa (dziennik `play3000`, od 1400) | 460% | w tym SSG 64%, choć SSG milczy |
| SSG: cisza bez liczenia, bez dzieleń; FM: fazy leniwe, wyrównanie struktur | 228% (`play3000`), 269% (`play4500`) | `play4500`: bez SSG 177%, bez FM 112% |
| + `eg4`, wyjście kanałów wg algorytmu, trafność SSG w pamięci podręcznej; wyjście FM w C | 211% | |
| + wyjście FM w asm 68k | 190% | bez SSG 152%, bez FM i SSG (pętla, liczniki) 27% |

Z tego: FM ok. 125% (głównie obsługa każdej próbki i takty obwiedni – co 3 próbki wszystkie
12 operatorów), SSG 38% (gdy gra), pętla 27%. Do zrobienia: operator odwiedzany w takcie tylko,
gdy jego obwiednia się zmienia (z szybkości), pętla próbek w asm.

Zmiany (wszystkie dokładne – sumy kontrolne `ymbench` i dźwięk całej maszyny 4500 klatek
identyczne z wersją wyjściową, FMDIV 1 i 2):
- struktury YM2203 z polami 32-bit wyrównanymi do 4 (vbcc wyrównuje do 2, a nierówne długie
  słowa na 68060 są wolne); `roundtrip_fp` bez pętli; dzielenia w pętli próbek zastąpione
  przesunięciami / mnożeniem (`/5` jako `*52429>>18`, dokładne dla |x| ≤ 65536);
- przygotowanie (`prepare`) tylko kanałów, których rejestry zapisano (zapisy 0x27 timera –
  15 na klatkę – nie przeliczają operatorów);
- **fazy leniwe:** operator jest odwiedzany tylko w takcie obwiedni (co 3 próbki), przy
  przygotowaniu i (z SSG-EG) co próbkę; faza w próbce t = `phase + step*(t-pt)`;
- część obwiedni wyjścia operatora (`eg4`) liczona przy odwiedzinach, nie przy każdej próbce;
  wyjście kanału osobno dla każdego z 8 algorytmów (sumy bez nasycania – nie może wystąpić);
- **SSG:** kanał w trybie obwiedni z obwiednią zatrzymaną na 0 jest niesłyszalny; gdy żaden
  licznik nie może zmienić wyjścia (zwykła cisza), kroki są tylko liczone i wykonywane hurtem
  przed zapisem rejestru SSG (albo co 4096 kroków), wyjście stałe; `skip_counter` bez dzielenia
  dla okresu 0/1.
- wyjście FM (4 operatory, 8 algorytmów, sprzężenie) w asemblerze 68k (`ym2203_68k.s`, na
  Amidze domyślnie; offsety struktur sprawdzane `ym2203_layout_ok()`).

**Weryfikacja rdzenia asm (testy na A1200):**

- `CpuTest6809 [TESTY [ZIARNO [CYKLE]]]` (`tests/cputest6809.c`): losowa pamięć 64 KB, losowe
  rejestry, po kolei wszystkie kody stron 0, 10 i 11 z losowymi argumentami; rdzeń C i asm
  wykonują tę samą instrukcję (albo `CYKLE` – losowe ciągi), porównanie rejestrów, cykli i całej
  pamięci. Strony `3A`, `C0`–`C1` idą przez funkcje obsługi (wolne ścieżki asm). Znalazł dwa błędy:
  - asm: `rd_slow` wpisywał wynik do całego `d1` (`move.l`), a ściąganie słowa ze stosu
    (`PULS`/`PULU`/`RTS`) trzyma tam już starszy bajt → przy stosie na stronie I/O starszy bajt
    ginął. Poprawka: `move.b`;
  - **rdzeń C** (nie asm): `ST16`/`ADD16`/`SUB16`/`CMP16` brały rejestr przed obliczeniem adresu,
    MAME (`regop16()`) – po nim, więc np. `STX ,--X` zapisuje już zmniejszone X. Poprawione według
    MAME; ślady MAME nadal 0 różnic (w nich ten przypadek nie wystąpił).
  Po poprawkach: 30 000 testów pojedynczych instrukcji – 0 różnic.
- `Lockstep ROMDIR [KLATKI [OD [STEP]]]`: od klatki `OD` porównanie stanu po każdym przebiegu CPU,
  z `STEP` – potem instrukcja po instrukcji w pierwszym różnym przebiegu. `LockstepT` – to samo
  z wariantem rdzenia asm zapisującym każdą instrukcję do bufora (`gen_6809_68k.py --trace`),
  przy różnicy wypisuje ostatnie instrukcje asm i sprawdza tablice stron.
  Znalazł błąd, którego testy losowe nie mogły wykryć (płaska pamięć 64 KB): **długie skoki
  względne** (`LBSR`/`LBRA`/`LBcc`) liczyły nowy wskaźnik `a4 + przesunięcie` w pamięci hosta.
  CPU B w klatce 2401 robi `LBSR` z banku ROM (`57B7`) do stałego ROM-u (`FD00`, przez
  zawinięcie 16 bitów) – w pamięci hosta to inny obszar, więc wykonał bajty spod `BD00`.
  Poprawka: skoki długie mapują PC od nowa (`set_pc`); krótkie zostają przy `lea` (zakładają,
  jak wykonanie sekwencyjne, że kod nie przechodzi przez granicę obszaru – w grze tak jest).
  Krok po kroku błąd znikał, bo każde wywołanie rdzenia mapuje PC od nowa.
- `XainTest` z `XAIN_ASM` (rdzeń asm) i skryptem z pliku (`@PLIK`) – porównanie sum kontrolnych
  klatek z PC.

**Pomiary (A1200, 68060/50, okno 16 bit, `SCALE=2`, demo, `STATS` bez podziału czasu):**

| Wersja | kl./s | Uwagi |
|---|---|---|
| start fazy 4 | 2,3 | |
| pomijanie pętli + renderer szybki | 4,0–4,4 | |
| rdzeń C, `NOSOUND` | 6,5 | |
| rdzeń asm, Paula | 6,0 | synteza YM2203 zajmuje ponad połowę czasu |
| rdzeń asm, `NOSOUND` | 13,2 | |

`STATS=FULL` (podział zawyżony przez koszt `ReadEClock`): z dźwiękiem CPU A 9%, B 6%, dźwięk 6%,
YM 37%, MCU 10%; bez dźwięku A 19%, B 14%, dźwięk 13%, YM (liczniki) 8%, MCU 20%.

Dalej: synteza YM2203 (największy koszt), rdzeń MCU, koszt wywołań C przy pętlach oczekiwania,
pętla opóźnienia CPU dźwięku `8810`. Wariant „próbki zamiast YM2203” odłożony; gdyby powstał –
tylko przez AHI (decyzja użytkownika).

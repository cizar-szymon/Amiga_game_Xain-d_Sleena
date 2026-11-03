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
| 9b | Dźwięk z sampli zamiast emulacji (rozdział 13): polecenia z zatrzasku `3A08` → sample (`sndplay.c`) przez Paulę/AHI | ✅ pierwsza wersja PC i A1200: 060/50 tempo jak bez dźwięku (33–54 kl./s); ⏳ odsłuch przez użytkownika |
| 10 | Wydanie: ikona, Tool Types, opisy PL/EN | ✅ 0.9 i 0.9.2 (12.10.2026; numer `XAIN_VERSION` widoczny jako `RELEASE x.y.z` na ekranie startu, lista zmian `XainSleena_release.txt` / `_release_en.txt` w paczce, uzupełniana przy każdym wydaniu): od 3.11.2026 `releases/amiga/XainSleena-<v>.zip` i `releases/pc/XainSleena-<v>-pc.zip` (`tools/make_release.sh`, rozdział 15; wcześniej LHA / ZIP bez numeru), opisy `XainSleena.txt` / `_en` / `_dev` / `_dev_en` (źródła `docs/release/amiga` i `docs/release/pc`, osobno dla każdej platformy); bez ROM-ów i bez `XainSleena.snd` (z ROM-ów) |

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
klawiatura: strzałki, Ctrl / Z = strzał, Spacja / Alt / X = skok, 5 = moneta, 1/2 = start, P = pauza, Esc = wyjście.
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
build\port\xainsleena.exe -rompath ROM                         # gra (okno, tylko klawiatura)
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

**Tryby indeksowe w kodzie instrukcji (12.10.2026):** 23% wykonywanych instrukcji gry to tryby
indeksowe (sonda `build/probe/idxstat.c`): `,X+` 20%, `,Y++` 13%, `,Y` 12%, `,X++` 9%, `,Y+` 6%,
`,S` 5%, `,X` 4%, przesunięcia 5-bitowe (00–7F) ok. 12%. Te są teraz liczone w miejscu
(`ea_indexed`: 5-bitowe ogólnie dla czterech rejestrów, 7 częstych przez porównania), reszta
nadal przez `idx_XX` (`jsr`/`rts`). `CpuBench`: mieszanka 5,43 → **6,41 MHz** (+18%), `LDA ,X+`
50 → 34 cykle, `LDA 5,X` 50 → 35,5, `LDA ,X` 50 → 38,5. `CpuTest6809` 30 000 + 10 000 i
`Lockstep` 5000 klatek bez różnic. W grze (demo) w granicach rozrzutu pomiaru: bez dźwięku
28–34,5 kl./s, z dźwiękiem 16,7–20 (6809 to tylko ok. 25–40% czasu klatki).

**Pomiar po trybach indeksowych (12.10.2026, AmiAgent, demo 4–50 s, okno `SCALE=2`, BLIT+M16,
`FRAMESKIP` automatyczne).** W tytule `fps` = klatki logiki/s, `drawn` = klatki narysowane/s:

| Zegar | Opcje | logika kl./s | narysowane/s |
|---|---|---|---|
| 100 MHz | `STATS NOSOUND` | **56–58,6 (pełne tempo)** | 14–44 (przewijanie 14–16) |
| 100 MHz | `STATS=FULL NOSOUND` | 41,6–57,4 | 10–21 |
| 100 MHz | `STATS` (Paula) | 24,8–40,9 | 6–10 |
| 50 MHz | `STATS NOSOUND` | 32,9–54 (przewijanie ok. 37) | 9–13 |
| 50 MHz | `STATS=FULL NOSOUND` | 26,9–37,5 | 7–10 |
| 50 MHz | `STATS=FULL` (Paula) | 14,0–16,4 | 5 |

Podział przy 50 MHz w przewijaniu (`STATS=FULL`, % czasu):
- bez dźwięku: emu 53 (A16 B9 S1–2 YM1–2 MCU11), render 11, present 30;
- z dźwiękiem: emu 68 (A9 B5 **S11 YM26–27** MCU7), render 7, present 19.

Present na narysowaną klatkę: porównanie 5–6 ms (ok. 7–8 ms także przy 0 zmienionych blokach),
przeliczenie 16–18 ms, zapis do VRAM 13–15 ms, blit 1 ms.

Wnioski:
- `STATS=FULL` sam zabiera dużo czasu (przy 100 MHz: 57 → 42–57 kl./s), więc do porównywania
  tempa używać `STATS`, a `STATS=FULL` tylko do podziału czasu.
- Z dźwiękiem procesor dźwięku i YM2203 zajmują ok. 40% czasu. To największa pozycja.
- Bez dźwięku przy przewijaniu największe jest wysyłanie obrazu `SCALE=2` (ok. 37 ms na klatkę).

**Okno czy pełny ekran (50 MHz, `STATS NOSOUND`, demo).** Przy przewijaniu (ok. 10–15 s):

| Wariant | logika kl./s | narysowane/s |
|---|---|---|
| okno `SCALE=2` (512×480) | ok. 37 | 10 |
| `SCREEN=FULL SCALE=2` (tryb 640×480 16 bit) | 37,1–37,9 | 10 |
| okno `SCALE=1` (256×240) | 42,3–43,7 | 11 |
| `SCREEN=FULL SCALE=1` (tryb 320×240 16 bit) | 42,5 | 11 |

Pełny ekran nie daje nic ponad okno, bo droga obrazu jest ta sama (bitmapa na karcie i blit).
Liczy się tylko ilość danych: `SCALE=1` jest o ok. 15% szybsze. `SCREEN=FULL SCALE=1` w trybie
320×240 daje duży obraz (powiększa monitor albo scaler) przy koszcie `SCALE=1`.

Przy 100 MHz `SCREEN=FULL SCALE=1 STATS NOSOUND`: logika **57,0–57,7 kl./s (pełne tempo)**,
narysowane 19–32/s (okno `SCALE=2`: 14–23/s w tych samych miejscach dema), w początkowej scenie
bez przewijania 50/s.

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

**Spowolnienie przy wielu przeciwnikach (zgłoszenie 16.10.2026, 0.9.6).** Czy to automat? Sonda
`build/probe/cpuload.c` (bez pomijania pętli, 20 000 klatek gry): główny CPU zajęty średnio
40–55% klatki, pomocniczy 30–50%; pełne obciążenie prawie tylko w przejściach scen – automat sam
w grze nie zwalnia. Ciężkie sceny: `build/probe/heavy.c` (gra z nieskończonymi życiami, `xain_assist`,
zapis stanów) – wulkaniczna planeta z kamieniami i gejzerami, `heavy_ins.bin` (8370 instr. 6809 na
klatkę, średnio ok. 7000) i `heavy_spr.bin` (15 sprite'ów). Nowa opcja Amigi **`LOADSTATE=plik`**
(stan z F10 lub z sondy; DIP-y nie są w stanie, więc `DSW1=3f` dla nieskończonych żyć).
A1200, `SCREEN=FULL SCALE=1 STATS`, logika / rysowane kl./s:

| Zegar | `heavy_ins` | `heavy_spr` |
|---|---|---|
| 100 MHz | 57,2–57,8 / 20–21 | 57,1–57,8 / 21–46 |
| 55 MHz (tak zastałem Amigę) | 34,0–34,7 / 9 | 34,6–52,1 / 9–16 |

`STATS=FULL` przy 100 MHz (`heavy_ins`): emu 74–75% (A 20, B 14–17, MCU 7–8, reszta ok. 30% –
kwanty, linie, I/O), render 9–10, present 13 (przeliczenie 7,7–8,0 ms na narysowaną klatkę),
dźwięk 2,4%. Wniosek: przy 55 MHz gra zwalnia naprawdę (logika 34 zamiast 57,4), przy 100 MHz
logika nadąża, ale rysowanych jest ok. 20 obrazów/s. Największe pozycje: rdzenie 6809 (A+B ok.
35%) i narzut kwantów (ok. 30%) – **poprawka niżej: te 30% to głównie koszt samego pomiaru.**

**Narzut kwantów – pomiar (17.10.2026, A1200 060/100, `FrameCost ROMDIR heavy_ins.bin 600`,
`port/tests/framecost.c`, bez obrazu i dźwięku):** emulacja klatki **9,3 ms**. Kwanty 48 cykli
(2× więcej) 11,1 ms, 32 cykle (3×) 12,9 ms → po odjęciu dodatkowych obrotów pętli oczekiwania
ok. **5,7 µs na kwant, 1,5 ms na klatkę (ok. 17%)**. Bez tablicy kwantów 9,77 ms, bez `SUBFAST`
9,8 ms. Jeden `ReadEClock` kosztuje ok. **14 µs** (1632 odczyty na klatkę = 23 ms!) – dlatego
`STATS=FULL` tak zawyża „emu”. Podział po odjęciu kosztu odczytów (przybliżony): główny CPU
4,3 ms, pomocniczy 3,2 ms, MCU 1,4 ms, reszta (linie, `3A05`, pętla) 0,25 ms, dźwięk 0,07 ms.
Sonda `build/probe/iostat.c` (PC, ten sam stan): główny 4374 instrukcji na klatkę, pomocniczy
3601; przez rdzeń C 0 (rzadkie opcody nie występują), przez procedury I/O ok. 140 odczytów (`3A05`
97) i 270 zapisów (`3A09` 109) głównego, 33 pomocniczego, 18 przerwań. Czyli ok. 8000 instrukcji
w ok. 7,5 ms = **ok. 90 cykli 68060 na instrukcję** (w `CpuBench` 33): kwanty i I/O tłumaczą ok.
1,1 ms, reszta to sam rdzeń przy różnorodnym kodzie gry (rozproszone procedury opcodów,
chybienia pamięci podręcznej 8 KB). Wniosek: rekompilacja ma sens – kod liniowy bez rozdziału.

**Wysyłanie obrazu – pomiar i rysowanie pasami (17.10.2026, A1200 060/100, `heavy_ins`).**
`STATS=FULL` w trybie BLIT czytał zegar przy każdym odcinku bloków (ok. 14 µs na odczyt), więc
dawne „przeliczenie 7,9 ms” było zawyżone. Nowa opcja **`PBENCH=n`** (z `LOADSTATE=`): każdy
etap n razy, jednym odczytem zegara na serię, wynik na stdout, potem koniec (nazwa opcji: parser
obcina nazwy do 11 znaków – `PRESENTBENCH` nie działał). Na obraz 256×240: rysowanie 8,4 ms,
porównanie `find_dirty_68k` 4,2 ms, `memcpy` 120 KB 3,4–3,9 ms (pamięć ok. 60–80 MB/s), przeliczenie
całego obrazu 5,6 ms (`convert_rows_68k`, ok. 9 cykli/piksel), zapis całego obrazu do VRAM 2,6 ms
(ok. 47 MB/s), blit 0,3 ms, `CacheClearE` i blokada bitmapy po 6 µs. W tej scenie zmienia się
ok. 650 z 960 bloków na klatkę (tło się przesuwa). Rysowanie + wysyłanie na klatkę:

| Wersja (`SCREEN=FULL SCALE=1`) | ms/obraz | obrazy/s w grze |
|---|---|---|
| dawniej (`video_render_frame` + `present`, MOVE16) | 17,4 | 20–21 |
| pasy 8 linii, poprzedni obraz w `shown` (kopiowanie bloków) | 18,7 | – |
| pasy + 32-bitowy skrót na blok (4 KB) zamiast poprzedniego obrazu | 17,2 | – |
| + bez MOVE16/`CacheClearE` (zwykłe długie słowa) | 15,7 | – |
| + `convert_fast_68k` (4 piksele na obrót, przeplot odczytów) | 14,8 | 22–24 |
| + skróty w asm (`hash_band_68k`) | 14,1 | 24 |
| + przeliczanie wprost do VRAM (bez `sfb` i `copy_rows`) | **12,4** | **28–29** |
| (próba) renderer piszący od razu piksele ekranu przez tablicę | 14,97 | wycofane: rysowanie 8,3 → 11,4 ms |

Okno `SCALE=2`: 14–15 → 17 obrazów/s. `VERIFY` (odczyt ekranu z karty co 30 obrazów): 0 złych
bloków na pełnym ekranie i w oknie `SCALE=2`. Ścieżka pasami: `render_present_bands` (BLIT,
16 bit, `SCALE=1|2`, bez `RENDER=LINE`), `video_render_begin` + `video_render_lines` (`video.c`);
`BANDS=OFF` – stara droga, `BANDDIRECT=OFF` – przez `sfb`. Zostaje: rysowanie 8,4 ms (pamięć
grafiki kafelków), przeliczenie + zapis do VRAM ok. 3,5 ms.

**0.9.8: paleta bez wysyłania całego obrazu (17.10.2026).** Raport `LAGLOG` z gry użytkownika
(0.9.7, pełny ekran `SCALE=2`, 22 min): 56% klatek spóźnionych, każda wolna klatka to ok. 905 z
960 bloków (22 ms rysowania), w 80% ze zmianą palety. Gra zmienia paletę bardzo często, a
`update_colours()` ustawiał `force_full`; w scenie `heavy_ins` „650 zmienionych bloków na klatkę”
to właśnie paleta, nie przewijanie. Teraz pas jest najpierw cały zamieniany na piksele karty
(`convert_fast_68k` do `nband`, pas w pamięci podręcznej), skróty liczone z pikseli karty, więc
zmiana palety wysyła tylko bloki, które zmieniły wygląd; wysyłanie: `copy_rows` (`SCALE=1`) albo
nowe `double_rows_68k` (`SCALE=2`), długimi słowami. `heavy_ins`, 100 MHz: bloków na obraz 653 → 49,
rysowanie + wysyłanie `SCALE=1` 12,4 → ok. 3,6 ms ponad samo rysowanie (13,3 ms razem), `SCALE=2`
22,5 (stara droga) → 14,1 ms; w grze pełny ekran `SCALE=1` 31–32, `SCALE=2` 29–30 obrazów/s;
`VERIFY` 0 złych bloków. `BANDDIRECT=OFF` usunięte. `LAGLOG` przerobiony: podsumowanie co 10 s
(spóźnione, obrazy/s, emulacja, rysowanie, najgorsza klatka, bloki, paleta, lot), stan automatu
przy ≥ 30% spóźnionych klatek w oknie (do 5, co najmniej minutę od poprzedniego).
- Zgłoszenie: po utracie życia, zanim gracz ruszy postacią, obraz jest źle narysowany (czarne
  obszary, zmieniane tylko przez przelatujące obiekty). Stan F10 użytkownika → na A1200 `VERIFY`
  przy `SCALE=1` w chwili śmierci i „GET READY” jednorazowo 96 i ok. 670 złych bloków. Przyczyna:
  skrót `h = rol(h, 5) ^ long` – dla bloku jednego koloru po 32 krokach (5 i 32 względnie
  pierwsze) zostaje tylko parzystość bitów koloru, więc jednolite niebo przy ściemnianiu i
  rozjaśnianiu często miało ten sam skrót i nie było wysyłane. **0.9.9:** FNV-1a (`h = (h ^ long) *
  16777619`, `mulu.l`); `VERIFY` 73 i 58 sprawdzeń przez śmierć: 0 złych bloków, tempo bez zmian.
  Do tego: okno pełnoekranowe bez `WA_NoCareRefresh`, z `IDCMP_REFRESHWINDOW` (uszkodzenie →
  wszystko od nowa), blit całej bitmapy co 32 obrazy, wysłanie wszystkiego co 256 (pasy) /
  `force_full` co 256 (stara droga) – przeciw uszkodzeniom, których system nie zgłasza.
- Zgłoszenie: największe zwolnienia w lotach w kosmosie przy jednym typie wrogiego statku –
  ⏳ raport `LAGLOG` / stan F10 z tej sceny.

**Rysowanie przyrostowe (0.9.13, testy tylko na A1200 – decyzja użytkownika).** Raport `LAGLOG`
gry użytkownika (0.9.12, `SCALE=1`): nawet przy 10–17 zmienionych blokach na obraz rysowanie ok.
13 ms (cały obraz 8,4 ms + przeliczenie wszystkich pasów), razem z emulacją ok. 22 ms. Teraz pas
jest rysowany tylko, gdy mógł się zmienić: wszystko po pełnym odświeżeniu, zmianie `pri` albo
przewinięcia (`scrollx`/`scrolly`); inaczej pasy zmienionych kafelków tła (`bgram0`/`bgram1`: wiersz
kafelka z indeksu, linie przez przewinięcie Y), zmienionego tekstu (`charram`, wiersz r = linie
8r–8r+7) i pasy ze sprite'ami teraz lub w poprzednim obrazie (`sprite_bands`, jak
`video_render_begin`). Pasy rysowane wprost do `shown` (obraz indeksów, zostaje); przy zmianie palety
pozostałe pasy tylko przeliczane. `present()` (stara droga) zeruje `prev_valid` (zamienia `shown`).
Nowe opcje testowe: `REPLAY=plik` (powtórka F10 na Amidze, potem koniec; na stdout `VERIFY` i liczby
pasów), `VERIFY=n`, `INCR=OFF`. Testy na powtórkach użytkownika (`dump1` odliczanie, `dump2` dżungla
z przewijaniem, `dump3` kolumny): `VERIFY=1` (każdy narysowany obraz) – 0 złych bloków w 174–257
sprawdzeniach na powtórkę (`SCALE=1` i `SCALE=2`). Czasy (`LAGLOG`, `SCALE=1`, 100 MHz), rysowanie
`INCR=OFF` → nowe: `dump3` 15,0–15,5 → 11,3–11,5 ms (26–28 → 34–37 obrazów/s), `dump1` 15,8–16,1 →
12,4–12,8 ms (28–30 → 37–39), `dump2` 16,5–18,2 → 14,1–17,8 ms (24–25 → 24–29). Pasów na obraz:
rysowanych 19–24 z 30, przeliczanych 23–28 – sprite'y (także pasek energii i ikony żyć) są
rozrzucone po ekranie; dalszy zysk wymagałby rysowania bloków 8×8 zamiast całych linii.

**Loty w kosmosie – pomiar (17.10.2026, A1200 060/100, `REPLAY=` powtórek F10 użytkownika `dump5`,
`dump6`, `SCALE=1`).** `LAGLOG` z gry: w lotach dwa tryby – spokojniej 260–310 bloków, ok. 340
zmian palety na 574 klatki, rysowanie 10–11,5 ms, 38–41 obrazów/s; formacje 510–600 bloków,
500–570 zmian palety (prawie co klatkę), 15–16,4 ms, 26–29 obrazów/s; emulacja 7–9 ms. Powtórki:
normalnie 16,3–16,4 ms (pasy rysowane 30 z 30 – tło przewija się w każdej klatce, rysowanie
przyrostowe nic nie daje), bez sprite'ów (`VSKIP=8`, nowa opcja pomiarowa) 15,2–15,5 ms, `INCR=OFF`
16,2–16,3 ms, same sprite'y bez tła i tekstu (`VSKIP=7`) 7,5–7,8 ms przy 50–80 blokach. Na PC
(`build/probe/replay_cost.c`): 7–8,5 tys. instrukcji 6809 na klatkę (formacja +ok. 1 tys.), do 29
widocznych sprite'ów. Wniosek: sprite'y to ok. 1 ms; resztę zabiera przewijane tło (rysowanie obu
warstw w każdej klatce i wysyłanie ok. 450 bloków zmienionych przez gwiazdy i paletę) oraz emulacja.

**Warstwy w lotach, próby (17.10.2026, A1200 060/100, powtórki F10, `SCALE=1`).** Zrzuty z gry (F10)
z `VSKIP`: gwiazdy to kafelki warstwy `BG2` (przewijanej rejestrami; kolor kropek – paleta); pod nią
`BG1` ze stacją, w lotach w 100% przezroczysta (już pomijana typem wiersza 0). Zrzuty AmiAgenta w trakcie
gry pokazują kolorowe pasy – błąd zrzutu (czyta ekran podczas blokad i zapisu VRAM gry), `VERIFY` = 0.
- Pomijanie zakrytych warstw (dla linii od góry: warstwa tła bez przezroczystego piksela → rysowanie od
  niej): obraz identyczny (`RCHECK` z rysowaniem pełnym: 0 z ok. 32 000 pasów), ale wolniej o ok. 0,4 ms
  (sprawdzanie linii kosztuje więcej, niż daje) – **wycofane**.
- Eksperyment użytkownika „70% mniej gwiazd w lotach” (`STARS=30`, kafelki `BG2` jako kolor tła): bloków
  wysyłanych 450 → 245, rysowanie 17,2 → 16,6 ms (3%) – warstwa przewija się w każdej klatce i jest
  rysowana w całości, wysyłanie bloków to mały koszt – **wycofane**.
- **0.9.14:** typ wiersza kafelka 3 = jeden kolor ≠ 0 (`row_types`), a w warstwie nieprzezroczystej
  wiersz koloru 0 też jako 3; renderer asm wypełnia go 8 (tekst: 4) długimi słowami. W lotach `BG2`:
  29% wierszy jednego koloru + 50% koloru 0. `RCHECK` (nowe znaczenie: każdy pas także rendererem w C,
  porównanie): 0 różnic w ok. 29 000 pasów (5 powtórek). Czasy rysowania: `dump5` 16,6 → 16,4, `dump6`
  16,8 → 16,1, `dump3` 11,7 → 10,9, `dump1` 12,6 → 12,4, `dump2` 14,5/18,1 → 14,1/17,8 ms.
- Wniosek: przy rysowaniu zostały głównie stałe koszty obrazu (rysowanie linii, przeliczenie 30 pasów,
  skróty, przy zmianie palety `update_colours`) – dalsze zyski będą małe; największą pozycją pozostaje
  emulacja (ok. 9 ms).

**Obraz w VRAM przesuwany blitterem karty, bloki 8×8, obraz mapy (0.9.15, 24.10.2026).** Na podstawie
nowych wniosków w `PORTING_PLAN.md` (AmiXcom: obraz zostaje na karcie, przewija go blitter karty).

*Pomiar przed zmianą* – `port/tests/scrolllog.c` (`build/port/scrolllog.exe ROM REPLAY [CSV]`,
podsumowanie `tools/scrolllog_summary.py`), powtórki F10 użytkownika, stan na koniec klatki:
- dolna warstwa zawsze `BG2` (tryby 0/4/6); przewija się tylko w co 2.–3. klatce (dżungla 338 z 911,
  lot 298 z 928), krok kilka pikseli; **gdy `BG2` się przesuwa, `BG1` przesuwa się o tyle samo**
  (w lotach `BG1` jest pusta i przesuwa się niezależnie);
- tekst to głównie nieprzezroczysta czarna ramka ekranu i panel (160–197 bloków) – przy przewijaniu stoi;
- warstwy nad dolną (tekst, `BG1`, sprite'y) pokrywają 250–590 z 960 bloków – osobna „płyta” samego
  `BG2` w VRAM (pierwotny krok 2 planu) wymagałaby składania tych bloków w każdej klatce;
- **cały poprzedni obraz przesunięty o krok tła różni się od nowego tylko w ok. 185 blokach**
  (bez przesunięcia 614–816) – wybrano przesuwanie całego obrazu, nie płyty.

*Rozwiązanie* (`main_amiga.c`: `render_present_shift`, `video.c`: `video_render_span`, bufor tła):
- Dwie bitmapy na karcie (`vbm`, `vbm2`). Gdy dolna warstwa przesunęła się od ostatniego obrazu
  (krok ≤ 64 px): `BltBitMap` `vbm` → `vbm2` z przesunięciem (obszar R bez nieruchomej ramki tekstu –
  nieprzezroczyste, niezmienione znaki, gdy tekst jest na wierzchu; ramka kopiowana bez przesunięcia),
  CPU składa tylko bloki: brzeg, sprite'y teraz i przesunięte stare, niepuste znaki tekstu teraz
  i przesunięte stare, zmienione kafelki, kafelki ze zmienioną grupą kolorów (16 wpisów palety),
  niepuste kafelki drugiej warstwy tła, gdy przesuwa się inaczej. Potem `vbm2` staje się `vbm`.
- Bez przesunięcia te same bloki (bez brzegu; tekst: zmienione znaki, po zmianie palety tekstu
  wszystkie) zapisywane wprost do `vbm`. Pasy (stara droga) tylko przy pełnym odświeżeniu, zmianie
  trybu priorytetu, tekście na dole (tryby 2, 3) i kroku > 64 px.
- `video_render_span`: renderer dla zakresu kolumn (asm tła: pierwszy fragment / całe kafle /
  ostatni fragment); warstwy górne pomijane dla całego odcinka, gdy ich kafle/znaki są tam puste;
  odcinki z przerwą do 3 bloków rysowane razem (`SPANGAP`), przeliczane i wysyłane tylko bloki zmian.
- Dolne tło jako obraz całej mapy 512×512 indeksów palety (`bgcache`, 512 KB), uaktualniany kafelek po
  kafelku (porównanie z kopią RAM kafelków) – linia tła to kopia; pierwszy obraz buduje cały (ok. 40 ms).
- `update_colours` przelicza tylko zmienione wpisy palety.
- `VERIFY` porównuje teraz okno z rendererem wzorcowym (`video_render_line`, jak MAME), a `RCHECK`
  – z rendererem C z kafelków (bez bufora mapy).

*Sprawdzenie:* PC `XAIN_CHECK` (5000 klatek gry, rysowanie całością i losowymi odcinkami) i `scrolllog`
(6 powtórek, odcinki + bufor mapy wobec `video_render_line`): 0 różnic. A1200 `VERIFY=1` (każdy obraz)
+ `RCHECK` na powtórkach `dump1/2/3/5`, `SCALE=1` i `2`, pełny ekran i okno: 0 złych bloków.

*Pomiary* (A1200 060/100 MHz, pełny ekran, `REPLAY` + `LAGLOG`; 0.9.14 = `XainSleenaB` użytkownika):

| Powtórka | 0.9.14 rysowanie | 0.9.15 rysowanie | obrazy/s |
|---|---|---|---|
| lot `dump5`, `SCALE=1` | 16,0–16,1 ms | 8,8–9,0 ms | 27 → 44–45 |
| lot `dump6`, `SCALE=1` | 16,8 ms | 9,5 ms | 27 → 42 |
| dżungla `dump2`, `SCALE=1` | 14,1 / 17,8 ms | 7,1 / 8,5 ms | 24–29 → 46–51 |
| `dump1`, `SCALE=1` | 12,4–12,8 ms | 8,3–8,7 ms | 38–39 → 54–56 |
| `dump3`, `SCALE=1` | 11,0–11,3 ms | 6,6–7,9 ms | 34–38 → 42–49 |
| lot `dump5`, `SCALE=2` | 20,9–21,2 ms | 12,8–13,1 ms | 21 → 31–32 |
| dżungla `dump2`, `SCALE=2` | 17,3 / 24,6 ms | 9,1 / 12,0 ms | 18–24 → 32–43 |

Kolejne kroki i próby (lot `dump5`, `SHIFTPROF`, µs na obraz rysowany blokami):

| Wersja | rysowanie+przeliczenie | razem rysowanie (`LAGLOG`) |
|---|---|---|
| tylko przesuwanie (bez bloków w obrazach bez przesunięcia) | – | 14,9 ms (obrazy bez przesunięcia rysowały prawie wszystkie pasy: nieaktualny `shown`) |
| + bloki także bez przesunięcia | 9 174 | 12,5 ms |
| + 2 wiersze kafelków w liście asm, łączenie odcinków (`SPANGAP` 0/3/6/32) | 8 300–9 100 | bez zmian – **nie pomogło** |
| + bufor mapy dla dolnego tła | 7 900 | – |
| + przeliczanie tylko bloków zmian (nie całych odcinków) | 7 250 (`SPANGAP=32`) | – |
| + pomijanie pustych warstw górnych w odcinku | 6 460 | 8,8–9,4 ms |

Zostało (lot, na obraz): ustalenie bloków ok. 1,1 ms, rysowanie+przeliczenie 6,5 ms (narzut wywołań na
linię i warstwę, sprite'y), zapis do VRAM 1,3 ms, blity 0,6 ms, paleta (przed `update_colours` tylko
zmian) 0,5 ms; emulacja ok. 9 ms. Możliwe dalej: rysowanie odcinka wszystkich 8 linii jednym
wywołaniem asm na warstwę, ustalanie bloków w asm, 64-bitowe rozkazy w binarce – niesprawdzone
(brak `m68k-amigaos-objdump`; vbcc z `-cpu=68060`).

**Szarpanie postaci na piechotę (0.9.16 FPS, 0.9.17, 25.10.2026).** Zgłoszenie: licznik `FPS` (nowa
opcja 0.9.16, liczba obrazów z ostatnich 57 klatek w prawym dolnym rogu) pokazuje 40–50, a ruch postaci
miejscami skacze – każda spóźniona klatka to pominięty obraz. Zrzuty F10 użytkownika
(`build/lag/stutter/dump1-6`, na Amidze `Work:XainSleena/s1-6.bin`), `SHIFTPROF=SRC` (nowe: bloki
według przyczyny): na piechotę `BG2` (niebo, planeta) stoi, a `BG1` (ziemia, skały) przesuwa się z
postacią – „druga warstwa” dawała 135–191 bloków na obraz, sprite'y 70–100.
- Bloki `BG1` w całości nieprzezroczyste teraz i w miejscu, skąd przychodzą (przesunięte o krok `BG1`),
  bez starego sprite'a/tekstu/licznika i zmiany koloru nad źródłem – kopiowane blitterem ze starego
  obrazu (prostokąty z wierszy bloków o tej samej masce); bez przesunięcia dolnej warstwy wtedy też
  dwie bitmapy (pełny blit + kopie). 57–117 bloków na obraz kopiuje karta.
- Renderer: warstwa rysowana od razu dla wszystkich linii odcinka (`video_bg_lines_68k`,
  `video_txt_lines_68k`: jedno wywołanie na wiersz kafli zamiast na linię).
- Ustalanie bloków maskami bitowymi (`layer_scan`: jedno przejście po 17 kaflach na wiersz bloków –
  widoczne / pełne / grupa koloru; `mask_moved_and`; brzeg maskami; `same_mem` zamiast `memcmp` vbcc,
  które porównuje bajtami): 2,1 → 1,3 ms.
- Próba bez zysku: bloki tylko do przekolorowania (nowa paleta, te same piksele) przeliczane z `shown`
  bez rysowania – w tych scenach prawie ich nie ma (zostawione, nie szkodzi).
- Sprawdzenie: PC `XAIN_CHECK` + `scrolllog` 0 różnic; A1200 `VERIFY=1 RCHECK FPS` na s1/s4/s5/dump5/dump2:
  0 złych bloków (`VERIFY` porównuje z `video_render_line`, pomija 2 bloki licznika).

| Zrzut (`SCALE=1`, `LAGLOG`) | 0.9.16 rysowanie / spóźnione | 0.9.17 rysowanie / spóźnione |
|---|---|---|
| s1 (piechota) | 9,6 ms / 25% | 7,4 ms / 12% |
| s4 (piechota, przewijanie) | 10,5 / 11,5 ms, 26 / 41% | 9,4 / 8,9 ms, 20 / 25% |
| s5 (piechota, dużo sprite'ów na końcu) | 8,0 / 13,5 ms, 21 / 48% | 6,6 / 11,6 ms, 14 / 39% |
| lot `dump5` | 8,8 ms / 22% | 7,4 ms / 9% |

Zostało: emulacja 8,5–10 ms (największa pozycja), sprite'y (w s5 ok. 114 bloków ze sprite'ami na
klatkę), rysowanie bloku nadal ok. 2× droższe niż w całym obrazie. Pełna płynność wymaga emulacja +
rysowanie < 17,4 ms także w najcięższych chwilach.

**Skok górą po zniszczeniu bazy (0.9.12).** Zrzut F10 użytkownika (0.9.11): odliczanie `COUNT DOWN`
po zniszczeniu bazy (trzeba wskoczyć do statku), joystick w górę nie skakał, Spacja tak. W zrzucie
`$52 = 03`, `xain_on_foot()` wymagało `$52 & 0F = 2`. Kod: główny CPU zapisuje `$52` = 0 (start
etapu), 2 (`83A7`), górne bity (`835F`, `864C`); pomocniczy 2 (`809B`), 3 (`8286`: typ etapu 7 i
przewinięcie ≥ `$1600`; `893F`), 1 (`B246`). Procedura gracza `830A` (tablica `833B` według stanu
obiektu) działa przy każdym `$52`, jeśli nie statek. Poprawka: na piechotę = `$52 & 0F` 2 albo 3.
Sonda `build/probe/upjump_state.c` (stan ze zrzutu, góra przez 40 klatek): przycisk 2 dochodzi
(`p1=24`), Y gracza 3000 → 5A80 → 3000 (skok).

**F10 – zrzut do debugowania (0.9.10).** Gra trzyma wejścia ostatnich 1024 klatek (`rec_in`, po
`read_inputs` i `xain_assist`) i stan automatu co 512 klatek (dwa bufory). F10 zapisuje, z pierwszym
wolnym numerem N: `RAM:XainSleena-dumpN.bin` (powtórka: `XRP1`, klatka startu, liczba klatek, długość
stanu – big endian, stan sprzed 512–1023 klatek, po 2 bajty wejść na klatkę), `stateN.bin` (stan teraz),
`dumpN.txt` (wydanie, opcje, plansza: `$39`, `$41`, `$48`, `$53`, `$52`, wspólny RAM 0030–006F, PC
procesorów, okno aktywne czy nie, `in_p1`/`in_p2`, joystick, wciśnięte klawisze, wejścia 16 ostatnich
klatek, 32 ostatnie zdarzenia klawiszy z numerem klatki), `dumpN.ppm` (obraz okna, `p96ReadPixelArray`
24 bit). PC: `xainsleena -rompath rom -dsw1 <z .txt> -replay dumpN.bin` (wejścia z nagrania bez
`xain_assist`, potem klawiatura; z `-headless -frames N -shots`). Sprawdzenie: powtórka 698 klatek,
ostatni obraz PC = obraz z Amigi. Pobieranie: `amiga_remote.py get RAM:XainSleena-dumpN.bin …`.
Zrzuty użytkownika trzymać w `build/lag/` (poza repo), z opisem planszy z `.txt`.

> **Gałęzie (17.10.2026, decyzja użytkownika):** `main` – emulacja procesorów (rdzeń asm 6809, bez
> tłumacza); gałąź **`recompiler`** – to samo plus tłumacz 6809 → 68k opisany niżej (`m6809_tr.c`,
> wzorce w `gen_6809_68k.py`, opcja `TR=ON`, `Lockstep … TR`, `tools/check_tpl_lst.py`). Opisy
> poniżej dotyczą gałęzi `recompiler`.

**Rekompilacja – prototyp 1 (17.10.2026), decyzje użytkownika: tłumaczenie przy starcie z ROM-ów
użytkownika (w programie nie ma kodu gry), najpierw jedna procedura.** `tools/gen_6809_68k.py`
wypisuje do `m6809_68k.s` także 274 wzorce (`_m6809_tpl`: każda procedura rdzenia jeszcze raz,
`@NEXT` zamiast rozdziału, odwołania na zewnątrz bezwzględne: `jsr/jmp (x).l`, skok warunkowy
na zewnątrz jako przeskok nad `jmp`, `movea.l #x,An` jako surowe `dc.w $207C` + `dc.l x`).
`port/src/m6809_tr.c` przy starcie skleja dla każdej instrukcji zakresu `movea.l #host,a4`,
`move.l #opcode,d0` i wzorzec; mapa `trmap` (pole `M6809` na 12420, PC 8000–FFFF → kod); rdzeń
sprawdza ją w `dispatch` (początek kwantu, `check_ints`, `c_step`, pętle oczekiwania) i w
`tr_next` (po instrukcjach sterujących). Opcje: gra `TR=ON`, `Lockstep … TR [TRTO=hex]
[STATE=plik]`, `FrameCost` mierzy też wariant z tłumaczeniem. Sprawdzanie wzorców:
`tools/check_tpl_lst.py` na listingu vasm (adresowanie względem PC, `bsr`).
- Błąd pierwszej wersji: vasm sam zamienił `move.l #idxtab,a0` na `lea idxtab(pc),a0` – po
  skopiowaniu wzorca adres zły → „Software Failure”, niedozwolona instrukcja (pierwszy długi
  `Lockstep` skończył się resetem A1200). Po poprawce `check_tpl_lst.py`: 0 problemów.
- `Lockstep Work:XainSleena/rom 20 0 TR STATE=heavy_ins.bin` (porównanie po każdym kwancie):
  **20 klatek identycznych**, 205 instrukcji `DCEC`–`DE88`, 0 bez wzorca, 25 624 B kodu.
- `FrameCost` (100 MHz, `heavy_ins`): bez tłumaczenia 9,37 ms, **z tłumaczeniem 10,1 ms – wolniej**.
  Wzorce to całe procedury rdzenia (śr. 125 B na instrukcję, np. każdy tryb indeksowy z łańcuchem
  porównań bajtu trybu) – kod procedury rośnie do 25 KB i nie mieści się w 8 KB pamięci podręcznej
  instrukcji, a rozdział, który znika, kosztował tylko kilkanaście cykli. Wniosek: zysk da dopiero
  kod wyspecjalizowany pod konkretną instrukcję (argumenty i adresy jako stałe, tryb indeksowy
  wybrany przy tłumaczeniu, bez odczytu argumentów z ROM-u przez a4) – zwarty, kilka–kilkanaście
  bajtów na instrukcję. `TR` zostaje domyślnie wyłączone.

**Rekompilacja – wersja 2 (17.10.2026).** `gen_templates2` w generatorze: wzorce bez odczytu
argumentów z kodu (miejsca na stałe z wartościami-wzorcami `$A5`/`$A5A5`, sprawdzanymi przez
tłumacza), kawałki adresu (bezpośredni, rozszerzony, 216 bajtów trybu indeksowego), cykle liczone
przez tłumacza (`_m6809_tq`, `_m6809_tqea`; 226 wzorców). `m6809_tr.c`: każda instrukcja to (1) kod
dopasowany + `subq #cykle,d7` + `ble.w` do zimnej zaślepki (a4 = następna instrukcja,
`exit_loop`), (2) skok `Bcc`/`LBcc`/`BRA` wewnątrz zakresu wprost do kodu celu (cele z mapy pętli
oczekiwania – nie), (3) wzorzec wersji 1 (JSR, RTS, PSHS…), (4) krok rdzenia C (`c_step`).
Lista sprite'ów: 205 instrukcji = 162 dopasowane, 32 skoki, 11 wzorców v1, 0 kroków C; **13 310 B**
(v1: 25 624 B). Błąd po drodze: miejsce na stałą z przesunięciem 0 traktowane jak „brak” (`A=A5`
w `Lockstep`) – liczba miejsc z pola `info`. Potem `Lockstep … TR STATE=heavy_ins.bin` 20 klatek
identycznych po każdym kwancie.
- `FrameCost` (100 MHz, `heavy_ins`, 600 klatek): bez 9300–9333 µs, **z tłumaczeniem 9200 µs**
  (−0,13 ms, 1,4%).
- Wniosek (poprawka wcześniejszego szacunku): procedura to 1360 instrukcji na klatkę; przy ok.
  35–40 cyklach 68060 na instrukcję w rdzeniu to tylko ok. 0,5 ms z 9,3 ms. Wszystkie instrukcje
  obu 6809 (ok. 8000 na klatkę) to ok. 2,8 ms – reszta emulacji to kwanty (3 procesory × 272),
  MCU, procedury I/O w C, linie. Wcześniejsze „90 cykli na instrukcję” wynikało z przypisania
  całego czasu `run_main`/`run_sub` (z narzutem kwantów i C) samym instrukcjom. Górna granica
  zysku z rekompilacji całego kodu: ok. 1–1,8 ms (10–20%), realnie ok. 1 ms. Tłumacz zostaje
  (`TR=ON`, domyślnie wyłączony); dalsze przyspieszanie lepiej zacząć od wysyłania obrazu (ok. 8 ms
  na narysowaną klatkę) i od kosztu kwantów/MCU.

## 13. Dźwięk z sampli (zamiast emulacji procesora dźwięku i YM2203)

Decyzja użytkownika (12.10.2026): emulacja dźwięku zabiera ok. 40% czasu 68060/50 (S 11% + YM
26–27%), więc procesor dźwięku i oba YM2203 nie będą emulowane. Gra pisze polecenie do
zatrzasku `3A08`; port odgrywa dla niego gotowy sampel na kanałach Pauli (AHI jako opcja).
Muzyka w kółko, efekty jako sample. Sample 8 bit, **15 977 Hz** (okres Pauli 222), generowane
**na PC** z ROM-ów użytkownika (nie do rozpowszechniania, jak ROM-y). Emulacja zostaje jako opcja
(`SOUND=EMU`) do porównań. Koszt na Amidze: Paula przez DMA, procesor tylko przy poleceniach
i dopisywaniu muzyki do Chip RAM – poniżej 1%.

**Program dźwiękowy** (ROM `p2-0.ic49`, disassembly `unidasm -arch m6809`): IRQ `8816` czyta
zatrzask (`$1000`), FIRQ `8E62` to tik z timera YM #1 (ok. 868/s), pętla główna `800B`.
- Polecenia `01`–`3B` (bit 7 = 0) to muzyka (tablica `8F7B`), `80`–`BC` efekty, `FF` / `FE` /
  `FD` / `F0`–`FE` zatrzymanie (wszystko / efekty / oba / jeden kanał). `3C`–`7F` i `BD`–`EF`
  są ignorowane.
- `$02`–`$0D`: polecenie grające na każdym z 12 kanałów (6 FM + 6 SSG). Polecenie równe `$02`
  jest pomijane – dlatego gra może wysyłać `09` w każdej klatce (muzyka już gra).
- Kanał c: wskaźnik sekwencji = baza `$0F+2c` + przesunięcie `$27+2c`, liczniki `$3F+4c`.
  Kanały nieużywane liczą bez końca (`$4F`–`$6E`), osobne odliczanie `$ED`, `$102`–`$104`, więc
  stan RAM całego programu nigdy się nie powtarza – pętli szuka się po kanałach.
- Demo + rozgrywka (`build/probe/sndcmd.c`, 9000 klatek): `FD` (start), `01`, `81` (moneta),
  `FF`, `0F` (start etapu), `09` (muzyka etapu, co klatkę), `8A` (strzał, przycisk 1), `88`
  (druga broń), `89` (jej wybuch), `85`, `8B`, `8C`, `90`, `97`, `A8`, `B5`, `B8`, `19`.

**Nagrania (`tools/sndrip.c`, budowa `sh tools/mksndrip.sh`):** tylko procesor dźwięku i YM2203,
reset, 60 klatek ciszy, polecenie, nagranie do 3 s ciszy albo 300 s; obok WAV stan RAM przy
każdej zmianie wskaźników (`cmd_XX.state`). Wszystkie 121 poleceń w 8 min. Efekty 0,1–7 s;
utwory i część efektów (`86`, `97`, `99`, `9C`, `A2`, `A4`, `AA`, `AE`, `AF`, `B1`, `B9`, `BA`)
grają dłużej niż 300 s.

**Pętle (`tools/snd_loop.py`):** kanały mają pętle różnej długości (np. `02`: bas/perkusja co
3,98 s, melodia co 14,56 s – nie jest to wielokrotność, więc dokładna pętla całości nie istnieje).
Pętlę wyznaczają kanały, które skaczą wstecz najrzadziej (melodia): para tików z tym samym
wskaźnikiem i licznikami tych kanałów, potwierdzona tym, że energia dźwięku w blokach 20 ms po
końcu pętli powtarza tę po początku aż do końca nagrania (błąd bliski najmniejszemu, potem
najkrótszy okres). Wyniki m.in.: `09` 0,31–37,47 s (błąd 0,04), `03` 52,17 s, `02` 60,0–103,7 s
(43,67 s; bas na złączeniu przesunięty o ok. 0,1 s), `07` 39,81 s, `0A` 58,83 s. Do sprawdzenia:
pętle zaczynające się późno (`04` 95 s, `0E` 242 s, `11` 125 s, `A2` 193 s) i długie efekty bez
pętli (ciągły dźwięk – pętla krótkiego wycinka).

**Polecenia używane przez grę (`tools/snd_calls.py`, listingi `unidasm` w `build/snddis`):**
główny procesor pisze do zatrzasku wprost (`$9F81`, w demie tylko gdy DIP „dźwięk w demie”) albo
wysyła kolejkę `$7E`/`$80` (do 4 poleceń, `$9D04`), do której dopisuje procesor pomocniczy
(`$4741`, wektor `$FE45`). Wartości wpisane w kod i tabele:
- muzyka: `01`, `02`–`09`, `0B` (tabele `$8203`: `06 09 08 04 02 05 01 0B` i `$8235`:
  `07 04 …` według etapu `<$48`), `0C`, `0D`, `0F`, `12`, `15`, `16`, `17`, `19`, `1A`,
  `1C`, `1D`;
- efekty: `81`, `82`, `85`, `88`, `89`, `8A`, `8B`, `8D`, `8E` (strzały według broni, tabela
  `$5614` w banku 0), `8C`, `8F`, `90` (widziane w grze), `91`, `94`, `96`, `97`, `98`, `99`, `9F`,
  `A1`, `A2`, `A3`, `A5`, `A8`, `AC`, `B4`, `B5`, `B8`, `BC`; zatrzymanie `FD`, `FE`, `FF`.

Pozostałe polecenia (np. `B9`, `AA`, `AE`) gra najpewniej nie wysyła; odegrane osobno dają
dziwne dźwięki albo trzymaną nutę bez końca (użytkownik: „sam dziwny szum”). Efekty ciągłe
używane przez grę (`97`, `99`, `A2`, `86`?) trwają, aż zastąpi je inny efekt na tych samych
kanałach albo `FE`/`FF`/`FD` – odtwarzacz ma przydzielać kanały według kanałów YM, których
dany efekt używa (komórki `$02`–`$0D`).

Poprawka przy okazji: `xain_take_audio` przy częstotliwości poniżej połowy FM (41,7 kHz / 2)
wyprzedzała zapis o próbkę, różnica bez znaku zawijała się i funkcja cofała się o 2048 próbek.
Przy 22 030 Hz nie występowało; porównania są teraz ze znakiem.

**Pierwsza wersja (12.10.2026).**
- `tools/snd_loop.py` (nowa wersja): pętla z budowy utworu – okres wzoru skoków każdego kanału
  grającego do końca (od najwcześniejszego skoku, co najmniej 3 powtórzenia), okres całości =
  najmniejsza wielokrotność okresu melodii zgodna z pozostałymi kanałami do 0,15 s (do 130 s),
  początek = najpóźniejszy początek wzoru; gdy to zawiedzie albo pętla kończy się po 150 s –
  zapasowo okres z energii dźwięku (efekty ciągłe `97`, `99`, `9C`, `A2`, `A4`…). Np. `09`
  0,17–37,33 s, `02` 74,30–117,98 s, `12` 29,2–138,9 s (melodia 27,43 s × 4 = perkusja 3,54 s × 31).
- `tools/make_sndpack.py` → `XainSleena.snd`: polecenia używane przez grę + krótkie (do 10 s)
  pozostałe, 71 poleceń, **18,5 MB** (najdłuższe utwory 110–139 s); filtr górnoprzepustowy
  ok. 13 Hz (stała składowa SSG), przenikanie 32 ms na złączeniu pętli, 8 bit znormalizowane do
  szczytu + głośność 0–64 (odtwarza poziom oryginału), wspólne dane identycznych poleceń.
- `port/src/sndplay.c`: 1 głos muzyki + 5 efektów, mieszane programowo do tej samej ścieżki co
  emulacja (Paula: okres **222** = 15 977 Hz, 1:1 bez przeliczania; AHI 22 050 Hz z krokiem
  16.16). Reguły programu dźwiękowego: muzyka, która już gra, jest pomijana; `FF` zatrzymuje
  muzykę, `FE` efekty, `FD` oba, `F0`–`FB` jeden kanał YM; efekt zastępuje efekty na tych samych
  kanałach YM (maska z `$02`–`$0D`), inaczej wolny głos albo najstarszy. Muzyka gra dalej pod
  efektami (w oryginale efekt zabiera muzyce kanały) – świadoma różnica.
- `xain.c`: `sound_cmd` wywoływane przy każdym zapisie `3A08`; z samplami `sound_cpu = 0`.
- PC: domyślnie sample, jeśli jest `XainSleena.snd` (obok programu, katalog wyżej, `.`, katalogi
  ROM), `-samples PLIK`, `-emusound`. Amiga: `SAMPLES=plik`, `EMUSOUND`, plik szukany w
  `ROMPATH` i `PROGDIR:` (+ `rom`, `roms`, `ROM`), ładowany w całości do Fast RAM.
- Porównanie `play1` (78 s, PC, sample vs emulacja): obwiednie skorelowane w **0,933**,
  przesunięcie 10 ms, poziom 81–95%, spektrogramy prawie identyczne. Różnica: po melodii
  startu etapu (`0F`, ok. 41 s) sampel wybrzmiewa ok. 1 s dłużej.
- A1200 060/50, okno `SCALE=2`, `STATS`, demo: **32,9–53,9 kl./s z dźwiękiem** (tytuł
  `Paula smp`) – tyle co bez dźwięku (33–54), wcześniej z emulacją ok. 14–16 (`STATS=FULL`).

Do zrobienia: odsłuch na Amidze przez użytkownika, mniejszy plik (np. ADPCM 4 bit albo krótsze
pętle), wybrzmiewanie po zatrzymaniu, opcja głośności muzyki względem efektów.

**Koszt dźwięku z sampli (A1200 060/100 MHz, `SCALE=1 STATS=FULL`, demo):** miksowanie
(`sndplay_mix`, C) + zamiana na 8 bit + wysyłanie do Pauli = **1,4–2,0% czasu** (pomiar `snd` w
tytule). Emulacja dźwięku zajmowała ok. 40% (przy 50 MHz). Od 12.10.2026 pomiary tylko przy
100 MHz (decyzja użytkownika).

Poprawki po pierwszym odsłuchu: bufory Pauli napełniane z miksera w czasie rzeczywistym (nie
„na klatkę” – przy grze wolniejszej niż 57,44 Hz były przerwy), wzmocnienie 3× przed 8 bit
(muzyka miała ok. 34 poziomy), interpolacja liniowa przy innej częstotliwości (AHI, PC); błąd:
`sndplay_reset` przy każdym klawiszu (brak klamer przy F3) – muzyka startowała od nowa przy
ruchu i strzale z klawiatury (znalezione opcją `SNDLOG`, zapis `RAM:XainSleena-snd.txt`).

**Częstotliwość sampli: 22 030 Hz (12.10.2026, decyzja użytkownika).** Porównanie pasma (rozgrywka
`play1` 48–78 s, udział energii powyżej 8 kHz): emulacja 48 kHz 8,5%, sample 15 977 Hz 1%
(granica ok. 8 kHz), sample 22 030 Hz 3,4% (8–12 kHz: 2,7% wobec 3,9% w emulacji). Film z
YouTube (gra, od 2:45) ma ponad 8 kHz 22% i mniej basu – inny tor nagrania, nie wzorzec.
Odsłuch 8 bit wobec 16 bit (`tools/snd_compare8.py`): bez słyszalnej różnicy; ograniczeniem było
pasmo, nie bity. Zmiany: skrypty czytają częstotliwość z `sndrip.txt`, Paula gra z okresem
liczonym z częstotliwości w pliku (22 030 Hz → 161, ten sam co emulacja), bufor 25 ms;
`snd_loop.py` w metodzie zapasowej bierze najkrótszy okres z błędem bliskim najlepszemu
(trzymany ton: `97` 3,9 s zamiast 113 s). `XainSleena.snd` 25,6 MB (było 18,5 MB). PC `play1`:
obwiednie zgodne z emulacją w 0,935. Na A1200 jeszcze nie sprawdzone (Amiga niedostępna).

**Ranking procedur głównego CPU (12.10.2026, sonda `build/probe/mainprof.c`, rozgrywka `play1`
klatki 1800–4500, rdzeń C z pomijaniem pętli oczekiwania, instrukcje faktycznie wykonane):**
4296 instrukcji na klatkę. Własne instrukcje procedur (z wywołanymi – „razem”):

| Procedura | Udział | Instr./klatkę | Co robi |
|---|---|---|---|
| `86E7` (pętla główna) | 19% | 827 | głównie dwie pętle oczekiwania: `8749`–`8752` (170 obrotów × 5 instr., w każdym zapis `3A09` przez C) i `8727`–`872B` (62 × 3) |
| `DD48` (z `DCEC`, razem 27%) | 17,5% + 8,9% | 750 + 380 | lista sprite'ów: `DCEC` przegląda 16 priorytetów × rekordy obiektów (7 B od `0281`/`0361`), `DD48` zamienia obiekt (pozycja − przewinięcie `<$59`/`<$5C`, tablice metasprite'ów `DE89`) na wpisy sprite'ów od `0100` |
| `8053` (NMI) | 6,3% | 272 | obsługa VBLANK |
| `87E1` + `809B` (FIRQ co 16 linii) | 11% | 257 + 227 | 15 wywołań na klatkę |
| `9A71` | 5,3% | 230 | |
| `8000` | 6,5% | 278 | program główny poza procedurami |

Wnioski: (1) pętle oczekiwania to wciąż ok. 24% wykonanych instrukcji – pomijanie działa w
obrębie kwantu 96 cykli (jedna linia), więc w każdej linii wykonuje się 1–2 obroty i wykrycie
pętli; tańsze byłoby sprawdzanie warunku pętli natywnie, bez wykonywania obrotów. (2) budowa
listy sprite'ów (`DCEC`/`DD48`) – ok. 27% – kandydat na wersję natywną (C/asm) z porównaniem
pamięci sprite'ów w `Lockstep`. (3) Arytmetyka 16-bit i brak liczb zmiennoprzecinkowych – FPU
nic nie da; koszt to rozpoznawanie instrukcji, dostęp do pamięci i flagi.

**Kwanty głównego CPU w pętlach oczekiwania z tablicy (punkt 1 z rankingu, 12.10.2026).**
`run_main` w `xain.c`: kwant (96 cykli) zaczynający się w pętli `8727`–`872B` albo
`8749`–`8752`, gdy jej warunek wyjścia jest fałszywy (`$3E` bit 0, `$3D & $3E` bit 1), DP = 0
i żadne przerwanie nie czeka, nie może opuścić pętli – w kwancie głównego CPU nic innego nie
działa. Jego wynik zależy tylko od PC, A, CC, przeniesionych cykli, długości kwantu, `$3D`,
`$3E` i linii NMI (`3A09` ją kasuje); pierwszy taki kwant wykonuje rdzeń, wynik (PC, PPC, A, CC,
NMI, cykle, licznik pominiętych) trafia do tablicy 64 wpisów w strukturze maszyny (osobno dla
każdej maszyny – `Lockstep`), następne są odtwarzane bez rdzenia. Wyłączane `slice_memo = 0`
(Amiga: `MEMO=OFF`, do pomiarów). Sprawdzenie `build/probe/memocheck.c`: dwie maszyny (z i bez),
8000 klatek z ruchem w cztery strony, strzałami i skokami – pamięć, rejestry i liczniki cykli
głównego i pomocniczego CPU oraz RAM MCU identyczne po każdej klatce. Odtwarzanych kwantów:
**144 z 272 na klatkę** (98% trafień w tablicy). ⏳ Pomiar na A1200 (100 MHz, `STATS=FULL`
z `MEMO=OFF` i bez) i `Lockstep` – Amiga niedostępna.

**Lista sprite'ów głównego CPU przetłumaczona na C (punkt 2, 12.10.2026).** `DCEC`/`DD48`
(ok. 27% instrukcji głównego CPU) działa przy włączonych przerwaniach (FIRQ co 16 linii, NMI)
i trwa ok. 25 kwantów, a CPU pomocniczy w tym czasie zmienia obiekty we wspólnym RAM – wersja
„natywna na raz” zmieniłaby przeplot i grę. Dlatego tłumaczenie instrukcja po instrukcji:
`tools/gen_sprites_c.py` czyta ROM (`DCEC`–`DE88`) i tworzy `port/src/m6809_spr.inc`
(dołączany na końcu `m6809.c`): jeden `case` na adres, argumenty i adresy wyliczone, flagi i
stos przez te same funkcje co rdzeń C (`f8`/`f16`, `push_regs`, `pull_regs`, `tfr_read`/
`tfr_write`, `cond`, `gosub`, `RD`/`WR`), po instrukcji `icount`/`total` o zmierzoną liczbę cykli
(`tools/spritecyc.c` → `tools/sprites_cycles.txt`, para adres → następny adres, bez rozrzutu;
brakujący wariant skoku warunkowego wyliczany jak w rdzeniu: `Bcc` tak samo, `LBcc` +1 przy
skoku). Zatrzymanie na granicy każdej instrukcji (koniec kwantu), zapis pod adres bez pamięci
i instrukcja bez zmierzonych cykli oddają sterowanie rdzeniowi (4 z 205 nigdy niewykonane).
`run_main`: kwant zaczynający się w `DCEC`–`DE88` bez czekającego przerwania idzie przez
`m6809_run_spr`, reszta kwantu po wyjściu z procedury – rdzeń. Wyłączane `hle_sprites = 0`
(Amiga `HLE=OFF`). Sprawdzenie `tools/memocheck.c` (maszyna bez obu skrótów kontra z nimi,
także `ONLY_HLE=1`): 12 000 klatek identycznych po każdej klatce. W rozgrywce ok. 23 kwanty na
klatkę przez kod przetłumaczony.

**Pomiar obu zmian na A1200 (060/100 MHz, `SCALE=1`, demo, 12.10.2026).** `Lockstep` 3000 klatek
(rdzeń C kontra asm, oba ze skrótami) identyczny. Tempo (`STATS`, logika / rysowane w drugiej
połowie dema): bez obu 57,2–57,7 / 17–29; tylko kwanty z tablicy (`HLE=OFF`) 57,1–57,7 / 19–33;
z przetłumaczoną listą sprite'ów 50,9–59,3 / 13–23 – **wolniej**. Kod C z vbcc (rejestry 6809 w
pamięci, wywołania `f8`/`RD`/`WR`) jest wolniejszy od rdzenia asm (rejestry 6809 w rejestrach
68060). Tłumaczenie zostaje, ale domyślnie wyłączone (`hle_sprites = 0`, Amiga `HLE=ON` do
testów); zysk dałoby dopiero tłumaczenie na asm 68k w stylu rdzenia (`gen_6809_68k.py`).
`STATS=FULL` nie pokazał różnic (A 15% w obu wariantach w przewijaniu). Kompilacja: jako część
`m6809.c` vbcc `-O2`/`-O3` trwał wiele minut (wklejanie funkcji pomocniczych 200 razy) – dlatego
osobny plik `m6809_spr.c`, funkcje po 24 instrukcje (`-O3` 5 s).

**Koszt dźwięku 22 030 Hz (A1200 060/100, `SCALE=1 STATS=FULL`, demo):** `snd` 1,77–2,67% czasu
(16 kHz: 1,37–1,96%), więcej w drugiej połowie dema (więcej efektów naraz).

**Koszt przełączania procesorów (A1200 060/100, 12.10.2026, `port/tests/slicecost.c` →
`SliceCost ROMDIR [KLATKI] [WYWOŁANIA]`).** Każda klatka to 272 kwanty (linie), w każdym główny,
pomocniczy CPU i MCU. Sonda na PC (`build/probe/slicestat.c`, rozgrywka): MCU w 258 kwantach
tylko czeka, pomocniczy w ok. 160 zaczyna kwant w `8014 JMP $8014` (czeka na IRQ), główny ok.
144 kwanty z tablicy. Koszt jednego kwantu na A1200: pomocniczy czekający przez rdzeń 8,4–13,1 µs,
MCU czekające 6,6–7,3 µs, główny czekający z tablicy 3,9–6,3 µs (przez rdzeń 20,8–24,6 µs),
`scanline()` 0,8–1,5 µs, `main_fast_update()` 0,2–0,3 µs, samo wejście/wyjście rdzenia asm bez
instrukcji 3,6–3,7 µs.

**Pomocniczy CPU czekający w `8014` bez rdzenia** (`run_sub` w `xain.c`): kwant zaczynający się
na `8014` (bajty `7E 80 14` sprawdzane), bez czekającego przerwania, to tylko powtarzanie
4-cyklowego `JMP` do wyczerpania cykli – liczone wprost (n = (icount+3)/4). Kwant 8,4 → 1,5 µs.
Wyłączane `sub_fast = 0` (Amiga `SUBFAST=OFF`). Sprawdzenie: `tools/memocheck.c` 12 000 klatek
identycznych, `Lockstep` na A1200 3000 klatek identycznych. Cała klatka emulacji (demo od
klatki 1000, bez obrazu): z oboma skrótami 9,2 ms, bez `SUBFAST` 11,0 ms, bez tablicy kwantów
9,7 ms, bez obu 14,0 ms. Gra (`SCALE=1 STATS`, druga połowa dema): rysowane 22–36 kl./s (było
19–33), logika pełne 57,4.
Dalej: MCU czekające (7 µs na kwant, 258 kwantów), druga pętla oczekiwania pomocniczego
`4B91`–`4B95` (ok. 30 kwantów), koszt wejścia do rdzenia asm (3,7 µs).

**Korekta pomiarów `SliceCost` (12.10.2026).** Pierwsze przebiegi `SliceCost` szły, gdy na A1200
działała równolegle gra – liczby bezwzględne w dwóch akapitach wyżej (koszt kwantów 6–24 µs,
cała klatka 9,2 / 11,0 / 14,0 ms) są przez to zawyżone mniej więcej dwukrotnie. Przy zatrzymanej
grze (dwa przebiegi, zgodne): wejście/wyjście rdzenia asm 1,0 µs, pomocniczy czekający przez
rdzeń 4,7 µs (`run_sub` 0,7–0,8 µs), MCU czekające 3,0–3,3 µs, główny czekający z tablicy 1,7 µs
(przez rdzeń 12,0 µs), `scanline()` 0,7–0,8 µs. Cała klatka emulacji bez obrazu (µs):

| Wariant | demo od 1000 | gra od 1800 |
|---|---|---|
| oba skróty | 5 133 | 6 800 |
| bez tablicy kwantów | 4 933–5 000 | 7 800 |
| bez `SUBFAST` | 6 400–6 466 | 7 866 |
| bez obu | 6 333 | 8 933 |

W grze każdy skrót daje ok. 1 ms na klatkę (razem 2,1 ms, 24% emulacji); w demie tablica
kwantów nic nie daje (sprawdzanie przy każdym kwancie kosztuje tyle, ile oszczędza).

**MCU czekające bez `idle_skip`/`timer_update`** (początek `mcu_run`): w pętli oczekiwania, licznik
z dzielnikiem 128 (tak ustawia go `pz-0.113`) i taktowany zegarem, licznik nie dochodzi do zera w
tym kwancie (`dec < tdr`, równoważne temu, że `idle_skip` nie ogranicza kwantu przez `room`) –
cały kwant od razu: cykle, PC w pętli, `prescale` i `tdr` jak w `timer_update`. Inaczej stara
droga. Sprawdzenie `tools/memocheck.c` (porównuje teraz też rejestry, cykle i licznik MCU),
także z wzorcem bez żadnego pomijania pętli (`REF_NOIDLE=1`; wtedy bez liczników `total`, które
przy pomijaniu liczą się inaczej o kilka cykli – tak było już wcześniej, sam stan bez zmian):
12 000 klatek identycznych. `Lockstep` 3000 klatek identycznych. A1200: kwant MCU 3,0 → 1,9 µs,
cała klatka w grze 6 800 → 6 266 µs, w demie 5 133 → 4 866 µs.

## 14. Start, opcje i ikona (12.10.2026)

**`START=EASY` (domyślnie; decyzja użytkownika) – `xain_assist` i `xain_patch_texts` w
`xain.c`.** Nie zmienia emulacji, tylko wejścia i jeden napis:
- monety: gdy gra nie trwa (`$0039` = 0) i kredytów (`$0031`) jest mniej niż 2, impuls na wejściu
  monety (6 klatek, 24 przerwy) – gra sama nalicza kredyt (z dźwiękiem monety); po uruchomieniu
  od razu ekran startu z `CREDIT 2`, także po końcu gry;
- napis `PUSH 1P OR 2P BUTTON` → `LEFT 1P OR RIGHT 2P ` (ROM `pa-09` 7885, bank 1 głównego
  CPU: kolor `02`, adres tekstu `2246` = wiersz 18 kolumna 6, 20 znaków ASCII – na ekranie kod
  ASCII − 20h –, koniec `03`); podmiana w pamięci tylko przy oryginalnych bajtach;
- na ekranie startu (`$0036` = 1, gra nie trwa) joystick w lewo = START 1P, w prawo = START 2P;
- ekran kontynuacji (główny CPU `840B`–`8467`): oryginał pisze tekst 09 (`78BE`, wiersz 8)
  `CONTINUE PLAY?` z `TIME`, potem tekst 0B (`7902`, `PUSH 1P BUTTON`, używany też na ekranie
  startu) albo 26 (`7D88`, `PUSH 2P BUTTON`, tylko tu) z `CREDIT`, i czeka na START 1, gdy
  `$003B` bit 0 = 1, inaczej na START 2 (gracz 1P w grze jednoosobowej widzi `PUSH 2P BUTTON`;
  oba naraz się nie liczą). Rekordy tekstu: kolor, adres, znaki, `00` koniec, `01` nowy adres,
  `03` nowy kolor (drukarka `A02E`). Zmiana w pamięci (tylko przy oryginalnych bajtach): tekst 09
  = `PRESS FIRE TO CONTINUE` (wiersz 8, kolumna 5, kolor 01, mieści się w miejscu starego),
  tekst 26 = `TIME` (kolor 03) i `CREDIT`, a kod zawsze pisze tekst 26: `841B` `LDA #$0B` →
  `#$0D` i `8421` `BNE $8425` → `BNE $8423` (oba warianty dochodzą do `LDA #$26`; tekst 0B na
  ekranie startu bez zmian). **Suma ROM-u musi zostać ta sama:** gra przy starcie dodaje bajty
  `4000`–`EFFF` (bank 0, `8045` → `FFA7`, wynik musi być `01`), a przy złej sumie ustawia
  `$1C1B`. Pierwsza wersja (`841B` → `#$26`, +1B) zmieniała sumę, a skutkiem było to, że
  w etapach lotu po śmierci nie było już przeciwników i etap kończył się sam (zgłoszone
  z A1200; odtworzone sondą `build/probe/shipdeath.c`: lot wymuszony wpisem `$49`/`$4A`/`$53`
  w `828A`, nieskończone życia; tylko zmiana `841B` to powoduje, zmiany w banku 1 nie). Teraz
  +2 −2, suma `01` (sonda `romsum.c`). Pomocniczy CPU ma podobny test (`FFBA`), jego ROM-u
  nie zmieniamy. `xain_assist`: gdy w wierszu 8 jest ten
  napis – brak kredytu → moneta; nowe naciśnięcie ognia (nie przytrzymany z gry) trzyma
  właściwy START. Sprawdzone na PC: ogień kontynuuje (kredyt 1 → 0), bez ognia po `TIME 0`
  koniec gry i ekran startu z `CREDIT 2`.
**Wersja na ekranie startu (START=EASY, od 0.9.1):** zamiast `CREDIT n` napis `RELEASE` + `XAIN_VERSION`
(`xain.h`; ta sama wartość w `$VER` Amigi i tytule okna PC). Tekst 07 (start przy kredytach ≥ 2, wskaźnik
bank 1 `77CE` → `7885`) przeniesiony do nieużywanych zer przed tablicą wskaźników (bank 1 `7790`;
`build/probe/freecheck.c`: 40 000 klatek identycznych ze zmienionymi `778B`–`77BF`), liczba kredytów
(`8188 LDA #$04 / JSR $A154`) wyłączona: `LDA #$9E / JSR $80DB` (RTS), suma ROM-u bez zmian (`romsum`:
`01`). Przy 1 kredycie (tekst 0B) zostaje `CREDIT` bez liczby – z monetami EASY tylko na chwilę.

`START=ARCADE` (PC `-arcade`): jak automat. Gracze w trybie 2P grają na zmianę tymi samymi
kontrolerami (wejścia P2 automatu są tylko dla obudowy cocktail), klawiatura i joystick działają
jednocześnie.

**`LIVES=3|4|6|INF`** (PC `-lives`): przełączniki DIP (DSW1 bity 7–6: C0, 80, 40, 00 =
nieskończone życia, w MAME „Infinite (Cheat)”).

**`DIFFICULTY=NORMAL|EASY|VERYEASY`** (PC `-easy`, `-veryeasy`; domyślnie NORMAL, nie z oryginału;
VERYEASY od 0.9.5 = ćwierć obrażeń, `easy_damage = 2`, dzielnik 4 w `dmg_acc`): EASY = połowa
obrażeń na piechotę (pasek `DAMAGE GAGE`). Zdrowie to `$0056`: bity 0–5 (pełne `10`, start
`849F`, w etapach lotu `0`, tam jedno trafienie zabija), bit 7 = przerysuj pasek, bit 6 = strona
odrzutu. Obrażenia zadaje pomocniczy CPU w banku 1 `7015` (wejście `FE36` → `FF00`: `STB $3000`
bank 1, `JSR $7000`, `STB $3000` bank 0): przy styku z obiektem odejmuje wartość ze stosu
(widziane 1 i 4), przy 0 ustawia `$1073` bit 0 (śmierć, obsługa `82A0`). Sondy:
`build/probe/damage.c` (pasek = tekst `08D`–`094`, znaki `BB`/`BC`/`BD`), `hpwrite.c` (kto pisze
`$56`). Bez zmiany ROM-u (sumy kontrolne, patrz wyżej): `easy_damage` w `xain.c` przy zapisie
banku z PC `FF05` dodaje 16 do zdrowia (procedura sama nigdy nie zabija), przy zapisie z PC
`FF0D` liczy obrażenia z różnicy i odejmuje połowę (`dmg_acc`: przy 1 co drugie trafienie),
przy 0 sama ustawia `$56 = 0` i `$1073` bit 0. Oba rdzenie zapisują PC za instrukcją (asm:
`wr_slow`). Sprawdzenie: `hpwrite` z EASY – 1 → 0, 1, 0…, 4 → 2, gracz w `play1` żyje dłużej;
`memocheck` z `EASYDMG=1` 9000 klatek identycznych.

**Ikona** `port/amiga/XainSleena.info` (Makefile kopiuje do `build/amiga`): 64×56 z kadru PC
(klatka 1900, ekran MISSION, Xain), obraz 4-kolorowy i ColorIcon w 22 kolorach gry
(`tools/make_icon.py` i `tools/icon_tooltypes.py` z Vastar). Tool Types: `SCREEN=FULL`
(domyślnie pełny ekran 320×240), `SCALE=1`, `LIVES=3`, wyłączone `(SCREEN=WINDOW)`, `(SCALE=2)`, `(LIVES=4)`, `(LIVES=6)`, `(LIVES=INF)`,
`CONTROL=JOY1`, `(CONTROL=JOY2)`, `(CONTROL=KEYBOARD)`, `(START=ARCADE)`, `(SOUND=AHI)`, `(NOSOUND)`, `(EMUSOUND)`, `(JOYPORT=BOTH)`, `(STATS)`,
`(ROMPATH=Work:roms)`. Sprawdzone na A1200 (z Shella): ekran startu z napisem i `CREDIT 2`,
strzałka w lewo startuje grę 1P.

**Sterowanie: `CONTROL=JOY1|JOY2|KEYBOARD`.** Próby na PC (dżungla, klatka 3400 `play1`): skok
z górą = sam skok, sama góra – nic, strzał z górą – poziomo, dół – strzał w przykucnięciu, więc na
piechotę góra nie ma własnej funkcji. Kod procesora pomocniczego: obsługa gracza `823D` sprawdza
`<$53` bit 7 (DP = 0, czyli wspólny RAM `$0053`): 0 → na piechotę (`8355`, przycisk 2 = skok),
1 → statek (`82CC` → `8A75` → `8AAB`: przycisk 1 tworzy obiekt 6 – strzał, przycisk 2 obiekt 7,
tylko gdy sloty `$1848`–`$184B` są wolne – dodatkowa, ograniczona broń). `$0053` ustawia główny
CPU na początku etapu (`84DD`–`84F3` z tablicy `85B0`; `8519`/`852F` – etapy lotu). Scena: IRQ
sub `8020` skacze według `<$41` (1 = gra `804A`, 3 = mapa planet – bank 0 `7D04`, dowolny
przycisk wybiera planetę); w grze na piechotę `<$52 & 0F` = 2 to ruch gracza.
`xain_on_foot()` = gra trwa, `$0041` = 1, `$0053` bit 7 = 0, `$0052 & 0F` = 2. Pierwsza wersja
używała `$0048` (rodzaj etapu) jako znacznika mapy – błąd: typ 0 to też planeta, więc na części
planet góra nie skakała (zgłoszenie użytkownika z A1200). Próba `onfoot` (12000 klatek z ruchem
w górę): na mapie 0 klatek z `on_foot`, z procedurą gracza na piechotę 5756 z `on_foot` i 217 bez
(wejście na planetę, śmierć). Etapu lotu nie sprawdziłem w działaniu – wniosek z kodu. Góra
skacze w JOY1 dla każdego typu urządzenia (wcześniej tylko `JP_TYPE_JOYSTK`).
- `CONTROL=JOY1` (domyślnie, joystick z jednym przyciskiem): ruch i strzał; **na piechotę góra =
  skok** (góra + przycisk 2), w lotach i na mapie góra to tylko ruch, a **Spacja** (przycisk 2)
  odpala dodatkową broń; niebieski przycisk joysticka pomijany (przypadkowe sygnały na pinie 9);
  pad CD32 – niebieski = przycisk 2;
- `CONTROL=JOY2`: joystick z dwoma przyciskami (drugi przycisk na pinie 9, `lowlevel.library`
  zgłasza go jako `JPF_BUTTON_BLUE`) = przycisk 2 (skok / broń), góra tylko ruch;
- `CONTROL=KEYBOARD`: port joysticka nie jest czytany (bez `ReadJoyPort`).

Klawiatura działa zawsze, od 0.9.3 według **standardu klawiszy** z `PORTING_PLAN.md`: strzałki =
ruch, **Z** = przycisk 1 (strzał), **X** = przycisk 2 (skok, w lotach broń), **P** = pauza; do tego
1 / 2 = start, 5 / 6 = moneta, F3 = reset, F10 = zapis stanu, Esc = wyjście. Zamienniki Ctrl / Alt /
Spacja usunięte; Spacja zostaje tylko przy `CONTROL=JOY1` jako drugi przycisk joysticka. Od 0.9.4
klawiatura jak joystick przy JOY1: strzałka w górę na piechotę = skok (`xain_on_foot`), w lotach tylko
ruch; **Y** = przycisk 2 (dodatkowa broń); X dalej przycisk 2. Wersja PC:
tylko klawiatura (obsługa pada SDL usunięta), te same klawisze. Tool Types ikony: `CONTROL=JOY1`,
`(CONTROL=JOY2)`, `(CONTROL=KEYBOARD)`.

**Skok w bok górą (zgłoszenie 15.10.2026, 0.9.6).** „W skoku nie da się zmienić kierunku, tylko
w górę; ze strzałem już tak.” Pomiar na PC (`xain_test`, klatka 3400 na planecie, X gracza =
`$00A9` + przewinięcie `$005A`): gra bierze kierunek skoku tylko w klatce przycisku 2 i w następnej
– prawo w +0/+1 → X 107 → 157, prawo w +2 i później → skok pionowy (107); w powietrzu kierunku
nie zmienia ani strzał, ani puszczenie góry. Strzał niczego nie odblokowuje – strzelając zwykle
trzyma się już kierunek, więc skok idzie w bok od razu. Na klawiaturze strzałka w górę wyprzedza
strzałkę w bok zwykle o więcej niż klatkę (joystick przechodzący na skos też potrafi). Poprawka:
`xain_up_jump()` (`xain.c`, Amiga: klawiatura i JOY1, PC: klawiatura) – przy skoku górą przycisk 2
czeka do `UP_JUMP_WAIT` = 3 klatek na lewo/prawo (od razu, gdy kierunek już jest). Sprawdzenie
(symulacja z `xain_up_jump`): prawo w +2…+4 → 157 (wcześniej 107), +5 → 107. Klawisz X / Spacja /
drugi przycisk bez zmian (bez opóźnienia). Koszt: pionowy skok górą ok. 3 klatki później.

**Zawieszenia w grze (zgłoszenie 12.10.2026, dwa razy na A1200, start z ikony).** Obraz stoi
(dżungla, w trakcie gry), muzyka z sampli gra dalej, sterowanie nie działa. Strażnik nie zapisał
`RAM:XainSleena-hang.txt`, a Esc zamknął grę od razu – pętla programu działała, stanęła emulowana
gra albo rysowanie. Klawisz P (pauza) obrazu nie ruszył. Przyczyna jeszcze nieznana. Narzędzie:
`xain_save_state` / `xain_load_state` (`xain.c`) – przenośny stan maszyny (big endian, 21 021 B:
trzy 6809, MCU z RAM i licznikiem, cały RAM, rejestry, liczniki YM, tablica `memo`); na Amidze
**F10** zapisuje `RAM:XainSleena-state.bin` i linię z PC procesorów i opcjami do
`RAM:XainSleena-state.txt` (mignięcie ekranu), na PC `xainsleena -loadstate PLIK` oraz próba
`build/probe/staterun` (z plikiem: dalsza gra z szybkimi ścieżkami i bez nich). Sprawdzone: stan
zapisany po 4000 klatkach i wczytany do drugiej maszyny – 5000 dalszych klatek identycznych; stan
z A1200 (F10 po 382 klatkach) wczytuje się na PC.

**Przyczyna znaleziona (F10 z trzeciego zawieszenia, klatka 26 327):** stan stoi tak samo
z szybkimi ścieżkami i bez nich – główny CPU w `8727` (czeka na `$3E` bit 0 od pomocniczego),
pomocniczy w banku 0 `4B91` (czeka na `$3D` bit 3), `$3D = 40`. Protokół: pomocniczy wpisuje
polecenie do `$42`, `CLR $2000` (IRQ głównego) aż zobaczy `$3D` bit 2, potem czeka na bit 3;
obsługa IRQ głównego (`80B9`) ustawia bit 2, wykonuje polecenie, kasuje bit 2 i ustawia bit 3.
Pętla główna zmienia `$3D` przez `LDA <$3D / ORA #n / STA <$3D` (`8703`, `8743`, `879B`, `87C0`)
przy włączonych przerwaniach: IRQ po `LDA`, a przed `STA` – `STA` kasuje bity 2 i 3 (zostaje `40`
z `8703`) i oba CPU czekają w nieskończoność. Histogram przerwanych adresów (`build/probe/irqrace.c`,
20 000 klatek losowej gry): IRQ trafia w kod pętli głównej, raz w `8709` (zaraz za `STA`); w 8 ×
80 000 klatek 4 trafienia w `8705`/`8707`. **Standardowy przebieg `memocheck` (play1, 9000
klatek) też się tak zawieszał** (main `8729`, sub `4B95`, `$3D = 40`; stąd 154 zamiast 58
powtórzonych kwantów na klatkę) – nikt tego nie zauważył, bo porównanie szybkich ścieżek
z wolnymi wychodziło identyczne. W MAME (`set_perfect_quantum`) przerwania przychodzą
w innych chwilach, więc może się to nie zdarzać albo zdarza rzadziej; na automacie też jest
możliwe, ale zależy od faz zegarów. Poprawka (`xain_init`): rdzeń nie przyjmuje IRQ głównego CPU
na adresach `8705 8707 8745 8747 879D 879F 87C2 87C4` (`M6809.irq_hold`, sprawdzane w `pending()`
rdzenia C; rdzeń asm przyjmuje przerwania przez krok rdzenia C, więc działa tak samo) – IRQ
wchodzi 1–2 instrukcje później. Z poprawką `memocheck` 9000 klatek identycznych bez
zawieszenia (main `A25D`, sub `8014`, `$3D = 43`), `NOHOLD=1` odtwarza stary błąd. Podobny
zapis-odczyt w `AC3B`–`AC5C` (zapamiętanie i odtworzenie `$3D` wokół dłuższej procedury) nie
jest chroniony.

## 15. Wydanie w repo (3.11.2026)

Układ wydań jak w portach Settlers i OpenDUNE (decyzja użytkownika 3.11.2026, `PORTING_PLAN.md`,
„Wydanie w repo”). Jeden skrypt `sh tools/make_release.sh [--no-full]`:

- numer z `XAIN_VERSION` (`port/src/xain.h`); odmawia, gdy `build/amiga/XainSleena` nie ma
  `$VER: XainSleena <v> (`, a `build/port/XainSleena.exe` napisu `Xain'd Sleena <v>` (tytuł okna);
- `releases/amiga/XainSleena/` (w gicie): program, ikona (`port/amiga/XainSleena.info`),
  szuflada `doc/` z ikoną `doc.info` (wszystkie teksty: opisy PL/EN, `_dev`, `_release`), pusta
  `rom/` z `ReadMe.txt` (`docs/release/amiga/rom_ReadMe.txt`); obok `releases/amiga/XainSleena.info`
  (ikona szuflady; obie z `tools/make_doc_icon.py --drawer` i `tools/icon/drawer.png` z Settlers);
- `releases/pc/XainSleena/` (w gicie): `XainSleena.exe`, `SDL2.dll`, `doc/`, pusta `rom/` z ReadMe;
- `releases/amiga/XainSleena-<v>.zip`, `releases/pc/XainSleena-<v>-pc.zip` (w gicie, tylko bieżące;
  starsze: `git rm --cached` i przeniesienie do `private/old/`);
- `private/XainSleena-<v>-full.zip` (+ `rom/xsleena/` z `ROM/xsleena` i `XainSleena.snd`)
  i `private/XainSleena-<v>-pc-full.zip` (+ `rom/xsleena.zip` składany z `ROM/xsleena`
  i `XainSleena.snd`) – tylko na tym PC, `private/` w `.gitignore`; starsze do `private/old/`;
- ZIP-y przez `tools/make_zip.py` (uprawnienia Unix: program 755, reszta 644, wpisy katalogów –
  inaczej UnZip na Amidze rozpakuje program bez bitu E);
- `tools/check_public.py` sprawdza drzewa i ZIP-y publiczne: plik o nazwie lub treści (SHA-1)
  pliku z `ROM/` albo `XainSleena.snd`, a także każdy `*.zip`, `*.lha`, `*.snd`, `*.bin`, `*.cfg`,
  `*.key` zatrzymuje wydanie.

Na Amidze: `UnZip XainSleena-<v>.zip -d Work:Games`, potem `xsleena.zip` do `XainSleena/rom`.
Pierwsze wydanie w tym układzie: 0.9.17 (binarki z 25.10.2026, bez przebudowy). Stare paczki 0.9.5
(`XainSleena_Amiga.lha`, `_Amiga_full.lha`, `XainSleena_PC.zip`, `_PC_full.zip`, katalogi
`releases/amiga`, `releases/pc`, `releases/pc_full`) przeniesione do `private/old/0.9.5/`.
Pliki rdzenia MiSTer (`releases/*.rbf`, `*.mra`) zostają w `releases/`, poza gitem.

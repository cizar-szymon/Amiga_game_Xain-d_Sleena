# Xain'd Sleena port - PC build (MSYS2 UCRT64: gcc, SDL2, pkg-config)
#
#   make            build ../build/port/xainsleena.exe and the test programs
#   make cputest    lockstep tests of the CPU cores against MAME traces (needs build/ref/cpu)

OUT      := ../build/port

CC       := gcc
CFLAGS   := -std=c99 -O2 -Wall -Wextra -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS   := $(shell pkg-config --libs sdl2) -mconsole
HEADERS  := $(wildcard src/*.h)

CORE     := src/m6809.c src/m6805.c src/mcu68705.c src/ym2203.c src/xain.c src/video.c src/romload.c src/sndplay.c

all: $(OUT)/xainsleena.exe $(OUT)/SDL2.dll $(OUT)/trace6809.exe $(OUT)/trace6805.exe $(OUT)/xain_test.exe $(OUT)/ymbench.exe $(OUT)/renderbench.exe

# so that the program also starts outside the MSYS2 shell
$(OUT)/SDL2.dll: | $(OUT)
	cp "$$(dirname "$$(command -v sdl2-config)")/SDL2.dll" $@

$(OUT)/xainsleena.exe: $(CORE) src/main_sdl.c $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(filter %.c,$^) $(SDL_LIBS) -o $@

$(OUT):
	mkdir -p $@

$(OUT)/trace6809.exe: src/m6809.c tests/trace6809.c src/m6809.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/trace6805.exe: src/m6805.c tests/trace6805.c src/m6805.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/xain_test.exe: $(CORE) tests/xain_test.c $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(CORE) tests/xain_test.c -o $@

$(OUT)/ymbench.exe: src/ym2203.c tests/ymbench.c src/ym2203.h | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/renderbench.exe: $(CORE) tests/renderbench.c $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) $(CORE) tests/renderbench.c -o $@

.PHONY: all

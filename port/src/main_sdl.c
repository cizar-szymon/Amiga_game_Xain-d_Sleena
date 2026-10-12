/*
 * Xain'd Sleena port - SDL2 frontend for the PC (development and testing).
 *
 *   xainsleena [options]
 *
 *   -rompath DIR        where to look for xsleena.zip, xsleena/ or the loose ROM files; may be
 *                       repeated, default: . roms rom ROM (and next to the program)
 *   -scale N            window scale (default 3)
 *   -dsw0 HH -dsw1 HH   DIP switches (hex, default 3F FF as MAME)
 *   -slice N            CPU time slice in 6809 cycles (divides 96, default 96)
 *   -frames N           quit after N frames
 *   -fast               no 57.44 Hz pacing
 *   -headless           no window and no sound output (for automated runs)
 *   -nosound            no sound (the YM2203 timers still run)
 *   -wav FILE           also write the sound to a WAV file (mono, 16 bit)
 *   -rate HZ            sample rate of the sound output and the WAV file (default 44100)
 *   -fmdiv N            FM output every Nth sample: 1 = exact (default), 2 = as on the Amiga
 *   -samples FILE       sound from samples (tools/make_sndpack.py); default: XainSleena.snd
 *                       next to the program, one or two folders up, in . or in a ROM folder
 *   -emusound           sound from the emulated sound CPU and YM2203 even if samples exist
 *   -arcade             start as the arcade: insert coins (5 / 6), PUSH 1P OR 2P BUTTON (1 / 2);
 *                       default: coins are inserted while no game runs, LEFT 1P OR RIGHT 2P
 *   -lives N            lives (DIP switches): 3, 4, 6 or inf
 *   -shots DIR LIST     save the screen as DIR/frame_NNNNN.bmp at these frames
 *                       (LIST: 100,200,1000-1100/10); frame N = after N+1 frames, as in MAME
 *   -dump DIR LIST      also save the memory state as DIR/frame_NNNNN.bin (snapinfo.py format)
 *   -script LIST        timed inputs, e.g. "1200:+coin1,1206:-coin1" (names as in
 *                       tools/mame_capture.py: coin1 coin2 start1 start2 up down left right fire
 *                       jump p2up p2down p2left p2right p2fire p2jump); effective one frame
 *                       after frame N, as with tools/mame_capture.py
 *   -stats              print the emulation time per frame every 2 seconds
 *
 * Keys: cursor keys = joystick, Left Ctrl / Z / Space = fire, Left Alt / X = jump,
 *       5 / 6 = coin, 1 / 2 = start, P = pause, F3 = reset, Esc = quit.
 * A game controller works too (A = fire, B = jump, Back = coin, Start = start).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "xain.h"
#include "video.h"
#include "romload.h"
#include "sndplay.h"

static Xain machine;
static Video video;
static uint32_t rgb[SCREEN_W * SCREEN_H];

/* ---- options ---- */

static const char *rompaths[16];
static int nrompaths;
static int scale = 3, fast, headless, stats, dsw0 = -1, dsw1 = -1, slice = 0;
static int nosound, rate = 44100, fmdiv = 1, emusound;
static const char *samples_path;
static SndPlay snd;
static uint8_t *snd_pack;
static int use_samples;
static int arcade, lives = -1;
static const char *wav_path;
static FILE *wav_fp;
static unsigned long wav_bytes;
static int16_t audio[8192];
static long max_frames = -1;
static const char *shot_dir, *dump_dir;
static long *shot_frames, *dump_frames;
static int shot_count, dump_count;

typedef struct { long frame; uint8_t *port; uint8_t bit; int press; } ScriptEvent;
static ScriptEvent script[4096];
static int script_count;

static uint8_t kb_p1, kb_p2, pad_p1, pad_p2, script_p1, script_p2;

static void usage(void)
{
    fprintf(stderr, "usage: xainsleena [-rompath DIR] [-scale N] [-dsw0 HH] [-dsw1 HH] [-slice N]\n"
                    "                  [-frames N] [-fast] [-headless] [-shots DIR LIST] [-dump DIR LIST]\n"
                    "                  [-script LIST] [-stats] [-nosound] [-wav FILE] [-rate HZ]\n");
    exit(2);
}

static int input_bit(const char *name, uint8_t **port, uint8_t *bit)
{
    static const struct { const char *name; int p2; uint8_t bit; } names[] = {
        { "right", 0, IN_RIGHT }, { "left", 0, IN_LEFT }, { "up", 0, IN_UP }, { "down", 0, IN_DOWN },
        { "fire", 0, IN_BUTTON1 }, { "jump", 0, IN_BUTTON2 },
        { "start1", 0, IN_P1_START1 }, { "start2", 0, IN_P1_START2 },
        { "p2right", 1, IN_RIGHT }, { "p2left", 1, IN_LEFT }, { "p2up", 1, IN_UP }, { "p2down", 1, IN_DOWN },
        { "p2fire", 1, IN_BUTTON1 }, { "p2jump", 1, IN_BUTTON2 },
        { "coin1", 1, IN_P2_COIN1 }, { "coin2", 1, IN_P2_COIN2 }
    };
    int i;
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (!strcmp(name, names[i].name)) {
            *port = names[i].p2 ? &script_p2 : &script_p1;
            *bit = names[i].bit;
            return 1;
        }
    return 0;
}

static void parse_script(const char *s)
{
    char *buf = malloc(strlen(s) + 1), *tok;
    strcpy(buf, s);
    for (tok = strtok(buf, ","); tok && script_count < 4096; tok = strtok(0, ",")) {
        ScriptEvent *e = &script[script_count];
        char *colon = strchr(tok, ':');
        if (!colon || (colon[1] != '+' && colon[1] != '-') || !input_bit(colon + 2, &e->port, &e->bit)) {
            fprintf(stderr, "bad script entry: %s\n", tok);
            exit(2);
        }
        e->frame = atol(tok);
        e->press = colon[1] == '+';
        script_count++;
    }
    free(buf);
}

/* "100,200,1000-1100/10" */
static int parse_frames(const char *s, long **out)
{
    char *buf = malloc(strlen(s) + 1), *tok;
    int n = 0, cap = 64;
    long *f = malloc(sizeof(long) * (size_t)cap);
    strcpy(buf, s);
    for (tok = strtok(buf, ","); tok; tok = strtok(0, ",")) {
        long a = atol(tok), b = a, step = 1, k;
        char *dash = strchr(tok, '-'), *slash = strchr(tok, '/');
        if (dash) b = atol(dash + 1);
        if (slash) step = atol(slash + 1);
        if (step < 1) step = 1;
        for (k = a; k <= b; k += step) {
            if (n == cap) { cap *= 2; f = realloc(f, sizeof(long) * (size_t)cap); }
            f[n++] = k;
        }
    }
    free(buf);
    *out = f;
    return n;
}

static int in_list(const long *list, int n, long v)
{
    int i;
    for (i = 0; i < n; i++)
        if (list[i] == v) return 1;
    return 0;
}

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), ""))
        if (!strcmp(a, "-rompath")) { if (nrompaths < 8) rompaths[nrompaths++] = NEXT(); }
        else if (!strcmp(a, "-scale")) scale = atoi(NEXT());
        else if (!strcmp(a, "-dsw0")) dsw0 = (int)strtol(NEXT(), 0, 16);
        else if (!strcmp(a, "-dsw1")) dsw1 = (int)strtol(NEXT(), 0, 16);
        else if (!strcmp(a, "-slice")) slice = atoi(NEXT());
        else if (!strcmp(a, "-frames")) max_frames = atol(NEXT());
        else if (!strcmp(a, "-fast")) fast = 1;
        else if (!strcmp(a, "-headless")) headless = fast = 1;
        else if (!strcmp(a, "-stats")) stats = 1;
        else if (!strcmp(a, "-nosound")) nosound = 1;
        else if (!strcmp(a, "-wav")) wav_path = NEXT();
        else if (!strcmp(a, "-rate")) rate = atoi(NEXT());
        else if (!strcmp(a, "-fmdiv")) fmdiv = atoi(NEXT());
        else if (!strcmp(a, "-samples")) samples_path = NEXT();
        else if (!strcmp(a, "-emusound")) emusound = 1;
        else if (!strcmp(a, "-arcade")) arcade = 1;
        else if (!strcmp(a, "-lives")) { const char *v = NEXT(); lives = !strcmp(v, "inf") ? 0 : atoi(v); }
        else if (!strcmp(a, "-script")) parse_script(NEXT());
        else if (!strcmp(a, "-shots")) { shot_dir = NEXT(); shot_count = parse_frames(NEXT(), &shot_frames); }
        else if (!strcmp(a, "-dump")) { dump_dir = NEXT(); dump_count = parse_frames(NEXT(), &dump_frames); }
        else usage();
#undef NEXT
    }
    if (scale < 1) scale = 1;
}

/* ---- ROMs ---- */

static int load_roms(void)
{
    static char paths[64][512];
    static char exe_paths[6][512];
    const char *src[64];
    int n = 0, i, d;
    if (nrompaths == 0) {
        rompaths[nrompaths++] = ".";
        rompaths[nrompaths++] = "roms";
        rompaths[nrompaths++] = "rom";
        rompaths[nrompaths++] = "ROM";
        {
            char *base = SDL_GetBasePath();
            if (base) {
                static const char *const rel[6] = { "", "rom", "roms", "ROM", "../../rom", "../../ROM" };
                for (i = 0; i < 6; i++) {
                    path_join(exe_paths[i], 512, base, rel[i]);
                    rompaths[nrompaths++] = exe_paths[i];
                }
                SDL_free(base);
            }
        }
    }
    for (d = 0; d < nrompaths && n + 3 <= 64; d++) {
        path_join(paths[n], 512, rompaths[d], "xsleena.zip");
        src[n] = paths[n];
        n++;
        path_join(paths[n], 512, rompaths[d], "xsleena");
        src[n] = paths[n];
        n++;
        snprintf(paths[n], 512, "%s", rompaths[d]);
        src[n] = paths[n];
        n++;
    }
    if (xain_load_roms(&machine, src, n)) {
        char msg[2048];
        int len = snprintf(msg, sizeof msg, "Cannot load the ROMs of Xain'd Sleena.\n\nPut xsleena.zip or "
                           "the ROM files into a rom folder next to the program or give -rompath DIR.\n\nLooked in:\n");
        for (d = 0; d < nrompaths && len < (int)sizeof msg - 1; d++)
            len += snprintf(msg + len, sizeof msg - (size_t)len, "%s\n", rompaths[d]);
        fprintf(stderr, "%s", msg);
        if (!headless)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Xain'd Sleena", msg, 0);
        return 0;
    }
    return 1;
}

/* ---- sound from samples ---- */

static void snd_cmd(void *ctx, uint8_t cmd)
{
    sndplay_command((SndPlay *)ctx, cmd);
}

static int try_samples(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;
    if (!fp) return 0;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    snd_pack = (uint8_t *)malloc((size_t)size);
    if (!snd_pack || fread(snd_pack, 1, (size_t)size, fp) != (size_t)size ||
        sndplay_init(&snd, snd_pack, (uint32_t)size)) {
        fclose(fp);
        free(snd_pack);
        snd_pack = 0;
        fprintf(stderr, "%s: not a sample file\n", path);
        return 0;
    }
    fclose(fp);
    printf("sound from samples: %s (%lu commands)\n", path, (unsigned long)snd.ncmds);
    return 1;
}

static int load_samples(void)
{
    char path[512];
    int i;
    if (samples_path)
        return try_samples(samples_path);
    {
        char *base = SDL_GetBasePath();
        if (base) {
            static const char *const rel[3] = { "XainSleena.snd", "../XainSleena.snd", "../../XainSleena.snd" };
            for (i = 0; i < 3; i++) {
                path_join(path, sizeof path, base, rel[i]);
                if (try_samples(path)) { SDL_free(base); return 1; }
            }
            SDL_free(base);
        }
    }
    if (try_samples("XainSleena.snd")) return 1;
    for (i = 0; i < nrompaths; i++) {
        path_join(path, sizeof path, rompaths[i], "XainSleena.snd");
        if (try_samples(path)) return 1;
    }
    return 0;
}

/* ---- files ---- */

static void put16(FILE *fp, unsigned v) { fputc((int)(v & 0xff), fp); fputc((int)((v >> 8) & 0xff), fp); }
static void put32(FILE *fp, unsigned long v) { put16(fp, (unsigned)(v & 0xffff)); put16(fp, (unsigned)(v >> 16)); }
static void put32be(FILE *fp, unsigned long v)
{
    fputc((int)(v >> 24), fp); fputc((int)((v >> 16) & 0xff), fp);
    fputc((int)((v >> 8) & 0xff), fp); fputc((int)(v & 0xff), fp);
}

static void save_bmp(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int x, y;
    unsigned long size = (unsigned long)SCREEN_W * 3 * SCREEN_H;
    if (!fp) { perror(path); return; }
    fputc('B', fp); fputc('M', fp);
    put32(fp, 54 + size); put32(fp, 0); put32(fp, 54);
    put32(fp, 40); put32(fp, SCREEN_W); put32(fp, SCREEN_H); put16(fp, 1); put16(fp, 24);
    put32(fp, 0); put32(fp, size); put32(fp, 2835); put32(fp, 2835); put32(fp, 0); put32(fp, 0);
    for (y = SCREEN_H - 1; y >= 0; y--)
        for (x = 0; x < SCREEN_W; x++) {
            const uint8_t *c = video.rgb[video.fb[y * SCREEN_W + x]];
            fputc(c[2], fp); fputc(c[1], fp); fputc(c[0], fp);
        }
    fclose(fp);
}

static void wav_header(FILE *fp, unsigned long bytes)
{
    fwrite("RIFF", 1, 4, fp); put32(fp, 36 + bytes); fwrite("WAVEfmt ", 1, 8, fp);
    put32(fp, 16); put16(fp, 1); put16(fp, 1); put32(fp, (unsigned long)rate); put32(fp, (unsigned long)rate * 2);
    put16(fp, 2); put16(fp, 16); fwrite("data", 1, 4, fp); put32(fp, bytes);
}

static void chunk(FILE *fp, const char *name, const uint8_t *data, unsigned long len)
{
    char n[16];
    memset(n, 0, sizeof n);
    memcpy(n, name, strlen(name) < sizeof n ? strlen(name) : sizeof n);
    fwrite(n, 1, 16, fp);
    put32be(fp, len);
    fwrite(data, 1, len, fp);
}

/* same format as tools/mame_capture.lua (read with tools/snapinfo.py) */
static void save_dump(const char *path)
{
    FILE *fp = fopen(path, "wb");
    uint8_t io[16], ym[2][256], mcu_ram[0x80];
    char regs[1024];
    int i;
    const M6809 *c[3];
    static const char *const tag[3] = { ":maincpu", ":sub", ":audiocpu" };
    if (!fp) { perror(path); return; }
    memset(io, 0, sizeof io);
    for (i = 0; i < 2; i++) { io[i] = machine.scrollx[1][i]; io[2 + i] = machine.scrolly[1][i];
                              io[4 + i] = machine.scrollx[0][i]; io[6 + i] = machine.scrolly[0][i]; }
    io[8] = machine.soundlatch;
    io[13] = machine.flip;
    io[15] = (uint8_t)(machine.pri | (machine.main_bank << 3));
    memcpy(ym[0], machine.ym[0].regs, 256);
    memcpy(ym[1], machine.ym[1].regs, 256);
    memset(mcu_ram, 0, sizeof mcu_ram);
    memcpy(mcu_ram + 0x10, machine.mcu.ram + 0x10, 0x70);
    fwrite("XSNP", 1, 4, fp);
    chunk(fp, "share1", machine.shared, 0x2000);
    chunk(fp, "charram", machine.charram, 0x800);
    chunk(fp, "bgram0", machine.bgram0, 0x800);
    chunk(fp, "bgram1", machine.bgram1, 0x800);
    chunk(fp, "spriteram", machine.spriteram, 0x180);
    chunk(fp, "palette", machine.palette, 0x200);
    chunk(fp, "palette_ext", machine.palette_ext, 0x200);
    chunk(fp, "sound_ram", machine.snd_ram, 0x800);
    chunk(fp, "mcu_ram", mcu_ram, 0x80);
    chunk(fp, "io_main", io, 16);
    chunk(fp, "sub_bank", &machine.sub_bank, 1);
    chunk(fp, "ym1", ym[0], 256);
    chunk(fp, "ym2", ym[1], 256);
    c[0] = &machine.main; c[1] = &machine.sub; c[2] = &machine.snd;
    regs[0] = 0;
    for (i = 0; i < 3; i++) {
        size_t l = strlen(regs);
        snprintf(regs + l, sizeof regs - l, "%s A=%X B=%X CC=%X DP=%X PC=%X S=%X U=%X X=%X Y=%X\n", tag[i],
                 c[i]->a, c[i]->b, c[i]->cc, c[i]->dp, c[i]->pc, c[i]->s, c[i]->u, c[i]->x, c[i]->y);
    }
    {
        size_t l = strlen(regs);
        snprintf(regs + l, sizeof regs - l, ":mcu:mcu A=%X CC=%X PC=%X S=%X X=%X\n", machine.mcu.cpu.a,
                 machine.mcu.cpu.cc, machine.mcu.cpu.pc, machine.mcu.cpu.s, machine.mcu.cpu.x);
    }
    chunk(fp, "regs", (const uint8_t *)regs, (unsigned long)strlen(regs));
    fclose(fp);
}

/* ---- input ---- */

static void set_bit(uint8_t *v, uint8_t bit, int on)
{
    if (on) *v |= bit;
    else *v &= (uint8_t)~bit;
}

static int handle_key(SDL_Keycode k, int down)
{
    switch (k) {
    case SDLK_LEFT: set_bit(&kb_p1, IN_LEFT, down); break;
    case SDLK_RIGHT: set_bit(&kb_p1, IN_RIGHT, down); break;
    case SDLK_UP: set_bit(&kb_p1, IN_UP, down); break;
    case SDLK_DOWN: set_bit(&kb_p1, IN_DOWN, down); break;
    case SDLK_LCTRL: case SDLK_z: case SDLK_SPACE: set_bit(&kb_p1, IN_BUTTON1, down); break;
    case SDLK_LALT: case SDLK_x: set_bit(&kb_p1, IN_BUTTON2, down); break;
    case SDLK_5: set_bit(&kb_p2, IN_P2_COIN1, down); break;
    case SDLK_6: set_bit(&kb_p2, IN_P2_COIN2, down); break;
    case SDLK_1: set_bit(&kb_p1, IN_P1_START1, down); break;
    case SDLK_2: set_bit(&kb_p1, IN_P1_START2, down); break;
    default: return 0;
    }
    return 1;
}

static void read_pad(SDL_GameController *pad)
{
    int ax, ay;
    pad_p1 = pad_p2 = 0;
    if (!pad) return;
    ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -16000) pad_p1 |= IN_LEFT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 16000) pad_p1 |= IN_RIGHT;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || ay < -16000) pad_p1 |= IN_UP;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ay > 16000) pad_p1 |= IN_DOWN;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) pad_p1 |= IN_BUTTON1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) pad_p1 |= IN_BUTTON2;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) pad_p2 |= IN_P2_COIN1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) pad_p1 |= IN_P1_START1;
}

static void apply_script(long frame)
{
    int i;
    for (i = 0; i < script_count; i++)
        if (script[i].frame == frame)
            set_bit(script[i].port, script[i].bit, script[i].press);
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    SDL_Window *window = 0;
    SDL_Renderer *renderer = 0;
    SDL_Texture *texture = 0;
    SDL_GameController *pad = 0;
    SDL_AudioDeviceID audio_dev = 0;
    Uint64 freq, next, t0, t_emu = 0;
    long frame = 0;
    int running = 1, paused = 0, i;

    parse_args(argc, argv);
    if (SDL_Init(headless ? SDL_INIT_TIMER : SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!load_roms())
        return 1;
    xain_init(&machine);
    if (dsw0 >= 0) machine.dsw0 = (uint8_t)dsw0;
    if (dsw1 >= 0) machine.dsw1 = (uint8_t)dsw1;
    if (lives >= 0)                                     /* DSW1 bits 7-6: 3 C0, 4 80, 6 40, infinite 00 */
        machine.dsw1 = (uint8_t)((machine.dsw1 & 0x3f) | (lives == 0 ? 0x00 : lives == 4 ? 0x80 : lives == 6 ? 0x40 : 0xc0));
    if (!arcade) {
        machine.autocoin = machine.joystart = 1;
        xain_patch_texts(&machine);
    }
    if (slice > 0 && XAIN_LINE_CYCLES % slice == 0) machine.slice = slice;
    if (!video_init(&video, &machine)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    machine.render_line = video_render_line;
    machine.render_ctx = &video;
    if (nosound && !wav_path)
        xain_set_sound(&machine, 0);
    else if (!emusound && load_samples()) {
        /* no sound CPU / YM2203: the commands start samples */
        use_samples = 1;
        xain_set_sound(&machine, 0);
        machine.sound_cpu = 0;
        machine.sound_cmd = snd_cmd;
        machine.sound_ctx = &snd;
    }
    xain_set_fm_div(&machine, fmdiv);
    if (rate < 8000 || rate > 96000)
        rate = 44100;
    if (!headless && !nosound) {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof want);
        want.freq = rate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        audio_dev = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
        if (audio_dev)
            SDL_PauseAudioDevice(audio_dev, 0);
        else
            fprintf(stderr, "no sound: %s\n", SDL_GetError());
    }
    if (wav_path) {
        wav_fp = fopen(wav_path, "wb");
        if (wav_fp)
            wav_header(wav_fp, 0);
        else
            perror(wav_path);
    }

    if (!headless) {
        window = SDL_CreateWindow("Xain'd Sleena", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  SCREEN_W * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE);
        renderer = SDL_CreateRenderer(window, -1, 0);
        SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                    SCREEN_W, SCREEN_H);
        for (i = 0; i < SDL_NumJoysticks(); i++)
            if (SDL_IsGameController(i) && (pad = SDL_GameControllerOpen(i)) != 0)
                break;
    }

    apply_script(0);
    freq = SDL_GetPerformanceFrequency();
    next = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                SDL_Keycode k = ev.key.keysym.sym;
                int down = ev.type == SDL_KEYDOWN;
                if (handle_key(k, down)) continue;
                if (!down || ev.key.repeat) continue;
                if (k == SDLK_ESCAPE) running = 0;
                else if (k == SDLK_p) paused = !paused;
                else if (k == SDLK_F3) { xain_reset(&machine); sndplay_reset(&snd); }
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(ev.cdevice.which);
            }
        }
        read_pad(pad);
        machine.in_p1 = (uint8_t)(kb_p1 | pad_p1 | script_p1);
        machine.in_p2 = (uint8_t)(kb_p2 | pad_p2 | script_p2);
        xain_assist(&machine);

        if (!paused) {
            t0 = SDL_GetPerformanceCounter();
            xain_run_frame(&machine);
            if (machine.palette_dirty)
                video_update_palette(&video, &machine);
            {
                int n = use_samples ? sndplay_frame(&snd, audio, (int)(sizeof audio / sizeof audio[0]), (uint32_t)rate)
                                    : xain_take_audio(&machine, audio, (int)(sizeof audio / sizeof audio[0]), rate);
                if (audio_dev && SDL_GetQueuedAudioSize(audio_dev) < (Uint32)rate / 5 * 2)
                    SDL_QueueAudio(audio_dev, audio, (Uint32)n * 2);   /* drop if far ahead */
                if (wav_fp) {
                    fwrite(audio, 2, (size_t)n, wav_fp);              /* little endian PC */
                    wav_bytes += (unsigned long)n * 2;
                }
            }
            t_emu += SDL_GetPerformanceCounter() - t0;
            if (shot_count && in_list(shot_frames, shot_count, frame)) {
                char path[512], file[64];
                snprintf(file, sizeof file, "frame_%05ld.bmp", frame);
                path_join(path, sizeof path, shot_dir, file);
                save_bmp(path);
            }
            if (dump_count && in_list(dump_frames, dump_count, frame)) {
                char path[512], file[64];
                snprintf(file, sizeof file, "frame_%05ld.bin", frame);
                path_join(path, sizeof path, dump_dir, file);
                save_dump(path);
            }
            /* MAME (mame_capture.lua) sets inputs in the frame notifier, after the input ports
               were updated for that frame: they take effect one frame later */
            apply_script(frame - 1);
            frame++;
            if (stats && frame % 115 == 0) {
                printf("frame %ld: %.2f ms per frame\n", frame, (double)t_emu * 1000.0 / freq / 115);
                t_emu = 0;
            }
            if (max_frames >= 0 && frame >= max_frames)
                running = 0;
        }

        if (!headless) {
            void *pixels;
            int pitch;
            if (SDL_LockTexture(texture, 0, &pixels, &pitch) == 0) {
                for (i = 0; i < SCREEN_W * SCREEN_H; i++) {
                    const uint8_t *c = video.rgb[video.fb[i]];
                    rgb[i] = 0xff000000u | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2];
                }
                for (i = 0; i < SCREEN_H; i++)
                    memcpy((uint8_t *)pixels + i * pitch, rgb + i * SCREEN_W, SCREEN_W * 4);
                SDL_UnlockTexture(texture);
            }
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, 0, 0);
            SDL_RenderPresent(renderer);
        }
        if (!fast) {
            Uint64 now;
            next += freq * 1000 / XAIN_FPS_X1000;
            now = SDL_GetPerformanceCounter();
            if (next > now)
                SDL_Delay((Uint32)((next - now) * 1000 / freq));
            else if (now - next > freq / 4)
                next = now;
        }
    }
    if (wav_fp) {
        fseek(wav_fp, 0, SEEK_SET);
        wav_header(wav_fp, wav_bytes);
        fclose(wav_fp);
    }
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    if (pad) SDL_GameControllerClose(pad);
    video_free(&video);
    SDL_Quit();
    return 0;
}

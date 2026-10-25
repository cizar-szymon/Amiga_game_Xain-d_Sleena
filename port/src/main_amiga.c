/*
 * Xain'd Sleena port - AmigaOS frontend: a window on the Workbench (or an own screen).
 *
 * Built on the Vastar / Wizard of Wor Amiga frontends: Intuition window, Picasso96 (BLIT or
 * WPA) or WriteChunkyPixels for the picture, timer.device for the 57.44 Hz pace,
 * lowlevel.library for the joystick, audio.device (Paula, default) or the AHI library.
 * Needs AmigaOS 3.1+ (V40) and a 68020+ (built for the 68060); AHI is optional.
 *
 * Shell:  XainSleena [SCALE=1|2|3] [ROMPATH=dir] [NOSOUND] [SOUND=PAULA|AHI]
 *                    [VIDEO=BLIT|WPA|WCP] [FRAMESKIP=AUTO|n] [SCREEN=WINDOW|FULL]
 *                    [LOADSTATE=file] [DSW0=hh] [DSW1=hh] [SLICE=n] [RENDER=LINE] [FMDIV=1|2] [CPU=C] [STATS|STATS=FULL] [VERIFY]
 *         FRAMESKIP: the game always runs at 57.44 Hz; AUTO (default) leaves out pictures
 *         when the Amiga falls behind, n draws one picture of n+1
 *         the ROMs (xsleena.zip from MAME, an xsleena drawer or the loose files) are looked
 *         for in ROMPATH, PROGDIR:, PROGDIR:rom, PROGDIR:roms and PROGDIR:ROM
 *         [START=EASY|ARCADE] [LIVES=3|4|6|INF]: EASY (default) inserts coins while no game
 *         runs and shows LEFT 1P OR RIGHT 2P (joystick left / right start 1 or 2 players);
 *         ARCADE as the machine (coins 5 / 6, start 1 / 2). LIVES: the DIP switch.
 *         [DIFFICULTY=NORMAL|EASY|VERYEASY]: not the original: EASY halves the damage on foot
 *         (the DAMAGE GAGE bar: a hit of 4 takes 2, a hit of 1 counts every second time),
 *         VERYEASY a quarter (4 takes 1, a hit of 1 counts every fourth time)
 *         [CONTROL=JOY1|JOY2|KEYBOARD]: see below
 *         [SAMPLES=file] [EMUSOUND]: the sound comes from samples (XainSleena.snd, made on the
 *         PC with tools/make_sndpack.py, looked for as SAMPLES, in PROGDIR: and the ROM
 *         drawers); without the file or with EMUSOUND the sound CPU and the YM2203 are emulated
 * Icon:   the same as tool types ("(...)" is switched off)
 *
 * CONTROL=JOY1 (default): joystick in port 2 moves and fires; on foot up jumps, in the flying
 *   stages up only moves; Space is the joystick's second button (the extra weapon)
 * CONTROL=JOY2: joystick with two buttons, the second button jumps
 * CONTROL=KEYBOARD: keyboard only (the joystick port is not read)
 *   (CD32 pad with JOY1 / JOY2: red fire, blue jump, play = start 1, green = coin, yellow = start 2)
 * Keyboard (always; the same keys in every port, PORTING_PLAN.md): cursor keys = move, Z = fire 1,
 *           X = fire 2 (jump / extra weapon); as the joystick with JOY1: on foot up jumps, in the
 *           flying stages up only moves and Y fires the extra weapon; P = pause, 1 / 2 = start,
 *           5 / 6 = coin, F3 = reset,
 *           F10 = save the machine state (diagnostics, RAM:XainSleena-state.bin),
 *           Esc / close / Ctrl-C = quit
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <devices/ahi.h>
#include <devices/audio.h>
#include <clib/alib_protos.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <libraries/lowlevel.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/lowlevel.h>
#include <proto/timer.h>
#include <proto/ahi.h>
#include <libraries/Picasso96.h>
#include <proto/Picasso96.h>
#include <utility/hooks.h>

#include "xain.h"
#include "video.h"
#include "romload.h"
#include "sndplay.h"

const char version[] = "$VER: XainSleena " XAIN_VERSION " (25.10.2026) arcade emulation";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *LowLevelBase;
struct Device *TimerBase;
struct Library *AHIBase;
struct Library *P96Base;

/* the picture: 256x240, blocks of 8x8 */
#define RENDER_W SCREEN_W
#define RENDER_H SCREEN_H
#define RENDER_BW (RENDER_W / 8)
#define RENDER_BH (RENDER_H / 8)

static Xain machine;
static Video video;
/* what the window shows (palette indices): the last presented picture. The renderer draws
   alternately into video.fb_mem and fb_other, so nothing has to be copied (present swaps) */
static uint16_t fb_other[RENDER_W * RENDER_H];
static uint16_t *shown = fb_other;
static int force_full = 1;
static int prev_valid;                /* the incremental bands may compare with the last picture */
static ULONG present_count;

static struct Window *win;
static LONG pens[512];
static int pens_ok;
static int scale = 2;
static UBYTE *chunky;

static struct MsgPort *timer_port;
static struct timerequest *timer_req;
static ULONG eclock_freq;
static uint64_t next_frame;

static UBYTE keys[128];

static int stats, paused, sound = 1;
/* FPS (0.9.16): the pictures drawn in the last second (57 frames of the game) in the bottom right
   corner of the picture, white on black, in blocks (30, 29) and (31, 29); painted in the screen
   format just before the blocks go to the board (16 bit screens with BLIT: the bands and blocks) */
#define FPS_BX 30
#define FPS_BY 29
static int show_fps, fps_value = -1, fps_frames, fps_drawn;
static int cpu_c;                     /* CPU=C: 6809 core in C instead of assembler */
static int fmdiv = 2;                 /* FMDIV=1: FM output at every sample (exact, slower) */
static int emusound;                  /* EMUSOUND: the sound CPU and YM2203 emulated, not samples */
static const char *samples_path;      /* SAMPLES=file */
static int use_samples;               /* sound from XainSleena.snd (sndplay.c) */
static int sndlog;                    /* SNDLOG: log of the sound commands (testing) */
static int memo_off;                  /* MEMO=OFF: no replay of the main CPU's wait slices */
static int subfast_off;               /* SUBFAST=OFF: waiting sub CPU slices on the core */
static int start_arcade;              /* START=ARCADE: coins and start as the arcade */
static int lives = -1;                /* LIVES=3|4|6|INF (0) */
static int easy_damage;               /* DIFFICULTY=EASY (1) / VERYEASY (2) / NODAMAGE (3) */
enum { CTL_KEYBOARD, CTL_JOY1, CTL_JOY2 };
static int control = CTL_JOY1;         /* CONTROL=KEYBOARD / JOY1 (up jumps) / JOY2 */
static int hle_on;                    /* HLE=ON: the sprite list routine translated to C (slower) */
static SndPlay snd;
static UBYTE *snd_pack;
static ULONG snd_pack_size;
static int redraw;                    /* REDRAW: every drawn frame sent whole (measurements) */
static int use_move16 = 1;            /* copies to VRAM in 16 byte bursts (MOVE16=OFF: long moves) */
static int render_lines;              /* RENDER=LINE: reference renderer during the frame */
static int frameskip = -1;            /* FRAMESKIP=n: draw 1 of n+1 frames; -1 = automatic */
#define MAX_AUTO_SKIP 3
static ULONG stat_drawn;
static const char *rompaths[4];
static int nrompaths;
static int dsw0 = -1, dsw1 = -1, slice = 0;
static char state_path[256];         /* LOADSTATE=file: start from an F10 machine state */
static char replay_path[256];        /* REPLAY=file: an F10 replay (state + inputs), then quit */
static UBYTE *replay_in;             /* p1, p2 per frame */
static ULONG replay_count, replay_pos;
static int joy_mouseport;             /* JOYPORT=BOTH: also a joystick in the mouse port */
static int screen_full;               /* SCREEN=FULL: an own screen instead of a window */
static struct Screen *scr;
static UWORD *blank_pointer;
static int ox, oy;                    /* where the picture starts in the window */

static void close_sound(void);
static void sndlog_write(void);
static void close_rtg(void);
static void prof_stage(int stage);
static uint64_t eclock_now(void);

/* ------------------------------------------------------------------ watchdog */

/*
 * Diagnostics for hangs on real hardware (as in Wizard of Wor / Vastar): after 3 s without
 * a new frame the state of the game task and its stack go to RAM:XainSleena-hang.txt.
 */
static volatile ULONG diag_frames;
static struct Task *main_task;
static volatile int wd_quit;
static BYTE wd_done_sig = -1;
static struct Process *wd_proc;

int main(int argc, char **argv);

static void report_hang(void)
{
    static ULONG stack[160];
    static char text[4096];
    struct Task *t = main_task;
    struct Process *pr = (struct Process *)t;
    ULONG state, sigwait, sigrecvd, sp, upper, n = 0, i;
    int len = 0;
    BPTR seg, fh;

    Disable();
    state = t->tc_State;
    sigwait = t->tc_SigWait;
    sigrecvd = t->tc_SigRecvd;
    sp = (ULONG)t->tc_SPReg;
    upper = (ULONG)t->tc_SPUpper;
    while (n < 160 && sp + n * 4 + 4 <= upper) {
        stack[n] = ((ULONG *)sp)[n];
        n++;
    }
    Enable();

    len += sprintf(text + len, "XainSleena hang: frames %lu state %lu sigwait %08lx sigrecvd %08lx sp %08lx upper %08lx\n",
                   diag_frames, state, sigwait, sigrecvd, sp, upper);
    len += sprintf(text + len, "main %08lx  cpu A pc %04x  cpu B pc %04x  sound pc %04x  mcu pc %03x  line %d\n",
                   (ULONG)main, (unsigned)machine.main.pc, (unsigned)machine.sub.pc,
                   (unsigned)machine.snd.pc, (unsigned)machine.mcu.cpu.pc, machine.line);
    seg = pr->pr_CLI ? ((struct CommandLineInterface *)BADDR(pr->pr_CLI))->cli_Module
                     : ((BPTR *)BADDR(pr->pr_SegList))[3];
    for (i = 0; seg && i < 8; i++) {
        ULONG *sl = (ULONG *)BADDR(seg);
        len += sprintf(text + len, "segment %lu: %08lx size %lu\n", i, (ULONG)(sl + 1), sl[-1]);
        seg = (BPTR)sl[0];
    }
    for (i = 0; i < n; i++)
        len += sprintf(text + len, "%08lx%s", stack[i], i % 8 == 7 ? "\n" : " ");
    len += sprintf(text + len, "\n");

    fh = Open((STRPTR)"RAM:XainSleena-hang.txt", MODE_NEWFILE);
    if (fh) {
        Write(fh, text, len);
        Close(fh);
    }
}

static void watchdog(void)
{
    ULONG last = diag_frames;
    int still = 0, reported = 0;

    while (!wd_quit) {
        Delay(25);
        if (diag_frames != last || paused) {
            last = diag_frames;
            still = 0;
        } else if (++still == 6 && !reported) {
            reported = 1;
            report_hang();
        }
    }
    Forbid();                           /* the game may unload us only after we are gone */
    Signal(main_task, 1UL << wd_done_sig);
}

static void start_watchdog(void)
{
    main_task = FindTask(NULL);
    wd_done_sig = AllocSignal(-1);
    if (wd_done_sig < 0)
        return;
    wd_proc = CreateNewProcTags(NP_Entry, (ULONG)watchdog, NP_Name, (ULONG)"XainSleena watchdog",
                                NP_Priority, 1, NP_StackSize, 8192, TAG_DONE);
}

static void stop_watchdog(void)
{
    if (wd_proc) {
        wd_quit = 1;
        Wait(1UL << wd_done_sig);
        wd_proc = NULL;
    }
    if (wd_done_sig >= 0)
        FreeSignal(wd_done_sig);
    wd_done_sig = -1;
}

/* ------------------------------------------------------------------ cleanup */

static void release_pens(void)
{
    int i;
    if (!pens_ok || !win)
        return;
    for (i = 0; i < 512; i++)
        if (pens[i] >= 0)
            ReleasePen(win->WScreen->ViewPort.ColorMap, (ULONG)pens[i]);
    pens_ok = 0;
}

static void lag_report(void);

static void cleanup(void)
{
    stop_watchdog();
    lag_report();
    close_sound();
    sndlog_write();
    if (snd_pack) {
        FreeVec(snd_pack);
        snd_pack = NULL;
    }
    if (timer_req) {
        CloseDevice((struct IORequest *)timer_req);
        DeleteIORequest((struct IORequest *)timer_req);
    }
    if (timer_port)
        DeleteMsgPort(timer_port);
    close_rtg();
    if (win) {
        release_pens();
        if (blank_pointer)
            ClearPointer(win);
        CloseWindow(win);
    }
    if (blank_pointer)
        FreeVec(blank_pointer);
    if (scr)
        CloseScreen(scr);
    if (chunky)
        FreeVec(chunky);
    video_free(&video);
    if (LowLevelBase)
        CloseLibrary(LowLevelBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
}

static void fail(const char *msg)
{
    struct EasyStruct es;
    if (IntuitionBase) {
        es.es_StructSize = sizeof es;
        es.es_Flags = 0;
        es.es_Title = (UBYTE *)"Xain'd Sleena";
        es.es_TextFormat = (UBYTE *)"%s";
        es.es_GadgetFormat = (UBYTE *)"OK";
        EasyRequest(NULL, &es, NULL, (ULONG)msg);
    } else {
        printf("%s\n", msg);
    }
    exit(20);
}

/* ------------------------------------------------------------------ video */

/* SCREEN=FULL: an own screen in the best mode for the picture (on RTG systems a mode of the
   graphics card, 16 bit preferred), a borderless backdrop window, the picture centred */
static void open_fullscreen(void)
{
    static ULONG black[] = { 1UL << 16, 0, 0, 0, 0 };
    const ULONG w = (ULONG)(RENDER_W * scale), h = (ULONG)(RENDER_H * scale);
    ULONG id = INVALID_ID, mode = INVALID_ID, depth = 8, best_area = 0xffffffffUL;
    int best_rank = 0;
    struct DimensionInfo dims;

    while ((id = NextDisplayInfo(id)) != INVALID_ID) {
        struct DisplayInfo disp;
        ULONG mw, mh, area;
        int rank;
        if (!GetDisplayInfoData(NULL, (UBYTE *)&disp, sizeof disp, DTAG_DISP, id) || disp.NotAvailable ||
            !GetDisplayInfoData(NULL, (UBYTE *)&dims, sizeof dims, DTAG_DIMS, id))
            continue;
        mw = (ULONG)(dims.Nominal.MaxX - dims.Nominal.MinX + 1);
        mh = (ULONG)(dims.Nominal.MaxY - dims.Nominal.MinY + 1);
        if (mw < w || mh < h || (disp.PropertyFlags & DIPF_IS_LACE))
            continue;
        /* 512 colours: 16 bit before 32/24 bit before 8 bit (pens, approximate) */
        rank = dims.MaxDepth >= 15 && dims.MaxDepth <= 16 ? 3 : dims.MaxDepth > 16 ? 2 : dims.MaxDepth == 8 ? 1 : 0;
        if (!rank)
            continue;
        area = mw * mh;
        if (rank > best_rank || (rank == best_rank && area < best_area)) {
            best_rank = rank;
            best_area = area;
            mode = id;
            depth = rank >= 2 ? dims.MaxDepth : 8;
        }
    }
    if (mode == INVALID_ID || !GetDisplayInfoData(NULL, (UBYTE *)&dims, sizeof dims, DTAG_DIMS, mode))
        fail("No screen mode for the full screen picture.\nTry a smaller SCALE or SCREEN=WINDOW.");
    scr = OpenScreenTags(NULL,
                         SA_DisplayID, mode,
                         SA_Width, (ULONG)(dims.Nominal.MaxX - dims.Nominal.MinX + 1),
                         SA_Height, (ULONG)(dims.Nominal.MaxY - dims.Nominal.MinY + 1),
                         SA_Depth, depth,
                         SA_Quiet, TRUE,
                         SA_ShowTitle, FALSE,
                         SA_Type, CUSTOMSCREEN,
                         SA_Title, (ULONG)"Xain'd Sleena",
                         SA_Colors32, (ULONG)black,
                         TAG_DONE);
    if (!scr)
        fail("Cannot open the screen.");
    if ((ULONG)scr->Width < w || (ULONG)scr->Height < h)
        fail("The screen mode is smaller than the picture.\nTry a smaller SCALE.");
    win = OpenWindowTags(NULL,
                         WA_CustomScreen, (ULONG)scr,
                         WA_Left, 0,
                         WA_Top, 0,
                         WA_Width, (ULONG)scr->Width,
                         WA_Height, (ULONG)scr->Height,
                         WA_Borderless, TRUE,
                         WA_Backdrop, TRUE,
                         WA_Activate, TRUE,
                         WA_RMBTrap, TRUE,
                         WA_SimpleRefresh, TRUE,
                         /* damage (another window or a requester over the screen) is reported:
                            the picture is sent whole again (only changed blocks are sent) */
                         WA_IDCMP, IDCMP_RAWKEY | IDCMP_INACTIVEWINDOW | IDCMP_ACTIVEWINDOW |
                                   IDCMP_REFRESHWINDOW,
                         TAG_DONE);
    if (!win)
        fail("Cannot open the window.");
    blank_pointer = AllocVec(16, MEMF_CHIP | MEMF_CLEAR);
    if (blank_pointer)
        SetPointer(win, blank_pointer, 1, 16, 0, 0);
    SetAPen(win->RPort, 0);
    RectFill(win->RPort, 0, 0, scr->Width - 1, scr->Height - 1);
    ox = (scr->Width - (int)w) / 2;
    oy = (scr->Height - (int)h) / 2;
}

static void open_window(void)
{
    if (screen_full)
        open_fullscreen();
    else
        win = OpenWindowTags(NULL,
                             WA_Title, (ULONG)"Xain'd Sleena",
                             WA_InnerWidth, RENDER_W * scale,
                             WA_InnerHeight, RENDER_H * scale,
                             WA_DragBar, TRUE,
                             WA_DepthGadget, TRUE,
                             WA_CloseGadget, TRUE,
                             WA_Activate, TRUE,
                             WA_RMBTrap, TRUE,
                             WA_SmartRefresh, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_INACTIVEWINDOW |
                                       IDCMP_ACTIVEWINDOW,
                             TAG_DONE);
    if (!win)
        fail("Cannot open the window.");
    if (!screen_full) {
        ox = win->BorderLeft;
        oy = win->BorderTop;
    }
    chunky = AllocVec((ULONG)(RENDER_W * scale * 8 * scale), MEMF_ANY);
    if (!chunky)
        fail("Not enough memory.");
}

/* ------------------------------------------------------------------ RTG video */

/*
 * Picasso96 screens (see "Warp RTG Developer notes", Wizard of Wor and Vastar):
 *   BLIT  the changed blocks are converted into a buffer in fast RAM (one row of blocks)
 *         and written with long moves into an off-screen bitmap on the board (VRAM is only
 *         written), then one blit of the rectangle around them into the window.
 *   WPA   a whole frame in fast RAM in the screen format, the changed runs go into the
 *         window with p96WritePixelArray.
 *   WCP   no Picasso96 / 8 bit screens: WriteChunkyPixels with pens (approximate colours).
 */
enum { VID_WCP, VID_BLIT, VID_WPA };
static const char *const video_names[] = { "WCP", "BLIT", "WPA" };
static int video_mode = VID_WCP;
static int video_request = -1;
static struct BitMap *vbm, *vbm2;      /* vbm2: SHIFT, the other picture (vbm is the shown one) */
static UBYTE *wfb;                    /* WPA frame */
static UBYTE *sfb_mem, *sfb;          /* BLIT: one row of blocks in the screen format, 16 byte aligned */
static ULONG sfb_bpr;
static ULONG wfb_bpr, scr_fmt;
static int vbpp;
static ULONG native[512];             /* palette index -> pixel in the screen format */
static ULONG native2[512];            /* 16 bit screens: the pixel twice (SCALE=2) */
static UWORD native16[512];           /* 16 bit screens: the pixel as a word (convert_fast_68k) */
static int rtg_ok;

/* amiga_68k.s: rows from fast RAM to VRAM, writes only (MOVE16 or long moves) */
void copy_rows(const UBYTE *src, UBYTE *dst, ULONG bytes, ULONG rows, ULONG src_mod, ULONG dst_mod,
               ULONG move16);
/* the changed 8x8 blocks of two pictures; 8 rows of indices into 16 bit pixels (SCALE 1/2) */
ULONG find_dirty_68k(const uint16_t *a, const uint16_t *b, UBYTE *dirty);
void convert_rows_68k(const uint16_t *src, UBYTE *dst, ULONG dst_bpr, ULONG pixels, const ULONG *lut, ULONG scale);
/* the same with the lookups interleaved: mode 1 lut UWORD[512], mode 3 (doubled) lut ULONG[512] */
void convert_fast_68k(const uint16_t *src, UBYTE *dst, ULONG dst_bpr, ULONG pixels, const void *lut, ULONG mode);
/* 8 rows of 16 bit screen pixels (512 bytes per row) doubled into VRAM (SCALE=2) */
void double_rows_68k(const uint16_t *src, UBYTE *dst, ULONG dst_bpr, ULONG pixels);

#define MAX_RUNS (RENDER_BW * RENDER_BH)
static UBYTE run_by[MAX_RUNS], run_x0[MAX_RUNS], run_x1[MAX_RUNS];
static int run_gap = 3;

static ULONG pixel_value(ULONG fmt, ULONG r, ULONG g, ULONG b, int pen)
{
    ULONG v;
    switch (fmt) {
    case RGBFB_CLUT:      return (ULONG)pen;
    case RGBFB_R5G6B5:    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
    case RGBFB_R5G5B5:    return (r >> 3) << 10 | (g >> 3) << 5 | b >> 3;
    case RGBFB_R5G6B5PC:  v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3; return (v >> 8 | v << 8) & 0xFFFF;
    case RGBFB_R5G5B5PC:  v = (r >> 3) << 10 | (g >> 3) << 5 | b >> 3; return (v >> 8 | v << 8) & 0xFFFF;
    case RGBFB_B5G6R5PC:  v = (b >> 3) << 11 | (g >> 2) << 5 | r >> 3; return (v >> 8 | v << 8) & 0xFFFF;
    case RGBFB_B5G5R5PC:  v = (b >> 3) << 10 | (g >> 3) << 5 | r >> 3; return (v >> 8 | v << 8) & 0xFFFF;
    case RGBFB_A8R8G8B8:  return r << 16 | g << 8 | b;
    case RGBFB_A8B8G8R8:  return b << 16 | g << 8 | r;
    case RGBFB_R8G8B8A8:  return r << 24 | g << 16 | b << 8;
    case RGBFB_B8G8R8A8:  return b << 24 | g << 16 | r << 8;
    default:              return 0;
    }
}

/* the game palette changed: new pixel values (RTG) or pens (8 bit), everything redrawn */
static void update_colours(void)
{
    static UBYTE col_prev[0x400];                       /* the palette the tables were made from */
    static int col_ok;
    int i;
    if (video_mode != VID_WCP) {
        /* true colour: only the entries written with a new value (in flying stages the palette
           changes in almost every frame, a few entries at a time) */
        for (i = 0; i < 512; i++)
            if (!col_ok || machine.palette[i] != col_prev[i] || machine.palette_ext[i] != col_prev[0x200 + i]) {
                xain_palette_rgb(&machine, i, video.rgb[i]);
                native[i] = pixel_value(scr_fmt, video.rgb[i][0], video.rgb[i][1], video.rgb[i][2], 0);
                native2[i] = (native[i] & 0xffff) * 0x00010001UL;
                native16[i] = (UWORD)native[i];
            }
        memcpy(col_prev, machine.palette, 0x200);
        memcpy(col_prev + 0x200, machine.palette_ext, 0x200);
        col_ok = 1;
        machine.palette_dirty = 0;
        force_full = 1;
        return;
    }
    video_update_palette(&video, &machine);
    if (video_mode == VID_WCP) {
        struct ColorMap *cm = win->WScreen->ViewPort.ColorMap;
        release_pens();
        for (i = 0; i < 512; i++) {
            ULONG r = video.rgb[i][0], g = video.rgb[i][1], b = video.rgb[i][2];
            pens[i] = ObtainBestPen(cm, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL,
                                    OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        }
        pens_ok = 1;
    } else {
        for (i = 0; i < 512; i++) {
            native[i] = pixel_value(scr_fmt, video.rgb[i][0], video.rgb[i][1], video.rgb[i][2], 0);
            native2[i] = (native[i] & 0xffff) * 0x00010001UL;
            native16[i] = (UWORD)native[i];
        }
    }
    force_full = 1;
}

static void open_rtg(void)
{
    struct BitMap *sbm = win->WScreen->RastPort.BitMap;
    ULONG fmt;
    int mode;

    if (video_request == VID_WCP)
        return;
    P96Base = OpenLibrary((STRPTR)P96NAME, 2);
    if (!P96Base || !p96GetBitMapAttr(sbm, P96BMA_ISP96))
        return;
    fmt = p96GetBitMapAttr(sbm, P96BMA_RGBFORMAT);
    scr_fmt = fmt;
    vbpp = (int)p96GetBitMapAttr(sbm, P96BMA_BYTESPERPIXEL);
    /* 512 colours need a true colour screen; 8 bit screens use pens (WCP) */
    if (!(vbpp == 2 && pixel_value(fmt, 255, 255, 255, 0)) && !(vbpp == 4 && pixel_value(fmt, 255, 255, 255, 0)))
        return;
    mode = video_request >= 0 ? video_request : vbpp == 4 ? VID_WPA : VID_BLIT;
    if (mode == VID_BLIT) {
        vbm = AllocBitMap((ULONG)(RENDER_W * scale), (ULONG)(RENDER_H * scale),
                          GetBitMapAttr(sbm, BMA_DEPTH), BMF_MINPLANES | BMF_DISPLAYABLE, sbm);
        if (vbm && (!p96GetBitMapAttr(vbm, P96BMA_ISP96) || p96GetBitMapAttr(vbm, P96BMA_RGBFORMAT) != fmt)) {
            FreeBitMap(vbm);
            vbm = NULL;
        }
        if (vbm) {
            sfb_bpr = ((ULONG)(RENDER_W * scale * vbpp) + 15) & ~15UL;
            sfb_mem = AllocVec(sfb_bpr * (ULONG)(8 * scale) + 16, MEMF_ANY | MEMF_CLEAR);
            if (sfb_mem)
                sfb = (UBYTE *)(((ULONG)sfb_mem + 15) & ~15UL);
            else {
                FreeBitMap(vbm);
                vbm = NULL;
            }
        }
        if (!vbm)
            mode = VID_WPA;
        else {
            /* SHIFT: the second picture on the board, the blitter moves vbm into it */
            vbm2 = AllocBitMap((ULONG)(RENDER_W * scale), (ULONG)(RENDER_H * scale),
                               GetBitMapAttr(sbm, BMA_DEPTH), BMF_MINPLANES | BMF_DISPLAYABLE, sbm);
            if (vbm2 && (!p96GetBitMapAttr(vbm2, P96BMA_ISP96) || p96GetBitMapAttr(vbm2, P96BMA_RGBFORMAT) != fmt)) {
                FreeBitMap(vbm2);
                vbm2 = NULL;
            }
        }
    }
    if (mode == VID_WPA) {
        wfb_bpr = (ULONG)(RENDER_W * scale * vbpp);
        wfb = AllocVec(wfb_bpr * (ULONG)(RENDER_H * scale) + 16, MEMF_ANY | MEMF_CLEAR);
        if (!wfb)
            return;
    }
    video_mode = mode;
    rtg_ok = 1;
    if (stats)
        printf("video: format %lu, %d bytes/pixel, using %s\n", fmt, vbpp, video_names[video_mode]);
}

static void close_rtg(void)
{
    if (vbm) {
        WaitBlit();
        FreeBitMap(vbm);
        vbm = NULL;
    }
    if (vbm2) {
        FreeBitMap(vbm2);
        vbm2 = NULL;
    }
    if (wfb)
        FreeVec(wfb);
    if (sfb_mem)
        FreeVec(sfb_mem);
    if (P96Base)
        CloseLibrary(P96Base);
}

/* the blocks that differ from what the window shows (all after a palette change or when the
   window was activated); then the new picture becomes the shown one (buffers swapped, the
   renderer draws the next picture into the other one) */
static int find_dirty(UBYTE dirty[RENDER_BH][RENDER_BW])
{
    int n;
    uint16_t *t;
    if (force_full) {
        memset(dirty, 1, RENDER_BH * RENDER_BW);
        n = RENDER_BH * RENDER_BW;
    } else {
        n = (int)find_dirty_68k(video.fb, shown, &dirty[0][0]);
    }
    force_full = 0;
    t = shown;
    shown = video.fb;
    video.fb = t;
    return n;
}

/* the dirty blocks as runs per block row; a run may bridge up to run_gap clean blocks */
static int collect_runs(UBYTE dirty[RENDER_BH][RENDER_BW], int gap)
{
    int by, n = 0;
    for (by = 0; by < RENDER_BH; by++) {
        int bx = 0;
        while (bx < RENDER_BW) {
            int run1;
            if (!dirty[by][bx]) {
                bx++;
                continue;
            }
            run_by[n] = (UBYTE)by;
            run_x0[n] = (UBYTE)bx;
            run1 = bx + 1;
            for (bx = run1; bx < RENDER_BW && bx < run1 + 1 + gap; bx++)
                if (dirty[by][bx])
                    run1 = bx + 1;
            run_x1[n++] = (UBYTE)run1;
            bx = run1;
        }
    }
    return n;
}

/* rows y0..y0+7, pixels x0..x0+pixels-1 of shown[] in the screen format at base (bpr) */
static void convert_rect(UBYTE *base, ULONG bpr, int y0, int x0, int pixels)
{
    if (vbpp == 2 && (scale == 1 || scale == 2)) {
        convert_rows_68k(&shown[y0 * RENDER_W + x0], base + (ULONG)(x0 * scale * 2), bpr, (ULONG)pixels,
                         scale == 2 ? native2 : native, (ULONG)scale);
        return;
    }
    int y, x, k;
    for (y = y0; y < y0 + 8; y++) {
        const uint16_t *src = &shown[y * RENDER_W + x0];
        UBYTE *row = base + (ULONG)((y - y0) * scale) * bpr + (ULONG)(x0 * scale * vbpp);
        if (vbpp == 2) {
            UWORD *d = (UWORD *)row;
            if (scale == 1)
                for (x = 0; x < pixels; x++)
                    d[x] = (UWORD)native[src[x]];
            else if (scale == 2)
                for (x = 0; x < pixels; x++)
                    ((ULONG *)d)[x] = native[src[x]] * 0x00010001UL;
            else
                for (x = 0; x < pixels; x++)
                    for (k = 0; k < scale; k++)
                        *d++ = (UWORD)native[src[x]];
        } else {
            ULONG *d = (ULONG *)row;
            for (x = 0; x < pixels; x++)
                for (k = 0; k < scale; k++)
                    *d++ = native[src[x]];
        }
        for (k = 1; k < scale; k++)
            CopyMem(row, row + (ULONG)k * bpr, (ULONG)(pixels * scale * vbpp));
    }
}

static void present_rtg(int n)
{
    int i;
    if (video_mode == VID_WPA) {
        struct RenderInfo ri;
        for (i = 0; i < n; i++)
            convert_rect(wfb + (ULONG)(run_by[i] * 8 * scale) * wfb_bpr, wfb_bpr, run_by[i] * 8,
                         run_x0[i] * 8, (run_x1[i] - run_x0[i]) * 8);
        if (stats > 1)
            prof_stage(4);
        ri.Memory = wfb;
        ri.BytesPerRow = (WORD)wfb_bpr;
        ri.pad = 0;
        ri.RGBFormat = (RGBFTYPE)scr_fmt;
        for (i = 0; i < n; i++) {
            const int y0 = run_by[i] * 8, x0 = run_x0[i] * 8, x1 = run_x1[i] * 8;
            p96WritePixelArray(&ri, (UWORD)(x0 * scale), (UWORD)(y0 * scale), win->RPort,
                               (UWORD)(ox + x0 * scale), (UWORD)(oy + y0 * scale),
                               (UWORD)((x1 - x0) * scale), (UWORD)(8 * scale));
        }
        return;
    }
    {
        /* per run: convert into sfb (fast RAM), copy to VRAM with writes only, then one
           blit of the rectangle around all runs */
        struct RenderInfo ri;
        LONG lock = p96LockBitMap(vbm, (UBYTE *)&ri, sizeof ri);
        int x0 = RENDER_BW, x1 = -1, y0 = RENDER_BH, y1 = -1;
        if (ri.Memory) {
            const ULONG bpr = (UWORD)ri.BytesPerRow, bw = (ULONG)(8 * scale * vbpp);
            for (i = 0; i < n; i++) {
                ULONG b0 = (ULONG)run_x0[i] * bw, b1 = (ULONG)run_x1[i] * bw;
                UBYTE *dst = (UBYTE *)ri.Memory + (ULONG)(run_by[i] * 8 * scale) * bpr + b0;
                ULONG m16 = use_move16 && !(((ULONG)dst | bpr | (b1 - b0)) & 15);
                if (vbpp == 2 && scale == 2) {
                    /* the 8 rows doubled horizontally once into sfb (8 KB at most: stays in the
                       data cache), each copied to both screen rows */
                    convert_rows_68k(&shown[run_by[i] * 8 * RENDER_W + run_x0[i] * 8], sfb + b0, sfb_bpr,
                                     (ULONG)(run_x1[i] - run_x0[i]) * 8, native2, 3);
                    if (m16)
                        CacheClearE(sfb + b0, sfb_bpr * 7 + (b1 - b0), CACRF_ClearD);   /* MOVE16 reads memory */
                    if (stats > 1)
                        prof_stage(6);
                    copy_rows(sfb + b0, dst, b1 - b0, 8, sfb_bpr, 2 * bpr, m16);
                    copy_rows(sfb + b0, dst + bpr, b1 - b0, 8, sfb_bpr, 2 * bpr, m16);
                } else {
                    convert_rect(sfb, sfb_bpr, run_by[i] * 8, run_x0[i] * 8, (run_x1[i] - run_x0[i]) * 8);
                    if (m16)
                        CacheClearE(sfb, sfb_bpr * (ULONG)(8 * scale), CACRF_ClearD);
                    if (stats > 1)
                        prof_stage(6);
                    copy_rows(sfb + b0, dst, b1 - b0, (ULONG)(8 * scale), sfb_bpr, bpr, m16);
                }
                if (stats > 1)
                    prof_stage(4);
                if (run_x0[i] < x0) x0 = run_x0[i];
                if (run_x1[i] - 1 > x1) x1 = run_x1[i] - 1;
                if (run_by[i] < y0) y0 = run_by[i];
                if (run_by[i] > y1) y1 = run_by[i];
            }
        }
        p96UnlockBitMap(vbm, lock);
        if (stats > 1)
            prof_stage(4);
        if (!ri.Memory) {
            force_full = 1;
            return;
        }
        if (x1 >= 0)
            BltBitMapRastPort(vbm, (WORD)(x0 * 8 * scale), (WORD)(y0 * 8 * scale), win->RPort,
                              (WORD)(ox + x0 * 8 * scale), (WORD)(oy + y0 * 8 * scale),
                              (WORD)((x1 + 1 - x0) * 8 * scale), (WORD)((y1 + 1 - y0) * 8 * scale), 0xC0);
    }
}

/* without Picasso96 or on 8 bit screens: WriteChunkyPixels with pens */
static void present_chunky(int n)
{
    int i, y, x, k;
    for (i = 0; i < n; i++) {
        const int y0 = run_by[i] * 8, x0 = run_x0[i] * 8, pixels = (run_x1[i] - run_x0[i]) * 8;
        const int w = pixels * scale;
        for (y = 0; y < 8; y++) {
            const uint16_t *src = &shown[(y0 + y) * RENDER_W + x0];
            UBYTE *line = &chunky[y * scale * w], *dst = line;
            for (x = 0; x < pixels; x++)
                for (k = 0; k < scale; k++)
                    *dst++ = (UBYTE)pens[src[x]];
            for (k = 1; k < scale; k++)
                memcpy(line + k * w, line, (size_t)w);
        }
        WriteChunkyPixels(win->RPort,
                          (ULONG)(ox + x0 * scale), (ULONG)(oy + y0 * scale),
                          (ULONG)(ox + (x0 + pixels) * scale - 1),
                          (ULONG)(oy + (y0 + 8) * scale - 1),
                          chunky, (LONG)w);
    }
}

static ULONG prof_blocks;
static uint64_t stat_stage[7], stage_t0;

/* VERIFY (testing): every 30th drawn frame read the window back from the board and compare
   every block with what it should show (the picture rendered again by the C renderer, not
   shown[], which SHIFT does not move); the result goes to the title */
static int verify, verify_every = 30;   /* VERIFY[=n]: every n-th drawn picture */
static ULONG verify_bad, verify_checks, verify_frames;
static int verify_bx = -1, verify_by;

static void verify_window(void)
{
    static UBYTE *buf;
    static uint16_t ref[RENDER_W * RENDER_H];
    const int w = RENDER_W * scale, h = RENDER_H * scale;
    struct RenderInfo ri;
    int bx, by;

    if (!buf)
        buf = AllocVec((ULONG)(w * h * vbpp), MEMF_ANY);
    if (!buf || !rtg_ok)
        return;
    {
        /* the reference renderer (pixel by pixel, as MAME) into ref */
        uint16_t *fb = video.fb;
        int y;
        video.fb = ref;
        for (y = XAIN_FIRST_LINE; y < XAIN_FIRST_LINE + RENDER_H; y++)
            video_render_line(&video, &machine, y);
        video.fb = fb;
    }
    WaitBlit();
    ri.Memory = buf;
    ri.BytesPerRow = (WORD)(w * vbpp);
    ri.pad = 0;
    ri.RGBFormat = (RGBFTYPE)scr_fmt;
    p96ReadPixelArray(&ri, 0, 0, win->RPort, (UWORD)ox, (UWORD)oy, (UWORD)w, (UWORD)h);
    verify_checks++;
    for (by = 0; by < RENDER_BH; by++)
        for (bx = 0; bx < RENDER_BW; bx++) {
            int y, x, bad = 0;
            for (y = by * 8; y < by * 8 + 8 && !bad; y++)
                for (x = bx * 8; x < bx * 8 + 8; x++) {
                    const UBYTE *row = buf + (ULONG)(y * scale) * ri.BytesPerRow;
                    ULONG want = native[ref[y * RENDER_W + x]], got;
                    if (vbpp == 2) {
                        got = ((const UWORD *)row)[x * scale];
                        want &= 0xFFFF;
                    } else {
                        got = ((const ULONG *)row)[x * scale] & 0xFFFFFF;
                        want &= 0xFFFFFF;
                    }
                    if (got != want) {
                        bad = 1;
                        break;
                    }
                }
            if (bad && !(show_fps && by == FPS_BY && bx >= FPS_BX)) {   /* not the FPS counter */
                verify_bad++;
                verify_bx = bx;
                verify_by = by;
            }
        }
}

/* STATS=FULL: time of the stages of presenting a frame (3 compare, 4 convert+write, 5 blit) */
static void prof_stage(int stage)
{
    uint64_t t = eclock_now();
    stat_stage[stage] += t - stage_t0;
    stage_t0 = t;
}

static void present(void)
{
    static UBYTE dirty[RENDER_BH][RENDER_BW];
    int n;
    if (stats > 1)
        stage_t0 = eclock_now();
    if (machine.palette_dirty)
        update_colours();
    if (redraw || (++present_count & 255) == 0)        /* also against unreported damage */
        force_full = 1;
    prev_valid = 0;                                     /* shown is swapped below */
    prof_blocks += (ULONG)find_dirty(dirty);
    n = collect_runs(dirty, video_mode == VID_BLIT ? 0 : run_gap);
    if (stats > 1)
        prof_stage(3);
    if (!n)
        return;
    if (video_mode == VID_WCP)
        present_chunky(n);
    else
        present_rtg(n);
    if (stats > 1)
        prof_stage(5);
    if (verify && ++verify_frames % (ULONG)verify_every == 0)
        verify_window();
}

/* BLIT, 16 bit, SCALE 1/2, fast renderer: the picture in bands of 8 lines that stay in the data
   cache: each band is rendered, compared with what the window shows (shown), the changed blocks
   are copied into shown, converted and written to VRAM; then one blit of the changed rectangle.
   The same result as video_render_frame + present() without the passes over whole pictures
   (render into fb, compare fb with shown, convert from fb: each ca. 2-4 ms at 100 MHz).
   BANDS=OFF: the old way (comparisons). */
static int use_bands = 1;
static int bands_skip;                /* PBENCH: 1 = nothing sent */
static ULONG bands_count;             /* drawn pictures: whole blit every 32, everything sent every 256 */
static uint16_t band[RENDER_W * 8];

/* What the window shows is kept as one 32 bit hash per block (4 KB, stays in the data cache)
   instead of the previous picture: a block is sent when the hash of its new pixels differs.
   (A changed block with the same hash - about 1 in 4e9 - would stay until it changes again;
   VERIFY compares the window with the picture.) */
static ULONG block_hash[RENDER_BH][RENDER_BW];

/* the blocks of band row by that changed (all with full): 1 in d[] */
/* amiga_68k.s: a 32 bit hash (FNV-1a over the long words) for each of the 32 blocks of a band */
ULONG hash_band_68k(const uint16_t *band, ULONG *hash, UBYTE *dirty, ULONG full);

static int band_diff(const uint16_t *b, int by, UBYTE *d, int full)
{
    return (int)hash_band_68k(b, block_hash[by], d, (ULONG)(full != 0));
}

static int bands_possible(void)
{
    return use_bands && video_mode == VID_BLIT && vbpp == 2 && (scale == 1 || scale == 2) && !render_lines;
}

/* Incremental drawing (0.9.13): a band of 8 lines is rendered again only when it can have
   changed since the last drawn picture - everything after a full redraw, a new priority mode or
   scroll position; else the bands of changed background tiles (from the tile's row and the
   scroll), of changed text cells, and the bands with sprites now or in the last picture. The
   picture of palette indices stays in shown; after a palette change the other bands are only
   converted again (and their hashes decide what is sent). INCR=OFF renders every band. */
static int incr_off;
static int rcheck;                    /* RCHECK: the assembler lines compared with the C renderer */
static ULONG rcheck_bands, rcheck_bad;
static UBYTE prev_bg0[0x800], prev_bg1[0x800], prev_txt[0x800], prev_scroll[8], prev_pri;
static ULONG prev_spr_bands, bands_rendered, bands_converted;

/* the bands (bit per band) covered by visible sprites, as video_render_begin finds them */
static ULONG sprite_bands(void)
{
    ULONG m = 0;
    int offs, t;
    for (offs = 0; offs < 0x180; offs += 4) {
        int attr = machine.spriteram[offs + 1];
        int sx = 238 - machine.spriteram[offs + 3];
        int sy = 240 - machine.spriteram[offs];
        if (sx <= -7) sx += 256;
        if (sy <= -7) sy += 256;
        if (sx >= SCREEN_W || sx <= -16)
            continue;
        for (t = (attr & 0x80) ? 0 : 1; t < 2; t++) {
            int ty = t == 0 ? sy - 16 : sy, y0, y1, b;
            if (ty + 16 <= XAIN_FIRST_LINE || ty >= XAIN_FIRST_LINE + SCREEN_H)
                continue;
            y0 = (ty < XAIN_FIRST_LINE ? XAIN_FIRST_LINE : ty) - XAIN_FIRST_LINE;
            y1 = (ty + 16 > XAIN_FIRST_LINE + SCREEN_H ? XAIN_FIRST_LINE + SCREEN_H : ty + 16) - XAIN_FIRST_LINE - 1;
            for (b = y0 >> 3; b <= y1 >> 3; b++)
                m |= 1UL << b;
        }
    }
    return m;
}

/* the bands showing background tiles that changed (tile RAM: code 000-3FF, attribute 400-7FF) */
static ULONG bg_bands(const UBYTE *now, const UBYTE *old, const UBYTE *scy)
{
    const int sy = scy[0] | scy[1] << 8;
    ULONG m = 0, rows = 0;
    int i, row, k;
    if (!memcmp(now, old, 0x800))
        return 0;
    for (i = 0; i < 0x800; i++)
        if (now[i] != old[i]) {
            int idx = i & 0x3ff;
            rows |= 1UL << (((idx >> 4) & 15) | ((idx >> 9) & 1) << 4);
        }
    for (row = 0; row < 32; row++)
        if (rows & (1UL << row))
            for (k = 0; k < 16; k++) {                      /* the lines of the tile row */
                int y = (row * 16 + k - sy) & 511;
                if (y >= XAIN_FIRST_LINE && y < XAIN_FIRST_LINE + SCREEN_H)
                    m |= 1UL << ((y - XAIN_FIRST_LINE) >> 3);
            }
    return m;
}

/* the bands with changed text cells (32 x 32 cells, row r = lines 8r..8r+7, not scrolled) */
static ULONG text_bands(void)
{
    ULONG m = 0;
    int i;
    if (!memcmp(machine.charram, prev_txt, 0x800))
        return 0;
    for (i = 0; i < 0x800; i++)
        if (machine.charram[i] != prev_txt[i]) {
            int r = (i & 0x3ff) >> 5, y = r * 8;
            if (y >= XAIN_FIRST_LINE && y < XAIN_FIRST_LINE + SCREEN_H)
                m |= 1UL << ((y - XAIN_FIRST_LINE) >> 3);
        }
    return m;
}

/* SHIFT (0.9.15): when both backgrounds moved by the same step since the last drawn picture,
   the card's blitter moves the picture on the board (vbm into vbm2, which then becomes vbm) and
   the CPU composes only the blocks the move cannot give: the new edge, the sprites (now and
   where they were), the text cells over the backgrounds (now and where they were) and changed
   tiles. The frame of opaque text cells that did not change (the black screen edge, the panel)
   is copied without the move. A new palette for the backgrounds (256-511), another priority mode
   or a step of more than 64 pixels: the bands. ScrollLog (port/tests/scrolllog.c, the F10
   replays): about 185 of 960 blocks in such pictures instead of all. SHIFT=OFF: never. */
static int shift_off;
static int span_gap = 3;                                /* SPANGAP=n */
static uint16_t nband[RENDER_W * 8];                    /* a band in the screen format */
static UBYTE pal_prev[0x400];                           /* the palette of the last drawn picture */
static ULONG stale[RENDER_BH];                          /* blocks where shown[] is not the window */
static UBYTE hash_bad[RENDER_BH];                       /* bands whose block_hash is not the window */
static WORD spr_prev[192][2];                           /* the sprites of the last drawn picture */
static int spr_prev_n;
static UBYTE char_opaque[1024];                         /* 1: all 64 pixels of the character pen != 0 */
static ULONG shift_pictures, shift_blocks, still_pictures, still_blocks;
/* SHIFTPROF (tests): the time of the stages of the blocks (palette, which blocks, blits, render +
   convert, write, window), summed with E clock reads at the stage ends (a read costs ca. 14 us) */
static int shiftprof;
static uint64_t sp_acc[7], sp_last;
static ULONG sp_src[7];
static uint64_t sp_w[8], sp_wlast;                       /* SHIFTPROF: the stages of finding the blocks */
#define WP(k) do { if (shiftprof == 1) { uint64_t t_ = eclock_now(); sp_w[k] += t_ - sp_wlast; sp_wlast = t_; } } while (0)                                  /* blocks added by: edge, sprites, text, tiles, colours, other layer */

/* SHIFTPROF: the blocks of d (+ rc) not yet counted go to source k */
static void sp_count(int k, const ULONG *d, const ULONG *rc, ULONG *seen)
{
    int by, bx;
    if (shiftprof != 2)
        return;
    for (by = 0; by < RENDER_BH; by++) {
        ULONG nw = (d[by] | rc[by]) & ~seen[by];
        for (bx = 0; bx < RENDER_BW; bx++)
            sp_src[k] += (nw >> bx) & 1;
        seen[by] |= nw;
    }
}
#define SP(k) do { if (shiftprof) { uint64_t t_ = eclock_now(); sp_acc[k] += t_ - sp_last; sp_last = t_; } } while (0)
static ULONG shift_no[5];                               /* not moved: off/first/pri, still, steps, area, lock */

/* the blocks covered by pixels x..x+w-1, y..y+h-1 of the picture (clipped) */
static void mark_rect(ULONG *m, int x, int y, int w, int h)
{
    int bx0, bx1, by;
    ULONG bits;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > RENDER_W) w = RENDER_W - x;
    if (y + h > RENDER_H) h = RENDER_H - y;
    if (w <= 0 || h <= 0)
        return;
    bx0 = x >> 3;
    bx1 = (x + w - 1) >> 3;
    bits = (bx1 == 31 ? 0xFFFFFFFFUL : (1UL << (bx1 + 1)) - 1) & ~((1UL << bx0) - 1);
    for (by = y >> 3; by <= (y + h - 1) >> 3; by++)
        m[by] |= bits;
}

/* the visible sprite tiles (16x16) as video_render_begin finds them: top left in picture pixels */
static int sprite_rects(WORD (*r)[2])
{
    int offs, t, n = 0;
    for (offs = 0; offs < 0x180; offs += 4) {
        int attr = machine.spriteram[offs + 1];
        int sx = 238 - machine.spriteram[offs + 3];
        int sy = 240 - machine.spriteram[offs];
        if (sx <= -7) sx += 256;
        if (sy <= -7) sy += 256;
        if (sx >= SCREEN_W || sx <= -16)
            continue;
        for (t = (attr & 0x80) ? 0 : 1; t < 2; t++) {
            int ty = t == 0 ? sy - 16 : sy;
            if (ty + 16 <= XAIN_FIRST_LINE || ty >= XAIN_FIRST_LINE + SCREEN_H)
                continue;
            r[n][0] = (WORD)sx;
            r[n][1] = (WORD)(ty - XAIN_FIRST_LINE);
            n++;
        }
    }
    return n;
}

/* the step of a scroll register pair since the last drawn picture (-256..255) */
static int scroll_step(const UBYTE *now, const UBYTE *old)
{
    int d = ((now[0] | now[1] << 8) - (old[0] | old[1] << 8)) & 511;
    return d >= 256 ? d - 512 : d;
}

static int same_mem(const void *a, const void *b, ULONG n);

/* the blocks of the changed tiles of a background (tile RAM: code 000-3FF, attribute 400-7FF) */
static void bg_changed(ULONG *m, const UBYTE *now, const UBYTE *old, const UBYTE *scx, const UBYTE *scy)
{
    const int sx = scx[0] | scx[1] << 8, sy = scy[0] | scy[1] << 8;
    int i;
    if (same_mem(now, old, 0x800))
        return;
    for (i = 0; i < 0x800; i++)
        if (now[i] != old[i]) {
            int idx = i & 0x3ff;
            int col = (idx & 15) | ((idx >> 4) & 16), row = ((idx >> 4) & 15) | ((idx >> 5) & 16);
            int x = (col * 16 - sx) & 511, y = (row * 16 - sy) & 511;
            if (x >= 496) x -= 512;
            if (y >= 496) y -= 512;
            mark_rect(m, x, y - XAIN_FIRST_LINE, 16, 16);
        }
}

/* the text cell of block (bx, by): 1 when it has a visible pixel; *opaque: all pixels */
static int text_cell(const UBYTE *ram, int bx, int by, int *opaque)
{
    int idx = (by + XAIN_FIRST_LINE / 8) * 32 + bx;
    int attr = ram[idx | 0x400], code = ram[idx] | ((attr & 3) << 8);
    *opaque = char_opaque[code];
    return !video.char_empty[code];
}

/* the text cells per block row as bit masks (bit bx): with a visible pixel (txt_ne), all pixels
   (txt_op), and txt_prev_ne of the last drawn picture; a row is looked at again only when its
   cells changed (chg: the changed cells). txt_masks_ok = 0: the masks are not of prev_txt. */
static ULONG txt_ne[RENDER_BH], txt_op[RENDER_BH], txt_prev_ne[RENDER_BH];
static int txt_masks_ok;

static void text_row(const UBYTE *ram, int by, ULONG *ne, ULONG *op)
{
    int bx, o;
    *ne = *op = 0;
    for (bx = 0; bx < RENDER_BW; bx++)
        if (text_cell(ram, bx, by, &o)) {
            *ne |= 1UL << bx;
            if (o) *op |= 1UL << bx;
        }
}

static void text_masks(ULONG *chg)
{
    int by, bx;
    for (by = 0; by < RENDER_BH; by++) {
        const int base = (by + XAIN_FIRST_LINE / 8) * 32;
        if (!txt_masks_ok)
            text_row(prev_txt, by, &txt_ne[by], &txt_op[by]);
        txt_prev_ne[by] = txt_ne[by];
        chg[by] = 0;
        if (!same_mem(machine.charram + base, prev_txt + base, 32) ||
            !same_mem(machine.charram + base + 0x400, prev_txt + base + 0x400, 32)) {
            for (bx = 0; bx < RENDER_BW; bx++)
                if (machine.charram[base + bx] != prev_txt[base + bx] ||
                    machine.charram[base + bx + 0x400] != prev_txt[base + bx + 0x400])
                    chg[by] |= 1UL << bx;
            text_row(machine.charram, by, &txt_ne[by], &txt_op[by]);
        }
    }
    txt_masks_ok = 1;
}

/* a block mask moved by k blocks toward bit 0 (k < 0: toward bit 31) */
static ULONG mask_shift(ULONG m, int k)
{
    return k >= 32 || k <= -32 ? 0 : k >= 0 ? m >> k : m << -k;
}

/* the blocks covered by the cells of src (one bit per 8x8 block) after the picture moved by
   dx, dy: the new block (bx, by) shows old pixels from bx * 8 + dx, by * 8 + dy on */
static void mark_moved(ULONG *d, const ULONG *src, int dx, int dy)
{
    const int qx = dx >= 0 ? dx / 8 : -((-dx + 7) / 8), rx = dx - 8 * qx;
    const int qy = dy >= 0 ? dy / 8 : -((-dy + 7) / 8), ry = dy - 8 * qy;
    int by, k;
    for (by = 0; by < RENDER_BH; by++)
        for (k = 0; k <= (ry ? 1 : 0); k++) {
            const int sy = by + qy + k;
            if (sy >= 0 && sy < RENDER_BH && src[sy])
                d[by] |= mask_shift(src[sy], qx) | (rx ? mask_shift(src[sy], qx + 1) : 0);
        }
}

/* the counter into nband (band FPS_BY in the screen format) */
static void fps_paint(void)
{
    static const UBYTE digit[10][7] = {   /* 5 x 7, bit 4 = left */
        { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
        { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
        { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
        { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
        { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }
    };
    const UWORD white = (UWORD)pixel_value(scr_fmt, 255, 255, 255, 0), black = (UWORD)pixel_value(scr_fmt, 0, 0, 0, 0);
    UWORD *p = &nband[FPS_BX * 8];
    int x, y, k, v = fps_value > 99 ? 99 : fps_value;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 16; x++)
            p[y * RENDER_W + x] = black;
    if (v < 0)
        return;                                         /* the first second: no number yet */
    for (k = 0; k < 2; k++) {
        const int dg = k == 0 ? v / 10 : v % 10, x0 = 2 + k * 7;
        if (k == 0 && dg == 0)
            continue;
        for (y = 0; y < 7; y++)
            for (x = 0; x < 5; x++)
                if (digit[dg][y] & (0x10 >> x))
                    p[y * RENDER_W + x0 + x] = white;
    }
}

/* the blocks whose whole 8x8 source after the move by dx, dy was in src (AND of the old blocks
   they come from; outside the picture: not) */
static void mask_moved_and(ULONG *dst, const ULONG *src, int dx, int dy)
{
    const int qx = dx >= 0 ? dx / 8 : -((-dx + 7) / 8), rx = dx - 8 * qx;
    const int qy = dy >= 0 ? dy / 8 : -((-dy + 7) / 8), ry = dy - 8 * qy;
    int by, k;
    for (by = 0; by < RENDER_BH; by++) {
        ULONG acc = 0xFFFFFFFFUL;
        for (k = 0; k <= (ry ? 1 : 0); k++) {
            const int sy = by + qy + k;
            const ULONG row = sy >= 0 && sy < RENDER_BH ? src[sy] : 0;
            acc &= mask_shift(row, qx) & (rx ? mask_shift(row, qx + 1) : 0xFFFFFFFFUL);
        }
        dst[by] = acc;
    }
}

/* 1 when n bytes (a multiple of 4) at a and b are the same (long words; memcmp of vbcc goes byte
   by byte) */
static int same_mem(const void *a, const void *b, ULONG n)
{
    const ULONG *p = a, *q = b;
    for (n >>= 2; n; n--)
        if (*p++ != *q++)
            return 0;
    return 1;
}

/* one pass over the visible tiles of a background, per block row: ne = the blocks with a visible
   pixel of it, op = the blocks all of whose pixels are of it, gc |= the blocks with a tile in a
   colour group of groups (group = gbase + attribute bits 4-6); any of them may be NULL.
   nerows / oprows: per tile the rows with a visible pixel / without pen 0 (bit r = row r) */
static void layer_scan(const UBYTE *ram, const UBYTE *scx, const UBYTE *scy, const UWORD *nerows, const UWORD *oprows,
                       int gbase, ULONG groups, ULONG *ne, ULONG *op, ULONG *gc)
{
    const int sx = (scx[0] | scx[1] << 8) & 511, sy = (scy[0] | scy[1] << 8) & 511, fx = sx & 15;
    int by, k, r;
    for (by = 0; by < RENDER_BH; by++) {
        const int ty = (by * 8 + XAIN_FIRST_LINE + sy) & 511, fy = ty & 15;
        const int n0 = 16 - fy < 8 ? 16 - fy : 8;
        ULONG mne = 0, mnot = 0, mgc = 0;
        for (r = 0; r < (n0 < 8 ? 2 : 1); r++) {
            const int row = ((ty >> 4) + r) & 31, rowpart = ((row & 0x0f) << 4) + ((row & 0x10) << 5);
            const UWORD bits = (UWORD)(r == 0 ? ((1 << n0) - 1) << fy : (1 << (8 - n0)) - 1);
            for (k = 0; k < 17; k++) {
                const int col = ((sx >> 4) + k) & 31, idx = (col & 0x0f) + ((col & 0x10) << 4) + rowpart;
                const int attr = ram[idx | 0x400], code = ram[idx] | ((attr & 7) << 8);
                const int x = k * 16 - fx, b0 = x < 0 ? 0 : x >> 3, b1 = (x + 15) >> 3 > 31 ? 31 : (x + 15) >> 3;
                ULONG bm;
                if (b0 > 31)
                    continue;
                bm = (b1 == 31 ? 0xFFFFFFFFUL : (1UL << (b1 + 1)) - 1) & ~((1UL << b0) - 1);
                if (ne && (nerows[code] & bits))
                    mne |= bm;
                if (op && (oprows[code] & bits) != bits)
                    mnot |= bm;
                if (gc && ((groups >> (gbase + ((attr & 0x70) >> 4))) & 1))
                    mgc |= bm;
            }
        }
        if (ne) ne[by] = mne;
        if (op) op[by] = ~mnot;
        if (gc) gc[by] |= mgc;
    }
}

/* writes the runs of blocks m of band by from nband into the picture at ri */
static void write_runs(const struct RenderInfo *ri, int by, ULONG m)
{
    const ULONG bpr = (UWORD)ri->BytesPerRow, bw = (ULONG)(8 * scale * 2);
    int bx = 0;
    while (bx < RENDER_BW) {
        int r0;
        UBYTE *dst;
        if (!(m & (1UL << bx))) {
            bx++;
            continue;
        }
        r0 = bx;
        while (bx < RENDER_BW && (m & (1UL << bx)))
            bx++;
        dst = (UBYTE *)ri->Memory + (ULONG)(by * 8 * scale) * bpr + (ULONG)r0 * bw;
        if (scale == 2)
            double_rows_68k(&nband[r0 * 8], dst, bpr, (ULONG)(bx - r0) * 8);
        else
            copy_rows((const UBYTE *)&nband[r0 * 8], dst, (ULONG)(bx - r0) * 16, 8, RENDER_W * 2, bpr, 0);
    }
}

/* the picture moved by the blitter; 0 = not possible here (the caller uses the bands) */
static int render_present_shift(void)
{
    /* per tile of [0] BG2 (gfx3), [1] BG1 (gfx2): bit r = row r with a visible pixel / without pen 0 */
    static UWORD ne_rows[2][2048], opq_rows[2][2048];
    ULONG cov[RENDER_BH], m1[RENDER_BH], ne_now[RENDER_BH], op_now[RENDER_BH], ne_old[RENDER_BH], op_old[RENDER_BH];
    ULONG m1any = 0;
    int odx = 0, ody = 0, two;
    ULONG seen[RENDER_BH], d[RENDER_BH], rc[RENDER_BH], keep[RENDER_BH], tchg[RENDER_BH], rows = 0, cols = 0, n = 0, groups;
    int dx, dy, b, other, still, fps_out = 0, bx, by, i, top, txtpal, rx0, rx1, ry0, ry1, vx0, vx1, vy0, vy1, s = scale;
    struct BitMap *tb;
    struct RenderInfo ri;
    SP(0);
    LONG lock;
    if (shiftprof == 1)
        sp_wlast = eclock_now();
    static int opaque_ready;

    /* the bottom layer moves the picture: BG2 (scroll [0]) in modes 0, 4, 6, BG1 ([1]) in 1, 5, 7;
       text at the bottom (2, 3): the bands */
    b = (machine.pri & 7) == 1 || (machine.pri & 7) == 5 || (machine.pri & 7) == 7;
    if (shift_off || !vbm2 || !prev_valid || machine.pri != prev_pri || (machine.pri & 7) == 2 || (machine.pri & 7) == 3)
        return shift_no[0]++, 0;
    dx = scroll_step(machine.scrollx[b], prev_scroll + 2 * b);
    dy = scroll_step(machine.scrolly[b], prev_scroll + 4 + 2 * b);
    still = !dx && !dy;                                   /* not moved: the blocks in place */
    if (dx > 64 || dx < -64 || dy > 64 || dy < -64)
        return shift_no[2]++, 0;
    other = dx != scroll_step(machine.scrollx[1 - b], prev_scroll + 2 * (1 - b)) ||
            dy != scroll_step(machine.scrolly[1 - b], prev_scroll + 4 + 2 * (1 - b));
    /* a new palette: the changed colour groups of 16; text (0-7) and sprites (8-15) are composed
       anyway, the backgrounds (BG1 16-23, BG2 24-31) where their tiles use such a group */
    groups = 0;
    if (!same_mem(machine.palette, pal_prev, 0x200) || !same_mem(machine.palette_ext, pal_prev + 0x200, 0x200))
        for (i = 0; i < 512; i++)
            if (machine.palette[i] != pal_prev[i] || machine.palette_ext[i] != pal_prev[0x200 + i])
                groups |= 1UL << (i >> 4);
    txtpal = (groups & 0xFF) != 0;
    if (!opaque_ready) {
        int c, r;
        for (c = 0; c < 1024; c++) {
            char_opaque[c] = 1;
            for (r = 0; r < 8; r++)
                if (!(video.chars_rowtype[c * 8 + r] & 1)) char_opaque[c] = 0;   /* types 1, 3 */
        }
        for (c = 0; c < 2048; c++) {
            ne_rows[0][c] = ne_rows[1][c] = opq_rows[0][c] = opq_rows[1][c] = 0;
            for (r = 0; r < 16; r++) {
                if (video.bg2_rowtype[c * 16 + r]) ne_rows[0][c] |= (UWORD)(1 << r);
                if (video.bg1_rowtype[c * 16 + r]) ne_rows[1][c] |= (UWORD)(1 << r);
                if (video.bg2_rowtype[c * 16 + r] & 1) opq_rows[0][c] |= (UWORD)(1 << r);
                if (video.bg1_rowtype[c * 16 + r] & 1) opq_rows[1][c] |= (UWORD)(1 << r);
            }
        }
        opaque_ready = 1;
    }
    /* the frame that stays: opaque text cells, unchanged, over everything (modes 0, 1, 6, 7) */
    top = (machine.pri & 7) <= 1 || (machine.pri & 7) >= 6;
    WP(0);
    text_masks(tchg);
    for (by = 0; by < RENDER_BH; by++) {
        keep[by] = top && !txtpal && !still ? txt_op[by] & ~tchg[by] : 0;
        if (keep[by] != 0xFFFFFFFFUL) {
            rows |= 1UL << by;
            cols |= ~keep[by];
        }
    }
    if (!rows)
        return shift_no[3]++, 0;
    /* R: the rectangle of the other blocks, moved; V: the part of it the move can fill */
    for (ry0 = 0; !(rows & (1UL << ry0)); ry0++) ;
    for (ry1 = RENDER_BH; !(rows & (1UL << (ry1 - 1))); ry1--) ;
    for (rx0 = 0; !(cols & (1UL << rx0)); rx0++) ;
    for (rx1 = RENDER_BW; !(cols & (1UL << (rx1 - 1))); rx1--) ;
    rx0 *= 8; rx1 *= 8; ry0 *= 8; ry1 *= 8;
    vx0 = dx < 0 ? rx0 - dx : rx0;
    vx1 = dx > 0 ? rx1 - dx : rx1;
    vy0 = dy < 0 ? ry0 - dy : ry0;
    vy1 = dy > 0 ? ry1 - dy : ry1;
    if (vx0 >= vx1 || vy0 >= vy1)
        return shift_no[3]++, 0;
    /* the blocks to compose (d) and, without a move, the ones only to colour again from shown[] (rc:
       a new palette, the same pixels - converted and written, not drawn) */
    memset(d, 0, sizeof d);
    memset(rc, 0, sizeof rc);
    memset(seen, 0, sizeof seen);
    {
        /* the edge: the blocks of R not wholly in V */
        ULONG rmr = 0, edge = 0;
        for (bx = rx0 >> 3; bx < rx1 >> 3; bx++) {
            rmr |= 1UL << bx;
            if (bx * 8 < vx0 || bx * 8 + 8 > vx1)
                edge |= 1UL << bx;
        }
        for (by = ry0 >> 3; by < ry1 >> 3; by++)
            d[by] |= by * 8 < vy0 || by * 8 + 8 > vy1 ? rmr : edge;
    }
    WP(1);
    sp_count(0, d, rc, seen);
    {
        static WORD now[192][2];
        int k = sprite_rects(now);
        for (i = 0; i < k; i++)
            mark_rect(d, now[i][0], now[i][1], 16, 16);
        for (i = 0; i < spr_prev_n; i++)                    /* where they were, moved */
            mark_rect(d, spr_prev[i][0] - dx, spr_prev[i][1] - dy, 16, 16);
    }
    if (show_fps) {                                         /* the counter: always, and where it was */
        d[FPS_BY] |= 3UL << FPS_BX;
        mark_rect(d, FPS_BX * 8 - dx, FPS_BY * 8 - dy, 16, 8);
    }
    WP(2);
    sp_count(1, d, rc, seen);
    if (still)                                              /* the text: changed cells; all after a new text palette */
        for (by = 0; by < RENDER_BH; by++) {
            d[by] |= tchg[by];
            if (txtpal) rc[by] |= txt_ne[by];
        }
    else {                                                  /* the cells with pixels now, and moved */
        for (by = 0; by < RENDER_BH; by++)
            d[by] |= txt_ne[by];
        mark_moved(d, txt_prev_ne, dx, dy);
    }
    WP(3);
    sp_count(2, d, rc, seen);
    bg_changed(d, machine.bgram0, prev_bg0, machine.scrollx[0], machine.scrolly[0]);
    bg_changed(d, machine.bgram1, prev_bg1, machine.scrollx[1], machine.scrolly[1]);
    sp_count(3, d, rc, seen);
    /* the backgrounds: blocks of tiles in a changed colour group (rc); of the other one, when it
       moved otherwise, the blocks with its pixels now and moved from before (cov) and those all of
       its pixels (op_now, op_old) */
    {
        const int o = 1 - b;
        const ULONG gb = b == 0 ? 0xFF000000UL : 0x00FF0000UL, go = o == 0 ? 0xFF000000UL : 0x00FF0000UL;
        if (groups & gb)
            layer_scan(b ? machine.bgram1 : machine.bgram0, machine.scrollx[b], machine.scrolly[b], ne_rows[b],
                       opq_rows[b], b ? 16 : 24, groups, NULL, NULL, rc);
        if (other || (groups & go))
            layer_scan(o ? machine.bgram1 : machine.bgram0, machine.scrollx[o], machine.scrolly[o], ne_rows[o],
                       opq_rows[o], o ? 16 : 24, groups, other ? ne_now : NULL, other ? op_now : NULL,
                       groups & go ? rc : NULL);
    }
    WP(4);
    sp_count(4, d, rc, seen);
    memset(cov, 0, sizeof cov);
    memset(m1, 0, sizeof m1);
    if (other) {                                            /* the upper background moved otherwise */
        const int o = 1 - b;
        const UBYTE *old = o ? prev_bg1 : prev_bg0;
        layer_scan(old, prev_scroll + 2 * o, prev_scroll + 4 + 2 * o, ne_rows[o], opq_rows[o], 0, 0, ne_old, op_old,
                   NULL);
        for (by = 0; by < RENDER_BH; by++)
            cov[by] = ne_now[by];
        mark_moved(cov, ne_old, dx, dy);
        /* its blocks with all pixels of this layer, now and where they come from in the last picture
           (moved by its own step), are copied from there by the blitter instead of drawn (the
           ground on foot moves, the sky below it not) - unless an old sprite, text cell or the
           counter was over the source, or their colours changed */
        odx = scroll_step(machine.scrollx[o], prev_scroll + 2 * o);
        ody = scroll_step(machine.scrolly[o], prev_scroll + 4 + 2 * o);
        if (odx >= -64 && odx <= 64 && ody >= -64 && ody <= 64) {
            ULONG contam[RENDER_BH];
            memset(contam, 0, sizeof contam);
            for (i = 0; i < spr_prev_n; i++)
                mark_rect(contam, spr_prev[i][0] - odx, spr_prev[i][1] - ody, 16, 16);
            mark_moved(contam, txt_prev_ne, odx, ody);
            if (show_fps)
                mark_rect(contam, FPS_BX * 8 - odx, FPS_BY * 8 - ody, 16, 8);
            mask_moved_and(m1, op_old, odx, ody);
            for (by = 0; by < RENDER_BH; by++)
                m1[by] &= cov[by] & op_now[by] & ~contam[by] & ~rc[by];
        }
        for (by = 0; by < RENDER_BH; by++)
            d[by] |= cov[by] & ~m1[by];
    }
    WP(5);
    sp_count(5, d, rc, seen);
    {
        const ULONG rm = (rx1 == RENDER_W ? 0xFFFFFFFFUL : (1UL << (rx1 >> 3)) - 1) & ~((1UL << (rx0 >> 3)) - 1);
        for (by = 0; by < RENDER_BH; by++) {
            /* moved, or shown[] not the window there: drawn; else only coloured again */
            if (!still)
                d[by] |= rc[by];
            else
                d[by] |= rc[by] & stale[by];
            rc[by] = still ? rc[by] & ~d[by] : 0;
            d[by] = by >= ry0 >> 3 && by < ry1 >> 3 ? d[by] & rm : 0;
            m1[by] = by >= ry0 >> 3 && by < ry1 >> 3 ? m1[by] & rm & ~d[by] : 0;
            m1any |= m1[by];
            if (shiftprof)
                for (bx = 0; bx < RENDER_BW; bx++)
                    sp_src[6] += (m1[by] >> bx) & 1;
        }
        /* the counter always (in the frame that stays it is copied unmoved, then drawn again) */
        fps_out = show_fps && (FPS_BY < ry0 >> 3 || FPS_BY >= ry1 >> 3 || !(rm & (1UL << FPS_BX)) ||
                               !(rm & (1UL << (FPS_BX + 1))));
        if (show_fps)
            d[FPS_BY] |= 3UL << FPS_BX;
        /* the move on the board: V from vbm moved, the frame outside R as it is */
        WP(6);
        SP(1);
        two = !still || m1any;                              /* into vbm2: moved, or copied blocks */
        tb = two ? vbm2 : vbm;
        if (still && two)
            BltBitMap(vbm, 0, 0, vbm2, 0, 0, (WORD)(RENDER_W * s), (WORD)(RENDER_H * s), 0xC0, 0xFF, NULL);
        if (!still) {
        BltBitMap(vbm, (WORD)((vx0 + dx) * s), (WORD)((vy0 + dy) * s), vbm2, (WORD)(vx0 * s), (WORD)(vy0 * s),
                  (WORD)((vx1 - vx0) * s), (WORD)((vy1 - vy0) * s), 0xC0, 0xFF, NULL);
        if (ry0 > 0)
            BltBitMap(vbm, 0, 0, vbm2, 0, 0, (WORD)(RENDER_W * s), (WORD)(ry0 * s), 0xC0, 0xFF, NULL);
        if (ry1 < RENDER_H)
            BltBitMap(vbm, 0, (WORD)(ry1 * s), vbm2, 0, (WORD)(ry1 * s), (WORD)(RENDER_W * s),
                      (WORD)((RENDER_H - ry1) * s), 0xC0, 0xFF, NULL);
        if (rx0 > 0)
            BltBitMap(vbm, 0, (WORD)(ry0 * s), vbm2, 0, (WORD)(ry0 * s), (WORD)(rx0 * s), (WORD)((ry1 - ry0) * s),
                      0xC0, 0xFF, NULL);
        if (rx1 < RENDER_W)
            BltBitMap(vbm, (WORD)(rx1 * s), (WORD)(ry0 * s), vbm2, (WORD)(rx1 * s), (WORD)(ry0 * s),
                      (WORD)((RENDER_W - rx1) * s), (WORD)((ry1 - ry0) * s), 0xC0, 0xFF, NULL);
        }
        if (m1any) {
            /* the blocks of the other layer from the last picture, by its step: rectangles of
               rows with the same blocks */
            by = 0;
            while (by < RENDER_BH) {
                const ULONG mk = m1[by];
                int y0 = by;
                if (!mk) {
                    by++;
                    continue;
                }
                while (by < RENDER_BH && m1[by] == mk)
                    by++;
                bx = 0;
                while (bx < RENDER_BW) {
                    int r0;
                    if (!(mk & (1UL << bx))) {
                        bx++;
                        continue;
                    }
                    r0 = bx;
                    while (bx < RENDER_BW && (mk & (1UL << bx)))
                        bx++;
                    BltBitMap(vbm, (WORD)((r0 * 8 + odx) * s), (WORD)((y0 * 8 + ody) * s), vbm2, (WORD)(r0 * 8 * s),
                              (WORD)(y0 * 8 * s), (WORD)((bx - r0) * 8 * s), (WORD)((by - y0) * 8 * s), 0xC0, 0xFF, NULL);
                }
            }
        }
        if (two)
            WaitBlit();
        SP(2);
        /* the composed blocks on top */
        video_render_begin(&video, &machine);
        lock = p96LockBitMap(tb, (UBYTE *)&ri, sizeof ri);
        if (!ri.Memory) {
            p96UnlockBitMap(tb, lock);
            return shift_no[4]++, 0;                        /* vbm is still the picture */
        }
        for (by = 0; by < RENDER_BH; by++) {
            uint16_t *band_px = &shown[by * 8 * RENDER_W];
            ULONG m = d[by], w = m | rc[by], up = m, down = m, rm2;
            /* rendered: the runs with gaps of up to span_gap blocks between them filled (a span
               costs about as much as a few blocks: 8 lines x 4 layers of calls); only the blocks
               of m are converted and written */
            for (i = 1; i <= span_gap; i++) {
                up |= m << i;
                down |= m >> i;
            }
            rm2 = m | (up & down);
            bx = 0;
            while (bx < RENDER_BW) {
                int r0;
                if (!(rm2 & (1UL << bx))) {
                    bx++;
                    continue;
                }
                r0 = bx;
                while (bx < RENDER_BW && (rm2 & (1UL << bx)))
                    bx++;
                video_render_span(&video, &machine, XAIN_FIRST_LINE + by * 8, 8, r0 * 8, bx * 8, band_px);
                if (rcheck) {
                    static uint16_t ref[RENDER_W * 8];
                    int y, x;
                    const int c = video_cache;
                    video_asm = 0;
                    video_cache = 0;                        /* the C renderer from the tiles */
                    video_render_span(&video, &machine, XAIN_FIRST_LINE + by * 8, 8, r0 * 8, bx * 8, ref);
                    video_asm = 1;
                    video_cache = c;
                    rcheck_bands++;
                    for (y = 0; y < 8; y++)
                        for (x = r0 * 8; x < bx * 8; x++)
                            if (ref[y * RENDER_W + x] != band_px[y * RENDER_W + x]) {
                                rcheck_bad++;
                                y = 8;
                                break;
                            }
                }
            }
            bx = 0;
            while (bx < RENDER_BW) {                        /* converted: the runs of m and rc */
                int r0;
                if (!(w & (1UL << bx))) {
                    bx++;
                    continue;
                }
                r0 = bx;
                while (bx < RENDER_BW && (w & (1UL << bx)))
                    bx++;
                convert_fast_68k(band_px + r0 * 8, (UBYTE *)&nband[r0 * 8], RENDER_W * 2, (ULONG)(bx - r0) * 8,
                                 native16, 1);
                n += (ULONG)(bx - r0);
            }
            if (w) SP(3);
            if (show_fps && by == FPS_BY)
                fps_paint();
            if (w && bands_skip != 1)
                write_runs(&ri, by, w);
            if (w) SP(4);
            if (still) {                                    /* the block hashes are not of these */
                stale[by] = (stale[by] & ~rm2) | (m1[by] & ~rm2);
                if (w || m1[by]) hash_bad[by] = 1;
            } else if (by >= ry0 >> 3 && by < ry1 >> 3) {  /* moved: shown[] and the hashes are not */
                stale[by] = (stale[by] | rm) & ~rm2;
                hash_bad[by] = 1;
            }
        }
        p96UnlockBitMap(tb, lock);
    }
    SP(5);
    if (still) {
        /* the window: the rectangle of the written blocks */
        int y0 = -1, y1 = 0, x0 = RENDER_BW, x1 = -1;
        ULONG all = 0;
        for (by = 0; by < RENDER_BH; by++)
            if (d[by] | rc[by] | m1[by]) {
                if (y0 < 0) y0 = by;
                y1 = by;
                all |= d[by] | rc[by] | m1[by];
            }
        if (y0 >= 0 && bands_skip != 1) {
            for (x0 = 0; !(all & (1UL << x0)); x0++) ;
            for (x1 = RENDER_BW - 1; !(all & (1UL << x1)); x1--) ;
            BltBitMapRastPort(tb, (WORD)(x0 * 8 * s), (WORD)(y0 * 8 * s), win->RPort, (WORD)(ox + x0 * 8 * s),
                              (WORD)(oy + y0 * 8 * s), (WORD)((x1 + 1 - x0) * 8 * s), (WORD)((y1 + 1 - y0) * 8 * s), 0xC0);
        }
        if (two) {
            struct BitMap *t = vbm;
            vbm = vbm2;
            vbm2 = t;
        }
        SP(6);
        still_pictures++;
        still_blocks += n;
        prof_blocks += n;
        return 1;
    }
    BltBitMapRastPort(vbm2, (WORD)(rx0 * s), (WORD)(ry0 * s), win->RPort, (WORD)(ox + rx0 * s), (WORD)(oy + ry0 * s),
                      (WORD)((rx1 - rx0) * s), (WORD)((ry1 - ry0) * s), 0xC0);
    if (fps_out)
        BltBitMapRastPort(vbm2, (WORD)(FPS_BX * 8 * s), (WORD)(FPS_BY * 8 * s), win->RPort, (WORD)(ox + FPS_BX * 8 * s),
                          (WORD)(oy + FPS_BY * 8 * s), (WORD)(16 * s), (WORD)(8 * s), 0xC0);
    {
        struct BitMap *t = vbm;
        vbm = vbm2;
        vbm2 = t;
    }
    SP(6);
    shift_pictures++;
    shift_blocks += n;
    prof_blocks += n;
    return 1;
}

static void render_present_bands(void)
{
    const ULONG bw = (ULONG)(8 * scale * 2);
    ULONG dirty, spr;
    int by, x0 = RENDER_BW, x1 = -1, y0 = RENDER_BH, y1 = -1, full, pal;
    /* a new palette does not mean a new picture here: the hashes are of the screen pixels, so
       only the blocks whose colours changed are sent (not everything as in present()) */
    full = force_full || redraw || (++bands_count & 255) == 0;
    pal = machine.palette_dirty;
    if (shiftprof)
        sp_last = eclock_now();
    if (pal)
        update_colours();
    force_full = 0;
    spr = sprite_bands();
    if (!full && !incr_off && render_present_shift())
        goto done;
    if (full || incr_off || !prev_valid || machine.pri != prev_pri ||
        memcmp(&machine.scrollx[0][0], prev_scroll, 4) || memcmp(&machine.scrolly[0][0], prev_scroll + 4, 4))
        dirty = (1UL << RENDER_BH) - 1;
    else
        dirty = spr | prev_spr_bands | bg_bands(machine.bgram0, prev_bg0, machine.scrolly[0]) |
                bg_bands(machine.bgram1, prev_bg1, machine.scrolly[1]) | text_bands();
    if (pal)                                              /* SHIFT left blocks of shown[] behind */
        for (by = 0; by < RENDER_BH; by++)
            if (stale[by]) dirty |= 1UL << by;
    if (show_fps)
        dirty |= 1UL << FPS_BY;
    txt_masks_ok = 0;
    video_render_begin(&video, &machine);
    for (by = 0; by < RENDER_BH; by++) {
        uint16_t *band_px = &shown[by * 8 * RENDER_W];  /* the picture of indices, kept */
        UBYTE d[RENDER_BW];
        struct RenderInfo ri;
        LONG lock;
        int bx = 0, n;
        if (dirty & (1UL << by)) {
            video_render_lines(&video, &machine, XAIN_FIRST_LINE + by * 8, 8, band_px);
            if (rcheck) {
                /* RCHECK (tests): the band again with the C renderer, compared pixel by pixel */
                static uint16_t ref[RENDER_W * 8];
                int k;
                const int c = video_cache;
                video_asm = 0;
                video_cache = 0;
                video_render_lines(&video, &machine, XAIN_FIRST_LINE + by * 8, 8, ref);
                video_asm = 1;
                video_cache = c;
                rcheck_bands++;
                for (k = 0; k < RENDER_W * 8; k++)
                    if (ref[k] != band_px[k]) {
                        rcheck_bad++;
                        break;
                    }
            }
            bands_rendered++;
            stale[by] = 0;
        } else if (!pal) {
            continue;                                     /* nothing can have changed */
        }
        convert_fast_68k(band_px, (UBYTE *)nband, RENDER_W * 2, RENDER_W, native16, 1);
        if (show_fps && by == FPS_BY)
            fps_paint();
        bands_converted++;
        n = band_diff(nband, by, d, full || hash_bad[by]);
        hash_bad[by] = 0;
        if (!n || bands_skip == 1)
            continue;
        prof_blocks += (ULONG)n;
        lock = p96LockBitMap(vbm, (UBYTE *)&ri, sizeof ri);
        if (!ri.Memory) {
            p96UnlockBitMap(vbm, lock);
            force_full = 1;                               /* everything again next time */
            prev_valid = 0;
            return;
        }
        while (bx < RENDER_BW) {
            const ULONG bpr = (UWORD)ri.BytesPerRow;
            int r0;
            UBYTE *dst;
            if (!d[bx]) {
                bx++;
                continue;
            }
            r0 = bx;
            while (bx < RENDER_BW && d[bx])
                bx++;
            dst = (UBYTE *)ri.Memory + (ULONG)(by * 8 * scale) * bpr + (ULONG)r0 * bw;
            /* long word writes only (MOVE16 needs CacheClearE per run and was slower, PBENCH) */
            if (scale == 2)
                double_rows_68k(&nband[r0 * 8], dst, bpr, (ULONG)(bx - r0) * 8);
            else
                copy_rows((const UBYTE *)&nband[r0 * 8], dst, (ULONG)(bx - r0) * 16, 8, RENDER_W * 2, bpr, 0);
            if (r0 < x0) x0 = r0;
            if (bx - 1 > x1) x1 = bx - 1;
            if (by < y0) y0 = by;
            if (by > y1) y1 = by;
        }
        p96UnlockBitMap(vbm, lock);
    }
    if ((bands_count & 31) == 0) {                      /* vbm holds the whole picture */
        x0 = y0 = 0;
        x1 = RENDER_BW - 1;
        y1 = RENDER_BH - 1;
    }
    if (x1 >= 0)
        BltBitMapRastPort(vbm, (WORD)(x0 * 8 * scale), (WORD)(y0 * 8 * scale), win->RPort,
                          (WORD)(ox + x0 * 8 * scale), (WORD)(oy + y0 * 8 * scale),
                          (WORD)((x1 + 1 - x0) * 8 * scale), (WORD)((y1 + 1 - y0) * 8 * scale), 0xC0);
done:
    /* what the next picture is compared with */
    memcpy(prev_bg0, machine.bgram0, 0x800);
    memcpy(prev_bg1, machine.bgram1, 0x800);
    memcpy(prev_txt, machine.charram, 0x800);
    memcpy(prev_scroll, &machine.scrollx[0][0], 4);
    memcpy(prev_scroll + 4, &machine.scrolly[0][0], 4);
    memcpy(pal_prev, machine.palette, 0x200);
    memcpy(pal_prev + 0x200, machine.palette_ext, 0x200);
    spr_prev_n = sprite_rects(spr_prev);
    prev_pri = machine.pri;
    prev_spr_bands = spr;
    prev_valid = 1;
    if (verify && ++verify_frames % (ULONG)verify_every == 0)
        verify_window();
}

/* LAGLOG (diagnostics of slowdowns): a summary of every 10 seconds of play (574 frames) goes
   to RAM:XainSleena-lag.txt when the game ends: late frames (the picture left out), drawn
   pictures, average emulation and drawing time, the slowest frame, blocks sent, palette
   changes, flying stage. When 30% or more of the frames of such a window were late, the
   machine state of 1-2 seconds earlier is written to RAM:XainSleena-lagN.bin (up to 5, at
   least a minute apart; load with LOADSTATE= / -loadstate, DIP switches as in lagN.txt). */
static int laglog;
#define LAG_WIN 574
#define LAG_MAX 720
typedef struct {
    UWORD frames, late, drawn, pal, fly, file;
    ULONG emu, draw, worst, blocks;
    UBYTE scene;
} LagWin;
static LagWin *lag_win;
static int lag_n, lag_files;
static ULONG lag_frames, lag_late_all, lag_last_file, lag_state_frame[2], lag_state_len;
static UBYTE *lag_state[2];

static void lag_init(void)
{
    lag_state_len = xain_save_state(&machine, NULL);
    lag_win = AllocVec(sizeof(LagWin) * LAG_MAX, MEMF_ANY | MEMF_CLEAR);
    lag_state[0] = AllocVec(lag_state_len, MEMF_ANY);
    lag_state[1] = AllocVec(lag_state_len, MEMF_ANY);
    if (!lag_win || !lag_state[0] || !lag_state[1])
        laglog = 0;
}

/* one frame: us_emu and us_draw in microseconds */
static void lag_frame(ULONG us_emu, ULONG us_draw, int drawn, int pal, int late, ULONG blocks)
{
    ULONG f = lag_frames++;
    LagWin *w;
    if ((f & 63) == 0) {                                  /* the state every 64 frames, two kept */
        int k = (int)((f >> 6) & 1);
        xain_save_state(&machine, lag_state[k]);
        lag_state_frame[k] = f;
    }
    if (lag_n >= LAG_MAX)
        return;
    w = &lag_win[lag_n];
    w->frames++;
    w->late += late ? 1 : 0;
    w->drawn += drawn ? 1 : 0;
    w->pal += pal ? 1 : 0;
    w->fly += (machine.shared[0x53] & 0x80) ? 1 : 0;
    w->emu += us_emu;
    w->draw += us_draw;
    w->blocks += blocks;
    if (us_emu + us_draw > w->worst) w->worst = us_emu + us_draw;
    w->scene = machine.shared[0x41];
    lag_late_all += late ? 1 : 0;
    if (w->frames < LAG_WIN)
        return;
    if (w->late * 10 >= w->frames * 3 && lag_files < 5 && (lag_files == 0 || f - lag_last_file >= 3445)) {
        int k = lag_state_frame[0] < lag_state_frame[1] ? 0 : 1;   /* the older state */
        char path[48], line[112];
        BPTR fh;
        lag_files++;
        lag_last_file = f;
        w->file = (UWORD)lag_files;
        sprintf(path, "RAM:XainSleena-lag%d.bin", lag_files);
        if ((fh = Open((STRPTR)path, MODE_NEWFILE))) {
            Write(fh, lag_state[k], (LONG)lag_state_len);
            Close(fh);
        }
        sprintf(path, "RAM:XainSleena-lag%d.txt", lag_files);
        if ((fh = Open((STRPTR)path, MODE_NEWFILE))) {
            sprintf(line, "state of frame %lu, window ending at frame %lu (%u%% late); dsw0 %02x dsw1 %02x\n",
                    lag_state_frame[k], f, (unsigned)(w->late * 100 / w->frames), machine.dsw0, machine.dsw1);
            Write(fh, line, (LONG)strlen(line));
            Close(fh);
        }
    }
    lag_n++;
}

static void lag_report(void)
{
    BPTR fh;
    char line[160];
    int i, n;
    if (!laglog || !lag_win)
        return;
    fh = Open((STRPTR)"RAM:XainSleena-lag.txt", MODE_NEWFILE);
    if (!fh)
        return;
    sprintf(line, "XainSleena " XAIN_VERSION " LAGLOG: %lu frames, %lu late (%lu%%), %d state files\n",
            lag_frames, lag_late_all, lag_frames ? lag_late_all * 100 / lag_frames : 0, lag_files);
    Write(fh, line, (LONG)strlen(line));
    sprintf(line, "options: scale %d %s video %s bands %d sound %d; one line = 10 s (%d frames)\n", scale,
            screen_full ? "full screen" : "window", video_names[video_mode], bands_possible(), sound, LAG_WIN);
    Write(fh, line, (LONG)strlen(line));
    sprintf(line, " time  late%% drawn/s emu_ms draw_ms worst_ms blocks/pic pal fly scene\n");
    Write(fh, line, (LONG)strlen(line));
    n = lag_n < LAG_MAX && lag_win[lag_n].frames ? lag_n + 1 : lag_n;
    for (i = 0; i < n; i++) {
        const LagWin *w = &lag_win[i];
        ULONG fr = w->frames ? w->frames : 1, dr = w->drawn ? w->drawn : 1;
        sprintf(line, "%2d:%02d  %4lu %7lu %6lu.%lu %6lu.%lu %8lu %10lu %3u %3s %5u%s\n", i / 6, (i % 6) * 10,
                (ULONG)w->late * 100 / fr, (ULONG)w->drawn * 5744 / fr / 100,
                w->emu / fr / 1000, w->emu / fr / 100 % 10, w->draw / dr / 1000, w->draw / dr / 100 % 10,
                w->worst / 1000, w->blocks / dr, w->pal, w->fly * 2 > w->frames ? "yes" : "-", w->scene,
                w->file == 1 ? "  <- lag1.bin" : w->file == 2 ? "  <- lag2.bin" : w->file == 3 ? "  <- lag3.bin" :
                w->file == 4 ? "  <- lag4.bin" : w->file == 5 ? "  <- lag5.bin" : "");
        Write(fh, line, (LONG)strlen(line));
    }
    Close(fh);
}

/* PBENCH=n (tests): the picture output measured n times per stage from two consecutive
   pictures of the scene (LOADSTATE=), one clock read per series; results to stdout, then quit */
static int present_bench, present_bench_m16;   /* PBENCH=n, BENCHM16 also tests MOVE16 alone */
static uint64_t eclock_now(void);

static ULONG bench_us(uint64_t t0, int n)
{
    return (ULONG)((eclock_now() - t0) * 1000000 / eclock_freq / (ULONG)n);
}

static void load_state(void);

/* n frames of the scene from the state: emulation only (mode 0), + video_render_frame and
   present() (1), + render_present_bands (2); microseconds per frame */
static ULONG bench_series(int mode, int n)
{
    uint64_t t;
    int i;
    load_state();
    force_full = 1;
    t = eclock_now();
    for (i = 0; i < n; i++) {
        xain_run_frame(&machine);
        if (mode == 1) {
            video_render_frame(&video, &machine);
            present();
        } else if (mode == 2) {
            render_present_bands();
        }
    }
    WaitBlit();
    return bench_us(t, n);
}

static void run_present_bench(void)
{
    static uint16_t buf_a[RENDER_W * RENDER_H], buf_b[RENDER_W * RENDER_H];
    static UBYTE dirty[RENDER_BH][RENDER_BW];
    const int n = present_bench;
    uint64_t t;
    ULONG copy_us;
    int i, by;
    setvbuf(stdout, NULL, _IONBF, 0);             /* each line on disk at once (if it hangs) */
    xain_run_frame(&machine);
    video_render_frame(&video, &machine);
    memcpy(buf_a, video.fb, sizeof buf_a);
    xain_run_frame(&machine);
    video_render_frame(&video, &machine);
    memcpy(buf_b, video.fb, sizeof buf_b);
    if (machine.palette_dirty)
        update_colours();
    printf("PBENCH %d: %s, %d bytes/pixel, scale %d, %s, move16 %d\n", n, video_names[video_mode], vbpp,
           scale, screen_full ? "full screen" : "window", use_move16);
    printf("changed blocks between the two pictures: %lu of %d\n",
           find_dirty_68k(buf_a, buf_b, &dirty[0][0]), RENDER_BW * RENDER_BH);
    t = eclock_now();
    for (i = 0; i < n; i++) video_render_frame(&video, &machine);
    printf("%-44s %6lu us\n", "render (whole picture)", bench_us(t, n));
    t = eclock_now();
    for (i = 0; i < n; i++) find_dirty_68k(buf_a, buf_b, &dirty[0][0]);
    printf("%-44s %6lu us\n", "compare (find_dirty_68k)", bench_us(t, n));
    t = eclock_now();
    for (i = 0; i < n; i++) memcpy(video.fb, (i & 1) ? buf_b : buf_a, sizeof buf_a);
    copy_us = bench_us(t, n);
    printf("%-44s %6lu us (subtracted below)\n", "memcpy of a picture", copy_us);
    t = eclock_now();
    for (i = 0; i < n; i++) {
        memcpy(video.fb, (i & 1) ? buf_b : buf_a, sizeof buf_a);
        present();
    }
    WaitBlit();
    printf("%-44s %6lu us\n", "present(), changed blocks", bench_us(t, n) - copy_us);
    t = eclock_now();
    for (i = 0; i < n; i++) {
        memcpy(video.fb, (i & 1) ? buf_b : buf_a, sizeof buf_a);
        force_full = 1;
        present();
    }
    WaitBlit();
    printf("%-44s %6lu us\n", "present(), whole picture", bench_us(t, n) - copy_us);
    {
        ULONG emu = bench_series(0, n), old = bench_series(1, n), bands = 0;
        printf("%-44s %6lu us\n", "emulation of a frame (subtracted below)", emu);
        printf("%-44s %6lu us\n", "render + present, old way (per frame)", old - emu);
        if (bands_possible()) {
            bands = bench_series(2, n);
            printf("%-44s %6lu us\n", "render + present in bands (per frame)", bands - emu);
            bands_skip = 1;
            printf("%-44s %6lu us\n", "  bands: render + hashes only", bench_series(2, n) - emu);
            bands_skip = 0;
            {
                ULONG b0 = prof_blocks;
                bench_series(2, n);
                printf("blocks sent per frame in bands: %lu\n", (prof_blocks - b0) / (ULONG)n);
            }
        }
        printf("blocks sent in the series: %lu\n", prof_blocks);
    }
    if (video_mode == VID_BLIT && vbpp == 2 && scale == 1) {
        struct RenderInfo ri;
        LONG lock;
        t = eclock_now();
        for (i = 0; i < n; i++)
            for (by = 0; by < RENDER_BH; by++)
                convert_rows_68k(&shown[by * 8 * RENDER_W], sfb, sfb_bpr, RENDER_W, native, 1);
        printf("%-44s %6lu us\n", "convert whole picture into fast RAM", bench_us(t, n));
        t = eclock_now();
        for (i = 0; i < n; i++)
            for (by = 0; by < RENDER_BH; by++)
                convert_fast_68k(&shown[by * 8 * RENDER_W], sfb, sfb_bpr, RENDER_W, native16, 1);
        printf("%-44s %6lu us\n", "  the same, convert_fast_68k", bench_us(t, n));
        t = eclock_now();
        for (i = 0; i < n; i++)
            for (by = 0; by < RENDER_BH; by++)
                convert_fast_68k(band, sfb, sfb_bpr, RENDER_W, native16, 1);
        printf("%-44s %6lu us\n", "  convert_fast_68k from one band (cached)", bench_us(t, n));
        t = eclock_now();
        for (i = 0; i < n; i++)
            CacheClearE(sfb, sfb_bpr * 8, CACRF_ClearD);
        printf("%-44s %6lu us\n", "CacheClearE of one block row", bench_us(t, n));
        t = eclock_now();
        for (i = 0; i < n; i++) {
            lock = p96LockBitMap(vbm, (UBYTE *)&ri, sizeof ri);
            p96UnlockBitMap(vbm, lock);
        }
        printf("%-44s %6lu us\n", "p96LockBitMap + p96UnlockBitMap", bench_us(t, n));
        lock = p96LockBitMap(vbm, (UBYTE *)&ri, sizeof ri);
        if (ri.Memory) {
            const ULONG bpr = (UWORD)ri.BytesPerRow;
            int m16;
            printf("VRAM %08lx, %lu bytes per row\n", (ULONG)ri.Memory, bpr);
            for (m16 = 0; m16 < 2; m16++) {
                if (m16 && ((((ULONG)ri.Memory | bpr) & 15) || present_bench_m16 == 0))
                    break;
                printf("copy test %d...\n", m16);
                t = eclock_now();
                for (i = 0; i < n; i++)
                    for (by = 0; by < RENDER_BH; by++)
                        copy_rows(sfb, (UBYTE *)ri.Memory + (ULONG)by * 8 * bpr, RENDER_W * 2, 8, sfb_bpr, bpr,
                                  (ULONG)m16);
                printf("%-44s %6lu us\n", m16 ? "copy whole picture to VRAM, MOVE16" : "copy whole picture to VRAM, long moves",
                       bench_us(t, n));
            }
        }
        p96UnlockBitMap(vbm, lock);
        t = eclock_now();
        for (i = 0; i < n; i++)
            BltBitMapRastPort(vbm, 0, 0, win->RPort, (WORD)ox, (WORD)oy, RENDER_W, RENDER_H, 0xC0);
        WaitBlit();
        printf("%-44s %6lu us\n", "blit whole picture to the screen", bench_us(t, n));
    }
    exit(0);
}

/* ------------------------------------------------------------------ timing */

static void open_timer(void)
{
    struct EClockVal ev;

    timer_port = CreateMsgPort();
    if (!timer_port)
        fail("Cannot create a message port.");
    timer_req = (struct timerequest *)CreateIORequest(timer_port, sizeof(struct timerequest));
    if (!timer_req || OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)timer_req, 0)) {
        if (timer_req)
            DeleteIORequest((struct IORequest *)timer_req);
        timer_req = NULL;
        fail("Cannot open timer.device.");
    }
    TimerBase = timer_req->tr_node.io_Device;
    eclock_freq = ReadEClock(&ev);
}

static uint64_t eclock_now(void)
{
    struct EClockVal ev;
    ReadEClock(&ev);
    return ((uint64_t)ev.ev_hi << 32) | ev.ev_lo;
}

/* wait until the next arcade frame (57.44 Hz) is due; returns 0 if it is overdue by more
   than a third of a frame */
static int pace(void)
{
    const uint64_t period = (uint64_t)eclock_freq * 1000 / XAIN_FPS_X1000;
    uint64_t now = eclock_now();

    if (next_frame == 0 || now > next_frame + eclock_freq / 4)
        next_frame = now;                                /* start or resync after a stall */
    next_frame += period;
    if (next_frame > now) {
        ULONG micros = (ULONG)((next_frame - now) * 1000000 / eclock_freq);
        if (micros > 1000) {
            timer_req->tr_node.io_Command = TR_ADDREQUEST;
            timer_req->tr_time.tv_secs = 0;
            timer_req->tr_time.tv_micro = micros;
            DoIO((struct IORequest *)timer_req);
        }
        return 1;
    }
    return now - next_frame <= period / 3;
}

/* ------------------------------------------------------------------ sound */

/*
 * Paula through audio.device (default; two channels play the same 8 bit buffers) at the
 * exact Paula rate for period 161 (PAL 22030 Hz), or the AHI library (double buffered).
 * The sound of a frame (about 384 samples) goes into a ring, from there into the buffers.
 */
#define SND_BUF 768                   /* 35 ms */
#define SND_RING 8192

static ULONG audio_rate = 22050;
static int16_t audio[2048];
static struct MsgPort *ahi_port;
static struct AHIRequest *ahi_io;
static struct AHIAudioCtrl *actrl;
static int16_t *snd_buf[2];
static struct Task *snd_task;
static BYTE snd_signal = -1;
static volatile UWORD snd_queued;
static volatile UWORD snd_to_fill;
static volatile ULONG snd_swaps;
static ULONG snd_handled;
static int16_t snd_ring[SND_RING];
static ULONG ring_r, ring_w;
enum { SND_NONE, SND_AHI, SND_PAULA };
static const char *const sound_names[] = { "no sound", "AHI", "Paula" };
static int sound_request = SND_PAULA;
static int sound_mode = SND_NONE;

static ULONG sound_func(__reg("a0") struct Hook *hook, __reg("a2") struct AHIAudioCtrl *ac,
                        __reg("a1") struct AHISoundMessage *msg)
{
    (void)hook;
    (void)msg;
    snd_queued ^= 1;
    AHI_SetSound(0, snd_queued, 0, 0, ac, 0);
    snd_to_fill = snd_queued;
    snd_swaps++;
    Signal(snd_task, 1UL << snd_signal);
    return 0;
}

static struct Hook sound_hook = { {NULL, NULL}, (HOOKFUNC)sound_func, NULL, NULL };

static void open_ahi(void)
{
    struct AHISampleInfo info;
    int i;

    audio_rate = 22050;
    ahi_port = CreateMsgPort();
    if (!ahi_port)
        return;
    ahi_io = (struct AHIRequest *)CreateIORequest(ahi_port, sizeof(struct AHIRequest));
    if (!ahi_io)
        return;
    ahi_io->ahir_Version = 4;
    if (OpenDevice((STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)ahi_io, 0)) {
        DeleteIORequest((struct IORequest *)ahi_io);
        ahi_io = NULL;
        return;
    }
    AHIBase = (struct Library *)ahi_io->ahir_Std.io_Device;

    snd_task = FindTask(NULL);
    snd_signal = AllocSignal(-1);
    for (i = 0; i < 2; i++)
        snd_buf[i] = AllocVec(SND_BUF * sizeof(int16_t), MEMF_PUBLIC | MEMF_CLEAR);
    if (snd_signal < 0 || !snd_buf[0] || !snd_buf[1])
        return;

    actrl = AHI_AllocAudio(AHIA_AudioID, AHI_DEFAULT_ID,
                           AHIA_MixFreq, AHI_DEFAULT_FREQ,
                           AHIA_Channels, 1,
                           AHIA_Sounds, 2,
                           AHIA_SoundFunc, (ULONG)&sound_hook,
                           TAG_DONE);
    if (!actrl)
        return;
    for (i = 0; i < 2; i++) {
        info.ahisi_Type = AHIST_M16S;
        info.ahisi_Address = snd_buf[i];
        info.ahisi_Length = SND_BUF;
        if (AHI_LoadSound((UWORD)i, AHIST_DYNAMICSAMPLE, &info, actrl) != AHIE_OK) {
            AHI_FreeAudio(actrl);
            actrl = NULL;
            return;
        }
    }
    if (AHI_ControlAudio(actrl, AHIC_Play, TRUE, TAG_DONE) != AHIE_OK) {
        AHI_FreeAudio(actrl);
        actrl = NULL;
        return;
    }
    snd_queued = 1;
    AHI_Play(actrl,
             AHIP_BeginChannel, 0,
             AHIP_Freq, audio_rate,
             AHIP_Vol, 0x10000,
             AHIP_Pan, 0x8000,
             AHIP_Sound, 0,
             AHIP_EndChannel, NULL,
             TAG_DONE);
}

#define PAULA_BUFS 3

static struct MsgPort *aud_port;
static struct IOAudio *aud_alloc;
static struct IOAudio *aud_io[PAULA_BUFS][2];
static BYTE *aud_buf[PAULA_BUFS];
static UBYTE aud_busy[PAULA_BUFS];

static void paula_send(int b, ULONG n)
{
    int c;
    for (c = 0; c < 2; c++) {
        aud_io[b][c]->ioa_Request.io_Command = CMD_WRITE;
        aud_io[b][c]->ioa_Request.io_Flags = ADIOF_PERVOL;
        aud_io[b][c]->ioa_Length = n;
        BeginIO((struct IORequest *)aud_io[b][c]);
    }
    aud_busy[b] = 2;
}

static int open_paula(void)
{
    static UBYTE channels[] = { 3, 5, 10, 12 };
    const ULONG clock = (GfxBase->DisplayFlags & PAL) ? 3546895 : 3579545;
    /* emulation: 161 = 22030 Hz on PAL, 22233 Hz on NTSC; samples: the period of their rate
       (22030 Hz: 161 on PAL), so they play 1:1 */
    const UWORD period = use_samples ? (UWORD)((clock + snd.rate / 2) / snd.rate) : 161;
    ULONG units;
    int b, c;

    audio_rate = (clock + period / 2) / period;
    aud_port = CreateMsgPort();
    if (!aud_port)
        return 0;
    aud_alloc = (struct IOAudio *)CreateIORequest(aud_port, sizeof(struct IOAudio));
    if (!aud_alloc)
        return 0;
    aud_alloc->ioa_Request.io_Message.mn_Node.ln_Pri = 1;
    aud_alloc->ioa_Request.io_Flags = ADIOF_NOWAIT;
    aud_alloc->ioa_Data = channels;
    aud_alloc->ioa_Length = sizeof channels;
    if (OpenDevice((STRPTR)AUDIONAME, 0, (struct IORequest *)aud_alloc, 0)) {
        DeleteIORequest((struct IORequest *)aud_alloc);
        aud_alloc = NULL;
        return 0;
    }
    units = (ULONG)aud_alloc->ioa_Request.io_Unit;
    for (b = 0; b < PAULA_BUFS; b++) {
        aud_buf[b] = AllocMem(SND_BUF, MEMF_CHIP | MEMF_CLEAR);
        if (!aud_buf[b])
            return 0;
        for (c = 0; c < 2; c++) {
            struct IOAudio *io = AllocMem(sizeof *io, MEMF_PUBLIC);
            if (!io)
                return 0;
            *io = *aud_alloc;
            io->ioa_Request.io_Unit = (struct Unit *)(units & (c ? 0x06 : 0x09));
            io->ioa_Data = (UBYTE *)aud_buf[b];
            io->ioa_Period = period;
            io->ioa_Volume = 64;
            io->ioa_Cycles = 1;
            aud_io[b][c] = io;
        }
    }
    for (b = 0; b < PAULA_BUFS; b++)
        paula_send(b, 64);
    return 1;
}

static void close_paula(void)
{
    int b, c;

    for (b = 0; b < PAULA_BUFS; b++)
        for (c = 0; c < 2; c++)
            if (aud_io[b][c]) {
                if (aud_busy[b]) {
                    AbortIO((struct IORequest *)aud_io[b][c]);
                    WaitIO((struct IORequest *)aud_io[b][c]);
                }
                FreeMem(aud_io[b][c], sizeof(struct IOAudio));
                aud_io[b][c] = NULL;
            }
    if (aud_alloc) {
        CloseDevice((struct IORequest *)aud_alloc);
        DeleteIORequest((struct IORequest *)aud_alloc);
        aud_alloc = NULL;
    }
    for (b = 0; b < PAULA_BUFS; b++)
        if (aud_buf[b]) {
            FreeMem(aud_buf[b], SND_BUF);
            aud_buf[b] = NULL;
        }
    if (aud_port) {
        DeleteMsgPort(aud_port);
        aud_port = NULL;
    }
}

static void paula_push(void)
{
    struct IOAudio *io;
    int b, c;

    while ((io = (struct IOAudio *)GetMsg(aud_port)))
        for (b = 0; b < PAULA_BUFS; b++)
            for (c = 0; c < 2; c++)
                if (aud_io[b][c] == io && aud_busy[b])
                    aud_busy[b]--;
    if (ring_w - ring_r > 4 * SND_BUF)
        ring_r = ring_w - 2 * SND_BUF;
    for (b = 0; b < PAULA_BUFS; b++) {
        ULONG avail = ring_w - ring_r, n, i;
        if (aud_busy[b] || avail < 2)
            continue;
        n = (avail < SND_BUF ? avail : SND_BUF) & ~1UL;
        for (i = 0; i < n; i++)
            aud_buf[b][i] = (BYTE)(snd_ring[ring_r++ % SND_RING] >> 8);
        paula_send(b, n);
    }
}

/*
 * Sound from samples: the buffers are filled straight from the mixer when the hardware has
 * played them, not per emulated frame, so the sound keeps its pace when the game is slower
 * than 57.44 Hz. Paula: buffers of SMP_BUF samples (25 ms, 3 queued = 75 ms latency at most);
 * 8 bit with gain SMP_GAIN and clipping (the music peaks at ca. 1/4 of the 16 bit range, so >> 8
 * alone would leave ca. 34 levels).
 */
#define SMP_BUF ((int)(audio_rate / 40) & ~1)     /* 25 ms, at most SND_BUF */
#define SMP_GAIN 3

static void samples_fill(void)
{
    static int16_t mix[SND_BUF];
    int i;
    if (sound_mode == SND_PAULA) {
        struct IOAudio *io;
        int b, c;
        while ((io = (struct IOAudio *)GetMsg(aud_port)))
            for (b = 0; b < PAULA_BUFS; b++)
                for (c = 0; c < 2; c++)
                    if (aud_io[b][c] == io && aud_busy[b])
                        aud_busy[b]--;
        for (b = 0; b < PAULA_BUFS; b++) {
            if (aud_busy[b])
                continue;
            sndplay_mix(&snd, mix, SMP_BUF, audio_rate);
            for (i = 0; i < SMP_BUF; i++) {
                LONG v = ((LONG)mix[i] * SMP_GAIN) >> 8;
                aud_buf[b][i] = (BYTE)(v > 127 ? 127 : v < -128 ? -128 : v);
            }
            paula_send(b, SMP_BUF);
        }
    } else if (sound_mode == SND_AHI && snd_swaps != snd_handled) {
        snd_handled = snd_swaps;
        SetSignal(0, 1UL << snd_signal);
        sndplay_mix(&snd, snd_buf[snd_to_fill], SND_BUF, audio_rate);
    }
}

static void open_sound(void)
{
    if (sound_request == SND_AHI) {
        open_ahi();
        if (actrl) {
            sound_mode = SND_AHI;
            return;
        }
    }
    if (open_paula()) {
        sound_mode = SND_PAULA;
        return;
    }
    close_paula();
    if (sound_request != SND_AHI) {
        open_ahi();
        if (actrl)
            sound_mode = SND_AHI;
    }
}

static void close_sound(void)
{
    close_paula();
    if (actrl) {
        AHI_ControlAudio(actrl, AHIC_Play, FALSE, TAG_DONE);
        AHI_FreeAudio(actrl);
        actrl = NULL;
    }
    if (ahi_io) {
        CloseDevice((struct IORequest *)ahi_io);
        DeleteIORequest((struct IORequest *)ahi_io);
    }
    if (ahi_port)
        DeleteMsgPort(ahi_port);
    if (snd_buf[0])
        FreeVec(snd_buf[0]);
    if (snd_buf[1])
        FreeVec(snd_buf[1]);
    if (snd_signal >= 0)
        FreeSignal(snd_signal);
}

static void audio_push(const int16_t *samples, int count)
{
    int i;

    if (sound_mode == SND_NONE)
        return;
    for (i = 0; i < count; i++)
        snd_ring[ring_w++ % SND_RING] = samples[i];
    if (ring_w - ring_r > SND_RING - SND_BUF)
        ring_r = ring_w - 2 * SND_BUF;

    if (sound_mode == SND_PAULA) {
        paula_push();
        return;
    }
    if (snd_swaps != snd_handled) {
        int16_t *dst = snd_buf[snd_to_fill];
        ULONG avail = ring_w - ring_r, n = avail < SND_BUF ? avail : SND_BUF;
        snd_handled = snd_swaps;
        SetSignal(0, 1UL << snd_signal);
        if (avail > 4 * SND_BUF) {
            ring_r = ring_w - 2 * SND_BUF;
            n = SND_BUF;
        }
        for (i = 0; i < (int)n; i++)
            dst[i] = snd_ring[ring_r++ % SND_RING];
        for (; i < SND_BUF; i++)
            dst[i] = 0;
    }
}

/* ------------------------------------------------------------------ input */

#define RAW_1      0x01
#define RAW_2      0x02
#define RAW_5      0x05
#define RAW_6      0x06
#define RAW_P      0x19
#define RAW_Y      0x15
#define RAW_Z      0x31
#define RAW_X      0x32
#define RAW_SPACE  0x40
#define RAW_ESC    0x45
#define RAW_UP     0x4C
#define RAW_DOWN   0x4D
#define RAW_RIGHT  0x4E
#define RAW_LEFT   0x4F
#define RAW_F3     0x52
#define RAW_F10    0x59

/* F10: a debug dump, numbered (n = the first free number, nothing is overwritten):
     RAM:XainSleena-dump<n>.bin    a replay: the machine state 9-18 s earlier and the inputs of
                                   every frame since then (PC: xainsleena -replay FILE)
     RAM:XainSleena-state<n>.bin   the machine state now (LOADSTATE=, PC -loadstate)
     RAM:XainSleena-dump<n>.txt    release, options, the stage (RAM of the game), keyboard,
                                   joystick, the last key events, whether the window was active
     RAM:XainSleena-dump<n>.ppm    the picture as the window shows it
   For it the inputs of the last 1024 frames and a state every 512 frames are kept. */
#define REC_SIZE 1024
static UBYTE rec_in[REC_SIZE][2];
static ULONG rec_frame, rec_snap_frame[2], rec_state_len;
static int rec_snap_ok[2];
static UBYTE *rec_state[2];
static struct { ULONG frame; UWORD code; } key_log[32];
static ULONG key_log_n;
static int win_active = 1;
static ULONG last_joy;

static void rec_init(void)
{
    rec_state_len = xain_save_state(&machine, NULL);
    rec_state[0] = AllocVec(rec_state_len, MEMF_ANY);
    rec_state[1] = AllocVec(rec_state_len, MEMF_ANY);
}

/* before each emulated frame (inputs already set) */
static void rec_before_frame(void)
{
    if ((rec_frame & 511) == 0 && rec_state[0] && rec_state[1]) {
        int k = (int)((rec_frame >> 9) & 1);
        xain_save_state(&machine, rec_state[k]);
        rec_snap_frame[k] = rec_frame;
        rec_snap_ok[k] = 1;
    }
    rec_in[rec_frame & (REC_SIZE - 1)][0] = machine.in_p1;
    rec_in[rec_frame & (REC_SIZE - 1)][1] = machine.in_p2;
}

static void put_be32(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16); p[2] = (UBYTE)(v >> 8); p[3] = (UBYTE)v;
}

static int write_file(const char *path, const void *data, ULONG len)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    LONG n;
    if (!fh)
        return 0;
    n = Write(fh, (APTR)data, (LONG)len);
    Close(fh);
    return n == (LONG)len;
}

static void save_state(void)
{
    char path[64], *text;
    UBYTE *buf;
    ULONG len = 0, cur_len = xain_save_state(&machine, NULL), i, start, count;
    int n, k, j;
    BPTR lock;
    for (n = 1; n < 100; n++) {                            /* the first free number */
        sprintf(path, "RAM:XainSleena-dump%d.txt", n);
        if (!(lock = Lock((STRPTR)path, ACCESS_READ)))
            break;
        UnLock(lock);
    }
    /* the replay: the older of the two states and the inputs since then */
    k = rec_snap_ok[0] && rec_snap_ok[1] ? (rec_snap_frame[0] < rec_snap_frame[1] ? 0 : 1) : rec_snap_ok[1] ? 1 : 0;
    start = rec_snap_frame[k];
    count = rec_snap_ok[k] ? rec_frame - start : 0;
    buf = AllocVec(16 + rec_state_len + REC_SIZE * 2 + cur_len, MEMF_ANY);
    text = AllocVec(4096, MEMF_ANY);
    if (!buf || !text) {
        if (buf) FreeVec(buf);
        if (text) FreeVec(text);
        return;
    }
    if (count) {
        memcpy(buf, "XRP1", 4);
        put_be32(buf + 4, start);
        put_be32(buf + 8, count);
        put_be32(buf + 12, rec_state_len);
        memcpy(buf + 16, rec_state[k], rec_state_len);
        for (i = 0; i < count; i++) {
            buf[16 + rec_state_len + i * 2] = rec_in[(start + i) & (REC_SIZE - 1)][0];
            buf[16 + rec_state_len + i * 2 + 1] = rec_in[(start + i) & (REC_SIZE - 1)][1];
        }
        sprintf(path, "RAM:XainSleena-dump%d.bin", n);
        write_file(path, buf, 16 + rec_state_len + count * 2);
    }
    xain_save_state(&machine, buf);
    sprintf(path, "RAM:XainSleena-state%d.bin", n);
    write_file(path, buf, cur_len);
    /* the description */
    len += sprintf(text + len, "XainSleena " XAIN_VERSION " F10 dump %d: frame %lu (machine frame %lu); replay from frame %lu, %lu frames\n",
                   n, rec_frame, (ULONG)machine.frame, start, count);
    len += sprintf(text + len, "options: scale %d, %s, video %s, bands %d, control %d, sound %d, lives %d, difficulty %d, start_arcade %d, dsw0 %02x dsw1 %02x\n",
                   scale, screen_full ? "full screen" : "window", video_names[video_mode], bands_possible(), control,
                   sound, lives, easy_damage, start_arcade, machine.dsw0, machine.dsw1);
    len += sprintf(text + len, "stage: playing %u, scene $41=%02x, stage type $48=%02x, %s ($53=%02x), $52=%02x, on foot %d\n",
                   machine.shared[0x39], machine.shared[0x41], machine.shared[0x48],
                   (machine.shared[0x53] & 0x80) ? "flying (ship)" : "not flying", machine.shared[0x53],
                   machine.shared[0x52], xain_on_foot(&machine));
    len += sprintf(text + len, "shared RAM 0030-006F:");
    for (j = 0x30; j < 0x70; j++)
        len += sprintf(text + len, "%s%02x", (j & 15) ? " " : "\n  ", machine.shared[j]);
    len += sprintf(text + len, "\nCPU: main pc %04x, sub pc %04x, mcu pc %03x\n", machine.main.pc, machine.sub.pc,
                   machine.mcu.cpu.pc);
    len += sprintf(text + len, "input: window %s, in_p1 %02x in_p2 %02x, joystick port 2 %08lx, keys held:",
                   win_active ? "active" : "NOT ACTIVE", machine.in_p1, machine.in_p2, last_joy);
    for (j = 0; j < 128; j++)
        if (keys[j])
            len += sprintf(text + len, " %02x", j);
    len += sprintf(text + len, "\ninputs of the last 16 frames (p1):");
    for (i = 16; i >= 1; i--)
        if (rec_frame >= i)
            len += sprintf(text + len, " %02x", rec_in[(rec_frame - i) & (REC_SIZE - 1)][0]);
    len += sprintf(text + len, "\nlast key events (frame:code, +80 = released):");
    for (i = key_log_n > 32 ? key_log_n - 32 : 0; i < key_log_n; i++)
        len += sprintf(text + len, "%s%lu:%02x", (i % 8) ? " " : "\n  ", key_log[i & 31].frame, key_log[i & 31].code);
    len += sprintf(text + len, "\n");
    sprintf(path, "RAM:XainSleena-dump%d.txt", n);
    write_file(path, text, len);
    /* the picture the window shows (RTG: 24 bit) */
    if (rtg_ok) {
        const ULONG w = (ULONG)(RENDER_W * scale), h = (ULONG)(RENDER_H * scale);
        UBYTE *img = AllocVec(w * h * 3 + 32, MEMF_ANY);
        if (img) {
            struct RenderInfo ri;
            int hl = sprintf((char *)img, "P6\n%lu %lu\n255\n", w, h);
            WaitBlit();
            ri.Memory = img + 32;
            ri.BytesPerRow = (WORD)(w * 3);
            ri.pad = 0;
            ri.RGBFormat = RGBFB_R8G8B8;
            p96ReadPixelArray(&ri, 0, 0, win->RPort, (UWORD)ox, (UWORD)oy, (UWORD)w, (UWORD)h);
            memmove(img + hl, img + 32, w * h * 3);
            sprintf(path, "RAM:XainSleena-dump%d.ppm", n);
            write_file(path, img, (ULONG)hl + w * h * 3);
            FreeVec(img);
        }
    }
    FreeVec(text);
    FreeVec(buf);
    DisplayBeep(NULL);
    force_full = 1;                                     /* the flash may have changed the screen */
}

/* LOADSTATE=file: the machine from an F10 state (repeatable tests and measurements of one scene) */
static void load_state(void)
{
    BPTR fh = Open((STRPTR)state_path, MODE_OLDFILE);
    uint32_t len = xain_save_state(&machine, NULL);
    UBYTE *buf = AllocVec(len + 16, MEMF_ANY);
    LONG got = -1;
    if (fh && buf)
        got = Read(fh, buf, (LONG)len + 16);
    if (fh)
        Close(fh);
    if (got <= 0 || !xain_load_state(&machine, buf, (uint32_t)got)) {
        if (buf) FreeVec(buf);
        fail("LOADSTATE: not a machine state.");
    }
    FreeVec(buf);
}

/* REPLAY=file (tests): an F10 replay (RAM:XainSleena-dumpN.bin: "XRP1", start frame, frame count,
   state length, state, 2 input bytes per frame) is played with the recorded inputs, then the game
   quits (LAGLOG / VERIFY results at the exit) */
static void load_replay(void)
{
    BPTR fh = Open((STRPTR)replay_path, MODE_OLDFILE);
    LONG size, got;
    UBYTE *buf;
    ULONG count, slen;
    if (!fh)
        fail("REPLAY: cannot open the file.");
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    buf = AllocVec((ULONG)size + 4, MEMF_ANY);
    got = buf ? Read(fh, buf, size) : -1;
    Close(fh);
    if (got != size || size < 16 || memcmp(buf, "XRP1", 4))
        fail("REPLAY: not a replay.");
    count = (ULONG)buf[8] << 24 | (ULONG)buf[9] << 16 | (ULONG)buf[10] << 8 | buf[11];
    slen = (ULONG)buf[12] << 24 | (ULONG)buf[13] << 16 | (ULONG)buf[14] << 8 | buf[15];
    if (16 + slen + count * 2 > (ULONG)size || !xain_load_state(&machine, buf + 16, slen))
        fail("REPLAY: not a replay.");
    replay_in = buf + 16 + slen;                        /* (kept until the end) */
    replay_count = count;
}

static void handle_messages(void)
{
    struct IntuiMessage *msg;
    int quit = 0;

    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = msg->Class;
        UWORD code = msg->Code;
        ReplyMsg((struct Message *)msg);
        if (cls == IDCMP_CLOSEWINDOW)
            quit = 1;
        else if (cls == IDCMP_RAWKEY) {
            int down = !(code & IECODE_UP_PREFIX);
            keys[code & 0x7F] = (UBYTE)down;
            key_log[key_log_n & 31].frame = rec_frame;
            key_log[key_log_n & 31].code = code;
            key_log_n++;
            if (code == RAW_ESC)
                quit = 1;
            else if (code == RAW_P)
                paused = !paused;
            else if (code == RAW_F3) {
                xain_reset(&machine);
                sndplay_reset(&snd);
            }
            else if (code == RAW_F10)
                save_state();
        } else if (cls == IDCMP_INACTIVEWINDOW) {
            memset(keys, 0, sizeof keys);
            win_active = 0;
        } else if (cls == IDCMP_ACTIVEWINDOW) {
            force_full = 1;                              /* repaint everything once */
            win_active = 1;
        }
        else if (cls == IDCMP_REFRESHWINDOW) {
            BeginRefresh(win);
            EndRefresh(win, TRUE);
            force_full = 1;                              /* the window was damaged */
        }
    }
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        quit = 1;
    if (quit)
        exit(0);
}

/* the arcade inputs from the keyboard and the joystick; ReadJoyPort() is slow on real
   hardware, so it is read once per frame */
static void read_inputs(void)
{
    static uint8_t up_wait;
    uint8_t p1 = 0, p2 = 0;
    int up_jump = keys[RAW_UP];                         /* as the joystick (JOY1): up jumps on foot */

    if (keys[RAW_LEFT]) p1 |= IN_LEFT;
    if (keys[RAW_RIGHT]) p1 |= IN_RIGHT;
    if (keys[RAW_UP]) p1 |= IN_UP;
    if (keys[RAW_DOWN]) p1 |= IN_DOWN;
    if (keys[RAW_Z]) p1 |= IN_BUTTON1;
    if (keys[RAW_X] || keys[RAW_Y] || (control == CTL_JOY1 && keys[RAW_SPACE])) p1 |= IN_BUTTON2;
    if (keys[RAW_1]) p1 |= IN_P1_START1;
    if (keys[RAW_2]) p1 |= IN_P1_START2;
    if (keys[RAW_5]) p2 |= IN_P2_COIN1;
    if (keys[RAW_6]) p2 |= IN_P2_COIN2;

    /* joystick port (and with JOYPORT=BOTH the mouse port): fire, second button = jump (JOY2;
       with JOY1 it is ignored: up jumps on foot, Space is button 2); CD32 pad: red = fire, blue = jump, play = start,
       green = coin, yellow = start 2 */
    if (LowLevelBase && control != CTL_KEYBOARD) {
        int port;
        for (port = 1; port >= (joy_mouseport ? 0 : 1); port--) {
            ULONG j = ReadJoyPort((ULONG)port), type = j & JP_TYPE_MASK;
            if (port == 1)
                last_joy = j;
            if (type != JP_TYPE_JOYSTK && type != JP_TYPE_GAMECTLR)
                continue;
            if (j & JPF_JOY_UP) {
                p1 |= IN_UP;
                /* JOY1: on foot up also jumps (up has no function of its own there: up + jump =
                   jump); in the flying stages up only moves, Space fires the extra weapon */
                if (control == CTL_JOY1)
                    up_jump = 1;
            }
            if (j & JPF_JOY_DOWN) p1 |= IN_DOWN;
            if (j & JPF_JOY_LEFT) p1 |= IN_LEFT;
            if (j & JPF_JOY_RIGHT) p1 |= IN_RIGHT;
            if (j & JPF_BUTTON_RED) p1 |= IN_BUTTON1;
            if ((j & JPF_BUTTON_BLUE) && (control == CTL_JOY2 || type == JP_TYPE_GAMECTLR))
                p1 |= IN_BUTTON2;
            if (type == JP_TYPE_GAMECTLR) {
                if (j & JPF_BUTTON_PLAY) p1 |= IN_P1_START1;
                if (j & JPF_BUTTON_GREEN) p2 |= IN_P2_COIN1;
                if (j & JPF_BUTTON_YELLOW) p1 |= IN_P1_START2;
            }
        }
    }
    machine.in_p1 = xain_up_jump(&up_wait, p1, up_jump && xain_on_foot(&machine));
    machine.in_p2 = p2;
}

/* ------------------------------------------------------------------ options */

static int same_text(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y)
            return 0;
    }
    return *a == *b;
}

static void parse_option(const char *opt)
{
    static char rp[4][256];
    char name[12];
    const char *val;
    int i;

    for (i = 0; i < 11 && opt[i] && opt[i] != '='; i++)
        name[i] = opt[i];
    name[i] = 0;
    val = opt[i] == '=' ? opt + i + 1 : 0;

    if (same_text(name, "SCALE") && val)
        scale = atoi(val);
    else if (same_text(opt, "NOSOUND"))
        sound = 0;
    else if (same_text(opt, "STATS"))
        stats = 1;
    else if (same_text(opt, "FPS"))
        show_fps = 1;
    else if (same_text(opt, "STATS=FULL"))
        stats = 2;
    else if (same_text(opt, "SOUND=AHI"))
        sound_request = SND_AHI;
    else if (same_text(opt, "SOUND=PAULA"))
        sound_request = SND_PAULA;
    else if (same_text(opt, "EMUSOUND"))
        emusound = 1;
    else if (same_text(opt, "START=ARCADE"))
        start_arcade = 1;
    else if (same_text(opt, "START=EASY"))
        start_arcade = 0;
    else if (same_text(opt, "DIFFICULTY=EASY"))
        easy_damage = 1;
    else if (same_text(opt, "DIFFICULTY=VERYEASY"))
        easy_damage = 2;
    else if (same_text(opt, "DIFFICULTY=NODAMAGE"))
        easy_damage = 3;
    else if (same_text(opt, "TEST")) {               /* testing stages: no damage, infinite lives */
        easy_damage = 3;
        lives = 0;
    }
    else if (same_text(opt, "DIFFICULTY=NORMAL"))
        easy_damage = 0;
    else if (same_text(name, "LIVES") && val)
        lives = same_text(val, "INF") ? 0 : atoi(val);
    else if (same_text(opt, "CONTROL=KEYBOARD"))
        control = CTL_KEYBOARD;
    else if (same_text(opt, "CONTROL=JOY1"))
        control = CTL_JOY1;
    else if (same_text(opt, "CONTROL=JOY2"))
        control = CTL_JOY2;
    else if (same_text(opt, "SUBFAST=OFF"))
        subfast_off = 1;                                 /* measurements */
    else if (same_text(opt, "HLE=ON"))
        hle_on = 1;                                      /* tests: translated sprite routine */
    else if (same_text(opt, "MEMO=OFF"))
        memo_off = 1;                                    /* measurements */
    else if (same_text(opt, "SNDLOG"))
        sndlog = 1;                                      /* testing */
    else if (same_text(name, "SAMPLES") && val) {
        static char sp[256];
        snprintf(sp, sizeof sp, "%s", val);
        samples_path = sp;
    }
    else if (same_text(opt, "NORTG") || same_text(opt, "VIDEO=WCP"))
        video_request = VID_WCP;
    else if (same_text(opt, "VIDEO=BLIT"))
        video_request = VID_BLIT;
    else if (same_text(opt, "VIDEO=WPA"))
        video_request = VID_WPA;
    else if (same_text(opt, "CPU=C"))
        cpu_c = 1;
    else if (same_text(name, "FMDIV") && val)
        fmdiv = atoi(val);
    else if (same_text(opt, "RENDER=LINE"))
        render_lines = 1;                                /* reference renderer */
    else if (same_text(opt, "VERIFY"))
        verify = 1;                                      /* testing */
    else if (same_text(name, "VERIFY") && val) {        /* VERIFY=n: every n-th drawn picture */
        verify = 1;
        verify_every = atoi(val) > 0 ? atoi(val) : 1;
    }
    else if (same_text(opt, "REDRAW"))
        redraw = 1;
    else if (same_text(opt, "MOVE16=OFF"))
        use_move16 = 0;
    else if (same_text(opt, "FRAMESKIP=AUTO"))
        frameskip = -1;
    else if (same_text(name, "FRAMESKIP") && val)
        frameskip = atoi(val);
    else if (same_text(opt, "JOYPORT=BOTH"))
        joy_mouseport = 1;
    else if (same_text(opt, "SCREEN=FULL"))
        screen_full = 1;
    else if (same_text(opt, "SCREEN=WINDOW"))
        screen_full = 0;
    else if (same_text(name, "DSW0") && val)
        dsw0 = (int)strtol(val, 0, 16);
    else if (same_text(name, "DSW1") && val)
        dsw1 = (int)strtol(val, 0, 16);
    else if (same_text(name, "SLICE") && val)
        slice = atoi(val);
    else if (same_text(opt, "LAGLOG"))
        laglog = 1;
    else if (same_text(name, "VSKIP") && val)
        video_skip = atoi(val);                          /* measurements: layers left out (1 text, 2 BG1, 4 BG2, 8 sprites) */
    else if (same_text(opt, "RCHECK"))
        rcheck = 1;
    else if (same_text(opt, "INCR=OFF"))
        incr_off = 1;
    else if (same_text(opt, "SHIFT=OFF"))
        shift_off = 1;
    else if (same_text(opt, "BGCACHE=OFF"))
        video_cache = 0;
    else if (same_text(opt, "SHIFTPROF"))
        shiftprof = 1;
    else if (same_text(opt, "SHIFTPROF=SRC"))
        shiftprof = 2;
    else if (same_text(name, "SPANGAP") && val)
        span_gap = atoi(val);
    else if (same_text(opt, "BANDS=OFF"))
        use_bands = 0;
    else if (same_text(opt, "BENCHM16"))
        present_bench_m16 = 1;
    else if (same_text(name, "PBENCH") && val)
        present_bench = atoi(val);
    else if (same_text(name, "REPLAY") && val)
        snprintf(replay_path, sizeof replay_path, "%s", val);
    else if (same_text(name, "LOADSTATE") && val)
        snprintf(state_path, sizeof state_path, "%s", val);
    else if (same_text(name, "ROMPATH") && val && nrompaths < 4) {
        snprintf(rp[nrompaths], sizeof rp[0], "%s", val);
        rompaths[nrompaths] = rp[nrompaths];
        nrompaths++;
    }
}

static void parse_tool_types(struct WBStartup *wb)
{
    struct Library *IconBase = OpenLibrary((STRPTR)"icon.library", 36);
    struct DiskObject *dob;
    BPTR old;

    if (!IconBase)
        return;
    old = CurrentDir(wb->sm_ArgList[0].wa_Lock);
    dob = GetDiskObject(wb->sm_ArgList[0].wa_Name);
    if (dob) {
        STRPTR *tt;
        for (tt = dob->do_ToolTypes; tt && *tt; tt++)
            if (**tt != '(')
                parse_option((const char *)*tt);
        FreeDiskObject(dob);
    }
    CurrentDir(old);
    CloseLibrary(IconBase);
}

/* ------------------------------------------------------------------ ROMs */

static void load_roms(void)
{
    static char paths[32][256];
    static const char *const dirs[] = { "PROGDIR:", "PROGDIR:rom", "PROGDIR:roms", "PROGDIR:ROM" };
    const char *src[32], *all[8];
    int n = 0, d, ndirs = 0;

    for (d = 0; d < nrompaths; d++)
        all[ndirs++] = rompaths[d];
    for (d = 0; d < 4; d++)
        all[ndirs++] = dirs[d];
    for (d = 0; d < ndirs && n + 3 <= 32; d++) {
        path_join(paths[n], 256, all[d], "xsleena.zip");
        src[n] = paths[n];
        n++;
        path_join(paths[n], 256, all[d], "xsleena");
        src[n] = paths[n];
        n++;
        snprintf(paths[n], 256, "%s", all[d]);
        src[n] = paths[n];
        n++;
    }
    if (xain_load_roms(&machine, src, n))
        fail("Cannot load the ROMs of Xain'd Sleena (MAME set xsleena).\n\n"
             "Put xsleena.zip or the ROM files into\nthe program drawer, its rom drawer\nor give ROMPATH=dir.");
}

/* ------------------------------------------------------------------ samples */

/* SNDLOG (testing): every sound command with the frame and what the player did, written to
   RAM:XainSleena-snd.txt at the end (repeats of the same command in a row are counted) */
static char *sndlog_buf;
static ULONG sndlog_len, sndlog_rep;
static uint8_t sndlog_last = 0xff;
#define SNDLOG_SIZE 200000

static void sndlog_flush_rep(void)
{
    if (sndlog_rep && sndlog_len < SNDLOG_SIZE - 64)
        sndlog_len += sprintf(sndlog_buf + sndlog_len, "        (x%lu more)\n", sndlog_rep);
    sndlog_rep = 0;
}

static void snd_cmd(void *ctx, uint8_t cmd)
{
    SndPlay *p = (SndPlay *)ctx;
    uint32_t age0 = p->voice[0].age;
    int i;
    sndplay_command(p, cmd);
    if (!sndlog || !sndlog_buf || sndlog_len > SNDLOG_SIZE - 128)
        return;
    if (cmd == sndlog_last && p->voice[0].age == age0) {
        sndlog_rep++;
        return;
    }
    sndlog_flush_rep();
    sndlog_last = cmd;
    sndlog_len += sprintf(sndlog_buf + sndlog_len, "%6lu %02X%s  voices:", (ULONG)machine.frame, cmd,
                          p->voice[0].age != age0 ? " MUSIC START" : "");
    for (i = 0; i <= SFX_VOICES; i++)
        if (p->voice[i].active)
            sndlog_len += sprintf(sndlog_buf + sndlog_len, " %d:%02X@%lu", i, p->voice[i].cmd, (ULONG)p->voice[i].pos);
    sndlog_buf[sndlog_len++] = '\n';
}

static void sndlog_write(void)
{
    BPTR fh;
    if (!sndlog_buf)
        return;
    sndlog_flush_rep();
    fh = Open((STRPTR)"RAM:XainSleena-snd.txt", MODE_NEWFILE);
    if (fh) {
        Write(fh, sndlog_buf, (LONG)sndlog_len);
        Close(fh);
    }
    FreeVec(sndlog_buf);
    sndlog_buf = NULL;
}

/* the whole sample file into (fast) memory */
static int try_samples(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG size;
    if (!fh)
        return 0;
    Seek(fh, 0, OFFSET_END);
    size = Seek(fh, 0, OFFSET_BEGINNING);
    if (size > 12) {
        snd_pack = AllocVec((ULONG)size, MEMF_ANY);
        if (snd_pack && Read(fh, snd_pack, size) == size && sndplay_init(&snd, snd_pack, (ULONG)size) == 0) {
            snd_pack_size = (ULONG)size;
            Close(fh);
            return 1;
        }
        if (snd_pack)
            FreeVec(snd_pack);
        snd_pack = NULL;
    }
    Close(fh);
    return 0;
}

static int load_samples(void)
{
    static const char *const dirs[] = { "PROGDIR:", "PROGDIR:rom", "PROGDIR:roms", "PROGDIR:ROM" };
    char path[256];
    int d;
    if (samples_path)
        return try_samples(samples_path);
    for (d = 0; d < nrompaths; d++) {
        path_join(path, sizeof path, rompaths[d], "XainSleena.snd");
        if (try_samples(path))
            return 1;
    }
    for (d = 0; d < 4; d++) {
        path_join(path, sizeof path, dirs[d], "XainSleena.snd");
        if (try_samples(path))
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ main */

static uint64_t stat_start, stat_emu, stat_render, stat_present, stat_busy, stat_snd;
static int stat_frames;
static char stat_title[260];

/* STATS=FULL: time of the parts of the emulation (xain_prof), measured in every fourth frame
   (prof_frames of stat_frames) without the cost of the time reading itself (prof_cost, measured
   at the start), so that the 1360 readings per frame distort the result less */
static uint64_t prof_part[5], prof_t, prof_cost;
static int prof_frames;

static void prof_hook(int part)
{
    uint64_t t = eclock_now();
    if (part >= 0) {
        uint64_t d = t - prof_t;
        prof_part[part] += d > prof_cost ? d - prof_cost : 0;
    }
    prof_t = t;
}

static void prof_calibrate(void)
{
    uint64_t t0 = eclock_now(), t1;
    int i;
    for (i = 0; i < 256; i++)
        prof_hook(-1);
    t1 = eclock_now();
    prof_cost = (t1 - t0) / 257;
}

static void update_stats(uint64_t t_end)
{
    uint64_t elapsed;
    if (!stat_start)
        stat_start = t_end;
    if (++stat_frames < 115)
        return;
    elapsed = t_end - stat_start;
    if (!elapsed)
        elapsed = 1;
    {
        ULONG fps10 = (ULONG)((uint64_t)stat_frames * eclock_freq * 10 / elapsed);
        ULONG busy = (ULONG)(stat_busy * 100 / elapsed);
        if (stats > 1)
        {
            /* the present stages in microseconds per drawn frame: compare, convert, write, blit */
            ULONG dr = stat_drawn ? (ULONG)stat_drawn : 1;
#define US(t) ((ULONG)((t) * 1000000 / eclock_freq / dr))
            sprintf(stat_title, "Xain %lu.%lu fps (drawn %lu) CPU %lu%%: emu %lu render %lu present(%s%s) %lu [us c%lu v%lu w%lu b%lu] blocks %lu %s%s",
                    fps10 / 10, fps10 % 10, (ULONG)((uint64_t)stat_drawn * eclock_freq / elapsed), busy,
                    (ULONG)(stat_emu * 100 / elapsed), (ULONG)(stat_render * 100 / elapsed), video_names[video_mode],
                    use_move16 ? ",M16" : "", (ULONG)(stat_present * 100 / elapsed), US(stat_stage[3]),
                    US(stat_stage[6]), US(stat_stage[4]), US(stat_stage[5]),
                    prof_blocks / (ULONG)stat_frames, sound_names[sound_mode], use_samples ? " smp" : "");
#undef US
        }
        if (stats > 1)
            sprintf(stat_title + strlen(stat_title), " snd %lu.%02lu%% [A%lu B%lu S%lu YM%lu MCU%lu]",
                    (ULONG)(stat_snd * 10000 / elapsed) / 100, (ULONG)(stat_snd * 10000 / elapsed) % 100,
                    (ULONG)(prof_part[0] * 100 * stat_frames / prof_frames / elapsed),
                    (ULONG)(prof_part[1] * 100 * stat_frames / prof_frames / elapsed),
                    (ULONG)(prof_part[2] * 100 * stat_frames / prof_frames / elapsed),
                    (ULONG)(prof_part[3] * 100 * stat_frames / prof_frames / elapsed),
                    (ULONG)(prof_part[4] * 100 * stat_frames / prof_frames / elapsed));
        else
            sprintf(stat_title, "Xain %lu.%lu fps (drawn %lu)  CPU %lu%%  %s%s", fps10 / 10, fps10 % 10,
                    (ULONG)((uint64_t)stat_drawn * eclock_freq / elapsed), busy, sound_names[sound_mode], use_samples ? " smp" : "");
    }
    if (verify)
        sprintf(stat_title + strlen(stat_title), "  verify: %lu bad blocks in %lu checks (last %d,%d)",
                verify_bad, verify_checks, verify_bx, verify_by);
    SetWindowTitles(win, (UBYTE *)stat_title, (UBYTE *)~0);
    stat_frames = 0;
    stat_emu = stat_render = stat_present = stat_busy = stat_snd = 0;
    stat_drawn = 0;
    memset(stat_stage, 0, sizeof stat_stage);
    memset(prof_part, 0, sizeof prof_part);
    prof_frames = 0;
    prof_blocks = 0;
    stat_start = t_end;
}

int main(int argc, char **argv)
{
    int i;

    if (argc == 0)
        parse_tool_types((struct WBStartup *)argv);
    for (i = 1; i < argc; i++)
        parse_option(argv[i]);
    if (scale < 1 || scale > 3)
        scale = 2;

    atexit(cleanup);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 40);
    if (!IntuitionBase || !GfxBase)
        fail("Needs AmigaOS 3.1 or newer.");
    LowLevelBase = OpenLibrary((STRPTR)"lowlevel.library", 40);   /* optional */

    load_roms();
    xain_init(&machine);
    if (dsw0 >= 0) machine.dsw0 = (uint8_t)dsw0;
    if (dsw1 >= 0) machine.dsw1 = (uint8_t)dsw1;
    if (slice > 0 && XAIN_LINE_CYCLES % slice == 0) machine.slice = slice;
    if (memo_off) machine.slice_memo = 0;
    if (hle_on) machine.hle_sprites = 1;
    if (subfast_off) machine.sub_fast = 0;
    if (lives >= 0)                                     /* DSW1 bits 7-6: 3 C0, 4 80, 6 40, infinite 00 */
        machine.dsw1 = (uint8_t)((machine.dsw1 & 0x3f) | (lives == 0 ? 0x00 : lives == 4 ? 0x80 : lives == 6 ? 0x40 : 0xc0));
    machine.easy_damage = easy_damage;
    if (!start_arcade) {
        machine.autocoin = machine.joystart = 1;
        xain_patch_texts(&machine);
    }
    if (state_path[0])
        load_state();
    if (replay_path[0])
        load_replay();
    if (!video_init(&video, &machine))
        fail("Not enough memory.");
    if (!m6809_layout_ok())
        fail("Internal error: M6809 struct layout.");
    if (!ym2203_layout_ok())
        fail("Internal error: YM2203 struct layout.");
    if (!cpu_c)
        machine.cpu_run = m6809_run_asm;                 /* CPU=C: the C core (slower, reference) */

    open_window();
    open_rtg();
    open_timer();
    if (sound && !emusound)
        use_samples = load_samples();
    if (sound)
        open_sound();
    if (sound_mode == SND_NONE)
        use_samples = 0;
    /* no sound or samples: no sound CPU / YM2203 at all; samples: the commands start samples */
    xain_set_sound(&machine, sound_mode != SND_NONE && !use_samples);
    machine.sound_cpu = sound_mode != SND_NONE && !use_samples;
    if (use_samples) {
        if (sndlog)
            sndlog_buf = AllocVec(SNDLOG_SIZE, MEMF_ANY);
        machine.sound_cmd = snd_cmd;
        machine.sound_ctx = &snd;
    }
    xain_set_fm_div(&machine, fmdiv);
    if (present_bench > 0)
        run_present_bench();
    if (laglog)
        lag_init();
    rec_init();
    start_watchdog();
    if (stats > 1)
        prof_calibrate();

    for (;;) {
        static int late, skipped;
        uint64_t t0 = 0, t1, t2, lt0 = 0, lt1 = 0, lt2 = 0;
        int draw, lpal = 0;
        ULONG lblocks = prof_blocks;
        if (stats)
            t0 = eclock_now();
        if (laglog)
            lt0 = eclock_now();
        if (replay_in) {
            if (replay_pos >= replay_count) {
                if (verify)
                    printf("REPLAY end: verify %lu bad blocks in %lu checks\n", verify_bad, verify_checks);
                if (rcheck)
                    printf("REPLAY end: RCHECK %lu of %lu bands differ from the C renderer\n", rcheck_bad, rcheck_bands);
                printf("REPLAY end: %lu frames, %lu drawn; per drawn picture %lu.%lu bands rendered, %lu.%lu converted, %lu blocks sent\n",
                       replay_count, stat_drawn, stat_drawn ? bands_rendered * 10 / stat_drawn / 10 : 0,
                       stat_drawn ? bands_rendered * 10 / stat_drawn % 10 : 0,
                       stat_drawn ? bands_converted * 10 / stat_drawn / 10 : 0,
                       stat_drawn ? bands_converted * 10 / stat_drawn % 10 : 0,
                       stat_drawn ? prof_blocks / stat_drawn : 0);
                printf("REPLAY end: SHIFT %lu pictures moved by the blitter (%lu blocks composed per picture), "
                       "%lu in place (%lu blocks); bands: %lu off/first/pri, %lu steps, %lu area, %lu lock\n",
                       shift_pictures, shift_pictures ? shift_blocks / shift_pictures : 0, still_pictures,
                       still_pictures ? still_blocks / still_pictures : 0, shift_no[0], shift_no[2], shift_no[3],
                       shift_no[4]);
                if (shiftprof && shift_pictures + still_pictures) {
                    static const char *const nm[7] = { "palette", "which blocks", "blits", "render+convert",
                                                       "write", "unlock", "window" };
                    int k;
                    printf("SHIFTPROF us per block picture:");
                    for (k = 0; k < 7; k++)
                        printf(" %s %lu", nm[k], (ULONG)(sp_acc[k] * 1000000 / eclock_freq /
                                                         (shift_pictures + still_pictures)));
                    printf("\n");
                    if (shiftprof == 1) {
                        static const char *const wn[7] = { "palette+tables", "text masks+frame+edge", "sprites",
                                                           "text", "tiles+colours", "other layer", "masks" };
                        printf("SHIFTPROF finding the blocks, us:");
                        for (k = 0; k < 7; k++)
                            printf(" %s %lu", wn[k], (ULONG)(sp_w[k] * 1000000 / eclock_freq /
                                                             (shift_pictures + still_pictures)));
                        printf("\n");
                    }
                    printf("SHIFTPROF blocks per block picture (before the frame mask): edge %lu sprites %lu text %lu "
                           "tiles %lu colours %lu other layer %lu (copied by the blitter %lu)\n",
                           sp_src[0] / (shift_pictures + still_pictures), sp_src[1] / (shift_pictures + still_pictures),
                           sp_src[2] / (shift_pictures + still_pictures), sp_src[3] / (shift_pictures + still_pictures),
                           sp_src[4] / (shift_pictures + still_pictures), sp_src[5] / (shift_pictures + still_pictures),
                           sp_src[6] / (shift_pictures + still_pictures));
                }
                exit(0);
            }
            machine.in_p1 = replay_in[replay_pos * 2];      /* the recorded inputs (assist included) */
            machine.in_p2 = replay_in[replay_pos * 2 + 1];
            if (!paused) replay_pos++;
        } else {
            read_inputs();
            xain_assist(&machine);
        }
        /* frame skipping: the game always runs at full speed, the picture is left out when
           the last frame finished too late (at most MAX_AUTO_SKIP in a row) */
        if (frameskip < 0)
            draw = !late || skipped >= MAX_AUTO_SKIP;
        else
            draw = skipped >= frameskip;
        if (!paused) {
            /* a frame that is not drawn is not rendered either. The fast renderer draws the
               whole picture at the end of the frame; RENDER=LINE: the reference renderer
               line by line during the frame (exact for changes in the middle of a frame) */
            machine.render_line = draw && render_lines ? video_render_line : 0;
            machine.render_ctx = &video;
            if (stats > 1) {
                xain_prof = (stat_frames & 3) == 0 ? prof_hook : 0;
                if (xain_prof) prof_frames++;
            }
            rec_before_frame();
            xain_run_frame(&machine);
            rec_frame++;
            if (use_samples)
            {
                uint64_t ts = stats > 1 ? eclock_now() : 0;
                samples_fill();
                if (stats > 1)
                    stat_snd += eclock_now() - ts;   /* mixing + Paula, inside "emu" */
            }
            else if (sound_mode != SND_NONE)
                audio_push(audio, xain_take_audio(&machine, audio, (int)(sizeof audio / sizeof audio[0]),
                                                  (int)audio_rate));
            if (stats) {
                t1 = eclock_now();
                stat_emu += t1 - t0;
            }
            if (laglog) {
                lt1 = eclock_now();
                lpal = machine.palette_dirty;
            }
            if (!draw) {
                skipped++;
                if (stats)
                    stat_busy += eclock_now() - t0;
                goto frame_done;
            }
            skipped = 0;
            stat_drawn++;
            if (bands_possible()) {
                render_present_bands();                  /* render + present together */
            } else {
                if (!render_lines)
                    video_render_frame(&video, &machine);
                if (stats) {
                    uint64_t tr = eclock_now();
                    stat_render += tr - t1;
                    t1 = tr;
                }
                present();
            }
            if (stats) {
                t2 = eclock_now();
                stat_present += t2 - t1;
                stat_busy += t2 - t0;
            }
        }
    frame_done:
        if (laglog && !paused) {
            lt2 = eclock_now();
            if (!lt1) lt1 = lt2;
            lag_frame((ULONG)((lt1 - lt0) * 1000000 / eclock_freq), (ULONG)((lt2 - lt1) * 1000000 / eclock_freq),
                      draw, lpal, late, prof_blocks - lblocks);
        }
        diag_frames++;
        if (show_fps && !paused && ++fps_frames >= 57) {   /* FPS: the pictures of the last second */
            fps_value = fps_drawn + (draw ? 1 : 0);
            fps_frames = fps_drawn = 0;
        } else if (show_fps && !paused && draw) {
            fps_drawn++;
        }
        handle_messages();
        late = !pace();
        if (stats)
            update_stats(eclock_now());
    }
    return 0;
}

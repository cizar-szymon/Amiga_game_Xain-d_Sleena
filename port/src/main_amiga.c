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
 *                    [DSW0=hh] [DSW1=hh] [SLICE=n] [RENDER=LINE] [FMDIV=1|2] [CPU=C] [STATS|STATS=FULL] [VERIFY]
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

const char version[] = "$VER: XainSleena " XAIN_VERSION " (12.10.2026) arcade emulation";

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
static int easy_damage;               /* DIFFICULTY=EASY (1) / VERYEASY (2) */
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

static void cleanup(void)
{
    stop_watchdog();
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
                         WA_NoCareRefresh, TRUE,
                         WA_IDCMP, IDCMP_RAWKEY | IDCMP_INACTIVEWINDOW | IDCMP_ACTIVEWINDOW,
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
static struct BitMap *vbm;
static UBYTE *wfb;                    /* WPA frame */
static UBYTE *sfb_mem, *sfb;          /* BLIT: one row of blocks in the screen format, 16 byte aligned */
static ULONG sfb_bpr;
static ULONG wfb_bpr, scr_fmt;
static int vbpp;
static ULONG native[512];             /* palette index -> pixel in the screen format */
static ULONG native2[512];            /* 16 bit screens: the pixel twice (SCALE=2) */
static int rtg_ok;

/* amiga_68k.s: rows from fast RAM to VRAM, writes only (MOVE16 or long moves) */
void copy_rows(const UBYTE *src, UBYTE *dst, ULONG bytes, ULONG rows, ULONG src_mod, ULONG dst_mod,
               ULONG move16);
/* the changed 8x8 blocks of two pictures; 8 rows of indices into 16 bit pixels (SCALE 1/2) */
ULONG find_dirty_68k(const uint16_t *a, const uint16_t *b, UBYTE *dirty);
void convert_rows_68k(const uint16_t *src, UBYTE *dst, ULONG dst_bpr, ULONG pixels, const ULONG *lut, ULONG scale);

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
    int i;
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
   every block with what it should show; the result goes to the title */
static int verify;
static ULONG verify_bad, verify_checks, verify_frames;
static int verify_bx = -1, verify_by;

static void verify_window(void)
{
    static UBYTE *buf;
    const int w = RENDER_W * scale, h = RENDER_H * scale;
    struct RenderInfo ri;
    int bx, by;

    if (!buf)
        buf = AllocVec((ULONG)(w * h * vbpp), MEMF_ANY);
    if (!buf || !rtg_ok)
        return;
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
                    ULONG want = native[shown[y * RENDER_W + x]], got;
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
            if (bad) {
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
    if (redraw)
        force_full = 1;
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
    if (verify && ++verify_frames % 30 == 0)
        verify_window();
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

/* F10: the machine state to RAM:XainSleena-state.bin (load on the PC: xainsleena -loadstate) and
   a line to RAM:XainSleena-state.txt; for hangs of the game */
static void save_state(void)
{
    uint32_t len = xain_save_state(&machine, NULL);
    UBYTE *buf = AllocVec(len, MEMF_ANY);
    BPTR fh;
    char text[256];
    if (!buf)
        return;
    xain_save_state(&machine, buf);
    fh = Open((STRPTR)"RAM:XainSleena-state.bin", MODE_NEWFILE);
    if (fh) {
        Write(fh, buf, (LONG)len);
        Close(fh);
    }
    FreeVec(buf);
    sprintf(text, "frame %lu paused %d main pc %04x sub pc %04x mcu pc %03x memo %d subfast %d hle %d"
            " mcu idle %d control %d start_arcade %d\n",
            (unsigned long)machine.frame, paused, machine.main.pc, machine.sub.pc, machine.mcu.cpu.pc,
            machine.slice_memo, machine.sub_fast, machine.hle_sprites, machine.mcu.idle, control, start_arcade);
    fh = Open((STRPTR)"RAM:XainSleena-state.txt", MODE_NEWFILE);
    if (fh) {
        Write(fh, text, (LONG)strlen(text));
        Close(fh);
    }
    DisplayBeep(NULL);
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
        } else if (cls == IDCMP_INACTIVEWINDOW)
            memset(keys, 0, sizeof keys);
        else if (cls == IDCMP_ACTIVEWINDOW)
            force_full = 1;                              /* repaint everything once */
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
    uint8_t p1 = 0, p2 = 0;

    if (keys[RAW_LEFT]) p1 |= IN_LEFT;
    if (keys[RAW_RIGHT]) p1 |= IN_RIGHT;
    if (keys[RAW_UP]) {
        p1 |= IN_UP;
        if (xain_on_foot(&machine)) p1 |= IN_BUTTON2;   /* as the joystick (JOY1): up jumps on foot */
    }
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
            if (type != JP_TYPE_JOYSTK && type != JP_TYPE_GAMECTLR)
                continue;
            if (j & JPF_JOY_UP) {
                p1 |= IN_UP;
                /* JOY1: on foot up also jumps (up has no function of its own there: up + jump =
                   jump); in the flying stages up only moves, Space fires the extra weapon */
                if (control == CTL_JOY1 && xain_on_foot(&machine))
                    p1 |= IN_BUTTON2;
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
    machine.in_p1 = p1;
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
    start_watchdog();
    if (stats > 1)
        prof_calibrate();

    for (;;) {
        static int late, skipped;
        uint64_t t0 = 0, t1, t2;
        int draw;
        if (stats)
            t0 = eclock_now();
        read_inputs();
        xain_assist(&machine);
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
            xain_run_frame(&machine);
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
            if (!draw) {
                skipped++;
                if (stats)
                    stat_busy += eclock_now() - t0;
                goto frame_done;
            }
            skipped = 0;
            stat_drawn++;
            if (!render_lines)
                video_render_frame(&video, &machine);
            if (stats) {
                uint64_t tr = eclock_now();
                stat_render += tr - t1;
                t1 = tr;
            }
            present();
            if (stats) {
                t2 = eclock_now();
                stat_present += t2 - t1;
                stat_busy += t2 - t0;
            }
        }
    frame_done:
        diag_frames++;
        handle_messages();
        late = !pace();
        if (stats)
            update_stats(eclock_now());
    }
    return 0;
}

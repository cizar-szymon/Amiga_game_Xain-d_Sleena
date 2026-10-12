/* Sound from samples: see sndplay.h and docs/PORTING.md (chapter 13). */
#include <string.h>
#include "sndplay.h"
#include "xain.h"                       /* XAIN_FPS_X1000 */

#define MIX_MAX 1024

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int sndplay_init(SndPlay *p, const uint8_t *pack, uint32_t size)
{
    uint32_t n, i, table_end;
    memset(p, 0, sizeof *p);
    if (size < 12 || memcmp(pack, "XSND", 4) || be16(pack + 4) != 1)
        return -1;
    p->rate = be16(pack + 6);
    n = be16(pack + 8);
    table_end = 12 + n * 20;
    if (table_end > size || !p->rate)
        return -1;
    for (i = 0; i < n; i++) {
        const uint8_t *t = pack + 12 + i * 20;
        SndEntry *e = &p->entry[t[0]];
        uint32_t off = be32(t + 8), len = be32(t + 12), loop = be32(t + 16);
        if (table_end + off + len > size || !len || loop >= len)
            continue;
        e->data = (const int8_t *)(pack + table_end + off);
        e->len = len;
        e->loop = loop;
        e->loops = (uint8_t)(t[1] & 1);
        e->mask = (uint16_t)be16(t + 2);
        e->vol = t[4] > 64 ? 64 : t[4];
        e->present = 1;
        p->ncmds++;
    }
    return 0;
}

void sndplay_reset(SndPlay *p)
{
    int i;
    for (i = 0; i <= SFX_VOICES; i++)
        p->voice[i].active = 0;
}

static void start(SndPlay *p, SndVoice *v, uint8_t cmd)
{
    v->e = &p->entry[cmd];
    v->pos = 0;
    v->frac = 0;
    v->cmd = cmd;
    v->age = ++p->clock;
    v->active = 1;
}

void sndplay_command(SndPlay *p, uint8_t cmd)
{
    int i;
    if (cmd == 0)
        return;
    if (cmd >= 0xf0) {
        if (cmd == 0xff || cmd == 0xfd)                         /* stop the music */
            p->voice[0].active = 0;
        if (cmd == 0xfe || cmd == 0xfd)                         /* stop the effects */
            for (i = 1; i <= SFX_VOICES; i++)
                p->voice[i].active = 0;
        if (cmd < 0xfd)                                         /* stop one YM channel */
            for (i = 0; i <= SFX_VOICES; i++)
                if (p->voice[i].active && (p->voice[i].e->mask >> (cmd & 0x0f) & 1))
                    p->voice[i].active = 0;
        return;
    }
    if (cmd < 0x3c) {                                           /* music */
        SndVoice *m = &p->voice[0];
        if (m->active && m->cmd == cmd)
            return;                                             /* already playing: ignored */
        if (p->entry[cmd].present)
            start(p, m, cmd);
        else
            m->active = 0;
        return;
    }
    if (cmd >= 0x80 && cmd < 0xbd && p->entry[cmd].present) {   /* effect */
        const SndEntry *e = &p->entry[cmd];
        SndVoice *free_v = 0, *oldest = 0;
        for (i = 1; i <= SFX_VOICES; i++) {
            SndVoice *v = &p->voice[i];
            if (v->active && (v->e->mask & e->mask || v->cmd == cmd))
                v->active = 0;                                  /* same YM channels: replaced */
            if (!v->active) {
                if (!free_v) free_v = v;
            } else if (!oldest || v->age < oldest->age)
                oldest = v;
        }
        start(p, free_v ? free_v : oldest, cmd);
    }
}

void sndplay_mix(SndPlay *p, int16_t *dst, int n, uint32_t out_rate)
{
    static int32_t acc[MIX_MAX];
    /* step in 16.16; rate < 65536, so rate << 16 fits in 32 bits */
    uint32_t step = out_rate ? (p->rate << 16) / out_rate : 0x10000;
    int done = 0;
    while (done < n) {
        int k = n - done > MIX_MAX ? MIX_MAX : n - done, i, vi;
        memset(acc, 0, (size_t)k * sizeof acc[0]);
        for (vi = 0; vi <= SFX_VOICES; vi++) {
            SndVoice *v = &p->voice[vi];
            const SndEntry *e;
            const int8_t *d;
            int32_t vol;
            uint32_t pos, len;
            if (!v->active)
                continue;
            e = v->e;
            d = e->data;
            vol = e->vol;
            pos = v->pos;
            len = e->len;
            if (step == 0x10000) {                              /* Paula at the file's rate */
                for (i = 0; i < k; i++) {
                    if (pos >= len) {
                        if (!e->loops) { v->active = 0; break; }
                        pos = e->loop;
                    }
                    acc[i] += d[pos++] * vol;
                }
            } else {
                uint32_t frac = v->frac;
                for (i = 0; i < k; i++) {
                    /* linear interpolation to the next sample (at the end: the loop start) */
                    uint32_t nx = pos + 1 < len ? pos + 1 : e->loops ? e->loop : pos;
                    int32_t s = d[pos] + (((int32_t)(d[nx] - d[pos]) * (int32_t)(frac >> 8)) >> 8);
                    acc[i] += s * vol;
                    frac += step;
                    pos += frac >> 16;
                    frac &= 0xffff;
                    while (pos >= len) {
                        if (!e->loops) { v->active = 0; break; }
                        pos -= len - e->loop;
                    }
                    if (!v->active) break;
                }
                v->frac = frac;
            }
            v->pos = pos;
        }
        for (i = 0; i < k; i++) {
            int32_t s = acc[i] * 2;                             /* 127 * 64 * 2: the original level */
            dst[done + i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        done += k;
    }
}

int sndplay_frame(SndPlay *p, int16_t *dst, int max, uint32_t out_rate)
{
    /* out_rate * 1000 / 57445 samples per frame, the remainder carried over */
    uint32_t t = p->frame_acc + out_rate * 1000u;
    int n = (int)(t / XAIN_FPS_X1000);
    p->frame_acc = t % XAIN_FPS_X1000;
    if (n > max) n = max;
    sndplay_mix(p, dst, n, out_rate);
    return n;
}

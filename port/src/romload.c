/* ROM loading, see romload.h. Contains a small inflate decoder
   (stored, fixed and dynamic Huffman blocks) for zip entries. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "romload.h"

/* ---- CRC-32 ---- */

static uint32_t crc_table[256];

uint32_t crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xffffffffUL, i;
    if (!crc_table[1]) {
        uint32_t n, c;
        int k;
        for (n = 0; n < 256; n++) {
            c = n;
            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
    }
    for (i = 0; i < len; i++)
        crc = crc_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffUL;
}

/* ---- inflate ---- */

typedef struct {
    const uint8_t *in;
    uint32_t inlen, inpos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    uint32_t outlen, outpos;
    int err;
} Inflate;

typedef struct {
    short count[16];
    short symbol[288];
} Huffman;

static int getbits(Inflate *s, int n)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < n) {
        if (s->inpos >= s->inlen) {
            s->err = 1;
            return 0;
        }
        v |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> n;
    s->bitcnt -= n;
    return (int)(v & ((1UL << n) - 1));
}

static void build_huffman(Huffman *h, const uint8_t *lengths, int n)
{
    short offs[16];
    int i;
    for (i = 0; i < 16; i++)
        h->count[i] = 0;
    for (i = 0; i < n; i++)
        h->count[lengths[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (i = 1; i < 15; i++)
        offs[i + 1] = (short)(offs[i] + h->count[i]);
    for (i = 0; i < n; i++)
        if (lengths[i])
            h->symbol[offs[lengths[i]]++] = (short)i;
}

static int decode(Inflate *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0, len, count;
    for (len = 1; len < 16; len++) {
        code |= getbits(s, 1);
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = 1;
    return -1;
}

static const short len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void inflate_codes(Inflate *s, const Huffman *lit, const Huffman *dist)
{
    int sym, len, d;
    for (;;) {
        sym = decode(s, lit);
        if (s->err)
            return;
        if (sym < 256) {
            if (s->outpos >= s->outlen) { s->err = 1; return; }
            s->out[s->outpos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) { s->err = 1; return; }
            len = len_base[sym] + getbits(s, len_extra[sym]);
            sym = decode(s, dist);
            if (sym < 0 || sym >= 30) { s->err = 1; return; }
            d = dist_base[sym] + getbits(s, dist_extra[sym]);
            if (s->err || (uint32_t)d > s->outpos || s->outpos + (uint32_t)len > s->outlen) {
                s->err = 1;
                return;
            }
            while (len--) {
                s->out[s->outpos] = s->out[s->outpos - d];
                s->outpos++;
            }
        }
    }
}

static int inflate_data(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t outlen)
{
    static Huffman lit, dist;      /* static: keeps the Amiga stack small */
    static uint8_t lengths[320];
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    Inflate s;
    int last, type, i;

    memset(&s, 0, sizeof s);
    s.in = in;
    s.inlen = inlen;
    s.out = out;
    s.outlen = outlen;
    do {
        last = getbits(&s, 1);
        type = getbits(&s, 2);
        if (type == 0) {
            uint32_t len;
            s.bitbuf = 0;
            s.bitcnt = 0;
            if (s.inpos + 4 > s.inlen)
                return -1;
            len = (uint32_t)(s.in[s.inpos] | (s.in[s.inpos + 1] << 8));
            s.inpos += 4;
            if (s.inpos + len > s.inlen || s.outpos + len > s.outlen)
                return -1;
            memcpy(s.out + s.outpos, s.in + s.inpos, len);
            s.inpos += len;
            s.outpos += len;
        } else if (type == 1) {
            for (i = 0; i < 144; i++) lengths[i] = 8;
            for (; i < 256; i++) lengths[i] = 9;
            for (; i < 280; i++) lengths[i] = 7;
            for (; i < 288; i++) lengths[i] = 8;
            build_huffman(&lit, lengths, 288);
            for (i = 0; i < 30; i++) lengths[i] = 5;
            build_huffman(&dist, lengths, 30);
            inflate_codes(&s, &lit, &dist);
        } else if (type == 2) {
            int nlen = getbits(&s, 5) + 257, ndist = getbits(&s, 5) + 1, ncode = getbits(&s, 4) + 4;
            if (nlen > 286 || ndist > 30)
                return -1;
            for (i = 0; i < 19; i++)
                lengths[order[i]] = (uint8_t)(i < ncode ? getbits(&s, 3) : 0);
            build_huffman(&lit, lengths, 19);
            i = 0;
            while (i < nlen + ndist && !s.err) {
                int sym = decode(&s, &lit), rep = 0;
                uint8_t val = 0;
                if (sym < 0)
                    return -1;
                if (sym < 16) {
                    lengths[i++] = (uint8_t)sym;
                    continue;
                }
                if (sym == 16) {
                    if (i == 0)
                        return -1;
                    val = lengths[i - 1];
                    rep = 3 + getbits(&s, 2);
                } else if (sym == 17) {
                    rep = 3 + getbits(&s, 3);
                } else {
                    rep = 11 + getbits(&s, 7);
                }
                if (i + rep > nlen + ndist)
                    return -1;
                while (rep--)
                    lengths[i++] = val;
            }
            build_huffman(&lit, lengths, nlen);
            build_huffman(&dist, lengths + nlen, ndist);
            inflate_codes(&s, &lit, &dist);
        } else {
            return -1;
        }
        if (s.err)
            return -1;
    } while (!last);
    return (int)s.outpos;
}

/* ---- files ---- */

static uint8_t *read_file(const char *path, uint32_t *len)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *buf;
    long n;
    if (!fp)
        return 0;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0) {
        fclose(fp);
        return 0;
    }
    buf = (uint8_t *)malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        buf = 0;
    }
    fclose(fp);
    *len = (uint32_t)n;
    return buf;
}

void path_join(char *out, int outlen, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    if (n == 0)
        snprintf(out, (size_t)outlen, "%s", name);
    else if (dir[n - 1] == '/' || dir[n - 1] == ':' || dir[n - 1] == '\\')
        snprintf(out, (size_t)outlen, "%s%s", dir, name);
    else
        snprintf(out, (size_t)outlen, "%s/%s", dir, name);
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

static int name_eq(const uint8_t *a, uint32_t alen, const char *b)
{
    uint32_t i;
    const uint8_t *base = a;
    /* compare only the file name part of the zip entry */
    for (i = 0; i < alen; i++)
        if (a[i] == '/')
            base = a + i + 1;
    alen -= (uint32_t)(base - a);
    if (alen != strlen(b))
        return 0;
    for (i = 0; i < alen; i++) {
        int x = base[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y)
            return 0;
    }
    return 1;
}

/* extract the entry at central directory header cd into r->dest */
static int zip_extract(const uint8_t *zip, uint32_t ziplen, const uint8_t *cd, RomRequest *r)
{
    uint32_t method = le16(cd + 10), csize = le32(cd + 20), usize = le32(cd + 24);
    uint32_t lh = le32(cd + 42), data;
    if (usize != r->size || lh + 30 > ziplen || le32(zip + lh) != 0x04034b50UL)
        return 0;
    data = lh + 30 + le16(zip + lh + 26) + le16(zip + lh + 28);
    if (data + csize > ziplen)
        return 0;
    if (method == 0) {
        if (csize != usize)
            return 0;
        memcpy(r->dest, zip + data, usize);
    } else if (method == 8) {
        if (inflate_data(zip + data, csize, r->dest, usize) != (int)usize)
            return 0;
    } else {
        return 0;
    }
    return 1;
}

static void load_from_zip(const char *path, const uint8_t *zip, uint32_t ziplen, RomRequest *req, int nreq)
{
    uint32_t eocd, cdpos, entries, e, p;
    int i, pass;
    if (ziplen < 22)
        return;
    for (eocd = ziplen - 22;; eocd--) {
        if (le32(zip + eocd) == 0x06054b50UL)
            break;
        if (eocd == 0 || ziplen - eocd > 65536 + 22)
            return;
    }
    entries = le16(zip + eocd + 10);
    cdpos = le32(zip + eocd + 16);

    /* pass 0: match by CRC and size, pass 1: by name (then the CRC is checked) */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < nreq; i++) {
            RomRequest *r = &req[i];
            if (r->found)
                continue;
            for (p = cdpos, e = 0; e < entries && p + 46 <= ziplen; e++) {
                const uint8_t *cd = zip + p;
                uint32_t nlen = le16(cd + 28);
                if (le32(cd) != 0x02014b50UL)
                    break;
                if (pass == 0 ? (le32(cd + 16) == r->crc && le32(cd + 24) == r->size)
                              : name_eq(cd + 46, nlen, r->name)) {
                    if (zip_extract(zip, ziplen, cd, r)) {
                        r->found = crc32_calc(r->dest, r->size) == r->crc ? 1 : 2;
                        if (r->found == 2)
                            fprintf(stderr, "%s: %s has a different CRC (%08lx, expected %08lx)\n",
                                    path, r->name, (unsigned long)crc32_calc(r->dest, r->size),
                                    (unsigned long)r->crc);
                        break;
                    }
                }
                p += 46 + nlen + le16(cd + 30) + le16(cd + 32);
            }
        }
    }
}

static void load_from_dir(const char *dir, RomRequest *req, int nreq)
{
    char path[512], name[64];
    int i;
    for (i = 0; i < nreq; i++) {
        RomRequest *r = &req[i];
        const char *n = r->name;
        if (r->found)
            continue;
        /* the name may list alternatives: "pb-0.ic24|pb-01.ic24" */
        while (*n && !r->found) {
            uint8_t *buf;
            uint32_t len = 0;
            size_t k = strcspn(n, "|");
            if (k >= sizeof name) k = sizeof name - 1;
            memcpy(name, n, k);
            name[k] = 0;
            n += k + (n[k] == '|');
            path_join(path, sizeof path, dir, name);
            buf = read_file(path, &len);
            if (!buf)
                continue;
            if (len == r->size) {
                memcpy(r->dest, buf, len);
                r->found = crc32_calc(r->dest, r->size) == r->crc ? 1 : 2;
                if (r->found == 2)
                    fprintf(stderr, "%s has a different CRC (expected %08lx)\n", path, (unsigned long)r->crc);
            } else {
                fprintf(stderr, "%s has the wrong size (%lu, expected %lu)\n", path,
                        (unsigned long)len, (unsigned long)r->size);
            }
            free(buf);
        }
    }
}

static int rom_load_ex(const char *const *sources, int nsources, RomRequest *req, int nreq, int quiet);

int rom_load(const char *const *sources, int nsources, RomRequest *req, int nreq)
{
    return rom_load_ex(sources, nsources, req, nreq, 0);
}

int rom_load_quiet(const char *const *sources, int nsources, RomRequest *req, int nreq)
{
    return rom_load_ex(sources, nsources, req, nreq, 1);
}

static int rom_load_ex(const char *const *sources, int nsources, RomRequest *req, int nreq, int quiet)
{
    int s, i, missing = 0;
    for (s = 0; s < nsources; s++) {
        uint32_t len = 0;
        uint8_t *buf = read_file(sources[s], &len);
        if (buf && len >= 4 && buf[0] == 'P' && buf[1] == 'K')
            load_from_zip(sources[s], buf, len, req, nreq);
        else
            load_from_dir(sources[s], req, nreq);
        free(buf);
    }
    for (i = 0; i < nreq; i++)
        if (!req[i].found) {
            if (!quiet)
                fprintf(stderr, "missing ROM: %s (size %lu, CRC %08lx)\n", req[i].name,
                    (unsigned long)req[i].size, (unsigned long)req[i].crc);
            missing++;
        }
    return missing;
}

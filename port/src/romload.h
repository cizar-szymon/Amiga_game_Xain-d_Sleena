/* ROM loading from MAME zip files or from directories with the ROM files */
#ifndef ROMLOAD_H
#define ROMLOAD_H

#include <stdint.h>

typedef struct RomRequest {
    const char *name;      /* file name as in MAME; alternatives for directories: "a|b" */
    uint32_t size;
    uint32_t crc;
    uint8_t *dest;
    int found;             /* set by rom_load: 1 = loaded with matching CRC, 2 = loaded but CRC differs */
} RomRequest;

uint32_t crc32_calc(const uint8_t *data, uint32_t len);

/* Look for every request in the given sources, in order. A source is a zip file
   (entries are matched by CRC and size first, then by name) or a directory (files are
   matched by name). Returns the number of requests that could not be loaded and
   prints problems to stderr. */
int rom_load(const char *const *sources, int nsources, RomRequest *req, int nreq);

/* the same without messages about missing ROMs (optional files) */
int rom_load_quiet(const char *const *sources, int nsources, RomRequest *req, int nreq);

/* join a directory and a file name ("DH0:" + "x" -> "DH0:x", "a" + "x" -> "a/x") */
void path_join(char *out, int outlen, const char *dir, const char *name);

#endif

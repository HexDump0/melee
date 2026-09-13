/*
 * Real HSD font atlases (S6, P-647 investigation).
 *
 * The decompilation's `sislib_font.c` / `hsd_3915.c` embed the atlases via
 * generated headers (retail DOL data, not committed, ADR-0005).  The port
 * reads the two ranges out of the user's own `sys/main.dol` and fills the
 * writable arrays here; everything else is the compiled text engine.
 */
#include <sysdolphin/baselib/hsd_3915.h>
#include <sysdolphin/baselib/sislib_font.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

#define DEBUG_FONT_ADDR 0x804088B8u
#define SISLIB_FONT_ADDR 0x8040CD40u

DebugFontGlyph HSD_DebugFontAtlas[128] __attribute__((aligned(32)));
TextGlyphTexture HSD_SisLib_FontAtlas[287] __attribute__((aligned(32)));

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

static const unsigned char* dol_range(const unsigned char* dol, size_t size,
                                      uint32_t addr, uint32_t length)
{
    int i;

    for (i = 0; i < 18; i++) {
        uint32_t off;
        uint32_t base;
        uint32_t section_size;

        if (i < 7) {
            off = be32(dol + 4 * i);
            base = be32(dol + 0x48 + 4 * i);
            section_size = be32(dol + 0x80 + 4 * i);
        } else {
            off = be32(dol + 0x1C + 4 * (i - 7));
            base = be32(dol + 0x64 + 4 * (i - 7));
            section_size = be32(dol + 0x9C + 4 * (i - 7));
        }
        if (off == 0 || section_size == 0 || addr < base ||
            (uint64_t) addr + length > (uint64_t) base + section_size)
        {
            continue;
        }
        if ((uint64_t) off + (addr - base) + length > size) {
            continue;
        }
        return dol + off + (addr - base);
    }
    return NULL;
}

int melee_port_fonts_load(void)
{
    void* dol = NULL;
    size_t dol_size = 0;
    const unsigned char* src;

    if (platform_disc_load_file("sys/main.dol", &dol, &dol_size) != 0) {
        return 0;
    }
    src = dol_range(dol, dol_size, DEBUG_FONT_ADDR,
                    (uint32_t) sizeof(HSD_DebugFontAtlas));
    if (src != NULL) {
        memcpy(HSD_DebugFontAtlas, src, sizeof(HSD_DebugFontAtlas));
    }
    src = dol_range(dol, dol_size, SISLIB_FONT_ADDR,
                    (uint32_t) sizeof(HSD_SisLib_FontAtlas));
    if (src != NULL) {
        memcpy(HSD_SisLib_FontAtlas, src, sizeof(HSD_SisLib_FontAtlas));
    }
    free(dol);
    return 1;
}

/* Portable GameCube GX tiled texture decoder for the native port. */
#ifndef MELEE_NATIVE_GX_TEXTURE_H
#define MELEE_NATIVE_GX_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Values from GXTexFmt (dolphin/gx/GXEnum.h). */
enum GxTextureFormat {
    TEX_FMT_I4 = 0,
    TEX_FMT_I8 = 1,
    TEX_FMT_IA4 = 2,
    TEX_FMT_IA8 = 3,
    TEX_FMT_RGB565 = 4,
    TEX_FMT_RGB5A3 = 5,
    TEX_FMT_RGBA8 = 6,
    TEX_FMT_CI4 = 8,
    TEX_FMT_CI8 = 9,
    TEX_FMT_CMPR = 14,
};

/* GXTlutFmt values. */
enum GxPaletteFormat {
    TEX_PAL_IA8 = 0,
    TEX_PAL_RGB565 = 1,
    TEX_PAL_RGB5A3 = 2,
};

/*
 * Decode big endian GameCube texture memory into malloc'd linear RGBA8.
 * The caller owns *out_rgba and must free it. On failure, *out_rgba is NULL
 * and error receives a short diagnostic when its buffer is non-empty.
 * Returns 0 on success and -1 on invalid input, truncation, or allocation
 * failure. CI/paletted formats are intentionally reported unsupported here.
 */
int gx_texture_decode(const void* pixels, size_t pixel_length,
                        int width, int height, int format,
                        uint8_t** out_rgba, char* error,
                        size_t error_length);

/*
 * Decodes paletted GX formats (TEX_FMT_CI4, TEX_FMT_CI8) using a palette that
 * has already been expanded to RGBA8.  The caller owns *out_rgba.
 */
int gx_texture_decode_ci(const void* indices, size_t index_length,
                           int width, int height, int format,
                           const uint8_t* palette, size_t palette_entries,
                           uint8_t** out_rgba, char* error,
                           size_t error_length);

#ifdef __cplusplus
}
#endif

#endif

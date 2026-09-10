/* Portable GameCube GX tiled texture decoder for the native demo. */
#ifndef MELEE_NATIVE_DEMO_TEXTURE_H
#define MELEE_NATIVE_DEMO_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Values from GXTexFmt (dolphin/gx/GXEnum.h). */
enum DemoTextureFormat {
    DEMO_TF_I4 = 0,
    DEMO_TF_I8 = 1,
    DEMO_TF_IA4 = 2,
    DEMO_TF_IA8 = 3,
    DEMO_TF_RGB565 = 4,
    DEMO_TF_RGB5A3 = 5,
    DEMO_TF_RGBA8 = 6,
    DEMO_TF_CI4 = 8,
    DEMO_TF_CI8 = 9,
    DEMO_TF_CMPR = 14,
};

/* GXTlutFmt values. */
enum DemoPaletteFormat {
    DEMO_PAL_IA8 = 0,
    DEMO_PAL_RGB565 = 1,
    DEMO_PAL_RGB5A3 = 2,
};

/*
 * Decode big endian GameCube texture memory into malloc'd linear RGBA8.
 * The caller owns *out_rgba and must free it. On failure, *out_rgba is NULL
 * and error receives a short diagnostic when its buffer is non-empty.
 * Returns 0 on success and -1 on invalid input, truncation, or allocation
 * failure. CI/paletted formats are intentionally reported unsupported here.
 */
int demo_texture_decode(const void* pixels, size_t pixel_length,
                        int width, int height, int format,
                        uint8_t** out_rgba, char* error,
                        size_t error_length);

/*
 * Decodes paletted GX formats (DEMO_TF_CI4, DEMO_TF_CI8) using a palette that
 * has already been expanded to RGBA8.  The caller owns *out_rgba.
 */
int demo_texture_decode_ci(const void* indices, size_t index_length,
                           int width, int height, int format,
                           const uint8_t* palette, size_t palette_entries,
                           uint8_t** out_rgba, char* error,
                           size_t error_length);

#ifdef __cplusplus
}
#endif

#endif

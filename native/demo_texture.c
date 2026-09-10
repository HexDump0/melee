#include "demo_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(char* error, size_t n, const char* text)
{
    if (error && n) {
        (void) snprintf(error, n, "%s", text);
    }
}

static uint16_t be16(const uint8_t* p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

static uint8_t expand4(unsigned v) { return (uint8_t) ((v << 4) | v); }
static uint8_t expand5(unsigned v) { return (uint8_t) ((v * 255u) / 31u); }
static uint8_t expand6(unsigned v) { return (uint8_t) ((v * 255u) / 63u); }

static void rgb565(uint16_t v, uint8_t* out)
{
    out[0] = expand5((v >> 11) & 31);
    out[1] = expand6((v >> 5) & 63);
    out[2] = expand5(v & 31);
    out[3] = 255;
}

static void rgb5a3(uint16_t v, uint8_t* out)
{
    if (v & 0x8000) {
        out[0] = expand5((v >> 10) & 31);
        out[1] = expand5((v >> 5) & 31);
        out[2] = expand5(v & 31);
        out[3] = 255;
    } else {
        out[0] = expand4((v >> 8) & 15);
        out[1] = expand4((v >> 4) & 15);
        out[2] = expand4(v & 15);
        out[3] = (uint8_t) ((((v >> 12) & 7) * 255u) / 7u);
    }
}

static void put(uint8_t* dst, int width, int height, int x, int y,
                const uint8_t* rgba)
{
    if (x < width && y < height) {
        memcpy(dst + ((size_t) y * (size_t) width + (size_t) x) * 4,
               rgba, 4);
    }
}

static void decode_i4(const uint8_t* src, uint8_t* dst, int w, int h)
{
    int by, bx, y, x;
    for (by = 0; by < (h + 7) / 8; ++by) {
        for (bx = 0; bx < (w + 7) / 8; ++bx) {
            for (y = 0; y < 8; ++y) {
                for (x = 0; x < 8; x += 2) {
                    uint8_t p[4];
                    uint8_t v = *src++;
                    p[0] = p[1] = p[2] = p[3] = (uint8_t) (v & 0xf0);
                    put(dst, w, h, bx * 8 + x, by * 8 + y, p);
                    p[0] = p[1] = p[2] = p[3] = (uint8_t) ((v & 15) * 17);
                    put(dst, w, h, bx * 8 + x + 1, by * 8 + y, p);
                }
            }
        }
    }
}

static void decode_i8(const uint8_t* src, uint8_t* dst, int w, int h)
{
    int by, bx, y, x;
    for (by = 0; by < (h + 3) / 4; ++by) {
        for (bx = 0; bx < (w + 7) / 8; ++bx) {
            for (y = 0; y < 4; ++y) for (x = 0; x < 8; ++x) {
                uint8_t p[4]; uint8_t v = *src++;
                p[0] = p[1] = p[2] = p[3] = v;
                put(dst, w, h, bx * 8 + x, by * 4 + y, p);
            }
        }
    }
}

static void decode_ia4(const uint8_t* src, uint8_t* dst, int w, int h)
{
    int by, bx, y, x;
    for (by = 0; by < (h + 3) / 4; ++by) {
        for (bx = 0; bx < (w + 7) / 8; ++bx) for (y = 0; y < 4; ++y)
            for (x = 0; x < 8; ++x) {
                uint8_t p[4]; uint8_t v = *src++;
                p[0] = p[1] = p[2] = expand4(v & 15); p[3] = expand4(v >> 4);
                put(dst, w, h, bx * 8 + x, by * 4 + y, p);
            }
        }
}

static void decode_ia8(const uint8_t* src, uint8_t* dst, int w, int h)
{
    int by, bx, y, x;
    for (by = 0; by < (h + 3) / 4; ++by) for (bx = 0; bx < (w + 3) / 4; ++bx)
        for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) {
            uint8_t p[4]; p[3] = *src++; p[0] = p[1] = p[2] = *src++;
            put(dst, w, h, bx * 4 + x, by * 4 + y, p);
        }
}

static void decode_16(const uint8_t* src, uint8_t* dst, int w, int h, int fmt)
{
    int by, bx, y, x;
    for (by = 0; by < (h + 3) / 4; ++by) for (bx = 0; bx < (w + 3) / 4; ++bx)
        for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x) {
            uint8_t p[4]; uint16_t v = be16(src); src += 2;
            if (fmt == DEMO_TF_RGB565) rgb565(v, p); else rgb5a3(v, p);
            put(dst, w, h, bx * 4 + x, by * 4 + y, p);
        }
}

static void decode_rgba8(const uint8_t* src, uint8_t* dst, int w, int h)
{
    int by, bx, i;
    for (by = 0; by < (h + 3) / 4; ++by) for (bx = 0; bx < (w + 3) / 4; ++bx) {
        uint8_t ar[16][2];
        for (i = 0; i < 16; ++i) { ar[i][0] = *src++; ar[i][1] = *src++; }
        for (i = 0; i < 16; ++i) {
            uint8_t p[4]; int x = i & 3, y = i >> 2;
            p[0] = ar[i][1]; p[1] = *src++; p[2] = *src++; p[3] = ar[i][0];
            put(dst, w, h, bx * 4 + x, by * 4 + y, p);
        }
    }
}

static void decode_cmpr(const uint8_t* src, uint8_t* dst, int w, int h)
{
    int by, bx, sub, y, x, c;
    for (by = 0; by < (h + 7) / 8; ++by) for (bx = 0; bx < (w + 7) / 8; ++bx)
        for (sub = 0; sub < 4; ++sub) {
            uint16_t c0 = be16(src), c1 = be16(src + 2); uint8_t p[4][4];
            uint8_t bits[16]; int sx = (sub & 1) * 4, sy = (sub >> 1) * 4;
            src += 4;
            rgb565(c0, p[0]); rgb565(c1, p[1]);
            if (c0 > c1) for (c = 0; c < 3; ++c) {
                p[2][c] = (uint8_t) ((2 * p[0][c] + p[1][c]) / 3);
                p[3][c] = (uint8_t) ((p[0][c] + 2 * p[1][c]) / 3);
            } else for (c = 0; c < 3; ++c) p[2][c] = (uint8_t) ((p[0][c] + p[1][c]) / 2);
            p[2][3] = 255; p[3][3] = c0 > c1 ? 255 : 0;
            if (c0 <= c1) p[3][0] = p[3][1] = p[3][2] = 0;
            for (y = 0; y < 4; ++y) { bits[y * 4] = *src++; }
            for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x)
                put(dst, w, h, bx * 8 + sx + x, by * 8 + sy + y,
                    p[(bits[y * 4] >> (6 - x * 2)) & 3]);
        }
}

int demo_texture_decode(const void* pixels, size_t pixel_length,
                        int width, int height, int format,
                        uint8_t** out_rgba, char* error, size_t error_length)
{    size_t blocks_x, blocks_y, bytes_per_block, expected, output_size;
    if (out_rgba) *out_rgba = NULL;
    if (!out_rgba || !pixels || width <= 0 || height <= 0) {
        fail(error, error_length, "invalid texture dimensions or output"); return -1;
    }
    switch (format) {
    case DEMO_TF_I4: bytes_per_block = 32; blocks_x = (size_t)(width + 7) / 8; blocks_y = (size_t)(height + 7) / 8; break;
    case DEMO_TF_I8: case DEMO_TF_IA4: bytes_per_block = 32; blocks_x = (size_t)(width + 7) / 8; blocks_y = (size_t)(height + 3) / 4; break;
    case DEMO_TF_IA8: case DEMO_TF_RGB565: case DEMO_TF_RGB5A3: bytes_per_block = 32; blocks_x = (size_t)(width + 3) / 4; blocks_y = (size_t)(height + 3) / 4; break;
    case DEMO_TF_RGBA8: bytes_per_block = 64; blocks_x = (size_t)(width + 3) / 4; blocks_y = (size_t)(height + 3) / 4; break;
    case DEMO_TF_CMPR: bytes_per_block = 32; blocks_x = (size_t)(width + 7) / 8; blocks_y = (size_t)(height + 7) / 8; break;
    default: fail(error, error_length, "unsupported GX texture format (CI requires a palette)"); return -1;
    }
    if (blocks_x > SIZE_MAX / blocks_y || blocks_x * blocks_y > SIZE_MAX / bytes_per_block ||
        (size_t) width > SIZE_MAX / (size_t) height || (size_t) width * (size_t) height > SIZE_MAX / 4) {
        fail(error, error_length, "texture size overflow"); return -1;
    }
    expected = blocks_x * blocks_y * bytes_per_block;
    output_size = (size_t) width * (size_t) height * 4;
    if (pixel_length < expected) { fail(error, error_length, "truncated GX texture data"); return -1; }
    *out_rgba = (uint8_t*) malloc(output_size);
    if (!*out_rgba) { fail(error, error_length, "RGBA8 allocation failed"); return -1; }
    memset(*out_rgba, 0, output_size);
    switch (format) {
    case DEMO_TF_I4: decode_i4(pixels, *out_rgba, width, height); break;
    case DEMO_TF_I8: decode_i8(pixels, *out_rgba, width, height); break;
    case DEMO_TF_IA4: decode_ia4(pixels, *out_rgba, width, height); break;
    case DEMO_TF_IA8: decode_ia8(pixels, *out_rgba, width, height); break;
    case DEMO_TF_RGB565: case DEMO_TF_RGB5A3: decode_16(pixels, *out_rgba, width, height, format); break;
    case DEMO_TF_RGBA8: decode_rgba8(pixels, *out_rgba, width, height); break;
    case DEMO_TF_CMPR: decode_cmpr(pixels, *out_rgba, width, height); break;
    }
    return 0;
}

int demo_texture_decode_ci(const void* indices, size_t index_length,
                           int width, int height, int format,
                           const uint8_t* palette, size_t palette_entries,
                           uint8_t** out_rgba, char* error, size_t error_length)
{
    size_t output_size;
    size_t expected;
    size_t blocks_x;
    size_t blocks_y;
    const uint8_t* src = (const uint8_t*) indices;
    uint8_t* dst;
    int by;
    int bx;
    if (out_rgba) *out_rgba = NULL;
    if (!out_rgba || !indices || !palette || width <= 0 || height <= 0 ||
        palette_entries == 0) {
        fail(error, error_length, "invalid CI texture arguments");
        return -1;
    }
    if (format != DEMO_TF_CI4 && format != DEMO_TF_CI8) {
        fail(error, error_length, "not an indexed GX texture format");
        return -1;
    }
    blocks_x = (size_t)(width + 7) / 8;
    blocks_y = format == DEMO_TF_CI4 ? (size_t)(height + 7) / 8
                                     : (size_t)(height + 3) / 4;
    expected = blocks_x * blocks_y * 32;
    if (index_length < expected) {
        fail(error, error_length, "truncated CI texture data");
        return -1;
    }
    if ((size_t) width > SIZE_MAX / (size_t) height ||
        (size_t) width * (size_t) height > SIZE_MAX / 4) {
        fail(error, error_length, "texture size overflow");
        return -1;
    }
    output_size = (size_t) width * (size_t) height * 4;
    dst = (uint8_t*) malloc(output_size);
    if (!dst) {
        fail(error, error_length, "RGBA8 allocation failed");
        return -1;
    }
    memset(dst, 0, output_size);
    for (by = 0; by < (height + (format == DEMO_TF_CI4 ? 7 : 3)) /
                         (format == DEMO_TF_CI4 ? 8 : 4);
         ++by) {
        for (bx = 0; bx < (width + 7) / 8; ++bx) {
            int y;
            int rows = format == DEMO_TF_CI4 ? 8 : 4;
            for (y = 0; y < rows; ++y) {
                int x;
                if (format == DEMO_TF_CI4) {
                    for (x = 0; x < 8; x += 2) {
                        unsigned int packed = *src++;
                        unsigned int hi = (packed >> 4) & 0xF;
                        unsigned int lo = packed & 0xF;
                        if (hi < palette_entries) {
                            put(dst, width, height, bx * 8 + x, by * 8 + y,
                                palette + hi * 4);
                        }
                        if (lo < palette_entries) {
                            put(dst, width, height, bx * 8 + x + 1,
                                by * 8 + y, palette + lo * 4);
                        }
                    }
                } else {
                    for (x = 0; x < 8; ++x) {
                        unsigned int index = *src++;
                        if (index < palette_entries) {
                            put(dst, width, height, bx * 8 + x, by * 4 + y,
                                palette + index * 4);
                        }
                    }
                }
            }
        }
    }
    *out_rgba = dst;
    return 0;
}

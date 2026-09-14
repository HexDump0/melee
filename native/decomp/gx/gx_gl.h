#ifndef MELEE_DECOMP_GX_GL_H
#define MELEE_DECOMP_GX_GL_H

/*
 * S2 GLES3 renderer for the GX HLE frame (EGL surfaceless/pbuffer, ADR-0009's
 * ES3-portable shader subset).  gx_gl consumes the draw list captured by
 * gx_hle and evaluates the captured GX TEV/channel/texture state.
 */
#include <stddef.h>
#include <stdio.h>

/* Creates an EGL pbuffer context and compiles the TEV program (headless). */
int gx_gl_init(int width, int height, char* error, size_t error_size);

/* Compiles the TEV program and allocates the streaming VBO assuming a GL
 * context is already current (the SDL viewer owns its window/context). */
int gx_gl_attach(int width, int height, char* error, size_t error_size);

void gx_gl_set_size(int width, int height);

/* Viewer toggles.  textures=0 forces white samples, lighting=0 forces the
 * flat raster colour, only_draw/hide_draw isolate a captured draw (-1 =
 * disabled), wireframe outlines each triangle. */
typedef struct GxGlOptions {
    int textures;
    int lighting;
    int only_draw;
    int hide_draw;
    int wireframe;
    int no_cull; /* debug: draw both faces */
    int no_alpha_test; /* debug: skip GX alpha compare */
} GxGlOptions;

void gx_gl_set_options(const GxGlOptions* options);

/* Deletes cached GL textures (call when switching models so recycled asset
 * addresses cannot hit stale cache entries). */
void gx_gl_clear_textures(void);

/* Drops the decoded-texture cache entry (if any) for a source image.  The
 * cache key is the CPU pointer, so CPU-updated images (THP movie planes,
 * EFB copies) must invalidate it or the GL texture keeps the first decode. */
void gx_gl_invalidate_texture(const void* image);

/* Renders the last frame captured by gx_hle into the pbuffer.  Returns the
 * number of draws submitted, or -1 on failure. */
int gx_gl_render_frame(void);

/* Reads the pbuffer back and writes a 24-bit BMP. */
int gx_gl_save_bmp(const char* path);

/* Reads the pbuffer back and appends one binary PPM (P6) frame to `f`.
 * Concatenated frames form an image2pipe stream for ffmpeg (--record). */
int gx_gl_write_ppm(FILE* f);

void gx_gl_set_clear(float r, float g, float b, float a);
void gx_gl_shutdown(void);

#endif

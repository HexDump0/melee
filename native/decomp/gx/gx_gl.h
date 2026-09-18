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

/*
 * Presentation aspect: the aspect ratio the 640x480 EFB is *authored* for.
 * The EFB is fitted into the window at this aspect and centred, so a window
 * of any other shape gets bars instead of a stretched picture.  4:3 is the
 * retail value; the widescreen feature raises it to the window's own aspect.
 * 0 fills the window unconditionally (pre-P-831 behaviour), which the
 * headless probe paths want.
 */
void gx_gl_set_display_aspect(float aspect);
float gx_gl_get_display_aspect(void);

/* The real drawable size, for code that has to derive an aspect from it. */
void gx_gl_get_window_size(int* width, int* height);

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
    /*
     * Cinematic presentation (P-864): bloom, a filmic curve, a vignette and
     * an anisotropy floor, applied to the finished frame on its way to the
     * window.  On by default -- it is what the port looks like now.
     *
     * Presentation only, like every other field here: it reads and writes the
     * colour buffer after the last draw of the frame, so a capture made with
     * it on is the same match as one made without it.  The headless render
     * harness turns it off so its output stays comparable to the console's.
     */
    int cinematic;
} GxGlOptions;

void gx_gl_set_options(const GxGlOptions* options);
/* What the backend is currently using, so a UI that toggles one field
 * starts from the live state instead of from a literal that has drifted. */
void gx_gl_get_options(GxGlOptions* options);

/* Deletes cached GL textures (call when switching models so recycled asset
 * addresses cannot hit stale cache entries). */
void gx_gl_clear_textures(void);

/* Drops the decoded-texture cache entry (if any) for a source image.  The
 * cache key is the CPU pointer, so CPU-updated images (THP movie planes,
 * EFB copies) must invalidate it or the GL texture keeps the first decode. */
void gx_gl_invalidate_texture(const void* image);
/* MELEE_GX_TEX_STATS=1 reports these every 30 match frames.  A cache that is
 * too small, or that is being invalidated more than it needs to be, does not
 * fail -- it just stops being a cache, and frame time is the only symptom. */
void gx_gl_texture_stats(unsigned long* hits, unsigned long* misses,
                         unsigned long* evictions, unsigned long* decodes,
                         unsigned long* invalidations, size_t* live);

/* Renders the last frame captured by gx_hle into the pbuffer.  Returns the
 * number of draws submitted, or -1 on failure. */
int gx_gl_render_frame(void);

/* Reads the pbuffer back and writes a 24-bit BMP. */
unsigned gx_gl_probe_nonblack(int gx_x, int gx_y, int gx_w, int gx_h);
unsigned gx_gl_probe_white(int gx_x, int gx_y, int gx_w, int gx_h);
int gx_gl_save_bmp(const char* path);

/* Reads the pbuffer back and appends one binary PPM (P6) frame to `f`.
 * Concatenated frames form an image2pipe stream for ffmpeg (--record). */
int gx_gl_write_ppm(FILE* f);

void gx_gl_set_clear(float r, float g, float b, float a);
void gx_gl_shutdown(void);

#endif

#ifndef MELEE_DECOMP_GX_GL_H
#define MELEE_DECOMP_GX_GL_H

/*
 * S2 GLES3 renderer for the GX HLE frame (EGL surfaceless/pbuffer, ADR-0009's
 * ES3-portable shader subset).  gx_gl consumes the draw list captured by
 * gx_hle and evaluates the captured GX TEV/channel/texture state.
 */
#include <stddef.h>

/* Creates an EGL pbuffer context and compiles the TEV program. */
int gx_gl_init(int width, int height, char* error, size_t error_size);

/* Renders the last frame captured by gx_hle into the pbuffer.  Returns the
 * number of draws submitted, or -1 on failure. */
int gx_gl_render_frame(void);

/* Reads the pbuffer back and writes a 24-bit BMP. */
int gx_gl_save_bmp(const char* path);

void gx_gl_set_clear(float r, float g, float b, float a);
void gx_gl_shutdown(void);

#endif

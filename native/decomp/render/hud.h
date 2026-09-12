#ifndef MELEE_DECOMP_RENDER_HUD_H
#define MELEE_DECOMP_RENDER_HUD_H

/*
 * Minimal GLES3 text overlay for the compiled viewer (P-611).
 *
 * Renders the port's own 5x7 bitmap alphabet (extras/font.h) as batched
 * pixel quads.  Supports A-Z, 0-9, '%', ':', '-', '/', '.', '+' only; the
 * caller must uppercase its text.  Draw after the scene frame and before
 * the buffer swap.
 */
#include <stddef.h>

int hud_init(char* error, size_t error_size);
void hud_shutdown(void);

/* Starts a batch for a width x height target (pixels). */
void hud_begin(int width, int height);

void hud_set_color(float r, float g, float b, float a);
void hud_text(float x, float y, float scale, const char* text);
void hud_printf(float x, float y, float scale, const char* fmt, ...);

/* Uploads and draws the batch; restores GL state enough for the next frame. */
void hud_end(void);

#endif

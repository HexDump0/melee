#ifndef MELEE_NATIVE_GX_OVERLAY_H
#define MELEE_NATIVE_GX_OVERLAY_H

/* CPU-side immediate-style geometry for HUD, grid and debug drawing.
 * Replaces the old fixed-function glBegin/glVertex path (ADR-0009); quads
 * and fans are converted to triangles and drawn through a shader. */
#include "gx/gl.h"
#include "gx/math.h"

int overlay_init(void);
void overlay_shutdown(void);

void ov_begin(GLenum mode);
void ov_end(void);
void ov_vertex3f(float x, float y, float z);
void ov_vertex2f(float x, float y);
void ov_color3f(float r, float g, float b);
void ov_color4f(float r, float g, float b, float a);
void ov_set_mvp(const Mat4 m);

void rect(float x, float y, float w, float h);
void circle(float x, float y, float z, float radius, int filled);
void draw_text(float x, float y, float size, const char *s);

#endif

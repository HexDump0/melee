#ifndef MELEE_NATIVE_GX_SHADER_H
#define MELEE_NATIVE_GX_SHADER_H

/*
 * Shader/program helpers.  The version header is the only place the desktop
 * GL / GLES split lives: bodies use an ES3-portable subset (ADR-0009).
 */
#include "gx/gl.h"

#if defined(GL_ES)
#define GLSL_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define GLSL_HEADER "#version 330\n"
#endif

GLuint gx_compile_shader(GLenum type, const char *body, const char *body2);
GLuint gx_link_program(GLuint vs, GLuint fs, const char *name);

#endif

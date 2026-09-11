#ifndef MELEE_NATIVE_GX_GL_H
#define MELEE_NATIVE_GX_GL_H

/* Single place where the OpenGL headers are pulled in.  GL_GLEXT_PROTOTYPES
 * exposes the GL 2+ entry points from Mesa/SDL's bundled glext header; a
 * future non-Linux target can swap this for a loader without touching the
 * rest of the port. */
#define GL_GLEXT_PROTOTYPES 1
#include <SDL.h>
#include <SDL_opengl.h>

#endif

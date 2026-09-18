/*
 * Shim for <printf.h> (P-502/W0).
 *
 * The decompilation includes <printf.h>, which is MSL's header; src/MSL is
 * excluded from the build (ADR-0011), so on glibc the include silently
 * resolves to /usr/include/printf.h instead.  musl -- and therefore
 * Emscripten -- has no such header, so those translation units stop
 * compiling.  The users only need the standard variadic-output declarations,
 * which stdio.h and stdarg.h already provide.
 */
#ifndef MELEE_SHIM_PRINTF_H
#define MELEE_SHIM_PRINTF_H
/* Searched before the system directory, so hand glibc builds back to its real
 * <printf.h> rather than quietly replacing it.  Everywhere without one --
 * musl by way of Emscripten, and MinGW -- gets the standard declarations,
 * which is all the callers use. */
#if defined(PORT_WASM) || defined(_WIN32)
#include <stdarg.h>
#include <stdio.h>
#else
#include_next <printf.h>
#endif
#endif

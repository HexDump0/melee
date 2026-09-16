/*
 * Shim for <execinfo.h> (P-502/W0).
 *
 * glibc's backtrace API has no musl or WebAssembly equivalent, and two
 * crash-diagnostic translation units include it: audio/sfx_debug.c and
 * decomp/boot/boot_triage.c.  Rather than gate those files out of the browser
 * build, supply the declarations and return "no frames".
 *
 * The degradation is deliberate and acceptable: in a browser the JavaScript
 * console already carries a stack for a trap, so the port's own symbolised
 * backtrace is redundant there.
 *
 * The shim directory is searched *before* the system one, so this header is
 * what every build finds.  Native builds must therefore be handed straight
 * back to glibc's real header with #include_next -- otherwise the port's
 * symbolised crash triage silently becomes a no-op on the desktop, which is
 * the exact class of regression this shim exists to avoid causing.
 */
#ifndef MELEE_SHIM_EXECINFO_H
#define MELEE_SHIM_EXECINFO_H

#ifndef PORT_WASM
#include_next <execinfo.h>
#else

#include <stddef.h>

static inline int backtrace(void** buffer, int size)
{
    (void) buffer;
    (void) size;
    return 0;
}

static inline char** backtrace_symbols(void* const* buffer, int size)
{
    (void) buffer;
    (void) size;
    return NULL;
}

static inline void backtrace_symbols_fd(void* const* buffer, int size, int fd)
{
    (void) buffer;
    (void) size;
    (void) fd;
}

#endif /* PORT_WASM */
#endif

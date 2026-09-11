#ifndef MELEE_NATIVE_DECOMP_SHIM_H
#define MELEE_NATIVE_DECOMP_SHIM_H

/*
 * Forced include (`-include`) for every upstream decomp TU compiled under
 * native/decomp/.  It is the entire platform shim for pure-C math TUs.
 *
 * src/Runtime/platform.h unconditionally typedefs ssize_t as `int`, but its
 * <dolphin/types.h> include pulls in <stdio.h> first, where glibc declares
 * ssize_t as `long` behind the __ssize_t_defined guard.  Pre-defining the
 * guard leaves the upstream typedef as the only one; the 32-bit `int` matches
 * what the MWERKS build had.  This is a glibc assumption (the port's only
 * verified target, see STATE.md).
 *
 * Keep this header minimal and warning-free under -Wall -Wextra -Wpedantic.
 * Never modify src/ or extern/dolphin.
 */
#if defined(__linux__)
#define __ssize_t_defined 1
#endif

/*
 * Host fixed-width pointer types: the decomp uses intptr_t/uintptr_t (127
 * uses) but MSL's stddef.h defines them as 32-bit. Always take the host's
 * 64-bit definitions first.
 */
#include <stdint.h>

#endif /* MELEE_NATIVE_DECOMP_SHIM_H */

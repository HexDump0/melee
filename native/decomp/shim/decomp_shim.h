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
 * Only needed on 64-bit hosts: on i686 glibc's ssize_t is already `int`, so
 * pre-defining the guard would leave <unistd.h> without any typedef at all
 * (the product/boot build is 32-bit, ADR-0012).
 *
 * Keep this header minimal and warning-free under -Wall -Wextra -Wpedantic.
 * Never modify src/ or extern/dolphin.
 */
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#define __ssize_t_defined 1
#endif

/*
 * Host fixed-width pointer types: the decomp uses intptr_t/uintptr_t (127
 * uses) but MSL's stddef.h defines them as 32-bit. Always take the host's
 * 64-bit definitions first.
 */
#include <stdint.h>

/*
 * S3 asset pipeline: every decompiled `HSD_ArchiveParse` call goes through the
 * platform converter first, which turns the caller's archive buffer from
 * big-endian GameCube data into host order in place (and consults the
 * conversion cache).  archive.c itself and the converter TU define
 * MELEE_ARCHIVE_INTERNAL so they see the real symbol.
 */
#ifndef MELEE_ARCHIVE_INTERNAL
#define HSD_ArchiveParse melee_port_HSD_ArchiveParse
#endif

#endif /* MELEE_NATIVE_DECOMP_SHIM_H */

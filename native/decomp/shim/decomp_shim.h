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

/*
 * The mod system's camera hook (ADR-0026).  Every compiled decomp call to
 * HSD_CObjSetCurrent and HSD_CObjLoadDesc lands in native/mod/mod_cobj.c
 * first, which is where UNBOUND_HOOK_CAMERA_SETUP is raised.  Same mechanism
 * as the archive rename above: link-time, free when no mod is loaded, and it
 * needs no dynamic loader, so it works in the browser too.
 *
 * cobj.c itself and the interposer TU define MELEE_COBJ_INTERNAL so they see
 * the real symbols.
 */
#ifndef MELEE_COBJ_INTERNAL
#define HSD_CObjSetCurrent unbound_HSD_CObjSetCurrent
#define HSD_CObjEndCurrent unbound_HSD_CObjEndCurrent
#define HSD_CObjLoadDesc unbound_HSD_CObjLoadDesc
#endif

/*
 * The main-menu label swap (P-838).  jobj.c defines these and mnmain.c calls
 * them, so the rename reaches the calls -- which is exactly why the menu's
 * own `mn_8022DB10` could not be done this way and is hooked through the
 * writable function pointer in `mn_803EB6B0` instead.
 */
#ifndef MELEE_JOBJ_INTERNAL
#define HSD_JObjReqAnim unbound_HSD_JObjReqAnim
#define HSD_JObjAnim unbound_HSD_JObjAnim
#endif

/*
 * S5: the engine stores a 32-bit address / 16.16 ratio into adjacent u16
 * fields with a `*(u32*) &pair = value` aliasing idiom (synth.c).  That is
 * only correct on big-endian: the host would put the low half in the first
 * field.  These helpers give the same layout on the host; the call sites are
 * `#ifdef PORT_PC`-gated (listed in learnings/decomp_port.md).
 */
#ifdef PORT_PC
#define MELEE_PORT_AX_SET_RATIO(dst, value)                                    \
    ((dst).ratioHi = (u16) ((u32) (value) >> 16),                              \
     (dst).ratioLo = (u16) (u32) (value))
#define MELEE_PORT_AX_GET_ADDR(addr)                                           \
    ((((u32) (addr).currentAddressHi) << 16) | (u32) (addr).currentAddressLo)
#define MELEE_PORT_AX_GET_U16PAIR(hi, lo) ((((u32) (hi)) << 16) | (u32) (lo))
#define MELEE_PORT_AX_SET_U16PAIR(hi, lo, value)                               \
    ((hi) = (u16) ((u32) (value) >> 16), (lo) = (u16) (u32) (value))
#endif

#endif /* MELEE_NATIVE_DECOMP_SHIM_H */

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
 * The Unbound opening movie (P-847).  lbmthp.c defines these and gmopening.c
 * calls them, so the rename reaches the calls: the interposer starts the
 * Unbound clip where the game asked for MvOpen.mth, and hands the real movie
 * over when the clip ends or the player presses a button.  The opening
 * scene's own per-frame pump is the second rename, which is what gives the
 * interposer a frame hook inside that scene and nowhere else.
 */
#ifndef MELEE_MTHP_INTERNAL
#define lbMthp_8001F410 unbound_lbMthp_8001F410
#define lbMthp_8001F578 unbound_lbMthp_8001F578
#endif

/*
 * The opening scene starts the movie's music *before* it starts the movie, so
 * with a clip in front of MvOpen.mth the Melee fanfare would play over the
 * Unbound logo and be seconds ahead of its own picture by the time that movie
 * began.  The interposer holds that one request back and makes it at the
 * hand-off instead.  Every other caller in the game -- there are 50 -- is
 * passed straight through; see mod_opening.c.
 */
#ifndef MELEE_AUDIO_AX_INTERNAL
#define lbAudioAx_80023F28 unbound_lbAudioAx_80023F28
#endif

/*
 * The press that skips the Unbound clip, spent so the opening scene does not
 * also act on it in the same frame.  Every other caller reads the real edge;
 * see mod_opening.c.  gm_1A36.c defines it, so it sees the real symbol.
 */
#ifndef MELEE_GM_INPUT_INTERNAL
#define gm_GetButtonsTriggered unbound_gm_GetButtonsTriggered
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
#define HSD_JObjAnimAll unbound_HSD_JObjAnimAll
#endif

/*
 * The menu description box (P-838).  Our entry has no SIS string of its own,
 * and borrowing another entry's reads as "View game records." under "Melee
 * Unbound".  Both calls are intercepted: the creator identifies *which* text
 * object is the description, and the setter blanks it for our entry.
 */
#ifndef MELEE_SISLIB_INTERNAL
#define HSD_SisLib_803A5ACC unbound_HSD_SisLib_803A5ACC
#define HSD_SisLib_803A6368 unbound_HSD_SisLib_803A6368
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


/*
 * A global symbol aliased to an offset inside another object (ADR-0011).
 *
 * The decompilation has a handful of console symbols that are really windows
 * into one larger block -- `lbl_8046E38C` is `Results_block_8046E1B0 + 0x1DC`.
 * The port expresses them as assembler aliases, which needs one thing the C
 * source cannot see: **i386-PE prefixes every C symbol with an underscore and
 * ELF does not**, so an alias written for Linux resolves to nothing on MinGW
 * and the link fails with an undefined reference to a symbol that is plainly
 * defined two lines above. `__USER_LABEL_PREFIX__` is the compiler's own
 * answer to that question -- `_` there, empty here.
 *
 * PORT_PC only; the GameCube build never sees these.
 */
#define MELEE_ASM_STR2(x) #x
#define MELEE_ASM_STR(x) MELEE_ASM_STR2(x)
#define MELEE_ASM_LP MELEE_ASM_STR(__USER_LABEL_PREFIX__)
#define MELEE_ASM_ALIAS(alias, base, offset)                                  \
    __asm__(".globl " MELEE_ASM_LP #alias "\n"                                \
            ".set " MELEE_ASM_LP #alias ", " MELEE_ASM_LP #base " + " #offset)

#endif /* MELEE_NATIVE_DECOMP_SHIM_H */

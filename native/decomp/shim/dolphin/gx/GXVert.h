#ifndef _DOLPHIN_GX_GXVERT_H_
#define _DOLPHIN_GX_GXVERT_H_

/*
 * ADR-0011 shim: this header shadows the SDK's
 * extern/dolphin/include/dolphin/gx/GXVert.h for the compiled game.  The
 * decomp's direct-mode vertex functions are static-inline stores to the
 * hardware FIFO address (0xCC008000), which has no meaning on the host.
 * They are routed to the GX HLE capture buffer instead; GXBegin marks a
 * primitive and gx_hle flushes it when the next command or frame boundary is
 * reached (GXEnd is an empty inline in GXGeometry.h, so it cannot flush).
 *
 * Keep the names and shapes identical to the SDK header so upstream code
 * compiles unchanged.
 */

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXFIFO_ADDR 0xCC008000

typedef union
{
    u8  u8;
    u16 u16;
    u32 u32;
    u64 u64;
    s8  s8;
    s16 s16;
    s32 s32;
    s64 s64;
    f32 f32;
    f64 f64;
} PPCWGPipe;

/* Raw FIFO users (e.g. src/melee/gm/gm_1832.c) still compile; the address is
 * only touched if such a function actually runs, which the S2/S4 paths do
 * not.  Everything that goes through the inline helpers below is captured. */
#define GXWGFifo (*(volatile PPCWGPipe *)GXFIFO_ADDR)

/* Implemented by native/decomp/gx/gx_hle.c: append one FIFO word to the
 * current direct-mode primitive in hardware (big-endian) order. */
void GXPortWGFifoU8(u8 x);
void GXPortWGFifoU16(u16 x);
void GXPortWGFifoU32(u32 x);
void GXPortWGFifoS8(s8 x);
void GXPortWGFifoS16(s16 x);
void GXPortWGFifoS32(s32 x);
void GXPortWGFifoF32(f32 x);

#define GXPORT_WRITE_u8 GXPortWGFifoU8
#define GXPORT_WRITE_u16 GXPortWGFifoU16
#define GXPORT_WRITE_u32 GXPortWGFifoU32
#define GXPORT_WRITE_s8 GXPortWGFifoS8
#define GXPORT_WRITE_s16 GXPortWGFifoS16
#define GXPORT_WRITE_s32 GXPortWGFifoS32
#define GXPORT_WRITE_f32 GXPortWGFifoF32

#define FUNC_1PARAM(name, T) \
static inline void name##1##T(T x) { GXPORT_WRITE_##T(x); }

#define FUNC_2PARAM(name, T) \
static inline void name##2##T(T x, T y) { GXPORT_WRITE_##T(x); GXPORT_WRITE_##T(y); }

#define FUNC_3PARAM(name, T) \
static inline void name##3##T(T x, T y, T z) { GXPORT_WRITE_##T(x); GXPORT_WRITE_##T(y); GXPORT_WRITE_##T(z); }

#define FUNC_4PARAM(name, T) \
static inline void name##4##T(T x, T y, T z, T w) { GXPORT_WRITE_##T(x); GXPORT_WRITE_##T(y); GXPORT_WRITE_##T(z); GXPORT_WRITE_##T(w); }

#define FUNC_INDEX8(name) \
static inline void name##1x8(u8 x) { GXPortWGFifoU8(x); }

#define FUNC_INDEX16(name) \
static inline void name##1x16(u16 x) { GXPortWGFifoU16(x); }

/* GXCmd / GXParam are only emitted by the SDK's inline command wrappers; the
 * port provides real GX API functions, so these exist for compile parity. */
FUNC_1PARAM(GXCmd, u8)
FUNC_1PARAM(GXCmd, u16)
FUNC_1PARAM(GXCmd, u32)
FUNC_1PARAM(GXParam, u8)
FUNC_1PARAM(GXParam, u16)
FUNC_1PARAM(GXParam, u32)
FUNC_1PARAM(GXParam, s8)
FUNC_1PARAM(GXParam, s16)
FUNC_1PARAM(GXParam, s32)
FUNC_1PARAM(GXParam, f32)
FUNC_3PARAM(GXParam, f32)
FUNC_4PARAM(GXParam, f32)

FUNC_3PARAM(GXPosition, f32)
FUNC_3PARAM(GXPosition, u8)
FUNC_3PARAM(GXPosition, s8)
FUNC_3PARAM(GXPosition, u16)
FUNC_3PARAM(GXPosition, s16)
FUNC_2PARAM(GXPosition, f32)
FUNC_2PARAM(GXPosition, u8)
FUNC_2PARAM(GXPosition, s8)
FUNC_2PARAM(GXPosition, u16)
FUNC_2PARAM(GXPosition, s16)
FUNC_INDEX16(GXPosition)
FUNC_INDEX8(GXPosition)

FUNC_3PARAM(GXNormal, f32)
FUNC_3PARAM(GXNormal, s16)
FUNC_3PARAM(GXNormal, s8)
FUNC_INDEX16(GXNormal)
FUNC_INDEX8(GXNormal)

FUNC_4PARAM(GXColor, u8)
FUNC_1PARAM(GXColor, u32)
FUNC_3PARAM(GXColor, u8)
FUNC_1PARAM(GXColor, u16)
FUNC_INDEX16(GXColor)
FUNC_INDEX8(GXColor)

FUNC_2PARAM(GXTexCoord, f32)
FUNC_2PARAM(GXTexCoord, s16)
FUNC_2PARAM(GXTexCoord, u16)
FUNC_2PARAM(GXTexCoord, s8)
FUNC_2PARAM(GXTexCoord, u8)
FUNC_1PARAM(GXTexCoord, f32)
FUNC_1PARAM(GXTexCoord, s16)
FUNC_1PARAM(GXTexCoord, u16)
FUNC_1PARAM(GXTexCoord, s8)
FUNC_1PARAM(GXTexCoord, u8)
FUNC_INDEX16(GXTexCoord)
FUNC_INDEX8(GXTexCoord)

FUNC_1PARAM(GXMatrixIndex, u8)

#ifdef __cplusplus
}
#endif

#endif

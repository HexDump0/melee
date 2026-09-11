#ifndef MELEE_NATIVE_DECOMP_DECOMP_MATH_H
#define MELEE_NATIVE_DECOMP_DECOMP_MATH_H

/*
 * Prototypes for the compiled decompilation math in native/decomp/.
 *
 * The upstream headers cannot be included from port code: they pull in the
 * GameCube platform chain (Runtime/platform.h -> dolphin/types.h) and use
 * typedefs that clash with libc.  Signatures are copied verbatim from
 * src/sysdolphin/baselib/mtx.h and the types mirror the upstream ones (Mtx is
 * f32[3][4] row-major; Vec3 is three f32s).  Keep in sync with the shim'd TU.
 */

typedef struct {
    float x, y, z;
} DecompVec3;

/* HSD_MtxSRT from src/sysdolphin/baselib/mtx.c. */
void HSD_MtxSRT(float m[3][4], DecompVec3* scale, DecompVec3* rot,
                DecompVec3* pos, DecompVec3* parent_scale);

#endif /* MELEE_NATIVE_DECOMP_DECOMP_MATH_H */

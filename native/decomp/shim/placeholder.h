#ifndef MELEE_NATIVE_DECOMP_PLACEHOLDER_H
#define MELEE_NATIVE_DECOMP_PLACEHOLDER_H

/*
 * Pull in the decompilation's declarations, then correct the non-Metrowerks
 * stand-ins for the GameCube float intrinsics.
 *
 * `__frsqrte`: the upstream fallback expands it to sqrt(x), but callers use
 * the result as a reciprocal-square-root estimate and apply Newton-Raphson
 * refinement.  Starting that iteration with sqrt(x) diverges for ordinary
 * world-space lengths and eventually injects NaNs into fighter inverse
 * kinematics.
 *
 * `__fabs`: the upstream fallback is `fabsf(f)`, which narrows the double
 * result MWCC's `__fabs` returns (generator.c:884 subtracts from M_PI and
 * compares against a float epsilon).  `fabs` keeps the double semantics.
 */
#include "../../../decomp/src/placeholder.h"

#include <math.h>
#undef __frsqrte
#define __frsqrte(x) (1.0 / sqrt((double) (x)))

#undef __fabs
#define __fabs(f) fabs(f)

#endif /* MELEE_NATIVE_DECOMP_PLACEHOLDER_H */

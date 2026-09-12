#ifndef MELEE_NATIVE_DECOMP_PLACEHOLDER_H
#define MELEE_NATIVE_DECOMP_PLACEHOLDER_H

/*
 * Pull in the decompilation's declarations, then correct the non-Metrowerks
 * stand-in for the GameCube `frsqrte` instruction.  The upstream fallback
 * expands it to sqrt(x), but callers use the result as a reciprocal-square-
 * root estimate and apply Newton-Raphson refinement.  Starting that iteration
 * with sqrt(x) diverges for ordinary world-space lengths and eventually
 * injects NaNs into fighter inverse kinematics.
 */
#include "../../../src/placeholder.h"

#include <math.h>
#undef __frsqrte
#define __frsqrte(x) (1.0 / sqrt((double) (x)))

#endif /* MELEE_NATIVE_DECOMP_PLACEHOLDER_H */

#ifndef MELEE_NATIVE_DECOMP_SHIM_STDBOOL_H
#define MELEE_NATIVE_DECOMP_SHIM_STDBOOL_H

/*
 * Host shim for <stdbool.h> on the decompiled-port build.
 *
 * The decomp uses `bool` (C99, 1 byte) and `BOOL` (dolphin/types.h, `int`)
 * interchangeably in function-pointer tables; 59+ sites mix them. MWCC
 * accepted the mismatch with a warning, but GCC treats it as an error, and
 * on x86-64 `_Bool` and `int` are not ABI-compatible for callback parameters
 * (the callee would read undefined upper register bits).
 *
 * Defining `bool` as `int` makes every TU agree with BOOL and makes the
 * callback ABI consistent. This is a port-only type decision (ADR-0011
 * rule 1 — shim first); the GameCube build keeps the real <stdbool.h>.
 */
#define bool int
#define true 1
#define false 0
#define __bool_true_false_are_defined 1

#endif /* MELEE_NATIVE_DECOMP_SHIM_STDBOOL_H */

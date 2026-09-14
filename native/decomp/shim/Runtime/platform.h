#ifndef MELEE_NATIVE_DECOMP_SHIM_RUNTIME_PLATFORM_H
#define MELEE_NATIVE_DECOMP_SHIM_RUNTIME_PLATFORM_H

/*
 * PC shadow of src/Runtime/platform.h for compiled decomp TUs.
 *
 * Includes the real header next in the include search path (the shim
 * directory is first).
 *
 * This header used to also #undef STATIC_ASSERT, because several decomp
 * headers (e.g. src/melee/ty/types.h) assert GameCube 32-bit struct offsets
 * with a raw STATIC_ASSERT(offsetof(...)) and those are false on a 64-bit
 * host.  ADR-0012 moved every compiled target to 32-bit, which made that
 * reasoning obsolete: the console offsets are simply correct here.  The
 * neutralisation was removed in P-714 so those assertions verify our layout
 * on every build instead of being silently discarded -- see ADR-0020.  The
 * file is kept as a pass-through so the shim include path stays uniform.
 *
 * `#include_next` is a GCC/Clang extension; this header is only on the
 * include path of native/decomp targets (compiled with -w) and is marked as a
 * system header so it never warns.
 */
#pragma GCC system_header

#include_next <Runtime/platform.h>


#endif /* MELEE_NATIVE_DECOMP_SHIM_RUNTIME_PLATFORM_H */

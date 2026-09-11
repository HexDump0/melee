#ifndef MELEE_NATIVE_DECOMP_SHIM_RUNTIME_PLATFORM_H
#define MELEE_NATIVE_DECOMP_SHIM_RUNTIME_PLATFORM_H

/*
 * PC shadow of src/Runtime/platform.h for compiled decomp TUs.
 *
 * 1. Includes the real header next in the include search path (the shim
 *    directory is first), then
 * 2. neutralises STATIC_ASSERT for the host build.
 *
 * Several decomp headers (e.g. src/melee/ty/types.h) assert GameCube 32-bit
 * struct offsets at compile time with raw STATIC_ASSERT(offsetof(...)). Those
 * assertions are true on the GC and false on a 64-bit host; they are compile
 * time only, so neutralising the macro keeps the TUs buildable without
 * touching src/ (ADR-0011 prefers shims). Runtime struct layout is
 * self-consistent because the whole game is compiled for the host.
 *
 * `#include_next` is a GCC/Clang extension; this header is only on the
 * include path of native/decomp targets (compiled with -w) and is marked as a
 * system header so it never warns.
 */
#pragma GCC system_header

#include_next <Runtime/platform.h>

#undef STATIC_ASSERT
#define STATIC_ASSERT(cond)                                                   \
    _Static_assert(1, "disabled on the host port build (GameCube layout)")

#endif /* MELEE_NATIVE_DECOMP_SHIM_RUNTIME_PLATFORM_H */

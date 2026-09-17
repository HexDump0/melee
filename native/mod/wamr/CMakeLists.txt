# WAMR (wasm-micro-runtime) as the desktop mod runtime -- ADR-0027.
#
# In its own directory so WAMR's build scripts, which call the directory-scoped
# include_directories(), cannot leak their include path into the port's own
# targets and shadow a decomp header.
#
# Interpreter only for now: AOT would want wamrc, which is an LLVM build of
# its own, and a mod that runs a handful of hooks per frame does not need it.
# P-835 revisits that when a mod is heavy enough to care.

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/wasm-micro-runtime)

set(WAMR_BUILD_PLATFORM "linux")
set(WAMR_BUILD_TARGET "X86_32")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
# Mods are freestanding (-nostdlib) and import only the unbound module, so
# none of WAMR's own library surfaces are wanted.  Every one left out is a
# host capability a mod cannot reach.
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_LIB_WASI_THREADS 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_BUILD_GC 0)
set(WAMR_BUILD_SHARED_HEAP 0)
set(WAMR_BUILD_DEBUG_INTERP 0)
# Hardware bound checks rely on signal handling and a large reserved address
# space; on a 32-bit host that is both scarce and in use by the game's own
# arena, so take the explicit bounds checks instead.
set(WAMR_DISABLE_HW_BOUND_CHECK 1)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(wamr_vm STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(wamr_vm PUBLIC
    ${WAMR_ROOT_DIR}/core/iwasm/include)
target_compile_options(wamr_vm PRIVATE -m32 -w)
target_link_options(wamr_vm PUBLIC -m32)
set_target_properties(wamr_vm PROPERTIES POSITION_INDEPENDENT_CODE OFF)

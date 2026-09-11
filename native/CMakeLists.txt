cmake_minimum_required(VERSION 3.16)
project(melee_native C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_package(OpenGL REQUIRED)

# P-301: compile pure-C decompilation math behind native/decomp/shim.
# src/sysdolphin/baselib/mtx.c is plain C and builds verbatim.  The SDK pairs
# (extern/dolphin/src/dolphin/mtx/{mtx.c,vec.c}) are Metrowerks asm and cannot
# be compiled by GCC/Clang; see native/AI/learnings/decomp_shim.md.  Upstream
# warnings are suppressed (-w) because src/ must not be modified; the shim
# itself is warning-free.  Only the referenced function sections survive the
# link (--gc-sections), so the TU's asm-dependent allocator functions do not
# need the unreachable SDK primitives.
add_library(melee_decomp_math OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/mtx.c)
target_include_directories(melee_decomp_math BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/include)
target_compile_options(melee_decomp_math PRIVATE
    -w -ffunction-sections -fdata-sections
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)

add_executable(melee
    main.c
    platform/disc.c
    platform/screenshot.c
    gx/texture.c
    gx/math.c
    gx/overlay.c
    gx/render.c
    gx/shader.c
    hsd/model.c
    hsd/aobj.c
    hsd/anim.c
    hsd/parts.c
    hsd/light.c
    game/attributes.c
    extras/physics.c
    extras/viewer.c
    extras/sandbox.c)
target_include_directories(melee PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(melee PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(melee PRIVATE PkgConfig::SDL2 OpenGL::GL m melee_decomp_math)
target_link_options(melee PRIVATE "LINKER:--gc-sections")
option(MELEE_SANITIZE "Enable memory and undefined behavior checks" OFF)
if(MELEE_SANITIZE)
  target_compile_options(melee PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(melee PRIVATE -fsanitize=address,undefined)
endif()

enable_testing()
add_executable(test_math tests/test_math.c gx/math.c)
target_include_directories(test_math PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_math PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(test_math PRIVATE m)
add_test(NAME math COMMAND test_math)

# P-301 parity: compiled HSD_MtxSRT vs the deleted hand copy and gx/math.c.
add_executable(test_decomp_mtx tests/test_decomp_mtx.c gx/math.c)
target_include_directories(test_decomp_mtx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test_decomp_mtx PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(test_decomp_mtx PRIVATE m melee_decomp_math)
target_link_options(test_decomp_mtx PRIVATE "LINKER:--gc-sections")
add_test(NAME decomp_mtx COMMAND test_decomp_mtx)

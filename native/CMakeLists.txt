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

# P-602 (S0a): run the compiled decomp HSD_ArchiveParse on a real PlMrNr.dat
# and diff its symbol table against the hand parser.  Built 32-bit like the
# eventual compiled port: archive.c relocates pointers into u32 slots and HSD
# structs assume 4-byte pointers (ADR-0012).  Skips without a disc image.
add_executable(test_decomp_hsd
    tests/test_decomp_hsd.c
    platform/disc.c
    decomp/sdk_math.c
    decomp/hsd_port_stubs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/archive.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/aobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/class.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/cobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/dobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/id.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/jobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/memory.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/mobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/mtx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/object.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/objalloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/pobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/robj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/spline.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/tobj.c)
target_include_directories(test_decomp_hsd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/include)
target_compile_options(test_decomp_hsd PRIVATE
    -m32 -w -ffunction-sections -fdata-sections
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(test_decomp_hsd PRIVATE m)
target_link_options(test_decomp_hsd PRIVATE -m32 "LINKER:--gc-sections")
add_test(NAME decomp_hsd COMMAND test_decomp_hsd)
set_tests_properties(decomp_hsd PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

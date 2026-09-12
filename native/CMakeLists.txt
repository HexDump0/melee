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

# ---------------------------------------------------------------------------
# S1 (P-604): boot skeleton.  Compile the decompilation's own main()
# (src/melee/gm/gmmain.c, renamed gm_main) for the host with the platform
# layer stubbed, and stop in a controlled way with a triage log.
#
# The whole game is compiled 32-bit (ADR-0012) behind the shim (ADR-0011);
# upstream warnings are suppressed because src/ must not be modified.  Three
# pure-C SDK TUs are compiled from extern/dolphin (never edited): the real SDN
# heap/arena and the Mtx44 projection helpers.  Files excluded on the host:
#   src/MSL/            glibc provides libc/math (ADR-0011)
#   src/MetroTRK/       GameCube debugger transport
#   Runtime/__mem.c     MSL memcpys; glibc provides them
#   baselib/debug.c     MSL FILE internals; replaced by decomp/debug_port.c
#   baselib/axdriver.c  drives the DSP hardware; replaced by platform/audio.c
#   sislib_font.c,
#   hsd_3915.c,
#   hsd_3A76.c          need generated font atlases (decomp build artifacts,
#                       ADR-0005); replaced by platform/font_stub.c for S1
#
# The real backends vs log-only stubs are listed in
# native/AI/learnings/decomp_boot.md.
file(GLOB_RECURSE MELEE_DECOMP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.c)
list(FILTER MELEE_DECOMP_SOURCES EXCLUDE REGEX
    "/src/(MSL|MetroTRK)/")
list(REMOVE_ITEM MELEE_DECOMP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/Runtime/__mem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/debug.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/axdriver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/sislib_font.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/hsd_3915.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/hsd_3A76.c)
list(APPEND MELEE_DECOMP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/src/dolphin/os/OSArena.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/src/dolphin/os/OSAlloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/src/dolphin/mtx/mtx44.c)

add_library(melee_decomp_game OBJECT ${MELEE_DECOMP_SOURCES})
target_include_directories(melee_decomp_game BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/include)
target_compile_options(melee_decomp_game PRIVATE
    -m32 -w -fgnu89-inline -ffunction-sections -fdata-sections
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/melee/gm/gmmain.c
    PROPERTIES COMPILE_DEFINITIONS "main=gm_main")

add_executable(melee_decomp_boot
    decomp/boot/boot_main.c
    decomp/boot/boot_triage.c
    decomp/debug_port.c
    decomp/sdk_math.c
    decomp/gx/gx_hle.c
    platform/os.c
    platform/gx_vi.c
    platform/dvd.c
    platform/pad_card.c
    platform/audio.c
    platform/font_stub.c
    platform/misc.c
    $<TARGET_OBJECTS:melee_decomp_game>)
target_include_directories(melee_decomp_boot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim)
target_include_directories(melee_decomp_boot SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/include)
target_compile_options(melee_decomp_boot PRIVATE
    -m32 -Wall -Wextra -Wpedantic -fgnu89-inline
    -ffunction-sections -fdata-sections -rdynamic -D_GNU_SOURCE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(melee_decomp_boot PRIVATE m ${CMAKE_DL_LIBS})
target_link_options(melee_decomp_boot PRIVATE
    -m32 -rdynamic "LINKER:--gc-sections")
add_test(NAME decomp_boot
    COMMAND melee_decomp_boot --boot-frames 5 --boot-timeout 30)
set_tests_properties(decomp_boot PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[boot\\] summary")
if(MELEE_SANITIZE)
  target_compile_options(melee_decomp_boot PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(melee_decomp_boot PRIVATE
      -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# S2 (P-606): compiled HSD + GX HLE character render.
#
# The same 32-bit game object library is driven by tests/test_decomp_render.c
# through the S2 asset bridge (decomp/hsd/hsd_scene.c) and the GX HLE backend
# (decomp/gx/gx_hle.c + gx_gl.c).  GLES3/EGL is used instead of SDL/desktop GL
# because 32-bit SDL2 is not installed on the reference machine; the shaders
# stay in ADR-0009's ES3 subset.  The prototype's texture decoder and scene
# light reader are reused (native/gx/texture.c, native/hsd/light.c).
add_executable(test_decomp_render
    tests/test_decomp_render.c
    decomp/render/render_scene.c
    decomp/hsd/hsd_scene.c
    decomp/gx/gx_hle.c
    decomp/gx/gx_gl.c
    decomp/debug_port.c
    decomp/sdk_math.c
    decomp/boot/boot_triage.c
    gx/texture.c
    hsd/light.c
    platform/disc.c
    platform/os.c
    platform/gx_vi.c
    platform/dvd.c
    platform/pad_card.c
    platform/audio.c
    platform/font_stub.c
    platform/misc.c
    $<TARGET_OBJECTS:melee_decomp_game>)
target_include_directories(test_decomp_render BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(test_decomp_render SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/include)
target_compile_options(test_decomp_render PRIVATE
    -m32 -Wall -Wextra -Wpedantic -Wno-overlength-strings -fgnu89-inline
    -ffunction-sections -fdata-sections -rdynamic -D_GNU_SOURCE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(test_decomp_render PRIVATE m EGL GLESv2
    ${CMAKE_DL_LIBS})
target_link_options(test_decomp_render PRIVATE
    -m32 -rdynamic "LINKER:--gc-sections")
add_test(NAME decomp_render COMMAND test_decomp_render
    --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_render.bmp)
set_tests_properties(decomp_render PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PASS_REGULAR_EXPRESSION "decomp_render: PASS")
add_test(NAME decomp_gx_direct COMMAND test_decomp_render --direct)
set_tests_properties(decomp_gx_direct PROPERTIES
    PASS_REGULAR_EXPRESSION "direct: PASS")
if(MELEE_SANITIZE)
  target_compile_options(test_decomp_render PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(test_decomp_render PRIVATE
      -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# P-611: interactive compiled-path viewer.
#
# SDL3 owns the window and the EGL/GLES3 context (ADR-0014; 32-bit SDL3 is
# Arch's lib32-sdl3), gx_gl attaches to it.  The target is skipped when the
# 32-bit SDL3 library is absent so the rest of the port still builds.
find_library(MELEE_SDL3_LIB32 NAMES SDL3 PATHS /usr/lib32 NO_DEFAULT_PATH)
if(MELEE_SDL3_LIB32)
  add_executable(melee_decomp_viewer
      decomp/render/viewer_main.c
      decomp/render/render_scene.c
      decomp/render/hud.c
      decomp/hsd/hsd_scene.c
      decomp/gx/gx_hle.c
      decomp/gx/gx_gl.c
      decomp/debug_port.c
      decomp/sdk_math.c
      decomp/boot/boot_triage.c
      gx/texture.c
      hsd/light.c
      platform/disc.c
      platform/os.c
      platform/gx_vi.c
      platform/dvd.c
      platform/pad_card.c
      platform/audio.c
      platform/font_stub.c
      platform/misc.c
      $<TARGET_OBJECTS:melee_decomp_game>)
  target_include_directories(melee_decomp_viewer BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
      ${CMAKE_CURRENT_SOURCE_DIR})
  target_include_directories(melee_decomp_viewer SYSTEM PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../src
      ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/include)
  target_compile_options(melee_decomp_viewer PRIVATE
      -m32 -Wall -Wextra -Wpedantic -Wno-overlength-strings -fgnu89-inline
      -ffunction-sections -fdata-sections -rdynamic -D_GNU_SOURCE
      -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
  # SDL3 requires a 1-byte _Bool, so the viewer TU opts out of the bool=int
  # shim (it never calls a decomp function with a bool parameter).
  set_source_files_properties(decomp/render/viewer_main.c PROPERTIES
      COMPILE_DEFINITIONS "MELEE_SHIM_REAL_STDBOOL")
  target_link_libraries(melee_decomp_viewer PRIVATE
      ${MELEE_SDL3_LIB32} m EGL GLESv2 ${CMAKE_DL_LIBS})
  target_link_options(melee_decomp_viewer PRIVATE
      -m32 -rdynamic "LINKER:--gc-sections")
else()
  message(STATUS "32-bit SDL3 not found; skipping melee_decomp_viewer")
endif()

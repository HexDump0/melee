cmake_minimum_required(VERSION 3.16)
project(melee_native C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# The decompilation lives in the `decomp/` git submodule (doldecomp/melee, see
# README).  The port carries its #ifdef PORT_PC portability fixes as a patch
# series under patches/ (ADR-0011: shim first, minimal patch second); apply
# them to the submodule work tree so every build sees them.  Idempotent.
find_program(MELEE_GIT_EXECUTABLE git REQUIRED)
execute_process(
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/apply_decomp_patches.sh"
    RESULT_VARIABLE MELEE_PATCH_RESULT
    OUTPUT_VARIABLE MELEE_PATCH_OUTPUT
    ERROR_VARIABLE MELEE_PATCH_ERROR)
if(NOT MELEE_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Failed to apply the decomp patch series.\n"
        "${MELEE_PATCH_OUTPUT}${MELEE_PATCH_ERROR}"
        "Run scripts/apply_decomp_patches.sh manually (is decomp/ initialized?)")
endif()

# The compiled game is 32-bit for original HSD structure layout.  GCC would
# otherwise evaluate scalar floating point through x87 and retain 80-bit
# intermediates, violating the source types at sequence points.  In
# particular, near-identical quaternion interpolation can take the spherical
# branch in extended precision, round the acosf argument to 1.0f, and divide
# by sin(0).  SSE2 keeps float/double evaluation at their declared widths and
# is also the faster scalar backend on supported PC hosts.
set(MELEE_32BIT_FP_OPTIONS -msse2 -mfpmath=sse)

# The decompilation reaches through type-punned pointers constantly: 72 sites
# mask a float's sign bit with `*(s32*) &f &= 0x7FFFFFFF`, the results screen
# reads a quantised store back with `*(u16*) &sp48_x` (P-709), and the HSD
# structures are walked through casts at every level.  MWCC compiled all of
# that literally; GCC's -O2 strict-aliasing model is entitled to assume the
# int lvalue and the float object are distinct, and it does -- a tree-wide
# -Wuninitialized sweep reports the sign-bit sites as reading an
# *uninitialised* int (particle.c:539 `abs_z`, gmresultplayer.c:609
# `abs_stick_y`, ft_0892.c:44 `spC`), which means the mask can be folded away
# entirely.  Turning strict aliasing off is the only fix that scales: the
# alternative is rewriting every pun in a read-only tree.  Costs a little
# optimisation and nothing else; the GameCube build is unaffected.
set(MELEE_DECOMP_UB_OPTIONS -fno-strict-aliasing)

# The decompilation carries the console's own layout contract: 189
# ASSERT_SIZE/ASSERT_OFFSET declarations, plus a `#pragma pack(push, 1)` around
# TmData::x37[] in melee/gm/types.h.  All of it is compiled out unless
# MUST_MATCH or LINT is defined, and the port defined neither -- so every one
# of those assertions was inert and TmData was silently mis-laid-out:
# sizeof(struct TmData) measured 0x5F8 against the console's 0x574, with
# TmUnkMenuData at 0x14 instead of 0x12, shifting every field after x37[64] by
# 132 bytes in the live gm_804771C4 Tournament Mode global.
#
# ADR-0012 is what makes this safe to switch on: the documented sizes are
# PowerPC sizes, and our compiled targets are 32-bit, so they are simply
# correct here.  (999sian/melee-pc withdrew this same idea in 51965c2 because
# on x86-64 every pointer-bearing struct legitimately differs -- that reasoning
# does not transfer to a 32-bit target.)  Verified: all 189 assertions pass
# tree-wide over the 993 compiled TUs.
#
# LINT must reach EVERY TU that sees these headers.  It changes packing, so a
# mixed build would give one struct two layouts -- worse than the bug.  The
# eight option lines below are exactly the targets that compile decompiled
# sources or their headers; melee_decomp_math (64-bit, mtx.c only) never
# reaches melee/gm/types.h.
set(MELEE_DECOMP_LAYOUT_OPTIONS -DLINT)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/mtx.c)
target_include_directories(melee_decomp_math BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(melee_decomp_math PRIVATE
    -w -ffunction-sections -fdata-sections
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)

# S5 (ADR-0013): the SDK AX bookkeeping/voice layer is pure C and runs
# unchanged against the host mixer; only DSP/AI (AXOut.c) is replaced by
# native/audio/ax_hle.c.  Kept as a list so the audio unit test can link it
# without the whole game library.
set(MELEE_AX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/sfx_debug.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/ax/AX.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/ax/AXAlloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/ax/AXAux.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/ax/AXSPB.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/ax/AXVPB.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/ax/AXCL.c
    # AXFX: delay.c is portable C.  reverb_std/reverb_hi/chorus contain MWCC
    # asm in extern/ and are replaced by native/decomp/axfx/ (ADR-0011 rule 3).
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/axfx/axfx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/axfx/delay.c
    # reverb_std/reverb_hi/chorus are hybrid asm in extern/ (ADR-0011 rule 3);
    # this replacement TU ports all three effects.
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/axfx/axfx_port.c)

add_executable(melee_prototype
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
target_include_directories(melee_prototype PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(melee_prototype PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(melee_prototype PRIVATE PkgConfig::SDL2 OpenGL::GL m melee_decomp_math)
target_link_options(melee_prototype PRIVATE "LINKER:--gc-sections")
option(MELEE_SANITIZE "Enable memory and undefined behavior checks" OFF)
if(MELEE_SANITIZE)
  # The sanitizer build is a Debug build, and Debug means -O0.  At -O0 GCC's C
  # front end will not fold a `const` integer into an integer constant
  # expression, so `src/MSL/math.h`'s `__fpclassifyf` -- `const s32 exp_mask`
  # used as a `case` label -- fails to compile, and the whole documented
  # ASan/UBSan workflow in TESTING.md has been unbuildable.  -O1 folds it, is
  # what the sanitizer documentation recommends anyway, and keeps the frame
  # pointers and line numbers the reports need.  Applied to every sanitized
  # target so they cannot drift apart.
  add_compile_options(-O1)
  target_compile_options(melee_prototype PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(melee_prototype PRIVATE -fsanitize=address,undefined)
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
    decomp/assets/hsd_convert.c
    decomp/sdk_math.c
    decomp/hsd_port_stubs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/archive.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/aobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/class.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/cobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/dobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/id.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/jobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/memory.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/mobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/mtx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/object.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/objalloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/pobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/robj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/spline.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/tobj.c)
target_include_directories(test_decomp_hsd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(test_decomp_hsd PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -w -ffunction-sections -fdata-sections -DPORT_PC=1
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(test_decomp_hsd PRIVATE m)
target_link_options(test_decomp_hsd PRIVATE -m32 "LINKER:--gc-sections")
add_test(NAME decomp_hsd COMMAND test_decomp_hsd)
set_tests_properties(decomp_hsd PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------------------------------------------------------------------
# S5 (ADR-0013): the software AX mixer unit test.  Synthesizes its own ADPCM
# fixture and host ARAM, so it needs no disc, GPU or audio device.
add_executable(test_audio
    tests/test_audio.c
    audio/ax_hle.c
    audio/ax_mixer.c
    platform/ssm.c
    platform/os.c
    platform/complete.c
    decomp/boot/boot_triage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/os/OSArena.c
    ${MELEE_AX_SOURCES})
set_source_files_properties(${MELEE_AX_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(test_audio BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(test_audio SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(test_audio PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -Wall -Wextra -Wpedantic -fgnu89-inline
    -ffunction-sections -fdata-sections -DPORT_PC=1 -D_GNU_SOURCE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(test_audio PRIVATE m)
target_link_options(test_audio PRIVATE
    -m32 -malign-double "LINKER:--gc-sections")
add_test(NAME audio COMMAND test_audio)
set_tests_properties(audio PROPERTIES PASS_REGULAR_EXPRESSION "test_audio: PASS")

# ---------------------------------------------------------------------------
# S3 (P-609): asset pipeline sweep.  Every Pl*Nr archive, one stage and the
# common scene archives load through the compiled loader after the platform
# converter has converted them; `stats.ok` fails the test on any relocation
# target the converter did not understand (desync detector).
add_executable(test_decomp_assets
    # One domain per translation unit; shared helpers in asset_common.c.
    tests/test_decomp_assets.c
    tests/asset_common.c
    tests/test_assets_fighters.c
    tests/test_assets_items.c
    tests/test_assets_stages.c
    tests/test_assets_scenes.c
    platform/disc.c
    decomp/assets/hsd_convert.c
    decomp/sdk_math.c
    decomp/hsd_port_stubs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/archive.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/aobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/class.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/cobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/dobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/id.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/jobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/list.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/memory.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/mobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/mtx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/object.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/objalloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/pobj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/robj.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/spline.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/tobj.c)
target_include_directories(test_decomp_assets PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(test_decomp_assets PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -w -ffunction-sections -fdata-sections -DPORT_PC=1
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(test_decomp_assets PRIVATE m)
target_link_options(test_decomp_assets PRIVATE -m32 "LINKER:--gc-sections")
# P-757, the DWARF cross-check.  The converter's walkers hand-transcribe field
# offsets the compiler already knows exactly, and a wrong one is silent -- it
# swaps the wrong four bytes and corrupts a struct in whatever scene happens to
# use it (P-746).  `tools/dwarf_types.c` exists only so the compiler emits
# debug info for every type the converter walks; it must carry the *same*
# layout flags as the rest of the port, or the offsets it reports are not the
# ones the running game uses.  `-g3 -fno-eliminate-unused-debug-types` is what
# makes the set of types explicit instead of "whatever this TU happened to
# reference".
add_library(melee_dwarf_types OBJECT tools/dwarf_types.c)
target_include_directories(melee_dwarf_types PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(melee_dwarf_types PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -w -g3 -fno-eliminate-unused-debug-types -DPORT_PC=1
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
add_test(NAME decomp_layout
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/dwarf_check.sh
            $<TARGET_OBJECTS:melee_dwarf_types>
            ${CMAKE_CURRENT_BINARY_DIR}/dwarf-layout.json
            ${CMAKE_CURRENT_SOURCE_DIR}/decomp/assets/hsd_convert.c
            # Ratchet, like MELEE_COVERAGE_FLOOR: the number of walkers with a
            # `DWARF:` annotation may only go up.  55 as of 2026-09-17, when
            # conv_ft_parts_block (`ftData_x8`) landed for P-842.
            55)
set_tests_properties(decomp_layout PROPERTIES
    PASS_REGULAR_EXPRESSION "dwarf_check: (PASS|SKIP)"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 120)

# P-502 gate zero: MWCC allocates bit-fields from the most significant bit and
# Clang ignores the scalar_storage_order attribute that reproduced that
# (ADR-0022), so the wasm build decoded subaction commands wrongly while
# compiling cleanly.  The port now states the layout instead
# (native/decomp/shim/decomp_cmd_bits.h, read through CMD_U); this walks every
# field of every command struct and compares it with what the console reads.
# It is compiler-independent, so it is also the test Emscripten must pass.
add_executable(test_bit_order tests/test_bit_order.c)
target_include_directories(test_bit_order BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(test_bit_order PRIVATE
    -m32 -malign-double -w ${MELEE_DECOMP_LAYOUT_OPTIONS} -DPORT_PC=1
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_options(test_bit_order PRIVATE -m32 -malign-double)
add_test(NAME bit_order COMMAND test_bit_order)
set_tests_properties(bit_order PROPERTIES
    PASS_REGULAR_EXPRESSION "test_bit_order: PASS")

add_test(NAME decomp_assets COMMAND test_decomp_assets)
set_tests_properties(decomp_assets PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PASS_REGULAR_EXPRESSION "decomp_assets: (PASS|SKIP)"
    ENVIRONMENT "MELEE_ASSET_CACHE=${CMAKE_CURRENT_BINARY_DIR}/asset-cache")

# ---------------------------------------------------------------------------
# P-688: the decompiled atanf fallback (src/melee/lb/lbtrigf.c) must be
# bit-identical to an independent, explicitly-rounded transcription over a
# deterministic sweep of the float encoding space.  Without the fallback the
# link silently picks glibc's atanf and the test fails on the divergent inputs.
add_executable(test_decomp_trig
    tests/test_decomp_trig.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/melee/lb/lbtrigf.c)
target_include_directories(test_decomp_trig PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(test_decomp_trig PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -w -ffunction-sections -fdata-sections -DPORT_PC=1
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(test_decomp_trig PRIVATE m)
target_link_options(test_decomp_trig PRIVATE -m32 "LINKER:--gc-sections")
add_test(NAME decomp_trig COMMAND test_decomp_trig)
set_tests_properties(decomp_trig PROPERTIES
    PASS_REGULAR_EXPRESSION "decomp_trig: PASS")

# ---------------------------------------------------------------------------
# S1 (P-604): boot skeleton.  Compile the decompilation's own main()
# (src/melee/gm/gmmain.c, renamed gm_main) for the host with the platform
# layer stubbed, and stop in a controlled way with a triage log.
#
# The whole game is compiled 32-bit (ADR-0012) behind the shim (ADR-0011);
# upstream warnings are suppressed because src/ must not be modified.  Three
# pure-C SDK TUs are compiled from extern/dolphin (never edited): the real SDN
# heap/arena and the Mtx44 projection helpers.  Files excluded on the host:
#   src/MSL/            glibc provides libc/math (ADR-0011); trigf.c and
#                       math_data.c are the P-688 exception (ADR-0018)
#   src/MetroTRK/       GameCube debugger transport
#   Runtime/__mem.c     MSL memcpys; glibc provides them
#   baselib/debug.c     MSL FILE internals; replaced by decomp/debug_port.c
#   sislib_font.c,
#   hsd_3915.c,
#   hsd_3A76.c          need generated font atlases (decomp build artifacts,
#                       ADR-0005); decomp/fonts.c fills them at runtime (S6)
#
# S5: baselib/axdriver.c is compiled (it drives the AX voice layer, not the
# DSP); the DSP/AI boundary is native/audio/ax_hle.c and the asm AXFX files
# are replaced by native/decomp/axfx/axfx_port.c.
#
# The real backends vs log-only stubs are listed in
# native/AI/learnings/decomp_boot.md.
file(GLOB_RECURSE MELEE_DECOMP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/*.c)
list(FILTER MELEE_DECOMP_SOURCES EXCLUDE REGEX
    "/src/(MSL|MetroTRK)/")
list(REMOVE_ITEM MELEE_DECOMP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/Runtime/__mem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/debug.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/sislib_font.c)
list(APPEND MELEE_DECOMP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/os/OSArena.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/os/OSAlloc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/mtx/mtx44.c
    # S3: the real DVDFS path/FST logic and the ARQ DMA queue; the drive and
    # ARAM hardware under them live in native/platform/{dvd,ar}.c.
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/dvd/dvdfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/src/dolphin/ar/arq.c
    ${MELEE_AX_SOURCES})

list(APPEND MELEE_DECOMP_SOURCES
    # P-685: host THP movie decoder replacing the MWCC-only extern THPDec.c.
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/thp_dec.c)

list(APPEND MELEE_DECOMP_SOURCES
    # P-688 (owner decision 2026-09-14): compile the console MSL trig TU
    # despite the src/MSL/ exclusion, so sinf/cosf/tanf match the retail
    # tables.  msl_port.c supplies fabsf__Ff and runs the table init.
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/MSL/trigf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/MSL/math_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/msl_port.c)
add_library(melee_decomp_game OBJECT ${MELEE_DECOMP_SOURCES})
target_include_directories(melee_decomp_game BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
# G-150: the game's non-ASCII string literals (character names, menu text)
# are stored in the decompilation as UTF-8, and the GameCube build converts
# them with sjiswrap (configure.py --sjiswrap) so MWCC emits Shift-JIS bytes.
# HSD_SisLib_803A67EC looks those bytes up two at a time as SJIS, so left as
# UTF-8 every lookup fails and the text renders as nothing.  Ask GCC for the
# same execution charset; Shift-JIS is ASCII-compatible, so plain literals
# are unchanged.
target_compile_options(melee_decomp_game PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -w -fgnu89-inline -ffunction-sections -fdata-sections
    -fexec-charset=CP932
    -Wno-error=incompatible-pointer-types -DPORT_PC=1
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/melee/gm/gmmain.c
    PROPERTIES COMPILE_DEFINITIONS "main=gm_main")

# S3: archive.c defines the real HSD_ArchiveParse; the converter TU implements
# the platform entry point.  Every other TU sees the shim's rename.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/archive.c
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/assets/hsd_convert.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_ARCHIVE_INTERNAL=1")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/cobj.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_COBJ_INTERNAL=1")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/jobj.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_JOBJ_INTERNAL=1")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/sysdolphin/baselib/sislib.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_SISLIB_INTERNAL=1")
# P-847: lbmthp.c defines the movie player entry points the Unbound opening
# interposes, and gm_1A36.c defines the button read it spends.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/melee/lb/lbmthp.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_MTHP_INTERNAL=1")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/melee/gm/gm_1A36.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_GM_INPUT_INTERNAL=1")
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src/melee/lb/lbaudio_ax.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_AUDIO_AX_INTERNAL=1")

# ---------------------------------------------------------------- mods
#
# The mod system (ADR-0026).  Melee Unbound's own features are not privileged
# port code: they are a mod, loaded through the same ABI and the same registry
# a third-party mod uses, and MELEE_NO_MODS=1 turns them off -- which is the
# vanilla-parity configuration the regression suite runs in.
#
# Two bindings, one registry: a `.wasm` dropped into mods/ on the desktop
# (ADR-0027, WAMR), and the same sources compiled straight in for the browser,
# which has no loader and where Unbound is the only mod.
add_subdirectory(mod/wamr)

add_library(melee_mod STATIC
    mod/mod.c
    mod/mod_cobj.c
    mod/mod_menu.c
    mod/mod_opening.c
    mod/mod_wasm.c)
target_include_directories(melee_mod BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../mods/include)
target_include_directories(melee_mod SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(melee_mod PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS}
    ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -Wall -Wextra -Wpedantic -fgnu89-inline -ffunction-sections -fdata-sections
    -D_GNU_SOURCE -DMELEE_MOD_BINDING_WASM=1
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
# The interposer defines the functions the shim renames, so it -- like cobj.c
# itself -- must see the real symbols.
set_source_files_properties(mod/mod_cobj.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_COBJ_INTERNAL=1")
set_source_files_properties(mod/mod_menu.c
    PROPERTIES COMPILE_DEFINITIONS "MELEE_JOBJ_INTERNAL=1;MELEE_SISLIB_INTERNAL=1")
set_source_files_properties(mod/mod_opening.c
    PROPERTIES COMPILE_DEFINITIONS
    "MELEE_MTHP_INTERNAL=1;MELEE_GM_INPUT_INTERNAL=1;MELEE_AUDIO_AX_INTERNAL=1")
target_link_libraries(melee_mod PUBLIC wamr_vm)

# mods/unbound/unbound.wasm: the default mod, built from the same sources the
# browser compiles in.  Freestanding wasm32 -- no wasi-sdk and no emsdk, just
# the clang and wasm-ld already on the machine.
set(MELEE_UNBOUND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../mods/unbound/src/unbound.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../mods/unbound/src/widescreen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../mods/unbound/src/credits.c)
find_program(MELEE_WASM_CLANG NAMES clang)
if(MELEE_WASM_CLANG)
  add_custom_command(
      OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/../mods/unbound/unbound.wasm
      COMMAND ${MELEE_WASM_CLANG} --target=wasm32 -nostdlib -O2 -std=c11
              -Wall -Wextra -Wpedantic -Werror
              -I${CMAKE_CURRENT_SOURCE_DIR}/../mods/include
              -Wl,--no-entry -Wl,--export-memory -Wl,--stack-first
              -o ${CMAKE_CURRENT_SOURCE_DIR}/../mods/unbound/unbound.wasm
              ${MELEE_UNBOUND_SOURCES}
      DEPENDS ${MELEE_UNBOUND_SOURCES}
              ${CMAKE_CURRENT_SOURCE_DIR}/../mods/include/unbound_mod.h
              ${CMAKE_CURRENT_SOURCE_DIR}/../mods/include/unbound_abi.h
      COMMENT "Building mods/unbound/unbound.wasm"
      VERBATIM)
  add_custom_target(unbound_mod ALL
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../mods/unbound/unbound.wasm)
else()
  message(WARNING "clang not found: mods/unbound/unbound.wasm will not be built")
endif()

add_executable(melee_decomp_boot
    decomp/boot/boot_main.c
    decomp/boot/boot_triage.c
    decomp/boot/match_boot.c
    decomp/debug_port.c
    decomp/sdk_math.c
    decomp/assets/hsd_convert.c
    decomp/gx/gx_hle.c
    platform/os.c
    platform/gx_vi.c
    platform/dvd.c
    platform/disc.c
    platform/ar.c
    platform/ssm.c
    platform/sem.c
    platform/hps.c
    platform/complete.c
    platform/card.c
    platform/pad_card.c
    audio/ax_hle.c
    audio/ax_mixer.c
    audio/wav.c
    decomp/fonts.c
    platform/misc.c
    $<TARGET_OBJECTS:melee_decomp_game>)
target_include_directories(melee_decomp_boot PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/../mods/include)
target_include_directories(melee_decomp_boot SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(melee_decomp_boot PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -Wall -Wextra -Wpedantic -fgnu89-inline
    -ffunction-sections -fdata-sections -rdynamic -D_GNU_SOURCE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(melee_decomp_boot PRIVATE melee_mod m ${CMAKE_DL_LIBS})
target_link_options(melee_decomp_boot PRIVATE
    -m32 -malign-double -rdynamic "LINKER:--gc-sections")
add_test(NAME decomp_boot
    COMMAND melee_decomp_boot --boot-frames 5 --boot-timeout 5)
set_tests_properties(decomp_boot PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[boot\\] summary"
    ENVIRONMENT "MELEE_NO_CARD=1"
    TIMEOUT 30)
# S4: the deterministic scripted match (GM_DEBUG_VS, Link/Mario on Final
# Destination, built-in 900-frame input script).  The position at the 600-frame
# budget is a stable regression value (two runs are byte-identical).
add_test(NAME decomp_match
    COMMAND melee_decomp_boot --boot-frames 600 --boot-timeout 90 --boot-match 20)
# S5.5: the compiled AX stack must render byte-identical PCM for the same
# scripted match (two runs, WAV compare + hash).
add_test(NAME decomp_audio
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_determinism.sh
            $<TARGET_FILE:melee_decomp_boot>
            ${CMAKE_CURRENT_BINARY_DIR}/audio-determinism)
set_tests_properties(decomp_audio PROPERTIES
    PASS_REGULAR_EXPRESSION "audio_determinism: PASS"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    ENVIRONMENT "MELEE_NO_CARD=1"
    TIMEOUT 240)
# The pinned position moved from (14.20,41.04) to (15.95,44.89) in P-824,
# and that is a fix rather than a drift: `ftData->x50`, the fighter's
# player-nudge box, was never byte-swapped, so the overlap radius every
# fighter shoved others with was a byte-reversed float -- 2.7e23 for Donkey
# Kong and Ness, a denormal near zero for everyone else.  With it converted,
# the two fighters in this scripted match push apart the way they should, so
# the trajectory legitimately differs.  Verified by reverting only the
# converter hunk and watching the old value come back exactly.
set_tests_properties(decomp_match PROPERTIES
    PASS_REGULAR_EXPRESSION "slot 0 stocks=3 dmg=0 pos=\\(15\\.95,44\\.89,0\\.00\\)"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    ENVIRONMENT "MELEE_NO_CARD=1"
    TIMEOUT 240)
# Combat smoke test: MELEE_HIT_TEST makes p0 jab while p1 walks into it, so a
# hitbox command must spawn a capsule, collide, and apply damage.  Guards the
# command -> hitbox -> collision -> damage path (hitboxes never activated when
# `spawn_hitbox_skip` read the wrong bit).
add_test(NAME decomp_hit
    COMMAND melee_decomp_boot --boot-frames 600 --boot-timeout 90 --boot-match 20)
set_tests_properties(decomp_hit PROPERTIES
    ENVIRONMENT "MELEE_HIT_TEST=1;MELEE_NO_CARD=1"
    PASS_REGULAR_EXPRESSION "\\[match\\] hit: slot [01] damage=[1-9]"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 240)
# Projectile specials end to end: MELEE_ITEM_TEST has p0 fire neutral-B on a
# cycle while p1 walks into the line of fire, so the run covers the whole
# chain -- subaction script, article spawn, article attributes, particles,
# collision, damage.  Each link in it was broken in turn and each failure
# looked like something else:
#   P-739/G-178  the attribute walk overran into the special-move command
#                scripts, so no script ran and no article ever spawned
#   P-742/G-179  the particle bank's HSD_PSCmdList headers were big-endian,
#                so one effect asked for ~4e8 particles and killed the heap;
#                and the hand-inlined GXWGFifo stores in psdisp.c wrote the
#                hardware FIFO address and segfaulted
#   P-743        Article::x4_specialAttributes was converted for food items
#                only, so the arrow launched at 2.67e23 and hit nothing
# Asserting damage rather than the spawn is what makes this cover all four:
# the arrow spawned long before it could fly, and flew before it could hit.
add_test(NAME decomp_projectile
    COMMAND melee_decomp_boot --boot-frames 900 --boot-timeout 150 --boot-match 20)
set_tests_properties(decomp_projectile PROPERTIES
    ENVIRONMENT "MELEE_ITEM_TEST=1;MELEE_NO_CARD=1"
    PASS_REGULAR_EXPRESSION "\\[match\\] hit: slot [01] damage=[1-9]"
    # ctest's PASS_REGULAR_EXPRESSION alone ignores the exit status, so the
    # run could match and then die; the particle crash happens after the first
    # hit.  Fail on the panic explicitly.
    FAIL_REGULAR_EXPRESSION "PANIC|assertion \"adr\" failed"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 300)
# Shield bubble: MELEE_SHIELD_TEST holds L from frame 260.  efLib_Update takes
# the bubble's scale from the shield joint's matrix, so an infinity or NaN
# there is a bubble nobody can see -- which is what happened while the guard
# blend pose (ftData->x20) was left big-endian: every joint in it read
# scale = 4.6006e-41, i.e. 1.0f byte-reversed, and blending it into the
# fighter poisoned the joint (P-747/G-182).  Assert the matrix is finite while
# the shield is up; the harness prints POSE BAD instead on the old data.
# 700 frames, not 290: the CPU reaches the shielding fighter around 320 and
# grabs, which is the P-748 path (Link's hookshot chain joints).  The
# FAIL_REGULAR_EXPRESSION below is what covers that.
add_test(NAME decomp_shield
    COMMAND melee_decomp_boot --boot-frames 700 --boot-timeout 150 --boot-match 20)
set_tests_properties(decomp_shield PROPERTIES
    ENVIRONMENT "MELEE_SHIELD_TEST=1;MELEE_NO_CARD=1"
    PASS_REGULAR_EXPRESSION "\\[shield\\] frame=[0-9]+ guard pose sane"
    FAIL_REGULAR_EXPRESSION "POSE BAD|PANIC|assertion"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 240)
# HUD stock icons: the per-player icon frame comes from gm_80168BF8 ->
# gm_80168B34.  Both decompiled functions relied on MWCC register leftovers
# (uninitialized `base`, missing `return`), so on GCC every player asked for
# frame 0 (Captain Falcon).  The harness checks the direct base values and
# that the two live players (Link vs Mario) get different frames.
add_test(NAME decomp_icons
    COMMAND melee_decomp_boot --boot-frames 600 --boot-timeout 90 --boot-match 20)
set_tests_properties(decomp_icons PROPERTIES
    ENVIRONMENT "MELEE_ICON_TEST=1;MELEE_NO_CARD=1"
    PASS_REGULAR_EXPRESSION "\\[icons\\] .*distinct=1"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 240)
# Match end -> 1P "clear" overlay.  gmvs.c's fn_8016D634 hands the finished
# match to gmregclear.c's fn_80180630, which builds the screen-blur GObj with
# lb_800138EC.  That function has no `return` in the decompilation, so GCC
# handed back NULL and the very next call (lb_800138D8) dereferenced it: every
# 1P stage segfaulted one frame after the "GAME!!" announcer (G-145).
# MELEE_GAMEOVER_TEST sets the two 1P rule bits onEnterDebugVs leaves clear
# (`x4_4`, stock match) on the live VS controller and zeroes player 2's
# stocks, which is the same OUTCOME_ELIMINATION the last KO produces.
add_test(NAME decomp_gameover
    COMMAND melee_decomp_boot --boot-frames 900 --boot-timeout 180 --boot-match 20)
set_tests_properties(decomp_gameover PROPERTIES
    ENVIRONMENT "MELEE_GAMEOVER_TEST=1;MELEE_NO_CARD=1"
    PASS_REGULAR_EXPRESSION "\\[gameover\\] clear scene active"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 300)
# P-759, the soak: the same headless match over many RNG seeds.  Until P-751
# the RNG was frozen, so every boot replayed one identical game and whole
# regions of the state space were never entered; unfreezing it turned up three
# latent crashes on the first arbitrary seed, and a 40-seed hand-sweep turned
# up P-762 at a 1-in-6 rate in 50 seconds.
#
# The seeds here are derived from a fixed base, so this case is reproducible
# like every other test -- it is a regression test, not a lottery.  Discovery
# is the same script with `MELEE_SOAK_SEEDS=200 MELEE_SOAK_SEED_BASE=random`,
# run by hand or nightly.  MELEE_SOAK_SEED_LIST pins the seven seeds that hit
# P-762 (Great Bay's unwalked model tree), so the fix cannot silently regress.
#
# Note this is the one test that deliberately overrides the blanket
# `MELEE_RNG_SEED=tick` set at the bottom of this file: soak.sh sets the seed
# per child, which is the entire point.  Do not "fix" that.
add_test(NAME decomp_soak
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/soak.sh
            $<TARGET_FILE:melee_decomp_boot>
            ${CMAKE_CURRENT_BINARY_DIR}/soak)
# MELEE_SOAK_JOBS is pinned rather than left at `nproc`: a boot that misses its
# --boot-timeout is reported as a hang, and running nproc children *inside* a
# ctest run that is already busy oversubscribes the machine enough to cause
# exactly that.  Seen as a one-off `decomp_soak` failure that passed on its own
# immediately after.  Discovery sweeps run by hand can still use every core.
set_tests_properties(decomp_soak PROPERTIES
    ENVIRONMENT "MELEE_SOAK_SEEDS=8;MELEE_SOAK_JOBS=3;MELEE_SOAK_SEED_LIST=0x612f394c 0x1e544e69 0x75fb59ae 0x13740648 0x4bd74cd4 0x13007f72 0x2a7e5ed4"
    PASS_REGULAR_EXPRESSION "soak: PASS"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    TIMEOUT 600)
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
    decomp/assets/hsd_convert.c
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
    platform/ar.c
    platform/ssm.c
    platform/sem.c
    platform/hps.c
    platform/complete.c
    platform/card.c
    platform/pad_card.c
    audio/ax_hle.c
    audio/ax_mixer.c
    decomp/fonts.c
    platform/misc.c
    $<TARGET_OBJECTS:melee_decomp_game>)
target_include_directories(test_decomp_render BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../mods/include)
target_include_directories(test_decomp_render SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
target_compile_options(test_decomp_render PRIVATE
    -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
    -Wall -Wextra -Wpedantic -Wno-overlength-strings -fgnu89-inline
    -ffunction-sections -fdata-sections -rdynamic -D_GNU_SOURCE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
target_link_libraries(test_decomp_render PRIVATE melee_mod m EGL GLESv2
    ${CMAKE_DL_LIBS})
target_link_options(test_decomp_render PRIVATE
    -m32 -malign-double -rdynamic "LINKER:--gc-sections")
add_test(NAME decomp_render COMMAND test_decomp_render
    --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_render.bmp)
set_tests_properties(decomp_render PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PASS_REGULAR_EXPRESSION "decomp_render: PASS")
add_test(NAME decomp_gx_direct COMMAND test_decomp_render --direct)
set_tests_properties(decomp_gx_direct PROPERTIES
    PASS_REGULAR_EXPRESSION "direct: PASS")
# P-615: GXCopyTex RGB565 capture + GXSetZTexture depth erase, headless EGL.
add_test(NAME decomp_efb COMMAND test_decomp_render --efb)
set_tests_properties(decomp_efb PROPERTIES
    PASS_REGULAR_EXPRESSION "efb: PASS")
# P-619: stage asset path (Gr*.dat map_head conversion + stage camera/lights)
# with a posed fighter placed on the stage.
add_test(NAME decomp_stage COMMAND test_decomp_render
    --stage GrNBa.dat --fighter PlMrNr.dat
    --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_stage.bmp)
set_tests_properties(decomp_stage PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PASS_REGULAR_EXPRESSION "decomp_render: PASS")
add_test(NAME decomp_stage_cam COMMAND test_decomp_render
    --stage GrNBa.dat --fighter PlMrNr.dat --stage-cam)
set_tests_properties(decomp_stage_cam PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PASS_REGULAR_EXPRESSION "decomp_render: PASS")
# P-677: render the parity matrix (characters, stages, GX fixtures) and write
# a pass/fail markdown report; SKIPs cleanly without the disc image.
add_test(NAME decomp_parity
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity_matrix.sh
            $<TARGET_FILE:test_decomp_render>
            ${CMAKE_CURRENT_BINARY_DIR}/parity-report.md)
set_tests_properties(decomp_parity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PASS_REGULAR_EXPRESSION "parity: (PASS|SKIP)"
    TIMEOUT 300)
# P-619 regression: stage -> model mode switch (viewer M key) must reload the
# remembered model, not the first archive in the list.
add_test(NAME decomp_stage_toggle COMMAND test_decomp_render
    --stage GrNBa.dat --draw-first 2 --toggle-mode 1 --no-gl)
set_tests_properties(decomp_stage_toggle PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    PASS_REGULAR_EXPRESSION "decomp_render: PASS")
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
  add_executable(melee
      decomp/render/viewer_main.c
      decomp/render/viewer_ui.c
      decomp/render/viewer_input.c
      decomp/render/viewer_dump.c
      decomp/render/render_scene.c
      decomp/render/hud.c
      decomp/render/sdl_audio.c
      decomp/hsd/hsd_scene.c
      decomp/assets/hsd_convert.c
      decomp/gx/gx_hle.c
      decomp/gx/gx_gl.c
      decomp/debug_port.c
      decomp/sdk_math.c
      decomp/boot/boot_triage.c
      decomp/boot/match_boot.c
      gx/texture.c
      hsd/light.c
      platform/disc.c
      platform/os.c
      platform/gx_vi.c
      platform/dvd.c
      platform/ar.c
      platform/ssm.c
      platform/sem.c
      platform/hps.c
      platform/complete.c
      platform/card.c
    platform/pad_card.c
      audio/ax_hle.c
      audio/ax_mixer.c
        decomp/fonts.c
      platform/misc.c
      $<TARGET_OBJECTS:melee_decomp_game>)
  target_include_directories(melee BEFORE PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/../mods/include)
  target_include_directories(melee SYSTEM PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/src
      ${CMAKE_CURRENT_SOURCE_DIR}/../decomp/extern/dolphin/include)
  target_compile_options(melee PRIVATE
      -m32 -malign-double ${MELEE_32BIT_FP_OPTIONS} ${MELEE_DECOMP_UB_OPTIONS} ${MELEE_DECOMP_LAYOUT_OPTIONS}
      -Wall -Wextra -Wpedantic -Wno-overlength-strings -fgnu89-inline
      -ffunction-sections -fdata-sections -rdynamic -D_GNU_SOURCE
      -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
  # SDL3 requires a 1-byte _Bool, so the viewer TU opts out of the bool=int
  # shim (it never calls a decomp function with a bool parameter).
  set_source_files_properties(
      decomp/render/viewer_main.c
      decomp/render/viewer_ui.c
      decomp/render/viewer_input.c
      decomp/render/viewer_dump.c
      decomp/render/sdl_audio.c
      PROPERTIES COMPILE_DEFINITIONS "MELEE_SHIM_REAL_STDBOOL")
  target_link_libraries(melee PRIVATE
      melee_mod ${MELEE_SDL3_LIB32} m EGL GLESv2 ${CMAKE_DL_LIBS})
  add_dependencies(melee unbound_mod)
  target_link_options(melee PRIVATE
      -m32 -malign-double -rdynamic "LINKER:--gc-sections")
  # S6: the retail frontend flow, headless and deterministic: boot -> title ->
  # main menu -> VS. Mode -> CSS (Mario + CPU) -> stage select -> match.
  add_test(NAME decomp_frontend COMMAND melee
      --frontend --no-items
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_vs.txt
      --frames 2400 --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_frontend.bmp)
  set_tests_properties(decomp_frontend PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "scene=2"
      TIMEOUT 600
      ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;MELEE_NO_CARD=1")
  # P-624/G-090: the title must enter the retail state: mode GM_TITLE with
  # the logo animation past its opaque frame-0 reveal card, looping from
  # frame 400.  The frontend flow drives the boot with a card so the memory
  # card scene completes; the probe logs once the title logo is animating.
  # P-698: the 1P clear screen's "STAGE CLEAR" banner is a POBJ_SHAPEANIM
  # mesh whose morph targets HSD blends on the CPU (pobj.c drawShapeAnim).
  # Those pools are the one vertex arrays the GX display-list decoder never
  # reads, so they stay big-endian and the CPU reader has to swap; without
  # that every component decodes as a denormal, the mesh collapses to a point
  # and the band over its black backdrop reads as a solid black box -- the
  # owner's report.  The probe counts non-black pixels inside the banner.
  add_test(NAME decomp_clear_banner COMMAND melee
      --match --frames 360 --no-hud
      --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_clear_banner.bmp)
  set_tests_properties(decomp_clear_banner PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "\\[gameover\\] banner nonblack=[1-9][0-9][0-9][0-9]+"
      TIMEOUT 600
      ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;MELEE_NO_CARD=1;MELEE_GAMEOVER_TEST=1")
  # P-701: the Classic splash screen's stage-marker chain is a lit-only
  # material (no texture sampled; colour is entirely the two lit raster
  # channels).  conv_scene_desc's fog-array walk ran past the end of the
  # array and byte-swapped an HSD_PEDesc's first word, clearing its
  # colour-update bit, so HSD_SetupPEMode called GXSetColorUpdate(0) and the
  # whole chain drew nothing.  The probe counts non-black pixels across the
  # marker row.
  add_test(NAME decomp_intro_markers COMMAND melee
      --match --frames 420 --no-hud
      --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_intro_markers.bmp)
  set_tests_properties(decomp_intro_markers PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "\\[intro\\] markers nonblack=[1-9][0-9][0-9][0-9][0-9]+"
      TIMEOUT 600
      ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;MELEE_NO_CARD=1;MELEE_INTRO_TEST=1")
  # P-704: fn_80160DE8 reads the US fighter-name width table as
  # lbl_803B75F8[ckind + 0x21] -- one past the end of a 33-entry array, which
  # only resolves to lbl_803B767C because the console linker laid the four
  # tables out back to back.  GCC gives each its own section, so the read
  # returns 0.0 and HSD_SisLib_803A7548 stores an x-scale of 0, drawing every
  # glyph at zero width.  Only the US branch uses those offsets, so force the
  # saved language to US and probe the name row of the VS splash.
  add_test(NAME decomp_intro_names COMMAND melee
      --match --frames 420 --no-hud
      --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_intro_names.bmp)
  set_tests_properties(decomp_intro_names PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "\\[intro\\] names white=[1-9][0-9][0-9]+"
      TIMEOUT 600
      ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;MELEE_NO_CARD=1;MELEE_INTRO_TEST=1;MELEE_INTRO_US=1")
  # P-700: SIS text is built with vsnprintf(buf, -1, ...), which the console
  # reads as unbounded; glibc drops the final character, so every string lost
  # its last glyph ("VERY EASY" -> "VERY EAS" plus the two glyphs the parser
  # then ran into).  The 1P CSS "TOTAL HIGH SCORE" value is right-aligned, so
  # a dropped character shortens it on the left; this probes the strip its
  # leading digit occupies only when the string survives intact.
  add_test(NAME decomp_classic_text COMMAND melee
      --match --frames 700 --no-hud
      --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_classic_text.bmp)
  set_tests_properties(decomp_classic_text PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "\\[classic\\] score_lead=[1-9][0-9]+"
      TIMEOUT 600
      ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;MELEE_NO_CARD=1;MELEE_CLASSIC_TEST=1")
  # G-150: the game's non-ASCII literals (character names, menu text) must
  # reach the binary as Shift-JIS, which is what HSD_SisLib_803A67EC looks up.
  # The GameCube build gets that from sjiswrap; this build gets it from
  # -fexec-charset=CP932.  Compiled as UTF-8 the VS screen's fighter names
  # render as garbage or vanish, so assert the encoding at the source.
  add_test(NAME decomp_classic_names COMMAND melee
      --match --frames 700 --no-hud
      --shot ${CMAKE_CURRENT_BINARY_DIR}/decomp_classic_names.bmp)
  set_tests_properties(decomp_classic_names PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "\\[classic\\] name_lead=[0-9a-f]+ sjis=1"
      TIMEOUT 600
      ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;MELEE_NO_CARD=1;MELEE_CLASSIC_TEST=1;MELEE_VIEWER_TRIAGE=1")
  add_test(NAME decomp_title COMMAND melee
      --frontend --no-items
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_vs.txt
      --frames 400)
  set_tests_properties(decomp_title PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION
          "\\[title\\] mode=0 logos=1 logo_anim=[0-9.]+ ok=1"
      TIMEOUT 300
      ENVIRONMENT "SDL_VIDEODRIVER=offscreen;SDL_AUDIODRIVER=dummy;MELEE_CARD_DIR=${CMAKE_CURRENT_BINARY_DIR}/title-card;MELEE_TITLE_TEST=1;MELEE_VIEWER_TRIAGE=1")
  # P-685: the opening movie renders real frames and the title attract demo
  # survives to the end of an idle run (colanim opcodes and the ftData part
  # table used to crash both paths).
  add_test(NAME decomp_opening
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_opening.sh
              $<TARGET_FILE:melee>
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_vs.txt
              ${CMAKE_CURRENT_BINARY_DIR}/frontend-opening)
  set_tests_properties(decomp_opening PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "frontend_opening: PASS"
      TIMEOUT 600)
  # P-847: the Unbound opening movie plays before MvOpen.mth, a press skips
  # it into that movie rather than past it, and MELEE_NO_MODS=1 removes it.
  add_test(NAME decomp_unbound_opening
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/unbound_opening.sh
              $<TARGET_FILE:melee>
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_vs.txt
              ${CMAKE_CURRENT_BINARY_DIR}/unbound-opening)
  set_tests_properties(decomp_unbound_opening PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "unbound_opening: PASS"
      TIMEOUT 900)
  # P-646: the game's own card filesystem (hsd_3A94 pump) must create save
  # data on an empty card and reload it; two short frontend runs so the
  # card image stays inside the build directory.
  add_test(NAME decomp_frontend_card
      COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_card.sh
              $<TARGET_FILE:melee>
              ${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_vs.txt
              ${CMAKE_CURRENT_BINARY_DIR}/frontend-card)
  set_tests_properties(decomp_frontend_card PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
      PASS_REGULAR_EXPRESSION "frontend_card: PASS"
      TIMEOUT 300)
else()
  message(STATUS "32-bit SDL3 not found; skipping the product melee target")
endif()

# The port seeds HSD_Rand from the host clock (PortRandomSeed), because the
# virtual timebase made every playthrough identical -- see P-751.  Tests have
# the opposite requirement: audio_determinism replays a run and diffs it, the
# opening test replays the attract demo, and a harness that lands a hit has to
# land it every time.  Pin the seed for every test rather than per test, so a
# new test is deterministic by default; the shipped game keeps real entropy.
#
# "tick" is the bare virtual-timebase value the seed used to be, so each test
# keeps the exact stream it was written against.  Other streams do find real
# bugs -- MELEE_RNG_SEED=0x13371337 segfaults in HSD_Synth_80389334, P-752 --
# but those belong in their own issue with their own repro, not in a blanket
# reseed of every harness.
#
# P-844: the product boots into the opening movie, as the console does.  Every
# harness here counts frames from the boot -- the frontend input scripts, the
# title probe's frame-400 window, the match tests' frame-600 position -- so the
# movie would shift all of them by its length.  Pin the skip-intro boot for the
# suite the same way and for the same reason as the seed; the one test that is
# about the movie (decomp_opening) clears it for the run that needs it.
get_property(melee_all_tests DIRECTORY PROPERTY TESTS)
if(melee_all_tests)
  set_property(TEST ${melee_all_tests} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION "MELEE_RNG_SEED=set:tick")
  set_property(TEST ${melee_all_tests} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION "MELEE_NO_OPENING=set:1")
endif()

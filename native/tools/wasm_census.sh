#!/bin/sh
# W0 census: compile every product translation unit for wasm32 and link them.
#
# Not a product build -- there is no HTML shell, MEM1 is still mapped at
# 0x80000000, and the main loop does not yield -- but it is the reproducible
# evidence for P-502/W0: what compiles, what links, and what is missing.
#
#   source ~/projects/emsdk/emsdk_env.sh     # Emscripten 6.0.9, pinned
#   native/tools/wasm_census.sh [outdir]
#
# Flags translated from the native melee/melee_decomp_game targets:
#   dropped  -m32 -malign-double -msse2 -mfpmath=sse   (x86-only)
#   dropped  -fexec-charset=CP932                      (clang rejects it; W0 item)
#   added    -DPORT_WASM=1                             (layered on PORT_PC)
#   added    -Wno-error=incompatible-function-pointer-types
#            clang errors where GCC warns; the native build relaxes the GCC
#            spelling of the same thing
#   link     --use-port=sdl3 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2
#            plus linear memory sized past the end of MEM1 (2.25 GiB) with
#            growth off, so 0x80000000 is a valid index and the reservation is
#            ASYNCIFY, because the disc read is asynchronous in a browser and
#            the game calls it synchronously from inside nested scene loops;
#            the suspend unwinds the wasm stack and resumes when the range
#            arrives.  Its size and frame-time cost is the W2 measurement.
#   link     EMULATE_FUNCTION_POINTER_CASTS, because the decompilation stores
#            heterogeneous callbacks in one table by laundering them through
#            `Event`: `(GObj_RenderFunc) (Event) fn`.  PPC and x86 ignore the
#            surplus or missing arguments; wasm's `call_indirect` compares the
#            callee's type with the table entry and **traps**.  A census with
#            -Wcast-function-type-strict counts **130 of these across 49
#            files** -- it is an idiom, not a bug list, and ADR-0011 keeps
#            src/ read-only.  The flag makes wasm tolerate what the other two
#            targets tolerate.  Re-run the census before assuming it can go:
#              emcc -fsyntax-only <tu> -Wno-everything \
#                   -Wcast-function-type-strict -Wincompatible-function-pointer-types
#            a single up-front yes/no -- see os.c:map_gc_ram.  Growth is off
#            deliberately: Mozilla bug 1660420 reports Memory.grow failing when
#            the maximum is 4 GB, and we never need to grow.  MAXIMUM_MEMORY is
#            therefore not passed -- emcc only honours it with growth on, and
#            leaving it in the line read as load-bearing when it was dead.
#            gx_gl.c calls glBlitFramebuffer, which is WebGL2-only; emcc links
#            the WebGL1 library unless asked
set -e
W=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-/tmp/melee-wasm-census}
mkdir -p "$OUT/obj"
command -v emcc >/dev/null || { echo "emcc not on PATH; source emsdk_env.sh" >&2; exit 1; }

# mods/include carries the Unbound ABI headers.  The mod system is not
# optional in the browser -- there is no loader, so Unbound is compiled in
# (ADR-0026) -- and viewer_main.c includes unbound_abi.h unconditionally.
INCS="-I$W/native/decomp/shim -I$W/native -I$W/decomp/src -I$W/decomp/extern/dolphin/include -I$W/mods/include"
# -O2 on the *compile* only.  This started as a census script and never passed
# an optimisation flag, so every browser build until now was -O0, which is why
# the first in-game report was "too slow" (the native product build is
# RelWithDebInfo).  The link deliberately stays at -O0: `wasm-opt --fpcast-emu`
# -- the pass behind EMULATE_FUNCTION_POINTER_CASTS -- miscompiles under -O2
# and the link dies in the validator with
#   [wasm-validator error in function byn$fpcast-emu$NNNN]
#   unexpected false: call* param number must match
# Clang's -O2 is where most of the win is; wasm-opt is a second-order pass.
# Revisit if the casts ever go away or Binaryen fixes the interaction.
BASE="-O2 -w -Wno-error=incompatible-function-pointer-types -fgnu89-inline
      -ffunction-sections -fdata-sections -fno-strict-aliasing
      -DLINT -DPORT_PC=1 -DPORT_WASM=1
      -DMELEE_MOD_BINDING_NATIVE=1"
SHIM="-include $W/native/decomp/shim/decomp_shim.h"

# The product's sources: the decompiled game (minus the three the native build
# removes), the dolphin SDK files it appends, and the port's own layer.
{
  find "$W/decomp/src" -name '*.c' | grep -vE "/src/(MSL|MetroTRK)/" \
    | grep -vE "baselib/(debug|sislib_font)\.c$|Runtime/__mem\.c$"
  for f in os/OSArena os/OSAlloc mtx/mtx44 dvd/dvdfs ar/arq \
           ax/AX ax/AXAlloc ax/AXAux ax/AXSPB ax/AXVPB ax/AXCL \
           axfx/axfx axfx/delay; do
    echo "$W/decomp/extern/dolphin/src/dolphin/$f.c"
  done
  for f in decomp/thp_dec audio/sfx_debug decomp/axfx/axfx_port \
           decomp/render/viewer_main decomp/render/viewer_ui \
           decomp/render/viewer_input decomp/render/viewer_dump \
           decomp/render/render_scene decomp/render/hud \
           decomp/render/sdl_audio decomp/hsd/hsd_scene \
           decomp/assets/hsd_convert decomp/gx/gx_hle decomp/gx/gx_gl \
           decomp/debug_port decomp/sdk_math decomp/boot/boot_triage \
           decomp/boot/match_boot gx/texture hsd/light \
           platform/disc platform/os platform/gx_vi platform/dvd \
           platform/ar platform/ssm platform/sem platform/hps \
           platform/complete platform/card platform/pad_card \
           audio/ax_hle audio/ax_mixer decomp/fonts platform/misc \
           platform/melee_config \
           mod/mod mod/mod_cobj mod/mod_menu mod/mod_opening \
           mod/mod_builtin; do
    echo "$W/native/$f.c"
  done
  # Unbound itself.  The desktop build hands WAMR a .wasm from mods/; a page
  # has no loader for a second module, so the browser compiles the mod in and
  # binds it through mod_builtin.c (ADR-0026).  mod_wasm.c is deliberately
  # absent -- there is no WAMR here.
  for f in unbound widescreen credits; do
    echo "$W/mods/unbound/src/$f.c"
  done
} | sort -u > "$OUT/sources.txt"

echo "compiling $(wc -l < "$OUT/sources.txt") translation units"
: > "$OUT/failures.txt"
while read -r src; do
  obj="$OUT/obj/$(echo "$src" | sed "s|$W/||; s|/|_|g; s|\.c$|.o|")"
  extra=""
  case "$src" in
    */melee/gm/gmmain.c)  extra="-Dmain=gm_main" ;;
    */baselib/archive.c|*/assets/hsd_convert.c) extra="-DMELEE_ARCHIVE_INTERNAL=1" ;;
  esac
  # The shim renames the functions the mod layer interposes, so both sides of
  # each pair -- the decomp TU that defines the real one and the interposer
  # that defines the renamed one -- must be compiled with the rename off.
  # These mirror the set_source_files_properties() lines in CMakeLists.txt;
  # when one moves, so does the other.
  case "$src" in
    */baselib/cobj.c)     extra="$extra -DMELEE_COBJ_INTERNAL=1" ;;
    */baselib/jobj.c)     extra="$extra -DMELEE_JOBJ_INTERNAL=1" ;;
    */baselib/sislib.c)   extra="$extra -DMELEE_SISLIB_INTERNAL=1" ;;
    */melee/lb/lbmthp.c)  extra="$extra -DMELEE_MTHP_INTERNAL=1" ;;
    */melee/gm/gm_1A36.c) extra="$extra -DMELEE_GM_INPUT_INTERNAL=1" ;;
    */melee/lb/lbaudio_ax.c) extra="$extra -DMELEE_AUDIO_AX_INTERNAL=1" ;;
    */mod/mod_cobj.c)     extra="$extra -DMELEE_COBJ_INTERNAL=1" ;;
    */mod/mod_menu.c)
      extra="$extra -DMELEE_JOBJ_INTERNAL=1 -DMELEE_SISLIB_INTERNAL=1" ;;
    */mod/mod_opening.c)
      extra="$extra -DMELEE_MTHP_INTERNAL=1 -DMELEE_GM_INPUT_INTERNAL=1 -DMELEE_AUDIO_AX_INTERNAL=1" ;;
  esac
  case "$src" in
    */viewer_*.c|*/sdl_audio.c) extra="$extra -DMELEE_SHIM_REAL_STDBOOL" ;;
  esac
  case "$src" in
    */mods/unbound/src/*.c|*/mod/mod_builtin.c)
      extra="$extra -DUNBOUND_MOD_BUILTIN=1" ;;
  esac
  # shellcheck disable=SC2086
  emcc -c "$src" -o "$obj" --use-port=sdl3 $INCS $BASE $extra $SHIM 2>"$OUT/err" \
    || echo "$src: $(grep -m1 'error:' "$OUT/err" | sed 's/.*error: //')" >> "$OUT/failures.txt"
done < "$OUT/sources.txt"

nfail=$(wc -l < "$OUT/failures.txt")
echo "compile failures: $nfail"
[ "$nfail" -eq 0 ] || { cat "$OUT/failures.txt"; exit 1; }

# How the synchronous disc read suspends.  MELEE_WASM_SUSPEND=asyncify keeps
# the old mechanism for a browser without JSPI.
#
# JSPI is the default because Asyncify's cost here is enormous and had never
# been measured -- the note above called it "the W2 measurement" and nobody
# took it.  `emcc -sASYNCIFY_ADVISE` reports **36,334 instrumented functions**,
# i.e. essentially the whole program, each rewritten into a state machine that
# saves and restores its locals on every call.
#
# The reason it is that broad is worth knowing before anyone tries to trim it
# with ASYNCIFY_ONLY: the advice output begins "invoke_iii is an import that
# can change the state".  Those invoke_* trampolines come from
# EMULATE_FUNCTION_POINTER_CASTS, which this build needs because the
# decompilation launders callbacks through `Event`.  Every indirect call
# therefore leaves wasm through a JS import, Asyncify must assume any import
# may suspend, and the whole call graph is poisoned.  An ASYNCIFY_ONLY list
# cannot fix that without first removing the trampolines.
#
# JSPI uses the engine's own stack switching and instruments nothing:
# **14.5 MB of wasm becomes 4.5 MB.**  Verified by the owner in Chrome and
# Firefox, booting, streaming the disc and playing.  It needs a recent engine;
# where that is not available, MELEE_WASM_SUSPEND=asyncify still links.
case "${MELEE_WASM_SUSPEND:-jspi}" in
  jspi)     SUSPEND="-sJSPI" ;;
  asyncify) SUSPEND="-sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=65536" ;;
  *) echo "MELEE_WASM_SUSPEND must be jspi or asyncify" >&2; exit 1 ;;
esac
echo "suspend mechanism: ${MELEE_WASM_SUSPEND:-jspi}"

# The Unbound opening clip is read from the host filesystem by path
# (`mod_opening.c` resolves `mods/unbound/files/MvUnbound.mth` and hands it to
# `platform_disc_add_host_file`).  A page has no filesystem, so the file is
# embedded at the same path inside MEMFS and the resolver needs no special
# case -- the relative path lands on `/mods/...`, which is where this puts it.
# Absent, the build still links and the retail opening plays, which is exactly
# what `mod_opening.c` does when the file is missing on the desktop.
MOVIE="$W/mods/unbound/files/MvUnbound.mth"
EMBED=""
if [ -f "$MOVIE" ]; then
  EMBED="--embed-file $MOVIE@/mods/unbound/files/MvUnbound.mth"
  echo "embedding $(du -h "$MOVIE" | cut -f1) Unbound opening clip"
else
  echo "no mods/unbound/files/MvUnbound.mth; the retail opening will play"
fi

echo "linking"
# shellcheck disable=SC2086
emcc "$OUT"/obj/*.o -o "$OUT/melee.html" --use-port=sdl3 $EMBED \
  --shell-file "$W/native/tools/wasm_shell.html" \
  -sINITIAL_MEMORY=2415919104 -sALLOW_MEMORY_GROWTH=0 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2 \
  $SUSPEND -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
  -sEMULATE_FUNCTION_POINTER_CASTS=1 \
  -sEXPORTED_RUNTIME_METHODS=callMain,HEAPU8
echo "linked: $(du -h "$OUT/melee.wasm" | cut -f1) wasm -> $OUT/melee.html"

# Gate zero, checked on the target that broke it.  MWCC's MSB-first bit-field
# allocation is stated explicitly in native/decomp/shim/decomp_cmd_bits.h
# because Clang ignores the attribute that used to carry it; this recomputes
# that header from the decompilation and runs the field-by-field test through
# node, so a drift or a regression fails the build rather than the game.
echo "gate zero: command bit order"
"$W/native/tools/gen_cmd_bits.py" --check
# shellcheck disable=SC2086
emcc "$W/native/tests/test_bit_order.c" -o "$OUT/test_bit_order.js" \
  -I"$W/native/tests" $INCS $BASE $SHIM
node "$OUT/test_bit_order.js"

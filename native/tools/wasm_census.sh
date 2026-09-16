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
#            gx_gl.c calls glBlitFramebuffer, which is WebGL2-only; emcc links
#            the WebGL1 library unless asked
set -e
W=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-/tmp/melee-wasm-census}
mkdir -p "$OUT/obj"
command -v emcc >/dev/null || { echo "emcc not on PATH; source emsdk_env.sh" >&2; exit 1; }

INCS="-I$W/native/decomp/shim -I$W/native -I$W/decomp/src -I$W/decomp/extern/dolphin/include"
BASE="-w -Wno-error=incompatible-function-pointer-types -fgnu89-inline
      -ffunction-sections -fdata-sections -fno-strict-aliasing
      -DLINT -DPORT_PC=1 -DPORT_WASM=1"
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
           audio/ax_hle audio/ax_mixer decomp/fonts platform/misc; do
    echo "$W/native/$f.c"
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
  case "$src" in
    */viewer_*.c|*/sdl_audio.c) extra="$extra -DMELEE_SHIM_REAL_STDBOOL" ;;
  esac
  # shellcheck disable=SC2086
  emcc -c "$src" -o "$obj" --use-port=sdl3 $INCS $BASE $extra $SHIM 2>"$OUT/err" \
    || echo "$src: $(grep -m1 'error:' "$OUT/err" | sed 's/.*error: //')" >> "$OUT/failures.txt"
done < "$OUT/sources.txt"

nfail=$(wc -l < "$OUT/failures.txt")
echo "compile failures: $nfail"
[ "$nfail" -eq 0 ] || { cat "$OUT/failures.txt"; exit 1; }

echo "linking"
emcc "$OUT"/obj/*.o -o "$OUT/melee.js" --use-port=sdl3 \
  -sALLOW_MEMORY_GROWTH=1 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2
echo "linked: $(du -h "$OUT/melee.wasm" | cut -f1) wasm"

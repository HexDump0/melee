#!/bin/sh
# Cross-build the Windows port with i686-w64-mingw32.
#
#   native/tools/windows_build.sh [outdir] [sdl3-prefix]
#
# See native/AI/reference/windows.md for why each flag is load-bearing.  The
# three that are not obvious:
#
#   -mno-ms-bitfields   MinGW defaults to MS bitfield layout, which is not
#                       what this tree is laid out for.  Without it four
#                       ASSERT_SIZE checks fail -- and without -DLINT they
#                       would not even fail, they would just be wrong.
#   -DLINT              turns those 190 layout assertions on.  Never build
#                       this tree without it.
#   --large-address-aware
#                       os.c maps GC RAM at a fixed 0x80000000, which a 32-bit
#                       process cannot reach otherwise.
#
# Mods are compiled in through mod_builtin.c, the same binding the browser
# build uses (ADR-0026), so this needs no WAMR.
set -e
W=$(cd "$(dirname "$0")/../.." && pwd)

# The decomp submodule is checked out pristine; its #ifdef PORT_PC fixes live
# in patches/ and are applied to the work tree (ADR-0011).  The CMake build
# does this at configure time -- do it here too, or a clean checkout fails on
# the first patched TU (debug_font.inc, which only the patch #ifdefs away).
# Idempotent, so a tree that already has them is untouched.
"$W/scripts/apply_decomp_patches.sh"

OUT=${1:-/tmp/melee-win}
SDL=${2:-$OUT/sdl3-prefix}
CC=${CC:-i686-w64-mingw32-gcc}

[ -d "$SDL/include/SDL3" ] || {
  echo "no SDL3 at $SDL -- cross-build it first (windows.md)" >&2; exit 1; }

mkdir -p "$OUT/obj"

INCS="-I$W/native/decomp/shim -I$W/native -I$W/decomp/src
      -I$W/decomp/extern/dolphin/include -I$W/mods/include
      -I$W/native/third_party/khronos -I$SDL/include"
# Matches the native melee target's options, plus -mno-ms-bitfields.
#   -Wno-error=incompatible-pointer-types  the decompilation stores
#       heterogeneous callbacks in one slot (AXFXSetHooks takes
#       `unsigned long` where the caller has `size_t`); GCC 14 made this an
#       error and the native build relaxes it the same way.
#   -fexec-charset=CP932  the game's Shift-JIS string literals.
BASE="-O2 -m32 -malign-double -mno-ms-bitfields -msse2 -mfpmath=sse
      -fno-strict-aliasing -fgnu89-inline -std=gnu11 -w -DNDEBUG
      -ffunction-sections -fdata-sections -fexec-charset=CP932
      -Wno-error=incompatible-pointer-types
      -DLINT -DPORT_PC=1 -DMELEE_MOD_BINDING_NATIVE=1"
SHIM="-include $W/native/decomp/shim/decomp_shim.h"

# The product's sources, kept in step with native/tools/wasm_census.sh.
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
           decomp/boot/match_boot gx/texture gx/gl_api hsd/light \
           platform/disc platform/os platform/gx_vi platform/dvd \
           platform/ar platform/ssm platform/sem platform/hps \
           platform/complete platform/card platform/pad_card \
           audio/ax_hle audio/ax_mixer decomp/fonts platform/misc \
           platform/melee_config \
           mod/mod mod/mod_cobj mod/mod_menu mod/mod_opening \
           mod/mod_builtin; do
    echo "$W/native/$f.c"
  done
  for f in unbound widescreen credits; do
    echo "$W/mods/unbound/src/$f.c"
  done
} > "$OUT/tus.txt"

echo "compiling $(wc -l < "$OUT/tus.txt") translation units"
n=0
while read -r tu; do
  n=$((n + 1))
  obj="$OUT/obj/$(echo "$tu" | sed "s|$W/||; s|/|_|g; s|\.c$|.o|")"
  # SDL wants a 1-byte bool; the decomp shim otherwise makes bool an int.
  extra=""
  case "$tu" in
    */viewer_main.c|*/viewer_ui.c|*/viewer_input.c|*/viewer_dump.c|*/sdl_audio.c|*/gl_api.c)
      extra="-DMELEE_SHIM_REAL_STDBOOL" ;;
  esac
  case "$tu" in
    */mods/unbound/src/*) extra="$extra -DUNBOUND_MOD_BUILTIN=1" ;;
  esac
  # The per-file defines the native target sets with
  # set_source_files_properties; keep this list in step with CMakeLists.txt.
  case "$tu" in
    */melee/gm/gmmain.c)          extra="$extra -Dmain=gm_main" ;;
    */baselib/archive.c|*/assets/hsd_convert.c)
                                  extra="$extra -DMELEE_ARCHIVE_INTERNAL=1" ;;
    */baselib/cobj.c)             extra="$extra -DMELEE_COBJ_INTERNAL=1" ;;
    */baselib/jobj.c)             extra="$extra -DMELEE_JOBJ_INTERNAL=1" ;;
    */baselib/sislib.c)           extra="$extra -DMELEE_SISLIB_INTERNAL=1" ;;
    */melee/lb/lbmthp.c)          extra="$extra -DMELEE_MTHP_INTERNAL=1" ;;
    */melee/gm/gm_1A36.c)         extra="$extra -DMELEE_GM_INPUT_INTERNAL=1" ;;
    */melee/lb/lbaudio_ax.c)      extra="$extra -DMELEE_AUDIO_AX_INTERNAL=1" ;;
  esac
  # shellcheck disable=SC2086
  $CC $BASE $extra $INCS $SHIM -c "$tu" -o "$obj" || {
    echo "FAILED: $tu" >&2; exit 1; }
done < "$OUT/tus.txt"

echo "linking"
# dbghelp: boot_triage's stack walk.  winmm: SDL's timers/audio.
# --large-address-aware: see the header of this file.
# shellcheck disable=SC2086
# MELEE_WIN_LINK=static (default) links SDL in, so a release is one file and
# `melee.exe` cannot be started without its DLL.  It is also the GUI subsystem,
# because that is what SDL's static pkg-config asks for: no console window
# appears when the game is double-clicked, and the launcher still captures
# output because it reads the child's pipes rather than a console.
#
# MELEE_WIN_LINK=shared is the console build, which is what you want when
# iterating from a terminal on real Windows -- a GUI-subsystem process there
# has nowhere to print.  It needs SDL3.dll beside it.
#
# -static-libgcc and a static libwinpthread either way: MinGW links
# libwinpthread-1.dll dynamically by default, which is one more file to ship
# and one more "the program can't start because ... is missing" box.
LINKMODE=${MELEE_WIN_LINK:-static}
if [ "$LINKMODE" = "static" ]; then
  SDLLIBS=$(PKG_CONFIG_PATH="$SDL/lib/pkgconfig" pkg-config --static --libs sdl3 2>/dev/null)
  [ -n "$SDLLIBS" ] || SDLLIBS="-L$SDL/lib -lSDL3 -mwindows -lm -lkernel32 -luser32
      -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32
      -lsetupapi -lshell32 -ldinput8"
else
  SDLLIBS="-L$SDL/lib -lSDL3"
fi

# shellcheck disable=SC2086
$CC -m32 "$OUT"/obj/*.o -o "$OUT/melee.exe" \
  $SDLLIBS -ldbghelp \
  -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic \
  -Wl,--large-address-aware -Wl,--gc-sections
if [ "$LINKMODE" != "static" ]; then
  cp "$SDL/bin/SDL3.dll" "$OUT/" 2>/dev/null || true
fi
echo "built $(du -h "$OUT/melee.exe" | cut -f1) ($LINKMODE) -> $OUT/melee.exe"

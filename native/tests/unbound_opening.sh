#!/bin/sh
# P-847 regression: the Unbound opening movie, and the retail one behind it.
#
# The claim under test is not "a logo appeared".  It is that the clip is a
# real MTH streamed through the game's own player, that the Melee movie still
# plays afterwards untouched, that a button skips the clip *into* that movie
# rather than past it to the title, and that MELEE_NO_MODS=1 removes the whole
# thing.  Four runs, one for each:
#
#   1. mid-clip     -- the Unbound mark is on screen and the log says which
#                      movie is playing,
#   2. past the end -- the clip finishes on its own and MvOpen.mth renders,
#   3. skipped      -- a press ends the clip early and the mode is still
#                      GM_OPENING_MV (24), i.e. the press did not reach the
#                      scene's own Start/A branch,
#   4. vanilla      -- MELEE_NO_MODS=1 boots straight into MvOpen.mth.
#
# Usage: unbound_opening.sh <melee-binary> <frontend-input-script> <work-dir>
set -e

melee="$1"
input="$2"
work="$3"

rm -rf "$work"
mkdir -p "$work"
printf 'channels 1\n3000 * -\n' >"$work/idle.txt"
printf 'channels 1\n60 * a\n65 * -\n3000 * -\n' >"$work/skip.txt"

# The boot only reaches the movie once a save exists; without one the game's
# own lbCardGame_DecideGameMode claims the boot for the memory-card scene.
MELEE_NO_OPENING=1 MELEE_CARD_DIR="$work" SDL_VIDEODRIVER=offscreen \
    SDL_AUDIODRIVER=dummy \
    "$melee" --frontend --no-items --input "$input" --frames 400 \
    --shot "$work/create.bmp" >"$work/create.log" 2>&1
if [ ! -s "$work/card_a/file_000.gcm" ]; then
    echo "unbound_opening: FAIL (save file was not created)"
    exit 1
fi

run() {
    name="$1"
    script="$2"
    frames="$3"
    shift 3
    env "$@" MELEE_NO_OPENING=0 MELEE_VIEWER_TRIAGE=1 MELEE_CARD_DIR="$work" \
        SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        "$melee" --frontend --no-items --input "$work/$script" \
        --frames "$frames" --shot "$work/$name.bmp" \
        >"$work/$name.log" 2>&1
}

# Fraction of sampled pixels that are not near-black.  The Unbound clip is a
# mark on flat black, so its numbers are small but never zero; MvOpen.mth's
# montage fills the screen.
bright() {
    python3 - "$1" <<'PYEOF'
import struct
import sys

data = open(sys.argv[1], "rb").read()
offset = struct.unpack_from("<I", data, 10)[0]
width = struct.unpack_from("<i", data, 18)[0]
height = struct.unpack_from("<i", data, 22)[0]
bpp = struct.unpack_from("<H", data, 28)[0]
if bpp != 24:
    raise SystemExit("unexpected BMP depth %d" % bpp)
stride = ((width * bpp + 31) // 32) * 4
bright = total = 0
for y in range(0, height, 4):
    row = offset + y * stride
    for x in range(0, width, 4):
        p = row + x * 3
        if data[p] > 40 or data[p + 1] > 40 or data[p + 2] > 40:
            bright += 1
        total += 1
print("%.4f" % (bright / total))
PYEOF
}

# 1. mid-clip: the mark is up and the player is streaming our file.
run clip idle.txt 110
if ! grep -q 'playing MvUnbound.mth before MvOpen.mth' "$work/clip.log"; then
    echo "unbound_opening: FAIL (the Unbound clip never started)"
    exit 1
fi
# The Melee movie's own BGM must not be running under the Unbound logo: the
# scene starts it before the movie, so it is held back to the hand-off.
if ! grep -q 'holding track 0x3e until MvOpen.mth starts' "$work/clip.log"; then
    echo "unbound_opening: FAIL (the movie's music was not held back)"
    exit 1
fi
if grep -q 'opening: starting track' "$work/clip.log"; then
    echo "unbound_opening: FAIL (the music started during the clip)"
    exit 1
fi
clip_bright=$(bright "$work/clip.bmp")
case "$clip_bright" in
0.0000|0.000[0-2])
    echo "unbound_opening: FAIL (clip frame is blank: $clip_bright)"
    exit 1
    ;;
esac

# 2. past the end: the clip finishes itself and hands over to the real movie.
run after idle.txt 480
if ! grep -q 'clip finished at frame' "$work/after.log"; then
    echo "unbound_opening: FAIL (the clip did not end on its own)"
    exit 1
fi
if ! grep -q 'mode=24 scene=0' "$work/after.log"; then
    echo "unbound_opening: FAIL (never reached GM_OPENING_MV)"
    exit 1
fi
if ! grep -q 'opening: starting track 0x3e' "$work/after.log"; then
    echo "unbound_opening: FAIL (the movie's music never started)"
    exit 1
fi
after_bright=$(bright "$work/after.bmp")
if [ "$(printf '%s\n0.2000\n' "$after_bright" | sort -g | head -1)" \
        = "$after_bright" ]; then
    echo "unbound_opening: FAIL (MvOpen.mth is not rendering: $after_bright)"
    exit 1
fi

# 3. skipped: the press ends the clip and is spent, so the scene's own Start/A
#    branch does not also fire and drop us at the title.
run skip skip.txt 200
if ! grep -q 'opening: skipped at frame' "$work/skip.log"; then
    echo "unbound_opening: FAIL (the press did not skip the clip)"
    exit 1
fi
# The last mode the run reported: GM_TITLE here would mean the scene acted on
# the same press the splash did.
if [ "$(grep -o 'mode=[0-9]*' "$work/skip.log" | tail -1)" != "mode=24" ]; then
    echo "unbound_opening: FAIL (the skip left GM_OPENING_MV: $(
        grep -o 'mode=[0-9]*' "$work/skip.log" | tail -1))"
    exit 1
fi
if ! grep -q 'opening: starting track 0x3e' "$work/skip.log"; then
    echo "unbound_opening: FAIL (a skip left the movie without its music)"
    exit 1
fi
skip_bright=$(bright "$work/skip.bmp")

# 4. vanilla: MELEE_NO_MODS=1 is the parity configuration, so nothing of the
#    above may survive it.
run vanilla idle.txt 120 MELEE_NO_MODS=1
if grep -q 'unbound. opening' "$work/vanilla.log"; then
    echo "unbound_opening: FAIL (the clip ran with MELEE_NO_MODS=1)"
    exit 1
fi
if ! grep -q 'mode=24 scene=0' "$work/vanilla.log"; then
    echo "unbound_opening: FAIL (vanilla boot did not reach the movie)"
    exit 1
fi

echo "unbound_opening: clip bright=$clip_bright skip bright=$skip_bright" \
     "movie bright=$after_bright"
echo "unbound_opening: PASS"

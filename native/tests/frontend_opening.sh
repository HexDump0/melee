#!/bin/sh
# P-685 regression: the opening movie and the title's attract demo.
#
#   1. the regular frontend flow creates a save (the boot then skips the card
#      wait, so the later runs are deterministic),
#   2. an idle frontend run must reach the title (frame ~4) and its attract
#      demo (GM_OPENING_MV scene 1) and exit cleanly - this is where the
#      colanim opcode and ftData part-table endianness bugs crashed,
#   3. MELEE_OPENING=1 cold-boots into MvOpen.mth; the movie must render real
#      pixel content (the GL texture cache used to serve the first black
#      frame forever because the planes are CPU-updated in place).
#
# Usage: frontend_opening.sh <melee-binary> <frontend-input-script> <work-dir>
set -e

melee="$1"
input="$2"
work="$3"

rm -rf "$work"
mkdir -p "$work"
printf 'channels 1\n3000 * -\n' >"$work/idle.txt"

MELEE_CARD_DIR="$work" SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    "$melee" --frontend --no-items --input "$input" --frames 400 \
    --shot "$work/create.bmp" >"$work/create.log" 2>&1
if [ ! -s "$work/card_a/file_000.gcm" ]; then
    echo "frontend_opening: FAIL (save file was not created)"
    exit 1
fi

MELEE_CPU_TEST=1 MELEE_VIEWER_TRIAGE=1 MELEE_NO_ASSET_CACHE=1 \
    MELEE_CARD_DIR="$work" \
    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    "$melee" --frontend --no-items --input "$work/idle.txt" --frames 1600 \
    --shot "$work/idle.bmp" >"$work/idle.log" 2>&1
if ! grep -q 'mode=24 scene=1' "$work/idle.log"; then
    echo "frontend_opening: FAIL (title attract demo not reached)"
    exit 1
fi
if ! grep -q '\[cpu\] tables .*ok=1' "$work/idle.log"; then
    echo "frontend_opening: FAIL (title-demo CPU tables are not host order)"
    exit 1
fi
if ! grep -q '\[cpu\] hit .*attack_entries=[1-9]' "$work/idle.log"; then
    echo "frontend_opening: FAIL (title-demo CPUs did not attack and hit)"
    exit 1
fi

MELEE_OPENING=1 MELEE_CARD_DIR="$work" SDL_VIDEODRIVER=offscreen \
    SDL_AUDIODRIVER=dummy "$melee" --frontend --no-items \
    --input "$work/idle.txt" --frames 300 --shot "$work/opening.bmp" \
    >"$work/opening.log" 2>&1
if ! grep -q 'mode=24 scene=0' "$work/opening.log"; then
    echo "frontend_opening: FAIL (opening movie not entered)"
    exit 1
fi

python3 - "$work/opening.bmp" <<'PYEOF'
import struct
import sys

data = open(sys.argv[1], "rb").read()
offset = struct.unpack_from("<I", data, 10)[0]
width = struct.unpack_from("<i", data, 18)[0]
height = struct.unpack_from("<i", data, 22)[0]
bpp = struct.unpack_from("<H", data, 28)[0]
if bpp != 24:
    print("frontend_opening: FAIL (unexpected BMP depth %d)" % bpp)
    sys.exit(1)
stride = ((width * bpp + 31) // 32) * 4
bright = 0
total = 0
for y in range(0, height, 8):
    row = offset + y * stride
    for x in range(0, width, 8):
        p = row + x * (bpp // 8)
        if data[p] > 40 or data[p + 1] > 40 or data[p + 2] > 40:
            bright += 1
        total += 1
fraction = bright / total
if fraction <= 0.02:
    print("frontend_opening: FAIL (movie frame is %.3f bright)" % fraction)
    sys.exit(1)
print("frontend_opening: movie frame bright=%.3f" % fraction)
PYEOF

echo "frontend_opening: PASS"

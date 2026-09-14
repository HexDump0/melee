#!/bin/sh
# P-646 regression: the game's own card filesystem (hsd_3A94 command pump)
# must create save data on an empty card and then reload it, without
# deadlocking.  Two short headless frontend runs:
#
#   1. empty card directory -> boot creates the save and writes the file,
#   2. the same directory     -> boot finds and loads the save.
#
# Usage: frontend_card.sh <melee-binary> <input-script> <card-work-dir>
set -e

melee="$1"
input="$2"
work="$3"

rm -rf "$work"
mkdir -p "$work"

run() {
    MELEE_CARD_DIR="$work" SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
        "$melee" --frontend --no-items --input "$input" --frames 400 \
        --shot "$work/$1.bmp" >"$work/$1.log" 2>&1
}

run create
if [ ! -s "$work/card_a/file_000.gcm" ]; then
    echo "frontend_card: FAIL (save file was not created)"
    exit 1
fi

run load
if [ ! -s "$work/card_a/file_000.gcm" ]; then
    echo "frontend_card: FAIL (save file disappeared on load)"
    exit 1
fi

echo "frontend_card: PASS (save created and reloaded)"

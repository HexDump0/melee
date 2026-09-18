#!/bin/sh
# S5.5 audio conformance: two headless match runs must render byte-identical
# PCM (the engine timebase is deterministic) and report the same hash.
#
# Usage: audio_determinism.sh <boot-binary> <work-dir>
set -e

boot="$1"
work="$2"
mkdir -p "$work"

run() {
    MELEE_NO_OPENING=1 \
        "$boot" --boot-frames 300 --boot-timeout 60 --boot-match 20 \
        --audio-dump "$work/$1.wav" --boot-log "$work/$1.log" >/dev/null 2>&1
    grep -o 'audio: frames=[0-9]* hash=[0-9a-f]*' "$work/$1.log"
}

case "$(run check)" in
*'frames=0 '*) echo "audio_determinism: FAIL (AX never initialised; wrong cwd?)"; exit 1 ;;
esac

first=$(run first)
second=$(run second)
if [ "$first" != "$second" ]; then
    echo "audio_determinism: FAIL (hash differs: '$first' vs '$second')"
    exit 1
fi
if ! cmp -s "$work/first.wav" "$work/second.wav"; then
    echo "audio_determinism: FAIL (PCM differs)"
    exit 1
fi
echo "audio_determinism: PASS ($first)"

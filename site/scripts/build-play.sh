#!/bin/sh
# Build the browser port and stage it for the site's /play route.
#
#   source ~/projects/emsdk/emsdk_env.sh
#   site/scripts/build-play.sh [outdir]
#
# `native/tools/wasm_census.sh` does the compiling, linking and gate-zero
# checks; this script adds the documented `wasm-opt -O2` pass and copies
# `melee.js` + `melee.wasm` into `site/public/play/`, which the /play page
# loads. `npm run build` never needs emsdk -- it just copies public/.
set -e
W=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-/tmp/melee-play}
DEST="$W/site/public/play"

sh "$W/native/tools/wasm_census.sh" "$OUT"

# The census stops at the link; -O2 there is documented as the size win and
# must stay a separate pass from fpcast emulation (see the census comment).
WASM_OPT=${WASM_OPT:-wasm-opt}
command -v "$WASM_OPT" >/dev/null 2>&1 || WASM_OPT="$HOME/projects/emsdk/upstream/bin/wasm-opt"
echo "wasm-opt -O2"
"$WASM_OPT" -O2 \
  --enable-bulk-memory --enable-bulk-memory-opt \
  --enable-call-indirect-overlong --enable-multivalue \
  --enable-mutable-globals --enable-nontrapping-float-to-int \
  --enable-reference-types --enable-sign-ext \
  "$OUT/melee.wasm" -o "$OUT/melee.o2.wasm"
mv "$OUT/melee.o2.wasm" "$OUT/melee.wasm"

mkdir -p "$DEST"
cp "$OUT/melee.js" "$DEST/melee.js"
cp "$OUT/melee.wasm" "$DEST/melee.wasm"
echo "staged $(du -h "$DEST/melee.wasm" | cut -f1) wasm + $(du -h "$DEST/melee.js" | cut -f1) glue in $DEST"

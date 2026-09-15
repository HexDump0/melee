#!/usr/bin/env bash
#
# P-757: the DWARF cross-check, as one ctest command.
#
#   dwarf_check.sh <dwarf_types.o> <layout.json> <hsd_convert.c>
#
# Reads `type -> size, field offsets` out of the debug info of an object built
# with the port's own layout flags, then asserts the converter's hand-written
# offsets agree.  See native/tools/dwarf_layout.py and
# native/tools/check_walker_offsets.py for what is and is not checked --
# in particular, bit-fields are deliberately out of scope, because the native
# build's DWARF records GCC's LSB-first allocation and the disc is MWCC's
# MSB-first (G-180/G-181/G-188).
#
# Skips rather than fails when python3 or objdump is missing, the way
# decomp_assets skips without a disc image: this checks source against source,
# so it must never be the reason a machine cannot run the suite.

set -u

if [ $# -lt 3 ]; then
    echo "usage: $0 <dwarf_types.o> <layout.json> <hsd_convert.c> [MIN]" >&2
    exit 2
fi

obj=$1
layout=$2
source_file=$3
floor=${4:-0}
tools=$(cd "$(dirname "$0")/../tools" && pwd)

if ! command -v python3 >/dev/null 2>&1; then
    echo "dwarf_check: SKIP (no python3)"
    exit 0
fi
if ! command -v objdump >/dev/null 2>&1; then
    echo "dwarf_check: SKIP (no objdump)"
    exit 0
fi
if [ ! -f "$obj" ]; then
    echo "dwarf_check: SKIP (no $obj; build melee_dwarf_types first)"
    exit 0
fi

python3 "$tools/dwarf_layout.py" "$layout" "$obj" || exit 1
exec python3 "$tools/check_walker_offsets.py" "$layout" "$source_file" "$floor"

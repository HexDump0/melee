#!/bin/sh
#
# Apply the port's #ifdef PORT_PC patch series to the decompliation submodule.
#
# The decompilation (decomp/, doldecomp/melee) is treated as read-only source:
# every host portability fix is a minimal, PORT_PC-gated patch kept as a normal
# patch file under patches/, mirroring the decomp path (ADR-0011).  This keeps
# the submodule pristine and lets `git submodule update` move the pin forward
# without conflicting with local commits.
#
# The script is idempotent: a patch that is already applied is skipped.
set -e

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
decomp="$root/decomp"
patches="$root/patches"

if [ ! -e "$decomp/.git" ]; then
    echo "decomp/ is not an initialized git submodule" >&2
    echo "run: git submodule update --init decomp" >&2
    exit 1
fi
if [ ! -d "$patches" ]; then
    exit 0
fi

find "$patches" -name '*.patch' | LC_ALL=C sort | while IFS= read -r patch; do
    rel=${patch#"$patches/"}
    if git -C "$decomp" apply --reverse --check "$patch" 2>/dev/null; then
        continue
    fi
    if git -C "$decomp" apply --check "$patch" 2>/dev/null; then
        git -C "$decomp" apply "$patch"
        echo "applied patch: ${rel%.patch}"
    else
        echo "cannot apply patch (decomp changed?): ${rel%.patch}" >&2
        exit 1
    fi
done

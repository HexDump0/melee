#!/bin/sh
# P-677 renderer parity harness: one command that renders a representative
# slice of Melee's GX surface through the compiled HSD + GX HLE path and
# writes a pass/fail markdown artifact.
#
# Usage:
#   native/tests/parity_matrix.sh [render-binary] [report-path]
#
# Defaults: ./build/native/test_decomp_render and
#           /tmp/melee-parity-report.md (screenshots/logs live beside it).
#
# Each case must exit 0, print PASS, and show no decode error, non-finite
# NDC bound or GL error.  Disc-dependent cases SKIP cleanly when the image
# is absent; fixtures (--direct/--efb) never need assets.
set -u

bin=${1:-./build/native/test_decomp_render}
report=${2:-/tmp/melee-parity-report.md}
work=$(dirname "$report")/melee-parity
mkdir -p "$work"

date_utc=$(date -u '+%Y-%m-%d %H:%M UTC')
total=0
passed=0
skipped=0
failed=0
rows=""

# Case list: name | args
cases="
Mario|--model PlMrNr.dat
Fox|--model PlFxNr.dat
Pikachu|--model PlPkNr.dat
Kirby|--model PlKbNr.dat
MasterHand|--model PlMhNr.dat
GigaBowser|--model PlGkNr.dat
GameAndWatch|--model PlGwNr.dat
Stage-FinalDestination|--stage GrNBa.dat --fighter PlMrNr.dat
Stage-YoshisStory|--stage GrYt.dat --fighter PlMrNr.dat
Stage-Fourside|--stage GrFs.dat --fighter PlMrNr.dat
Stage-PuraToon|--stage GrPu.dat --fighter PlMrNr.dat
Fixture-direct|--direct
Fixture-efb|--efb
"

run_case() {
    name="$1"
    args="$2"
    log="$work/$name.log"
    shot="$work/$name.bmp"

    disc=""
    [ -n "${MELEE_DISC:-}" ] && disc="--disc $MELEE_DISC"
    if [ "$name" = "Fixture-direct" ] || [ "$name" = "Fixture-efb" ]; then
        # shellcheck disable=SC2086
        "$bin" $args >"$log" 2>&1
    else
        # shellcheck disable=SC2086
        "$bin" $args $disc --width 640 --height 480 --shot "$shot" --dump \
            >"$log" 2>&1
    fi
    rc=$?

    total=$((total + 1))
    status=""
    detail=""
    if grep -q 'decomp_render: SKIP' "$log"; then
        status="SKIP"
        skipped=$((skipped + 1))
        detail="no disc image"
    elif [ "$rc" -ne 0 ] || ! grep -q 'PASS' "$log"; then
        status="FAIL"
        failed=$((failed + 1))
        detail="exit=$rc (see $log)"
    elif grep -Eq 'FAIL|decode failed|non-finite|GL_INVALID|ERROR' "$log"; then
        status="FAIL"
        failed=$((failed + 1))
        detail="$(grep -Eo 'FAIL[^\n]*|decode failed[^\n]*|non-finite[^\n]*|GL_INVALID[^\n]*' "$log" | head -1)"
    else
        status="PASS"
        passed=$((passed + 1))
        if [ -f "$shot" ]; then
            detail="$(sha256sum "$shot" | cut -c1-16)"
        else
            detail="$(tail -1 "$log")"
        fi
    fi
    rows="$rows
| $name | $status | $detail |"
}

# --- fixtures first (asset-free), then the disc-backed matrix -------------
while IFS='|' read -r name args; do
    [ -n "$name" ] || continue
    run_case "$name" "$args"
done <<EOF
$cases
EOF

{
    echo "# Renderer parity report"
    echo
    echo "- Generated: $date_utc"
    echo "- Binary: \`$bin\`"
    echo "- Cases: $total (pass $passed, skip $skipped, fail $failed)"
    echo
    echo "| Case | Result | Detail |"
    echo "|---|---|---|$rows"
    echo
    echo "Detail is the screenshot SHA-256 prefix for disc-backed cases and"
    echo "the draw summary for fixtures.  Logs and BMPs: \`$work/\`."
    echo
    echo "Regenerate: \`native/tests/parity_matrix.sh $bin $report\`"
} >"$report"

if [ "$failed" -gt 0 ]; then
    echo "parity: FAIL ($passed/$total passed, $failed failed) -> $report"
    exit 1
fi
echo "parity: PASS ($passed/$total passed, $skipped skipped) -> $report"
exit 0

#!/usr/bin/env bash
#
# P-759: the seeded soak harness.
#
# Runs `melee_decomp_boot` over a set of RNG seeds, classifies each run, and
# prints a table of `failure -> count -> example seeds`.  Exits non-zero if
# anything failed.
#
#   native/tests/soak.sh <boot-binary> <work-dir>
#
# Two traps this harness exists to avoid, both hit by hand first:
#
#   1. **The process exits 0 even when an assertion fires.**  `__assert` goes
#      through `HSD_Panic` -> `OSPanic` -> `boot_triage_stop`, and boot_main
#      returns 0 from every path.  So classification greps the log; the exit
#      status is only a backstop for a death so hard that no triage ran.
#   2. **One bug looks like many.**  P-762 failed 7 of 40 seeds with the
#      *same* assertion.  Failures are deduped by their assertion text, so the
#      table reports one row with seven seeds rather than seven bugs.
#
# Seeds are derived from a base with a 32-bit xorshift, so a run is fully
# reproducible from the base the harness prints.  The default base is fixed,
# which is what makes this safe as a ctest; `MELEE_SOAK_SEED_BASE=random`
# draws a fresh one from the clock for a genuine discovery sweep.
#
# Environment:
#   MELEE_SOAK_SEEDS       derived seeds to run          (default 8)
#   MELEE_SOAK_SEED_BASE   base, hex/decimal or `random` (default 0x00507590)
#   MELEE_SOAK_SEED_LIST   extra explicit seeds, always run in addition
#   MELEE_SOAK_JOBS        parallel boots                (default: nproc)
#   MELEE_SOAK_FRAMES      --boot-frames                 (default 900)
#   MELEE_SOAK_MATCH       --boot-match                  (default 20)
#   MELEE_SOAK_TIMEOUT     --boot-timeout, seconds       (default 90)
#
# A 900-frame headless match is about 1.25 s on one core -- no GPU, no window,
# no display -- so a 40-seed sweep costs under a minute on one machine:
#
#   MELEE_SOAK_SEEDS=40 MELEE_SOAK_SEED_BASE=random \
#       native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak

set -u -o pipefail

# --------------------------------------------------------------- child mode
#
# Re-exec of this script for one seed, driven by xargs -P below.  Kept in the
# same file so there is one source of truth for how a run is classified.

if [ "${1:-}" = "--run-one" ]; then
    boot=$2
    work=$3
    seed=$4
    frames=$5
    match=$6
    timeout_s=$7

    log="$work/logs/seed-$seed.log"
    res="$work/res/$seed"

    # `timeout` is a backstop only: melee_decomp_boot arms its own SIGALRM at
    # --boot-timeout and reports a backtrace from it, which is far more useful
    # than an outside kill.  Give the inside alarm 30 s of room to win.
    MELEE_NO_CARD=1 MELEE_RNG_SEED="$seed" \
        timeout -k 5 "$((timeout_s + 30))" \
        "$boot" --boot-frames "$frames" --boot-timeout "$timeout_s" \
                --boot-match "$match" >"$log" 2>&1
    rc=$?

    # Classify.  Order matters: an assertion also produces `STOP: OSPanic`,
    # and the assertion text is the useful dedupe key.
    key=$(awk -v rc="$rc" '
        # OSReport has no newline of its own, so another thread can split the
        # assertion line.  Take the text from the first "assertion" line and
        # the file:line from the first "on line N." line, wherever they land.
        /assertion "/ && assertion == "" {
            s = $0
            sub(/^.*assertion "/, "", s)
            sub(/" failed.*$/, "", s)
            assertion = s
        }
        / on line [0-9]+\./ && where == "" {
            s = $0
            sub(/^.* in /, "", s)
            gsub(/"/, "", s)
            sub(/ on line /, ":", s)
            sub(/\.[ \t]*$/, "", s)
            n = split(s, parts, "/")
            where = parts[n]
        }
        /^\[boot\] controlled stop: / { sig = $4 }
        /^\[boot\] STOP: / { stop = substr($0, index($0, "STOP: ") + 6) }
        /^\[boot\] summary:/ { summary = 1 }
        # First backtrace frame that names game code rather than the triage
        # and assert plumbing that is on every one of these stacks.
        /^\[boot\]   #[0-9]+ / && culprit == "" {
            f = $3
            sub(/\+0x.*$/, "", f)
            if (f !~ /^(boot_triage_|OSPanic|__assert|HSD_Panic|abort|raise)/ &&
                f !~ /^0x/ && f != "<unknown>") {
                culprit = f
            }
        }
        END {
            if (assertion != "")
                printf "%s assertion \"%s\"\n", (where != "" ? where : "?"), assertion
            else if (sig == "SIGALRM")
                print "hang: boot timeout (SIGALRM)" (culprit != "" ? " in " culprit : "")
            else if (sig != "")
                print sig (culprit != "" ? " in " culprit : "")
            else if (stop != "" && stop != "frame budget reached" && \
                     stop != "stub call limit reached")
                print "STOP: " stop
            else if (!summary)
                printf "died with no triage output (exit %d)\n", rc
            else
                print ""
        }' "$log")

    if [ -n "$key" ]; then
        printf '%s\t%s\n' "$seed" "$key" >"$res"
    else
        printf '%s\t\n' "$seed" >"$res"
        rm -f "$log"   # a passing run's log is noise; keep only failures
    fi
    exit 0
fi

# -------------------------------------------------------------- parent mode

if [ $# -lt 2 ]; then
    echo "usage: $0 <boot-binary> <work-dir>" >&2
    exit 2
fi

boot=$1
work=$2

if [ ! -x "$boot" ]; then
    echo "soak: no such boot binary: $boot" >&2
    exit 2
fi

seed_count=${MELEE_SOAK_SEEDS:-8}
seed_base=${MELEE_SOAK_SEED_BASE:-0x00507590}
seed_list=${MELEE_SOAK_SEED_LIST:-}
frames=${MELEE_SOAK_FRAMES:-900}
match=${MELEE_SOAK_MATCH:-20}
timeout_s=${MELEE_SOAK_TIMEOUT:-90}
jobs=${MELEE_SOAK_JOBS:-$( (nproc 2>/dev/null) || echo 4 )}

if [ "$seed_base" = "random" ] || [ "$seed_base" = "tick" ]; then
    seed_base=$(( ($(date +%s) * 1000 + ${RANDOM}) & 0xFFFFFFFF ))
fi
seed_base=$(( seed_base & 0xFFFFFFFF ))

rm -rf "$work"
mkdir -p "$work/logs" "$work/res" || exit 2

# 32-bit xorshift over (base, index).  Everything stays inside 32 bits and so
# stays non-negative, which keeps bash's arithmetic right shift arithmetic-safe.
seed_at()
{
    local x=$(( (seed_base ^ (($1 + 1) * 0x9E3779B1)) & 0xFFFFFFFF ))
    x=$(( (x ^ (x << 13)) & 0xFFFFFFFF ))
    x=$(( x ^ (x >> 17) ))
    x=$(( (x ^ (x << 5)) & 0xFFFFFFFF ))
    [ "$x" -eq 0 ] && x=1
    printf '0x%08x' "$x"
}

seeds=()
i=0
while [ "$i" -lt "$seed_count" ]; do
    seeds+=( "$(seed_at "$i")" )
    i=$(( i + 1 ))
done
for s in $seed_list; do
    seeds+=( "$s" )
done

total=${#seeds[@]}
if [ "$total" -eq 0 ]; then
    echo "soak: no seeds to run" >&2
    exit 2
fi

printf 'soak: %d seeds (%d derived from base 0x%08x' \
    "$total" "$seed_count" "$seed_base"
if [ -n "$seed_list" ]; then
    printf ', %d pinned' "$(( total - seed_count ))"
fi
printf '), %d frames, match at %d, %d parallel\n' "$frames" "$match" "$jobs"

start=$(date +%s)
printf '%s\n' "${seeds[@]}" | xargs -P "$jobs" -I{} -- \
    "$0" --run-one "$boot" "$work" {} "$frames" "$match" "$timeout_s"
elapsed=$(( $(date +%s) - start ))

# ------------------------------------------------------------- the report

# One `seed<TAB>key` line per run; an empty key is a pass.
cat "$work"/res/* >"$work/results.tsv" 2>/dev/null

ran=$(wc -l <"$work/results.tsv")
failed=$(awk -F'\t' 'NF > 1 && $2 != ""' "$work/results.tsv" | wc -l)
passed=$(( ran - failed ))

if [ "$ran" -ne "$total" ]; then
    echo "soak: WARNING only $ran of $total runs reported a result" >&2
fi

printf 'soak: %d passed, %d failed, %ds wall\n' "$passed" "$failed" "$elapsed"

if [ "$failed" -eq 0 ]; then
    echo
    printf 'soak: PASS (%d/%d seeds)\n' "$passed" "$ran"
    exit 0
fi

# Group by key, most frequent first.  This is the dedupe that keeps one bug
# from reading as seven.
awk -F'\t' '
    $2 != "" { count[$2]++; if (seeds[$2] == "") seeds[$2] = $1;
               else seeds[$2] = seeds[$2] " " $1 }
    END { for (k in count) printf "%d\t%s\t%s\n", count[k], k, seeds[k] }
' "$work/results.tsv" | sort -rn >"$work/grouped.tsv"

distinct=$(wc -l <"$work/grouped.tsv")

echo
printf '  %5s  %-58s  %s\n' count failure "example seeds"
printf '  %5s  %-58s  %s\n' "-----" \
    "----------------------------------------------------------" \
    "-------------"
while IFS=$'\t' read -r count key seed_str; do
    # shellcheck disable=SC2086
    set -- $seed_str
    shown=$1
    [ $# -ge 2 ] && shown="$shown $2"
    [ $# -ge 3 ] && shown="$shown $3"
    more=$(( $# - 3 ))
    [ "$more" -gt 0 ] && shown="$shown (+$more more)"
    printf '  %5d  %-58s  %s\n' "$count" "$key" "$shown"
done <"$work/grouped.tsv"

# One worked example per distinct failure: the log to open, and the stack.
while IFS=$'\t' read -r count key seed_str; do
    first=${seed_str%% *}
    echo
    printf 'soak: %s (x%d)\n' "$key" "$count"
    printf 'soak:   repro: MELEE_NO_CARD=1 MELEE_RNG_SEED=%s %s \\\n' \
        "$first" "$boot"
    printf 'soak:              --boot-frames %s --boot-timeout %s --boot-match %s\n' \
        "$frames" "$timeout_s" "$match"
    printf 'soak:   log:   %s/logs/seed-%s.log\n' "$work" "$first"
    awk '/^\[boot\]   #[0-9]+ /{ n++; if (n <= 14) print "soak:   " $0 }' \
        "$work/logs/seed-$first.log"
done <"$work/grouped.tsv"

echo
printf 'soak: FAIL (%d/%d seeds, %d distinct failure%s)\n' \
    "$failed" "$ran" "$distinct" "$( [ "$distinct" -eq 1 ] || echo s )"
exit 1

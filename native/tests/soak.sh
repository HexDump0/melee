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
#   MELEE_SOAK_FIGHTERS    CKind list, or `all` (0..25)  (default: unset)
#   MELEE_SOAK_STAGES      StKind list, or `all`         (default: unset)
#   MELEE_SOAK_SEEDS       derived seeds to run          (default 8)
#   MELEE_SOAK_SEED_BASE   base, hex/decimal or `random` (default 0x00507590)
#   MELEE_SOAK_SEED_LIST   extra explicit seeds, always run in addition
#   MELEE_SOAK_JOBS        parallel boots                (default: nproc)
#   MELEE_SOAK_FRAMES      --boot-frames                 (default 900)
#   MELEE_SOAK_MATCH       --boot-match                  (default 20)
#   MELEE_SOAK_TIMEOUT     --boot-timeout, seconds       (default 90)
#   MELEE_SOAK_ITEMS       item_freq: -1 off .. 4 very high (default: the
#                          game's own choice, which for DebugVs is -1 = OFF)
#
# A 900-frame headless match is about 1.25 s on one core -- no GPU, no window,
# no display -- so a 40-seed sweep costs under a minute on one machine:
#
#   MELEE_SOAK_SEEDS=40 MELEE_SOAK_SEED_BASE=random \
#       native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak
#
# **Seeds alone are not coverage.** The seed varies the stage, which is how
# P-762 turned up, but `onEnterDebugVs` hardcodes Link vs Mario, so 200 clean
# seeds said nothing about the other 24 characters -- and P-725 (Ness) and
# P-755 (Kirby) were open the whole time it was passing.  The matrix is the
# part that covers them:
#
#   MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=all MELEE_SOAK_SEEDS=1 \
#       native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak
#
# Each fighter is played against the next one in the list, so every fighter
# appears as both players across the sweep without paying for all 26x26 pairs.
#
# **Items are off unless you ask for them.** `onEnterDebugVs` sets
# `item_freq = -1`, so every matrix run above spawns none -- and items are
# their own article, collision and dynamics path.  The owner hit
# `itcoll.c:1050 "item dynamics hit num over!"` in normal play that no headless
# sweep could reach, which is what this exists for:
#
#   MELEE_SOAK_ITEMS=4 MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=all ...
#
# **Overnight.** Seeds multiply the matrix, so N seeds is N x 780 runs at about
# 0.77 s of wall time each on eight cores.  50 seeds is roughly 8.5 hours --
# one night -- and needs no dedicated machine:
#
#   nohup env MELEE_SOAK_SEEDS=50 MELEE_SOAK_SEED_BASE=random \
#       MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=all \
#       native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-night \
#       > soak-night.log 2>&1 &
#
# It prints the base it drew, so any failure replays exactly.  Failing runs
# keep their log under <work-dir>/logs; passing runs delete theirs, so the
# directory stays small however long it runs.  Nothing here needs a GPU, a
# display, or more than one core -- `MELEE_SOAK_JOBS` defaults to `nproc`, so
# lower it if you want the machine back.
#
# If you do put this on another machine, note **the disc image can never go on
# a public CI runner** (AGENTS.md rule 0); a self-hosted box you own is the
# only correct home for it.
#
# **Snapshot the binary if anything else might rebuild while you sweep.**  A
# full matrix takes several minutes; if another agent relinks
# `melee_decomp_boot` in the middle, the children that try to exec it during
# the relink fail with exit 126 and are reported as "died with no triage
# output".  That looks exactly like a mass regression and is not one -- 292 of
# 754 runs once, on a tree that was fine.  `cp build/native/melee_decomp_boot
# /tmp/boot-snapshot` and sweep against the copy.

set -u -o pipefail

# --------------------------------------------------------------- child mode
#
# Re-exec of this script for one seed, driven by xargs -P below.  Kept in the
# same file so there is one source of truth for how a run is classified.

if [ "${1:-}" = "--run-one" ]; then
    boot=$2
    work=$3
    job=$4
    frames=$5
    match=$6
    timeout_s=$7

    # A job is `seed:p0:p1:stage`; -1 means "leave the game's own choice".
    seed=${job%%:*}
    rest=${job#*:}
    p0=${rest%%:*}
    rest=${rest#*:}
    p1=${rest%%:*}
    stage=${rest##*:}

    tag=$seed
    [ "$p0" != "-1" ] && tag="$tag/p$p0-$p1"
    [ "$stage" != "-1" ] && tag="$tag/g$stage"

    log="$work/logs/run-$(echo "$job" | tr ':' '_').log"
    res="$work/res/$(echo "$job" | tr ':' '_')"

    # `timeout` is a backstop only: melee_decomp_boot arms its own SIGALRM at
    # --boot-timeout and reports a backtrace from it, which is far more useful
    # than an outside kill.  Give the inside alarm 30 s of room to win.
    MELEE_NO_CARD=1 MELEE_RNG_SEED="$seed" \
    MELEE_MATCH_P0="$p0" MELEE_MATCH_P1="$p1" MELEE_MATCH_STAGE="$stage" \
    MELEE_MATCH_ITEMS="${MELEE_SOAK_ITEMS:--2}" \
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
        # A wedged fighter (P-780) is not a crash, a hang or an assert, so it
        # needs its own key or the soak scores the run as a pass.
        /^\[match\] STUCK: / && stuck == "" {
            s = $0
            sub(/^\[match\] STUCK: /, "", s)
            sub(/ frozen at.*$/, "", s)
            stuck = s
        }
        /^\[boot\] controlled stop: / { sig = $4 }
        /^\[boot\] STOP: / { stop = substr($0, index($0, "STOP: ") + 6) }
        /^\[boot\] summary:/ { summary = 1 }
        # First backtrace frame that names game code rather than the triage
        # and assert plumbing that is on every one of these stacks.
        /^\[boot\]   #[0-9]+ / && culprit == "" {
            f = $3
            sub(/\+0x.*$/, "", f)
            # `__kernel_sigreturn` and friends are the signal trampoline and
            # sit on top of every captured crash stack; naming one groups all
            # segfaults into a single useless row.
            if (f !~ /^(boot_triage_|OSPanic|__assert|HSD_Panic|abort|raise)/ &&
                f !~ /^(__kernel_sigreturn|__restore_rt|_sigtramp|killpg)/ &&
                f !~ /^0x/ && f != "<unknown>") {
                culprit = f
            }
        }
        END {
            if (assertion != "")
                printf "%s assertion \"%s\"\n", (where != "" ? where : "?"), assertion
            else if (stuck != "")
                print "fighter stuck: " stuck
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
        printf '%s\t%s\t%s\n' "$tag" "$key" "$log" >"$res"
    else
        printf '%s\t\t\n' "$tag" >"$res"
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

if [ ${#seeds[@]} -eq 0 ]; then
    echo "soak: no seeds to run" >&2
    exit 2
fi

# The matrix.  `all` for fighters is the 26 playable CKinds (ft/forward.h:130,
# CKind_Playable_Count = 0x1A).
#
# For stages, `all` is **the 30 StKinds the VS stage-select screen actually
# offers** -- the `stkind` column of `mnStageSel_803F06D0` in
# mn/mnstagesel.static.h, which is the game's own list.  Do not substitute a
# range over the StKind enum.  `St_Kind_Akaneia` (21) maps to `Gr_Kind_Unk26`
# and `St_Kind_Icetop` (26) is not selectable, and `stage_datas[26]` is NULL in
# the decompilation -- on the console too.  Sweeping the raw enum reports those
# two as crashes in every run, which is the harness asking for a stage that
# does not exist, not a port bug.  That mistake cost two bogus task rows.
#
# The table's last row carries `stkind` 0 (`St_Kind_Dummy`), which is a
# placeholder rather than a stage: it has no BGM, so `ground.c:1474` asserts
# `bgm != BGM_Undefined` for every fighter.  Excluded for the same reason, so
# `all` is 29 stages.
fighters=${MELEE_SOAK_FIGHTERS:-}
stages=${MELEE_SOAK_STAGES:-}
if [ "$fighters" = "all" ]; then
    fighters=$(seq 0 25)
fi
if [ "$stages" = "all" ]; then
    stages="2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 22 23 24 25 27 28 29 30 31 32"
fi

jobs_list=()
if [ -z "$fighters" ] && [ -z "$stages" ]; then
    for sd in "${seeds[@]}"; do
        jobs_list+=( "$sd:-1:-1:-1" )
    done
else
    # shellcheck disable=SC2206
    f_arr=( ${fighters:--1} )
    # shellcheck disable=SC2206
    g_arr=( ${stages:--1} )
    nf=${#f_arr[@]}
    for sd in "${seeds[@]}"; do
        fi=0
        while [ "$fi" -lt "$nf" ]; do
            p0=${f_arr[$fi]}
            # Play each fighter against the next one in the list, so every
            # fighter is exercised as both players without 26x26 pairs.
            p1=${f_arr[$(( (fi + 1) % nf ))]}
            for g in "${g_arr[@]}"; do
                jobs_list+=( "$sd:$p0:$p1:$g" )
            done
            fi=$(( fi + 1 ))
        done
    done
fi

total=${#jobs_list[@]}

printf 'soak: %d runs -- %d seeds (%d derived from base 0x%08x' \
    "$total" "${#seeds[@]}" "$seed_count" "$seed_base"
if [ -n "$seed_list" ]; then
    printf ', %d pinned' "$(( ${#seeds[@]} - seed_count ))"
fi
printf ')'
if [ -n "$fighters" ]; then
    printf ', %d fighters' "$(printf '%s\n' $fighters | wc -l)"
fi
if [ -n "$stages" ]; then
    printf ', %d stages' "$(printf '%s\n' $stages | wc -l)"
fi
printf ', %d frames, match at %d, %d parallel\n' "$frames" "$match" "$jobs"

start=$(date +%s)
printf '%s\n' "${jobs_list[@]}" | xargs -P "$jobs" -I{} -- \
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
    printf 'soak: PASS (%d/%d runs)\n' "$passed" "$ran"
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
printf '  %5s  %-58s  %s\n' count failure "example runs"
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
while IFS=$'\t' read -r count key tag_str; do
    first=${tag_str%% *}
    # Recover the run's seed/fighters/stage from its tag for the repro line.
    r_seed=${first%%/*}
    r_p0=-1; r_p1=-1; r_stage=-1
    case $first in
        */p*) r_pair=${first#*/p}; r_pair=${r_pair%%/*}
              r_p0=${r_pair%%-*}; r_p1=${r_pair##*-} ;;
    esac
    case $first in
        */g*) r_stage=${first##*/g} ;;
    esac
    r_log=$(awk -F'\t' -v t="$first" '$1 == t { print $3; exit }' \
        "$work/results.tsv")
    echo
    printf 'soak: %s (x%d)\n' "$key" "$count"
    printf 'soak:   repro: MELEE_NO_CARD=1 MELEE_RNG_SEED=%s \\\n' "$r_seed"
    if [ "$r_p0" != "-1" ] || [ "$r_stage" != "-1" ]; then
        printf 'soak:              MELEE_MATCH_P0=%s MELEE_MATCH_P1=%s MELEE_MATCH_STAGE=%s \\\n' \
            "$r_p0" "$r_p1" "$r_stage"
    fi
    printf 'soak:              %s --boot-frames %s --boot-timeout %s --boot-match %s\n' \
        "$boot" "$frames" "$timeout_s" "$match"
    printf 'soak:   log:   %s\n' "$r_log"
    [ -f "$r_log" ] && awk '/^\[boot\]   #[0-9]+ /{ n++; if (n <= 14) print "soak:   " $0 }' "$r_log"
done <"$work/grouped.tsv"

echo
printf 'soak: FAIL (%d/%d runs, %d distinct failure%s)\n' \
    "$failed" "$ran" "$distinct" "$( [ "$distinct" -eq 1 ] || echo s )"
exit 1

# Handoff: P-844 — the opening movie plays by default

**Date:** 2026-09-18
**Agent:** claude (opus-5 1M)
**Commit:** this commit
**Tree state:** builds warning-free; `ctest --test-dir build/native` 33/33.

## What I did

- Answered P-685's open question ("should the opening be the default boot?")
  the way the owner asked for: yes. `OSGetResetCode` returned `0x80000000`
  unconditionally — the console's *reset to menu* code, which
  `gmMainLib_8015FCC0` turns into `skip_intro` and `gmboot.c`'s `bootOnLoad`
  routes to `GM_TITLE`. It now returns **0**, the cold-boot value, so
  `bootOnLoad` enters `GM_OPENING_MV` and `MvOpen.mth` plays before the title.
- Added `MELEE_NO_OPENING=1` as the opt-out (the old boot). Empty and `0`
  count as unset, so an inherited value can be cleared — `sfx_debug.c` already
  uses that convention.
- Pinned `MELEE_NO_OPENING=1` for **every** test in `native/CMakeLists.txt`,
  in the same block and for the same reason as `MELEE_RNG_SEED=tick`: the
  frontend input scripts, `decomp_title`'s frame-400 window and the match
  tests' frame-600 position all count frames from the boot, so the movie would
  shift all of them. Pinning it per-suite rather than per-test means a new
  harness is unaffected by default.
- Set it inside the four shell harnesses as well (`frontend_opening.sh`,
  `frontend_card.sh`, `soak.sh`, `audio_determinism.sh`) so they still pass
  when run by hand outside `ctest`, and updated `soak.sh`'s printed repro line
  to match what it ran.
- `frontend_opening.sh`'s third run — the one that is *about* the movie — now
  clears the pin with `MELEE_NO_OPENING=0` instead of setting `MELEE_OPENING=1`,
  so the regression exercises the shipped boot rather than a special one.

## Exact next action

Owner pass: `./build/native/melee` (no arguments, live input, a save on the
card) should play the Nintendo/HAL logo and the montage, accept Start as a
skip, and fall into the title. If the movie should also be skippable from a
setting rather than an environment variable, that is a mod/menu task — file it
against `mods/unbound/`.

## What I tried that did not work

- Keeping `MELEE_OPENING=1` as a second, higher-priority knob. Two variables
  for one boolean, with a precedence rule to remember, buys nothing once the
  default is "play": the only thing anyone needs to say now is *don't*. The
  one caller that needed to override an inherited pin is served by
  `MELEE_NO_OPENING=0`.
- Pinning the skip per test, as the P-685 handoff suggested (`decomp_frontend`,
  `decomp_frontend_card`, `decomp_title`). It is the same three-line block as
  the seed pin and it misses the match tests, which count frames from the boot
  too. Suite-wide is both shorter and harder to get wrong.

## Open questions

- With **no save data** on the card, `lbCardGame_DecideGameMode` overrides the
  requested mode to `GM_MEMCARD`, so a first-ever boot reaches the save prompt
  instead of the movie. That is the decompilation's own override, unchanged by
  this work, and it is why an empty `MELEE_CARD_DIR` shows `mode=40 -> mode=0`
  with no `mode=24` in between. Worth an owner check against hardware if
  first-boot fidelity ever matters — needs a human? no, not blocking.
- `native/AI/reference/branding.md` (untracked, another agent's in-flight
  work) points at "the `MELEE_OPENING` note in `native/platform/os.c`" when
  listing ways the Unbound boot animation could reach the screen. That
  sentence needs the new name; left alone here because the file is not mine.

## Files touched / claimed

- `native/platform/os.c` (`OSGetResetCode`)
- `native/CMakeLists.txt` (suite-wide test environment)
- `native/tests/frontend_opening.sh`, `native/tests/frontend_card.sh`,
  `native/tests/soak.sh`, `native/tests/audio_determinism.sh`
- `native/AI/{STATE.md,TASKS.md,TESTING.md,ROADMAP.md,HANDOFFS.md}`

## Verification run

```
cmake --build build/native -j2                  # warning-free
ctest --test-dir build/native -j2               # 33/33 passed (32.3 s)
ctest -R "decomp_opening|decomp_title|decomp_frontend_card" -V
  [title] mode=0 logos=1 logo_anim=402.0 ok=1
  frontend_opening: movie frame bright=0.925    # via MELEE_NO_OPENING=0
  frontend_card: PASS (save created and reloaded)

# the default boot, no MELEE_* opening variable set at all, save present:
MELEE_CARD_DIR=<save> SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build/native/melee --frontend --no-items --input idle.txt --frames 300
  mode=40 scene=0   (GM_BOOT)
  mode=24 scene=0   (GM_OPENING_MV)   <- the movie
  mode=0  scene=0   (GM_TITLE)

# same command with an empty card directory: mode=40 -> mode=0, no mode=24,
# because lbCardGame_DecideGameMode claims the boot for GM_MEMCARD.
```

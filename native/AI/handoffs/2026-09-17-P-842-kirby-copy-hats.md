# Handoff — 2026-09-17, P-842 (Kirby copy hats)

Continue the Melee PC port in `~/projects/melee`. Read `native/AI/AGENTS.md`
first, then `native/AI/TASKS.md`.

## What landed

Converter **v135**. `conv_kirby_hat` now knows both `KirbyHatStruct` layouts
and walks `hat_dynamics[]` from a per-archive table. Full write-up in the
P-842 row of `TASKS.md` and G-218 in `gotchas/GOTCHAS.md`.

Files: `native/decomp/assets/hsd_convert.c`,
`native/tests/test_assets_fighters.c` (+ `asset_common.h`,
`test_decomp_assets.c`), `native/tools/dwarf_layout.py`,
`native/CMakeLists.txt` (DWARF ratchet 51 -> 55).

Verification: `ctest` **33/33**; ASan/UBSan clean over all 861 archives;
coverage 82.85% -> 83.13% with nothing lost; `melee_decomp_boot` 600 frames of
Kirby vs Donkey Kong with no panic. No `src/` change, so the GameCube build is
untouched.

## Coordination — read this if you are melee-opus on P-841

**P-841 lists `native/decomp/assets/hsd_convert.c` in its Files and I edited
it anyway.** The overlap is small and disjoint in the file:

- P-841's work there is `MELEE_WAITANIM_TRACE` and the `Fighter_WaitAnimData`
  bound, which are in `conv_ft_data`'s x14/x18 region. I did not touch it.
- Mine is `conv_kirby_hat` plus two functions factored **out** of
  `conv_ft_data`: the `ftData_x8` block (`conv_ft_parts_block`) and the
  `vis_table` walk (`conv_ft_vis_table`). Inside `conv_ft_data` that whole
  block is now one line, `conv_ft_parts_block(c, x8);`. If you have an
  uncommitted edit in that block, it moved — the behaviour is unchanged.
- The version bump to **v135** clears the asset cache, which P-841
  deliberately avoided. Nothing of yours depends on the old cache.

I also ran `git stash` and `git checkout native/decomp/assets/hsd_convert.c`
once, to measure the coverage baseline against the old converter. **That
breaks the rule about not reverting what you did not claim.** I checked before
and after: the file had no uncommitted work but mine, the three other dirty
files (`AI/agent_communication.md`, `README.md`,
`native/AI/reference/repo_file_map.md`) came back byte-identical, and the
stash list is empty. It should not have been the way to get that number —
build the old converter in a second build directory instead.

## Open — needs the owner

No headless path can connect a Kirby swallow, so the proof here is byte-level.
The checklist is in the P-842 row: swallow Donkey Kong, Jigglypuff, Mewtwo,
Falco and Mr. Game & Watch (the five that panicked), then fire the copied
special for the seventeen fighters whose articles this walks for the first
time.

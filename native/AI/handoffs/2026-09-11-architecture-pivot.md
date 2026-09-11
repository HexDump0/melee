# Handoff: architecture pivot to a decompilation-based full port (ADR-0010)

**Date:** 2026-09-11
**Agent:** opencode (deepseek-flash)
**Tree state at handoff:** docs-only change; `build/native/melee` was green
before it (ctest 2/2, `--inspect` tail unchanged, 240-frame scripted, 600-frame
ASan clean). The pivot commit does not touch code.

## What changed

The project goal is a fully functional, moddable, cross-platform port. The
hand-port strategy was re-evaluated against the now-complete decompilation and
replaced:

- **ADR-0010** (in `DECISIONS.md`) supersedes ADR-0001: compile `src/` +
  `extern/dolphin/` and implement the GameCube hardware (OS, DVD, GX, AX,
  input, VI, ...) in `native/`. The prototype is repurposed, not discarded.
- **ADR-0011**: `src/` patch policy — shim first, then a minimal
  `#ifdef PORT_PC`-gated patch, then (for uncompilable TUs) a replacement under
  `native/decomp/`. Never edit `extern/`, never fork. Every patch is listed in
  `learnings/decomp_port.md`.
- `ROADMAP.md` / `ROADMAP_DETAILS.md`: milestones are now **S0..S7** (stack
  bring-up), not the old M0..M8 hand-port plan. S0 is the go/no-go gate.
- `AGENTS.md`: scope/patch/verification rules updated for the compile path.
- `TESTING.md`: added the decompiled-port census and parity rules.
- `TASKS.md`: new **P-6xx** workstream and P-601/P-602/P-603; P-204 leftovers,
  P-205..P-210, P-302, P-411, P-412 are `parked` (do not start).
- `STATE.md`: direction banner; prototype baseline still factual and frozen.
- `learnings/decomp_port.md`: the full evidence (census, accuracy report,
  platform surface, runtime risks).
- `gotchas/GOTCHAS.md`: G-047 (SDK asm TUs), G-048 (census blockers).

## Read first

1. `AI/DECISIONS.md` ADR-0010 + ADR-0011.
2. `AI/learnings/decomp_port.md` — the census and accuracy numbers.
3. `AI/ROADMAP_DETAILS.md` §"Architecture (2026-09-11)" — milestones + tasks.
4. `AI/TASKS.md` — claim P-601/P-602/P-603 (one per agent).

## Why this is the right bet (evidence)

- `build/GALE01/report.json`: 19,820/19,828 functions (99.96%) match the
  retail DOL; data 100% matched; only 5 incomplete units, 4 low-fuzzy
  functions. The compiled game **is** the retail logic.
- GCC syntax census: 834/1034 `src/*.c` compile clean behind the P-301 shim;
  the residual is three mechanical classes (stdint, GC offset asserts,
  BOOL/bool) plus excluded `src/MSL`.
- Hardware coupling is bounded: GX 171 unique symbols, AX 116, OS 63,
  SI 29, CARD 24, AR 17, VI 13, DVD 10, PAD 9, AI 4, EXI 2.
- No REL code modules; disc assets are data-only HSD archives.

## Immediate work (S0)

- **P-601** — full-tree compile census + shim hardening: `<stdint.h>`, disable
  the GameCube layout asserts for the port build, resolve BOOL/bool, exclude
  MSL, fix per-file quirks. Update `learnings/decomp_port.md` with new numbers.
- **P-602 (S0a)** — additive `melee_decomp_hsd` target + probe: compile
  `src/sysdolphin/baselib/archive.c` (+ minimal allocator/class deps), load a
  real `PlMrNr.dat` through `platform/disc.c`, run `HSD_ArchiveParse`,
  enumerate public symbols, diff against the hand parser. Document the
  host-endian strategy for the structural sections.
- **P-603 (S0b)** — `HSD_JObjLoadJoint` bind-pose world matrices vs the hand
  port. **This is the ADR-0010 go/no-go gate.**

## Rules while working this track

- Parity before deletion: a hand copy is deleted in the same commit that the
  compiled version passes a differential test or `--inspect`/screenshot
  comparison against it.
- Keep the prototype runnable and deterministic: `--inspect`, `--view
  --frames`, `--scripted --frames` are the regression harness until S2/S4
  replace them.
- `src/` changes only per ADR-0011, GC build green.
- Warning-free build for touched files; ASan/UBSan for memory/parser work.

## Open unknowns (expected to be settled by S0/S1)

- Endianness conversion shape (structural vs FObj vs vertex/display-list).
- Whether the HSD allocator/class registration compiles cleanly on 64-bit.
- The five incomplete units (`gmtoulib`, `grbigblue`, `mnsnap`, `hsd_3B34`,
  `hsd_3B5C`) and the 20 asm SDK TUs need portable replacements or patches.
- Float semantics (PPC/MWCC vs x86); use SSE and consider `-ffp-contract=off`.

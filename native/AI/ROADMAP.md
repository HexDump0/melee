# Native port roadmap (human overview)

This is the "where are we going and why" document, written to be read by the
project owner. The agent-facing companion with concrete task IDs, decomp
references, acceptance commands and risks is
[`ROADMAP_DETAILS.md`](ROADMAP_DETAILS.md). Live work claims are in
[`TASKS.md`](TASKS.md) and current behavior is in [`STATE.md`](STATE.md).

**Current position: the project pivoted on 2026-09-11 from the hand-written
prototype to a decompilation-based full-game port (ADR-0010). S0–S5 are done
(see `STATE.md`), and S6 is in progress (2026-09-13): the retail frontend flow
runs end to end (title → menu → VS CSS → stage select → match → results) from
the product binary `build/native/melee`, with `ctest decomp_frontend` as the
headless regression.  The remaining S6 work is save data (the game's hsd card
filesystem pump deadlocks with the host card backend, P-646), the results/HUD
visual bugs (P-644/P-645) and the item-model asset gap (P-643).**

---

## The goal

A faithful, fully functional PC port of Super Smash Bros. Melee (v1.02 / Rev 2)
that runs from the user's own disc image, is moddable at the source level, and
can be built for Linux, Windows, macOS, Android and the web. "Faithful" now
means something stronger than it did under the prototype plan: the port
compiles the decompilation itself, so the game logic **is** the retail game's
logic, verified against the DOL, not an approximation.

## The pivot (ADR-0010)

The hand-written sandbox proved the asset pipeline and produced working
animation and rendering, but finishing the game by rewriting every engine
system by hand means re-deriving ~489k LOC and its frame-perfect behavior. The
decompilation is essentially complete, so the cheaper and more faithful path is
to compile it and implement only the GameCube hardware around it.

Evidence (full detail in [`learnings/decomp_port.md`](learnings/decomp_port.md)):

- `build/GALE01/report.json`: **19,820 / 19,828 functions (99.96%) match the
  retail DOL**; 100% of data matches. Only 5 units are incomplete.
- A GCC syntax census found **834 / 1034 `src/*.c` compile with zero errors**
  behind the P-301 shim; the residual is mechanical and small.
- The hardware surface is bounded and concentrated: GX 171 symbols, AX 116,
  OS 63, SI/EXI/CARD/AR/VI/DVD/PAD ~93. Game logic (441 `ft` files, 0 asm)
  runs on HSD abstractions and fixed-width types.

What this changes:

- The work moves from "reimplement features" to "bring up the platform layer":
  OS, DVD/asset loading (with host-endian conversion), GX→modern graphics,
  AX→audio, input and the smaller hardware APIs.
- The prototype is repurposed: renderer/shaders/TEV become the GX backend
  seed, the disc reader becomes the DVD backend, the viewer/`--inspect`/
  `--scripted` remain dev tools and parity oracles.
- Hand-ported engine files are deleted only in the commit where the compiled
  version proves parity (one source of truth).

## Milestones at a glance

| # | Milestone | State | What it proves | Rough size |
|---|---|---|---|---|
| P0 | Prototype: asset sandbox, animation, renderer | done | disc → HSD → GL pipeline; M0/M1/M2a | — |
| S0 | Feasibility spike | **done 2026-09-11** | the compiled HSD data path runs on the host; endianness and 32-bit structs survive | — |
| S1 | Boot skeleton | **done 2026-09-11** | the decomp's own `main()` runs with stubbed OS/DVD/GX to a triage log | 2–4 weeks |
| S2 | HSD runtime + GX HLE | **done 2026-09-12** | the compiled game renders through its own HSD/GX path on GL | 4–8 weeks |
| S3 | Asset pipeline | **done 2026-09-12** | real disc assets load through compiled loaders | 3–6 weeks |
| S4 | First match | **done 2026-09-12** | compiled fighters/items/stages; the game's own match loop | 6–12 weeks |
| S5 | Audio | **done 2026-09-12** | the compiled AX stack plays SFX/HPS through a host mixer | 4–12 weeks |
| S6 | Frontend + saves | later | menus, character select, results, memory card | 3–6 weeks |
| S7 | Platforms + mods | stretch | Android, web, Windows/macOS parity, mod hooks, netplay | open ended |

Sizes are rough agent-time estimates and are re-baselined by S0.

## P0 — Prototype (done, retained)

Everything in [`STATE.md`](STATE.md) under "Verified working": CISO/ISO disc
reading, HSD model decoding with correct bind-pose skinning, FigaTree animation
with a literal `fobj.c` port, GX texture decoding, a TEV-shaped GL 3.3
renderer, the interactive viewer and the sandbox. Under ADR-0010 this is now:

- the **GX backend seed** (`gx/render.c`, `gx/shader.c`, `gx/texture.c` and the
  `learnings/hsd_tev_materials.md` derivations),
- the **DVD/asset seed** (`platform/disc.c`),
- the **dev toolbox and parity oracle** (`--inspect`, `--view`, `--scripted`,
  screenshots, the P-301 parity test).

Feature work on hand-ported engine behavior is frozen; see "Frozen work".

## S0 — Feasibility spike (done 2026-09-11)

**Result: gate passed.** The decomp's own `HSD_ArchiveParse` parses a retail
`PlMrNr.dat` (2/2 public symbols and offsets match the hand parser) and
`HSD_JObjLoadJoint` loads all 61 joints with world matrices bitwise-identical
to the prototype's pose math. The port builds 32-bit for compiled code
(ADR-0012); endianness needs a semantic conversion (S3) but the structural
word-swap is proven. Evidence: `learnings/decomp_port.md` §6, ctest
`decomp_hsd`.

## S1 — Boot skeleton

**Goal.** Run the game's own entry (`src/melee/gm/gmmain.c:130`) with stubbed
OS/DVD/GX/VI and learn what it actually needs, in dependency order.

**Deliverable.** The decomp's `main()` links and runs to a controlled failure
trace, with an OS/DVD stub layer that logs each unimplemented call.

**Exit criteria.** A boot log that reaches an intentional stop, plus the
first concrete list of backend work items derived from it.

**Risks.** Arena/memory initialization assumptions; thread/interrupt boot
sequencing; the five incomplete units may need hand C.

## S2 — HSD runtime + GX HLE

**Goal.** Make the compiled HSD render.

**Deliverable.** The `src/sysdolphin` layer compiles fully; a GX backend
implements the command/vertex/TEV/texture path HSD uses, grown from the
prototype renderer (ES3-portable shaders stay).

**Exit criteria.** A character renders through the compiled HSD + GX path with
screenshot parity against the hand renderer; `--inspect` numbers are explained
where they differ (the compiled path uses real HSD structures).

**Risks.** The GX FIFO/vertex-format surface is bigger than the prototype's
batch model; display-list and FObj endianness must be handled by the backend
(see S3).

## S3 — Asset pipeline

**Goal.** Load every real disc asset through the compiled loaders.

**Deliverable.** A host-endian conversion pipeline (structural words, FObj
streams, display lists, textures; per-format), cached and versioned, driven by
the format knowledge already written down in `learnings/`.

**Exit criteria.** All 26 character archives, stages and common assets load;
cross-character bounds match `STATE.md`'s documented numbers or the deviation
is explained by the compiled path.

**Risks.** This is the least forgiving part: a wrong swap shows up as garbage
geometry, silent desyncs or crashes. The hand parser is the reference oracle.

## S4 — First match

**Goal.** The real game loop with real fighters, items and a real stage.

**Deliverable.** Compiled `ft`, `it`, `gr` and the `gm` scene loop; two
fighters on a stage with the engine's physics, collision, camera and stock
rules. This subsumes the old M3+M4.

**Exit criteria.** A deterministic scripted match runs headless; positions are
compared against the current sandbox where meaningful and against the decomp's
own constants where possible; 600-frame ASan run clean.

**Risks.** 64-bit/float divergences only show up in long simulations; input and
tick order must match. Budget time for differential debugging.

## S5 — Audio (done 2026-09-12)

**Result.** ADR-0013 option A: the decompilation's `axdriver.c`, the SDK's
pure-C AX voice layer and a new host software mixer (`native/audio/`) run the
game's own SFX/HPS code; `.ssm`/`.sem`/`.hps` convert in the DVD backend.
Boot/title music and match SFX play deterministically (two runs
byte-identical); `reverb_std` is ported from asm; quality is verified
headlessly (`ctest audio`, `ctest decomp_audio`, `--audio-dump`) and the
owner confirmed it sounds right (2026-09-13). Evidence:
`learnings/decomp_audio.md`, `STATE.md`.

## S6 — Frontend and saves

**Goal.** The game as a product, not just a match.

**Deliverable.** Compiled `mn` menus, character select, results, and memory
card/save handling (external format, platform layer).

**Exit criteria.** Boot → menu → select → match → results → save/load.

## S7 — Platforms and mods (stretch)

- Android (SDL2 + GLES3) and web (Emscripten + WebGL2) — the ES3-portable
  shaders and thin platform layer are deliberate groundwork; blockers are
  asset size/delivery, threading and audio.
- Windows/macOS parity.
- A mod layer: source-level hooks and data-driven asset overrides on top of the
  compiled game.
- Netplay (rollback) once the simulation is deterministic.

## Parallel / supporting work

- **P-601** full-tree compile census and shim hardening (S0).
- **P-602/P-603** the S0a/S0b probes.
- **P-401/P-402/P-403** CI, fuzzing and the GX format census stay useful for the
  port; the fuzz fixtures and GX documentation feed the backend.
- **P-501/P-502** audio and WASM memos are now on the critical path for S5/S7.

## Frozen work (do not start)

The hand-port engine items — P-204 leftovers, P-205..P-210, P-302, P-411,
P-412 — are superseded by compiled modules and are frozen (`TASKS.md` marks
them `parked`). Do not extend the hand HSD parser or renderer beyond fixes the
bring-up itself needs.

## Critical path

`S0 → S1 → S2 → S3 → S4 → S5 → S6 → S7`, with S5 able to overlap S4 once S3
lands. S0 is the go/no-go gate.

## What "done" looks like

Boot → menu → character select → a stock match on a real stage against a CPU or
a second player, with the retail game's movement, hitboxes, knockback and
animation; GX-correct rendering; original audio; saves; source-level modding;
and builds for Linux, Windows, macOS, Android and web — all from the user's own
disc image.

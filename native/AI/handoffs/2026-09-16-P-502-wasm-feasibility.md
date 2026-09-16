# Handoff: P-502 — WASM/browser feasibility audit for independent review

**Date:** 2026-09-16
**Agent:** codex (gpt-5), with three parallel read-only audit lanes
**Commit:** this documentation commit (see git history)
**Tree state:** documentation only; no build/runtime files changed. The shared
tree already contained another agent's uncommitted
`native/decomp/assets/hsd_convert.c` change and it was not touched or staged.

## What I did

- Audited the compiled game's build, memory, DVD, main-loop, GX, AX, input,
  card, and diagnostic boundaries for an Emscripten target.
- Ran the work in three read-only lanes: repository/compiler blockers,
  browser-platform constraints, and phased architecture/delivery design.
- Recorded the resulting plan as **proposed** ADR-0025. Nothing in the ADR is
  accepted yet and this task is deliberately in `review`.
- Checked the recommendations against official Emscripten and SDL3
  documentation; links are in ADR-0025.

## Findings the reviewer should try to falsify

### 1. The fixed MEM1 address is the first runtime blocker

`native/platform/os.c` maps 24 MiB at `0x80000000` and seeds the OS arena with
those pointers. `native/platform/dvd.c` writes boot/FST state into the same
range, and the SDK macros derive cached pointers by adding `0x80000000`.
Preserving the mapping in wasm requires reaching `0x81800000` (2,072 MiB)
before normal heap headroom. A production build should instead allocate MEM1
low and centralize address translation. The direct high-address
classifications found in compiled sources are concentrated in `lbfile.c`,
`lbmemory.c`, and `ftdata.c`; re-run the scan before assuming it is exhaustive.

### 2. The remaining big-endian bit-fields are a correctness blocker

`native/decomp/src/Runtime/platform.h` defines `PORT_BF_BE`, and
`native/decomp/src/melee/lb/types.h` defines `CMD_BE`, through GCC
`scalar_storage_order`. ADR-0022 records Clang 22 ignoring that attribute.
Emscripten is Clang-based, so suppressing the warning would silently change
the meaning of command scripts and flags. W0 must choose and test portable
masks/accessors or generated target-safe declarations while preserving all
layout assertions.

### 3. The current compile flags are not target-neutral

The compiled-game object target inherits `-m32`, `-malign-double`, SSE,
`-mfpmath=sse`, `-fexec-charset=CP932`, native EGL/GLES linkage, and POSIX
diagnostic assumptions. Local Clang 22 probes found:

- `--target=wasm32` can emit a wasm object;
- `-fexec-charset=CP932` is rejected;
- `-msse2` and `-mfpmath=sse` are not usable for wasm32; and
- `scalar_storage_order("big-endian")` is unsupported when unknown
  attributes are errors.

Fifteen compiled source/header files contain non-ASCII text. A build-generated
CP932 source view is one plausible solution, but it is not proven and should
not be accepted without a byte-level test using the existing Shift-JIS probe.
Four patched files also contain GNU `.globl/.set` aliases and need a wasm
linker audit.

### 4. The full game must yield without changing its state machines

`viewer_main.c` calls `gm_main()`, and the game remains inside nested scene
loops. `native/platform/gx_vi.c::VIWaitForRetrace` is the existing per-frame
platform seam that pumps completions, audio, callbacks, and presentation.
The fastest vertical slice is narrow Asyncify suspension there and at browser
disc waits. The reviewer should insist on a 600-frame size/performance/parity
measurement; a manual cooperative frame step is cleaner but much more
invasive. Pthreads should not be the default because they require
SharedArrayBuffer/cross-origin isolation and a separately built artifact.

### 5. Disc data should stay outside wasm memory

`native/platform/disc.c` already implements synchronous random ISO/CISO reads,
but a retail image is roughly 1.4 GiB. Do not preload it into MEMFS. Keep the
user-selected `File` in JavaScript, request `Blob` ranges behind a `read_at`
abstraction, and retain the CISO block logic. Asyncify can bridge those reads
for the first spike. WORKERFS is a worker-only alternative worth measuring,
not an assumed answer. Saves/settings can use IDBFS/OPFS later; the disc should
not be duplicated into persistent browser storage by default.

### 6. Reuse the renderer and audio boundaries

`gx_gl.c` is already GLSL ES 300/GLES3-shaped and has `gx_gl_attach`, so use
WebGL2 and compile out EGL creation. Do not restart the parked Aurora/WebGPU
path. Probe EFB depth/copy readback first. The AX mixer already emits 32 kHz
stereo through the SDL sink; start it only after a user gesture and measure
latency before considering an AudioWorklet backend.

## Exact next action

1. Read ADR-0025 and independently reproduce the evidence above, especially
   the `CMD_BE`/`PORT_BF_BE` behavior and all fixed/high-bit MEM1 assumptions.
2. Write a short review below ADR-0025 or amend it directly: accept, revise, or
   reject each numbered proposed decision. Resolve the `PORT_WASM` versus
   `PORT_PC` relationship explicitly.
3. If and only if the ADR is accepted, split W0 into small claimed tasks. The
   first implementation should be a compile/layout probe with a pinned
   Emscripten version, not the HTML shell or renderer.
4. Before any implementation edit, re-read `AI/agent_communication.md`; use a
   single owner for `native/CMakeLists.txt`, and do not touch GX files while a
   renderer claim is active.

Expected W0 output: all compiled-game TUs either link as wasm or appear in a
named failure census; every layout assertion remains enabled; a focused test
proves command/flag bit order and CP932 bytes; native CTest and the matched
GameCube build are unchanged.

## What I tried that did not work

- Treating the exact `0x80000000` mapping as a shipping design fails the
  memory budget: merely reaching the end of MEM1 requires 2,072 MiB.
- Treating `scalar_storage_order` as a warning-only issue is incorrect: Clang
  ignores the semantic attribute, so the resulting program can compile and
  still read the wrong bits.
- Preloading a complete ISO/CISO into MEMFS scales wasm memory with disc size
  and is therefore not viable.
- Starting with pthreads adds hosting headers, SharedArrayBuffer, worker
  proxying, and dual artifacts before profiling shows they are needed.
- Starting a new WebGPU renderer discards the already-portable GLES work and
  conflicts with ADR-0017.

## Open questions

- What exact portable representation replaces `CMD_BE`/`PORT_BF_BE` with the
  least source divergence? — needs a technical review: yes.
- Should the web target define both `PORT_PC` and `PORT_WASM`, or should the
  two be siblings with shared host helpers? — needs an architecture decision:
  yes.
- Does scoped Asyncify pass the size/frame-time gate, or is a frame-step
  refactor justified? — needs a measured spike: yes.
- Pin Emscripten's SDL3 port, or vendor/build SDL3 separately? — needs a
  reproducibility decision after the first compile probe: yes.
- Is Chromium + Firefox the initial release matrix, with Safari/mobile after
  the memory gate, or must mobile pass W0? — needs an owner product decision:
  yes.

## Files touched / claimed

- `native/AI/DECISIONS.md`
- `native/AI/TASKS.md`
- `native/AI/HANDOFFS.md`
- `native/AI/handoffs/2026-09-16-P-502-wasm-feasibility.md`
- `AI/agent_communication.md` for the temporary coordination claim only

No runtime, renderer, build, patch, test, decompilation, or asset-converter
file was edited.

## Verification run

Read-only evidence gathering only; no behavior changed, so the runtime suite
was not rebuilt. Before committing the documentation:

```sh
git diff --check
git diff -- native/AI AI/agent_communication.md
git status --short
```

The independent reviewer should add actual Emscripten compile/link commands
and results to the W0 handoff; this feasibility pass did not claim a pinned
Emscripten environment.

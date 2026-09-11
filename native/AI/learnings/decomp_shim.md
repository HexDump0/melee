# Compiling decompilation math behind a shim (P-301)

Outcome of the staged experiment: **pure-C HSD math compiles verbatim and is
now the single source of truth for `HSD_MtxSRT`; the Dolphin SDK math files
cannot be compiled at all and stay hand-ported.**  The narrow win was worth
landing; the "compile `extern/dolphin/mtx/*.c`" half of the plan is dead.

## 1. Recon: what is actually compilable

The handoff named three source files.  Their include chains and portability:

| Source | Includes (transitively) | GCC/Clang |
|---|---|---|
| `src/sysdolphin/baselib/mtx.c` | `mtx.h` -> `Runtime/platform.h`, `dolphin/mtx.h`, `objalloc.h` -> `debug.h` -> `dolphin/os.h` | **compiles** (one libc clash, see §2) |
| `extern/dolphin/src/dolphin/mtx/mtx.c` | `<dolphin.h>`, `<dolphin/mtx.h>` | **cannot compile** — Metrowerks asm |
| `extern/dolphin/src/dolphin/mtx/vec.c` | same | **cannot compile** — Metrowerks asm |

The SDK files are hybrid C/asm written for MWCC:

- function-level asm: `asm void PSMTXCopy(...) { psq_l f0, 0(src), 0, qr0 ... }`
  (`mtx.c` lines 71, 136, 313, 748; `vec.c` lines 18, 42, 65, 210, 288);
- inline `asm { ... }` blocks inside plain C functions: `PSMTXIdentity`
  (`mtx.c:38`), `PSMTXTranspose` (`:237`), `PSMTXTrans` (`:713`), etc.

GCC reports 55 errors on `mtx.c` and `1` on `vec.c` (`expected '(' before
'void'`, `unknown type name 'psq_l'`, `nofralloc`, ...).  No header shim can
fix this: the PPC instructions are in the `.c` body.  The portable `C_MTX*` /
`C_VEC*` twins live in the same translation units, so they cannot be compiled
either.  Options for the SDK layer are therefore exactly two:

1. hand-port the ~15 primitives the HSD layer calls (what this commit keeps
   doing), or
2. check in patched copies of the SDK files (`#ifdef __MWERKS__` around the asm
   like `ACGC-PC-Port/src/static/dolphin/mtx/vec.c` does) plus a PC backend.

Option 2 is a project-level decision, not a shim; it needs an ADR before it is
used for the fighter work.  Scale of the problem in this repo:

- asm files: **4 / 1034** under `src/` (3 `melee/`, 1 `sysdolphin/`),
  **20 / 148** under `extern/dolphin/src/` — concentrated in the SDK.
- `src/melee/ft`: 441 files, **0** with asm; however 175 include
  `<dolphin/mtx.h>`, so they call `PSMTX*`/`PSVEC*`.

## 2. Shim inventory

The shim is one file, `native/decomp/shim/decomp_shim.h`, force-included
(`-include`) into every decomp TU:

```c
#if defined(__linux__)
#define __ssize_t_defined 1
#endif
```

`src/Runtime/platform.h` typedefs `ssize_t` as `int`, but `<dolphin/types.h>`
includes `<stdio.h>` first, where glibc declares `ssize_t` as `long` behind
`__ssize_t_defined`; pre-defining the guard leaves the upstream typedef as the
only one (32-bit `int`, as MWERKS had).  This is the *only* platform clash in
the whole include chain — `dolphin/os.h`, `objalloc.h` and `debug.h` parse
unchanged.  The force-include is applied only to the `melee_decomp_math`
target; port objects never see it.

Do not use `#include_next` in a shim header: it is a GCC extension and trips
`-Wpedantic`, which shim headers must pass.  Do not shadow
`Runtime/platform.h` with a copy; it drifts from `src/`.

## 3. Build recipe

`native/CMakeLists.txt`:

```cmake
add_library(melee_decomp_math OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/sysdolphin/baselib/mtx.c)
target_include_directories(melee_decomp_math BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../extern/dolphin/include)
target_compile_options(melee_decomp_math PRIVATE
    -w -ffunction-sections -fdata-sections
    -include ${CMAKE_CURRENT_SOURCE_DIR}/decomp/shim/decomp_shim.h)
```

- `-w` for the upstream TU (`src/` must not be modified; it emits pedantic
  warnings and an unused variable).  The shim is warning-free.
- `-ffunction-sections -fdata-sections` plus the link's
  `-Wl,--gc-sections` (`target_link_options`) is what makes a *whole* decomp TU
  usable when only some functions are referenced: `HSD_MtxSRT` has no external
  calls, so every other section (including the allocator functions that
  reference `HSD_ObjAlloc`/`PSMTX*`) is discarded before symbol resolution.
  Without `--gc-sections` the link fails on `PSMTXCopy`, `PSVECMag`, etc.
- A thin port-side prototype header, `native/decomp/decomp_math.h`, mirrors
  `Mtx`/`Vec3` for port code; the upstream headers cannot be included from the
  port build (platform chain clash in §2).

## 4. Parity

`tests/test_decomp_mtx.c` (`ctest -R decomp_mtx`) checks `HSD_MtxSRT` against:

1. a literal transcription of the deleted hand copy in `hsd/model.c`
   (differential oracle per `AGENTS.md` §0.1);
2. an independent `gx/math.c` composition `T * Rz * Ry * Rx * S` (proves the
   row-major 3x4 -> column-major `Mat4` layout too).

```
decomp_mtx: 100000 samples, ref worst 0, gx oracle worst 3.57627869e-06
decomp_mtx: all checks passed
```

The compiled function is **bitwise identical** to the hand copy over 100k
randomized SRTs including the parent-scale correction.  Replaced in the same
commit:

- `hsd/model.c:make_local_mtx` — formula body deleted, thin `Vec3` marshaller
  left behind; both bind-pose and animated call sites unchanged.
- Verification: `--inspect` output and the `--view`, `--view --animate`, and
  `--scripted` BMPs are byte-identical to the pre-change build (sha256
  compared, all three).

## 5. What this means for P-302 (effort)

- **Math shims save nothing by themselves.**  The hand math is finished and
  tested (`gx/math.c`, `aobj.c`, `model.c`); the SDK primitives it needs are
  asm and must stay hand-ported.  This commit only removes ~50 lines of
  duplicated SRT formula and gives P-302 a working compile recipe.
- **The real payoff is the fighter/engine code**: ~46.9k LOC in
  `src/sysdolphin/baselib` (15.9k in GX-touching files) and ~137.8k LOC in
  `src/melee/ft`.  Of those, only a subset is needed for M3/M4, but it is tens
  of thousands of lines versus ~6.2k LOC of hand-port produced so far.
- **The real cost is not the shim**: it is (a) a portable PC backend for the
  asm SDK primitives (~15 functions, small) and (b) the GX/OS/allocator
  surface.  `jobj.c`, `gobj.c`, `dobj.c`, `aobj.c`, `fobj.c`, `spline.c` are
  GX-free; `pobj.c`, `cobj.c`, `displayfunc.c` have 118 GX call sites, and the
  port's existing renderer (`gx/render.c`) would have to be bridged or
  replaced.  The `Fighter` struct ABI question from P-302 is untouched.

**Recommendation:** before betting the fighter work on compiling `src/`, run a
second, scoped spike: pick one representative GX-light fighter file (e.g.
`src/melee/lb/lbanim.c`, 145 LOC, zero GX) and compile it against a minimal
HSD API surface plus the hand SDK backend.  If the shim stays under a few
hundred lines, invest in the backend; if it mushrooms, keep hand-porting.

## Reproduce

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
ctest --test-dir build/native --output-on-failure
./build/native/melee --inspect
```

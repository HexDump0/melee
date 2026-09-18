# Building for Windows

Status: **the port layer compiles, nothing links yet.** 43 of 56 port-layer
translation units build with `i686-w64-mingw32-gcc`, and all 200 sampled
`decomp/src` units build with zero errors. What is left is three third-party
dependencies, not portability work.

## The flags, and why each one is load-bearing

```sh
i686-w64-mingw32-gcc \
  -m32 -malign-double -mno-ms-bitfields \
  -msse2 -mfpmath=sse -fno-strict-aliasing -fgnu89-inline -std=gnu11 \
  -DPORT_PC=1 -DLINT \
  -I native/decomp/shim -I native -I decomp/src \
  -I decomp/extern/dolphin/include -I mods/include \
  -include native/decomp/shim/decomp_shim.h
```

Link with:

```sh
-Wl,--large-address-aware
```

### `-mno-ms-bitfields` — **new, and Windows-only**

MinGW defaults to **MS bitfield layout**, which is not what the decompilation
was laid out for. Measured: without it, `mod_cobj.c` alone fails three
`ASSERT_SIZE` checks, and the census fails four distinct ones —
`sizeof(struct Fighter) == 0x23EC`, `gmm_x0 == 0x8518`,
`gmm_x0_vsmodes`, `VsSceneController == 0x2528`. With it, **zero**.

This is the single most dangerous flag on the list, because the failure mode
without `-DLINT` is *silent*: the structs are simply laid out differently and
the game reads the wrong offsets.

### `-DLINT` — how the above was caught at all

Enables the 190 `ASSERT_SIZE`/`ASSERT_OFFSET` static assertions
(`Runtime/platform.h`). Without it they expand to nothing, and a layout
mismatch compiles clean. Never build this tree without it.

### `-Wl,--large-address-aware` — the 24 MB at `0x80000000`

`os.c` maps GC main RAM at a fixed `0x80000000`, and the decompilation
branches on that address (`lbFile_800164A4` picks a DVD read type from
`dst >= 0x80000000`). A 32-bit process is confined to `0x00000000-0x7FFFFFFF`
unless it is large-address aware **on a 64-bit host**.

Measured under Wine: the flag moves `lpMaximumApplicationAddress` from
`0x7FFEFFFF` to `0xFFFEFFFF`, and `VirtualAlloc` returns exactly
`0x80000000`. **Caveat worth keeping:** Wine allocated there *without* the
flag too, so Wine is not enforcing the 2 GB ceiling and cannot prove the flag
is required. Real Windows does enforce it. Do not read the Wine result as
confirmation that the flag is optional.

### The rest

`-m32` and `-malign-double` are ADR-0012 and struct layout. `-msse2
-mfpmath=sse` is `MELEE_32BIT_FP_OPTIONS`; `-fno-strict-aliasing` is
`MELEE_DECOMP_UB_OPTIONS`, and it matters because 72 sites mask a float's
sign bit through an `s32*`.

## What is ported

| | |
|---|---|
| `os.c` | `mmap(MAP_FIXED_NOREPLACE)` → `VirtualAlloc(MEM_RESERVE\|MEM_COMMIT)` |
| `boot_triage.c` | `backtrace`/`dladdr` → DbgHelp `CaptureStackBackTrace`/`SymFromAddr`; `sigaction` → `AddVectoredExceptionHandler`; `sigsetjmp` → `setjmp` behind `BOOT_SETJMP` |
| `card.c`, `hsd_convert.c` | `mkdir(path, mode)` → `melee_mkdir` (`platform/port_fs.h`) |
| `sfx_debug.c` | its private `backtrace_symbols` dump is POSIX-only; boot_triage owns the platform stack walk |

**One guarantee is weaker on Windows.** The POSIX crash reporter runs on a
`sigaltstack`, so a stack overflow can still be reported. Windows has no
equivalent: the handler runs on the stack that just overflowed, with one guard
page of grace. `CaptureStackBackTrace` fits inside it where glibc's
`backtrace()` did not, so the common case prints — but a stack-overflow report
from a Windows build is less trustworthy than the same report on Linux.

## What is left

1. **SDL3** (9 files) — cross-build for `i686-w64-mingw32`; it is an ordinary
   CMake cross-build, not in Arch's repos.
2. **EGL/GLES3 headers** (2 files) — only `gx_gl_init`, the *headless* probe
   path, needs EGL. The game takes its context from SDL
   (`SDL_GL_CONTEXT_PROFILE_ES` 3.0, `viewer_main.c`), so **ANGLE is a
   fallback, not a prerequisite**: NVIDIA and Intel drivers expose
   `WGL_EXT_create_context_es2_profile` natively. Try without ANGLE first.
3. **WAMR** (`mod_wasm.c`) — supports Windows upstream; a build with mods
   disabled links without it.
4. `boot_main.c` — the headless harness, not the game binary. Needs a Windows
   watchdog (`alarm` → a timer thread) and the vectored handler. Not on the
   path to a playable `.exe`.

## Testing without Windows

Wine 11.15 runs the cross-built binaries, which is how the memory-map spike was
answered. Treat it as a smoke test: as the caveat above shows, Wine is more
permissive than Windows about address space, and it will not catch everything.

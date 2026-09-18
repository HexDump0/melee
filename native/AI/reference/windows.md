# Building for Windows

Status: **it builds and runs.** `native/tools/windows_build.sh` compiles all
1046 translation units and links an 18 MB `melee.exe` that needs only
`SDL3.dll` beside it. Verified under Wine: `melee.exe --controls` prints the
same binding table as the Linux build, byte for byte.

```sh
# once: cross-build SDL3 (see "Dependencies" below)
native/tools/windows_build.sh /tmp/melee-win /path/to/sdl3-prefix
```

**Not yet run as a game.** `--controls` exercises startup, the config layer and
SDL, but nothing has opened a window or read a disc on Windows, so the GL path
and the `0x80000000` mapping are unproven *in the real binary* -- the mapping
was proven separately by a standalone spike.

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

## Dependencies

**SDL3**, cross-built — it is not in Arch's repos and the build is ordinary:

```sh
cmake -S SDL3-3.4.14 -B b -DCMAKE_TOOLCHAIN_FILE=mingw-toolchain.cmake \
      -DCMAKE_INSTALL_PREFIX=$PWD/sdl3-prefix \
      -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build b -j2 && cmake --install b
```

**The GLES3 API** comes from `native/third_party/khronos` (see
`THIRD-PARTY.md`). Linux links `libGLESv2` and calls the functions directly;
**Windows has no such library**, so `native/gx/gl_api.h` declares the 80 entry
points the port uses as pointers and fetches them with
`SDL_GL_GetProcAddress`. The macro keeps the call sites reading as ordinary GL
code on both platforms.

**ANGLE is a fallback, not a prerequisite.** The game takes its context from
SDL (`SDL_GL_CONTEXT_PROFILE_ES` 3.0), and EGL appears only in `gx_gl_init`,
the headless probe, which is compiled out on Windows. NVIDIA and Intel drivers
expose `WGL_EXT_create_context_es2_profile` natively. If a machine cannot give
an ES 3.0 context, dropping ANGLE's `libEGL.dll` and `libGLESv2.dll` beside the
exe is the fix, and no code changes.

**WAMR is not needed.** Mods are compiled in through `mod_builtin.c`, the same
binding the browser build uses (ADR-0026), so `mod_wasm.c` is left out.

## What is left

1. **Run it as a game on real Windows** — a window, a disc, the GL path. This
   is the only unproven part.
2. `boot_main.c` — the headless harness, not the game binary. Needs a Windows
   watchdog (`alarm` → a timer thread). Not on the path to a playable `.exe`.
3. The `--large-address-aware` requirement still wants confirmation on real
   Windows; Wine does not enforce the ceiling it would test.

## Problems this hit, so the next person does not

**`<printf.h>`** — glibc has one, MinGW does not. The shim already had a
fallback but tested for wasm specifically.

**Per-file defines.** The native target sets eight of them with
`set_source_files_properties` (`-Dmain=gm_main`, `MELEE_ARCHIVE_INTERNAL`, …).
They are duplicated in `windows_build.sh`; if you add one to CMakeLists, add it
there too or the link fails with a missing `gm_main`.

**The underscore.** 18 symbols are defined by inline GAS aliases —
`lbl_8046E38C` is `Results_block_8046E1B0 + 0x1DC`. **i386-PE prefixes C
symbols with `_`; ELF does not**, so every alias resolved to nothing and the
link failed with undefined references to symbols defined two lines above.
`MELEE_ASM_ALIAS` in the shim uses `__USER_LABEL_PREFIX__`, which is the
compiler's own answer.

**`libwinpthread-1.dll`.** MinGW links it dynamically by default. The link
statically resolves it instead, so the result is `melee.exe` + `SDL3.dll` and
nothing else.

## Testing without Windows

Wine 11.15 runs the cross-built binaries, which is how the memory-map spike was
answered. Treat it as a smoke test: as the caveat above shows, Wine is more
permissive than Windows about address space, and it will not catch everything.

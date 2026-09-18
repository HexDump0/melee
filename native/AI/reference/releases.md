# Release assets — the naming scheme

The launcher downloads the port from GitHub releases and picks its asset **by
name**. That name is a contract between whatever publishes a release and
`launcher/src-tauri/src/release.rs`, so it is written here rather than only in
the code. Change one and you change both.

## The scheme

```
melee-<os>-<arch>[.exe]
```

| Field | Values |
|---|---|
| `os` | `linux`, `macos`, `windows` |
| `arch` | `x86` |
| `.exe` | Windows only |

So a release publishes, as its assets:

```
melee-linux-x86
melee-windows-x86.exe
```

**`arch` is the *port's*, and the port is 32-bit** (ADR-0012: `archive.c`
relocates pointers in place into `u32` slots). The launcher is an ordinary
64-bit binary, so it must not derive the architecture from its own
`cfg!(target_arch)` — the first version did, asked for an `x86_64` asset that
will never exist, and the failure would have read as "no build for your
platform" rather than as the bug it was. `release.rs` pins it in `PORT_ARCH`
with a test that fails if it ever says `x86_64` again.

The `os` **is** taken from the launcher's own target, correctly: it runs on the
machine that will run the port.

The launcher builds the name it wants from its own `cfg!(target_os)` and
`cfg!(target_arch)`, asks the GitHub API for the latest release, and takes the
asset whose name matches exactly. There is no fuzzy matching and no fallback
to "the only asset": a wrong binary is worse than a clear failure, and when
nothing matches the launcher lists what the release *did* contain, because "no
build for your platform" with no list costs somebody an afternoon.

## Raw binaries, not archives

Each asset is the executable itself, uncompressed and untarred.

The port is a single binary and takes its assets from the player's own disc
image, so there is nothing to bundle alongside it. Shipping a `.tar.gz` would
mean carrying `tar`, `flate2` and `zip` into the launcher in order to unpack
something with one file in it, and every one of those is a dependency that has
to be kept and audited for as long as the launcher exists.

**Windows keeps that promise by linking SDL statically.** A shared build needs
`SDL3.dll` beside the exe, which would have made Windows the one platform
whose release is two files and forced the launcher to learn about companion
downloads. `MELEE_WIN_LINK=static` (the default in
`native/tools/windows_build.sh`) produces a 22 MB `melee.exe` with no SDL
import at all -- verified by running it from a directory containing nothing
else. The side effect is the **GUI subsystem**: no console window appears on a
double-click, and nothing prints if you run it from `cmd.exe`. The launcher is
unaffected, because it reads the child's pipes rather than a console.
`MELEE_WIN_LINK=shared` gives the console build for terminal debugging.

Linux links the system SDL3 as usual: there the library is a package, not
something a player has to be handed.

**If a release ever needs more than the binary** — a data folder, a licence, a
default `melee.toml` — that decision changes, and this file is where it
changes first. Do not quietly start publishing archives; `release.rs` writes
the downloaded bytes straight to disk and marks them executable, so an archive
would install as a corrupt binary that fails at exec time with nothing useful
in the message.

## What the launcher does with it

- Installs to `$XDG_DATA_HOME/melee-unbound/bin/` (never next to the launcher,
  which may be installed somewhere the user cannot write).
- Downloads to `<asset>.part` and renames, so an interrupted download cannot
  leave a half-written binary where a working one used to be.
- Marks it `0755` on Unix.
- Records the path in `~/.config/melee/launcher.json`, and prefers it over any
  locally built `build/native/melee`.

The repository it queries is `HexDump0/melee`, the `REPO` constant in
`release.rs`.

## Publishing a release

`.github/workflows/release.yml` does it. Four jobs: the Windows port
(cross-compiled from Linux, the path that was actually proven), the Linux port
(32-bit, building SDL3 from source because no distribution ships one), the
launcher (a matrix, built on its own OS because Tauri cross-compilation is not
worth the trouble), and publish.

```sh
# 1. the gates, all three
cmake --build build/native -j2 && (cd build/native && ctest -j2)
(cd decomp && ninja) && sha1sum decomp/build/GALE01/main.dol   # must match build.sha1
git status --short                                              # must be empty

# 2. the version, in all three places it is written
#    launcher/src-tauri/tauri.conf.json, launcher/src-tauri/Cargo.toml,
#    mods/unbound/mod.toml

# 3. a dry run before the tag, so a failure does not strand one
gh workflow run release.yml --ref master

# 4. tag and push
git tag -a v1.0.0 -m "Melee Unbound 1.0.0" && git push origin v1.0.0
```

**Do the dry run.** A tag that fails to build leaves a tag pointing at a commit
with no assets, and the fix is either a force-push or a v1.0.1 that exists only
because of a typo. `workflow_dispatch` is enabled for exactly this.

The assets land as `melee-linux-x86`, `melee-windows-x86.exe`, the launcher's
`.deb`/`.AppImage`/`.msi`, and generated notes.

**After the first release**, check the launcher end to end: Check for update →
Install → Launch. That is the first time the naming scheme is exercised by the
thing it exists for, and a mismatch there is silent until someone tries it.

## What the first dry run found

Worth keeping, because both are the kind of thing that only appears on a
runner.

**Windows needed `g++-mingw-w64-i686`.** The port is C, but SDL3's CMakeLists
calls `enable_language(CXX)`, so configuring fails without a C++ cross
compiler that nothing ever uses.

**Ubuntu's i386 is a partial architecture**, and that cost two CI runs before
the right fix was obvious. It carries what Wine and 32-bit games need and
little else, so building anything 32-bit there means discovering one missing
`:i386` dev package per run: first `libglib2.0-dev` (pulled in by
`libpulse-dev`), then `libxcursor-dev`, and however many more were queued
behind it.

The fix is not a longer package list, it is **Debian**: the job runs in a
`debian:bookworm` container, where i386 is complete. That ends the class rather
than the instance, and it has a second benefit -- an older glibc than the
runner's, so the released binary runs on more distributions rather than fewer.

Two habits that came out of it and are worth keeping:

- **Install before checkout.** `actions/checkout` needs `git` inside the
  container; without it the submodules are silently skipped and the failure
  arrives much later, looking like something else.
- **Check all the headers at once.** A step that verifies every header the
  build needs (`GL/gl.h`, `EGL/egl.h`, `GLES3/gl3.h`, `X11/Xcursor/Xcursor.h`,
  …) turns "one missing package per run" into one run that names all of them.

PulseAudio and Wayland stay off regardless, so the released Linux binary is
**X11 and ALSA**: Wayland desktops run it through XWayland and PulseAudio or
PipeWire through their ALSA compatibility layer, which is how most 32-bit Linux
games already work.

The launcher job passed first time, AppImage included -- which is the answer to
the one thing that could not be checked locally.

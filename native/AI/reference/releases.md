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

The binary the port builds is `build/native/melee` (32-bit x86, see ADR-0011).
A release job renames it per the scheme above and uploads it; nothing else in
the tree needs to know the scheme.

**Not yet automated.** There is no release workflow in this repository, so
until one exists the launcher's Download button reports "no release found",
which is the honest result rather than an error.

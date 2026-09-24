<p align="center">
  <a href="https://melee.hexdump0.pw">
    <img src="assets/melee-unbound-banner.svg" alt="Melee Unbound" width="720">
  </a>
</p>

<h1 align="center">Melee Unbound</h1>

<p align="center">
  <a href="https://melee.hexdump0.pw/play"><img alt="Play in browser" src="https://img.shields.io/badge/play-in_browser-a08aff?labelColor=000000"></a>
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-linux_%7C_windows_%7C_wasm-a08aff?labelColor=000000">
  <a href="https://discord.gg/87kwPPetPD"><img alt="Discord" src="https://img.shields.io/badge/discord-join-5865F2?labelColor=000000&logo=discord&logoColor=white"></a>
</p>

A native port of **Super Smash Bros. Melee** (v1.02 / Rev 2, `GALE01`) that
compiles the game's own decompiled source natively and adds several quality of life features

> Bugs and crashes might be present. This project is under active development and is not fully stable yet.

## Features

- **Source** Built directly from
  [`doldecomp/melee`](https://github.com/doldecomp/melee); the port
  is compiled natively from it with a small set of patches in `patches/`.
- **Renderer.** A custom GX high-level emulator translates display lists to
  OpenGL/GLES 3, including TEV combiner shaders, texture decode and the
  asset converter that reads models, stages and effects off disc.
- **Audio.** AX is emulated on the host: a software mixer for the game's
  voices, sequences and ADPCM banks..
- **Mods.** WebAssembly mods run under WAMR through a small host ABI
  (`mod.toml` + `.wasm` in `mods/`). The QoL features are a mod
- **Browser.** Compiles to wasm32 (Emscripten, SDL3, WebGL2,
  JSPI).
- **Launcher.** A Tauri app for settings, mods and crash reports etc

## Play it

**In a browser**: <https://melee.hexdump0.pw/play>: 

**Prebuilt releases**: [Releases](https://github.com/HexDump0/melee/releases)

## Build

**Requirements**: a 32-bit toolchain
(`gcc-multilib`), CMake, Ninja, SDL3 (`lib32-sdl3`), and Mesa with 32-bit
EGL/GLESv2

```sh
git clone --recurse-submodules https://github.com/HexDump0/melee
cd melee
./scripts/apply_decomp_patches.sh            # idempotent; CMake runs it too
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j"$(nproc)"
ctest --test-dir build/native                # full regression suite
```

```sh
native/tools/windows_build.sh /tmp/melee-win /path/to/sdl3-prefix
```

See [`native/AI/reference/windows.md`](native/AI/reference/windows.md) for the
toolchain, the static-link default and what is still unverified there.

For the browser build


```sh
source ~/projects/emsdk/emsdk_env.sh          # Emscripten 6.0.9, pinned
site/scripts/build-play.sh
```

## Running and testing

```sh
./build/native/melee                       # retail frontend, live input
./build/native/melee --match               # a debug match, no menus
./build/native/melee --match 600           # ...for 600 frames, then exit
ctest --test-dir build/native              # headless suite
```

Look at [`native/AI/TESTING.md`](native/AI/TESTING.md) for a list of all the configurable options 

## Mods

A mod is a directory in `mods/` with a `mod.toml` manifest and a WebAssembly
module

## Repository layout

```
.
├── native/          the port: platform layer, GX HLE, renderer, viewer, tests
│   └── AI/          port agent hub: STATE.md, ROADMAP*.md, TASKS.md, learnings/
├── launcher/        Tauri shell around the port (settings, mods, crashes)
├── site/            the website, including the browser build at /play
├── mods/            WebAssembly mods; unbound/ is the default one
├── patches/         #ifdef PORT_PC portability patches for the decompilation
├── scripts/         setup helpers (patch application)
├── assets/          logo artwork, boot animation, gameplay capture
├── AI/              decompilation agent hub (claims, workflow, learnings)
└── decomp/          git submodule: doldecomp/melee (pinned, never committed into)
```

## Legal

A fan project. Not affiliated with or endorsed by Nintendo. No game assets are
distributed in this repository, and none ever will be: you need your own copy
of Melee v1.02 (`GALE01`) to build or run anything that loads data. The
decompilation in `decomp/` is [`doldecomp/melee`](https://github.com/doldecomp/melee)'s
work under its own terms; the port's own code in `native/`, `launcher/` and
`site/`
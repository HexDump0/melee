# Melee PC port (native/)

A native PC port of **Super Smash Bros. Melee (v1.02 / Rev 2, `GALE01`)** that
compiles the game's own decompiled source and replaces only the GameCube
hardware around it (OS, DVD/asset loading, GX→OpenGL/GLES, AX→audio, input,
memory card). Game logic comes from the decompilation, not from a rewrite.

## Repository layout

```
.
├── native/          the port: platform layer, GX HLE, renderer, viewer, tests
│   └── AI/          port agent docs: STATE.md, ROADMAP*.md, TASKS.md, learnings/
├── AI/              decompilation agent hub (claims, workflow, learnings)
├── patches/         #ifdef PORT_PC portability patches for the decompilation
├── scripts/         setup helpers (patch application)
└── decomp/          git submodule: doldecomp/melee (pinned)
```

`decomp/` is **not** ours: it is a pinned checkout of
[`doldecomp/melee`](https://github.com/doldecomp/melee). The port never commits
inside it. The few host fixes the decompilation needs live in `patches/` and
are applied to the submodule work tree by `scripts/apply_decomp_patches.sh`
(the CMake configure step runs this automatically).

## Setup

```sh
git clone --recurse-submodules <this repo>
cd melee
./scripts/apply_decomp_patches.sh          # idempotent; cmake also does this
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j"$(nproc)"
```

A disc image of the retail game is required for anything that loads assets;
put it in `iso/` (git-ignored) or pass a path to the tools. No game data is
committed to this repository.

## Running

```sh
./build/native/melee --inspect                 # disc → HSD pipeline smoke test
./build/native/melee_decomp_boot --boot-frames 600 --boot-timeout 90
./build/native/melee_decomp_viewer             # interactive compiled viewer
./build/native/melee_decomp_viewer --match     # live compiled match
ctest --test-dir build/native                  # full regression suite
```

Agent workflow, milestones and current state: [`native/AI/STATE.md`](native/AI/STATE.md),
[`native/AI/ROADMAP.md`](native/AI/ROADMAP.md), [`native/AI/TASKS.md`](native/AI/TASKS.md).
Repository rules for agents: [`native/AI/AGENTS.md`](native/AI/AGENTS.md).

## Updating the decompilation

```sh
git -C decomp fetch origin master
git -C decomp checkout <new-pin>           # or: git submodule update --remote decomp
./scripts/apply_decomp_patches.sh          # fails loudly if a patch conflicts
git add decomp && git commit -m "Update decomp submodule"
```

Fix any patch conflict inside `patches/`, never by editing `decomp/` directly:
the submodule must stay reproducible from the pin.

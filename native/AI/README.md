# Melee native port — AI coordination hub

This folder is the shared memory and operating manual for every agent working
on the native port (`native/`). Read it before touching code.

The GameCube decompilation has its own hub at `/AI/` (locally excluded). That
one is about matching PowerPC assembly. **This one is about porting the game to
PC/WASM.** Do not mix the two workflows or move files between them.

## Start here

| Read | Why |
|---|---|
| [`AGENTS.md`](AGENTS.md) | Hard rules. Violating these wastes hours. |
| [`STATE.md`](STATE.md) | What exists today, what is broken, current metrics. |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | How the demo is put together and where to change it. |
| [`TASKS.md`](TASKS.md) | Workstreams, open tasks, and how to claim one. |
| [`TESTING.md`](TESTING.md) | How to run and verify without a display. |
| [`gotchas/GOTCHAS.md`](gotchas/GOTCHAS.md) | Mistakes that already cost time once. |

## Quick start for a new agent

```sh
# 1. Sync
git pull

# 2. Read the cold-start set
cat native/AI/AGENTS.md native/AI/STATE.md native/AI/TASKS.md

# 3. Claim a task by editing TASKS.md (see the claim protocol inside)

# 4. Build and verify the current baseline
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
./build/native/melee-demo --inspect

# 5. Do the work, verify per TESTING.md, write a learning/gotcha if you learned
#    anything non-obvious, record a handoff if you stop mid-task, commit.
```

## Directory map

```
native/AI/
├── README.md              <- this file
├── AGENTS.md              <- rules of engagement
├── ARCHITECTURE.md        <- design and data flow
├── STATE.md               <- as-built status snapshot (update it!)
├── TASKS.md               <- backlog + claims
├── ROADMAP.md             <- milestone plan
├── TESTING.md             <- verification protocol
├── HANDOFFS.md            <- how to hand off work + index
├── DECISIONS.md           <- architecture decision log (ADR)
├── learnings/             <- durable technical knowledge
│   ├── README.md
│   ├── hsd_archive_format.md
│   ├── hsd_models_and_skinning.md
│   ├── gx_display_lists.md
│   ├── gx_textures.md
│   ├── disc_assets.md
│   └── fighter_data.md
├── gotchas/
│   ├── README.md
│   └── GOTCHAS.md         <- numbered, symptom -> cause -> fix
├── workflows/
│   ├── inspect_an_asset.md
│   ├── add_a_character.md
│   ├── debug_rendering.md
│   └── verify_headless.md
├── reference/
│   ├── glossary.md
│   ├── repo_file_map.md
│   └── external_resources.md
├── templates/
│   ├── learning.md
│   ├── gotcha.md
│   ├── handoff.md
│   ├── decision.md
│   └── task.md
├── handoffs/              <- dated handoff notes, one file per session
└── logs/                  <- optional long-form session logs
```

## The one-paragraph summary

`native/` is the native port. It reads a user-supplied disc image, decodes HSD
model archives and GX textures, and renders a two-player sandbox with SDL2 +
OpenGL. Models, textures, movement attributes and animation all come from the
disc and are evaluated with ports of the engine's own functions: bind-pose and
per-frame envelope/shared/rigid skinning, the `fobj.c` curve player, and
`ftAnim_8006F4C8`-style FigaTree binding. The long-term goal is to keep
replacing original sandbox code with decompiled engine code (`src/`) until the
port is faithful, adding audio, menus, netplay and a WASM target, without ever
requiring the user to redistribute game assets.

# External resources

Use judgement: verify against the disc data in this repo before trusting any
third-party description of a format.

## Primary

- `doldecomp/melee` — this decompilation. `src/sysdolphin`, `src/melee/ft`.
- `encounter/objdiff` — diffing tool used by the decomp; already configured in
  the repo. Not needed for the port directly.
- The retail disc image (user-supplied) — the ground truth for all formats.

## Reference ports and tools

- **ACGC-PC-Port** (https://github.com/flyngmt/ACGC-PC-Port) — Animal Crossing
  GameCube PC port, local checkout at `ACGC-PC-Port/`. Useful for: SDL/GL
  platform structure, GX texture decoders, disc/CISO reading, `pc_main.c`
  layout. **Not** reusable for Melee display lists: its `GXCallDisplayList`
  interprets Animal Crossing's custom N64-GBI-derived PC command stream, and it
  requires a 32-bit build. Read, don't copy.
- **HSDLib / HSD viewers** — community HSD parsers in other languages. Useful
  as a cross-check for struct offsets; prefer the decomp as the source of
  truth.
- **Melee modding wikis / disc maps** — helpful for asset naming
  (`Pl*`, `Gr*`) but verify names against the FST.

## GameCube references

- **YAGCD** (Yet Another GameCube Documentation) — disc format, FST, CISO
  context.
- **libogc / Dolphin SDK headers** — `extern/dolphin/include` in this repo is
  the authoritative GX enum source for our purposes.

## SDL / OpenGL

- SDL2 wiki: window, GL context, game controller, `SDL_VIDEODRIVER=offscreen`.
- OpenGL 2.1 fixed-function reference (docs.gl) — used for the demo renderer.
- Mesa `llvmpipe`/`radeonsi` — the software/hardware renderers used headlessly.

## Keeping this list useful

- Add a link only if you actually used it and can say what for.
- Note the version/date if the resource changes often.
- Never link to sites that distribute the game or extracted assets.

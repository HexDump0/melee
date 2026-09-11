# Workflow: add or verify a character

## Selecting a character today

No code change is required for a new character model:

```sh
./build/native/melee --view --model PlFxNr.dat      # Fox
./build/native/melee --view --model-index 8         # by list position
./build/native/melee --model PlPkNr.dat --inspect   # parser only
```

The viewer's `N`/`P` keys cycle through every `Pl*Nr.dat`.

## Verification checklist for a new character

Run all of these and record the numbers in `STATE.md`:

```sh
./build/native/melee --model PlXXNr.dat --inspect
./build/native/melee --model PlXXNr.dat --inspect --list-parts
SDL_VIDEODRIVER=offscreen ./build/native/melee --model PlXXNr.dat \
    --view --frames 3 --screenshot /tmp/xx.bmp
```

- Triangle count is plausible (Melee fighters are roughly 4k-8k triangles).
- Bounds are roughly 5-25 units tall and centered near the origin.
- Textures decode (count > 0) and the screenshot is recognizable.
- No ASan/UBSan report during a 600-frame scripted run.

## Character-specific work

Per-character gameplay needs more than a new file:

1. `ftDataXX` attributes (`PlXX.dat`, not the model): extend
   `game/attributes.c` into a generic attribute loader instead of Mario-only.
   Acceptance: `--model PlFxNr.dat` reports Fox values.
2. Animation: P-201. Every character shares the same `AObj` format.
3. Parts/visibility for face expression meshes: see `learnings/fighter_data.md`.

## Do not

- Hardcode per-character texture formats; decode generically.
- Special-case a character in the parser. If the format differs, treat it as
  an unknown variant and document it first.

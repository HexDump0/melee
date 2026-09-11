# Workflow: inspect an unknown asset

Use this when you need to understand a `.dat`, a stage, or a new HSD structure.

## 0. Safety

Work on a copy outside the repo, or read the disc directly. Never commit asset
bytes. Put probes in `/tmp/opencode` and port findings into `learnings/`.

## 1. Find the file

```sh
./build/native/melee --list-models            # Pl*Nr.dat
./build/native/melee --all-models --list-models
```

For arbitrary names, write a short Python FST lister (see
`learnings/disc_assets.md`) or extend `disc_list` usage temporarily.

## 2. Extract it once

Use Python with the CISO/FST code from `learnings/disc_assets.md` and dump the
file to `/tmp/opencode/<name>.dat`. Do not add extraction to the shipped tool
without a decision entry.

## 3. Parse the header

```
file_size data_size nb_reloc nb_public nb_extern
```

Data starts at 0x20. The public symbol table gives you named roots; pick the
`_joint` one for models. Write these down.

## 4. Validate the structure in Python first

- Walk `child`/`next`/`dobjdesc`, print counts and bounds.
- For vertices, decode descriptors and compare adjacent strip vertex positions
  (they should be close for a coherent mesh).
- Histogram display-list first bytes; only draw opcodes are valid.
- Check `bind_world * stored_mtx == I` for joints with a matrix.

Fast iteration beats C compile cycles. Verify one non-obvious fact at a time.

## 5. Port the finding to C

Add the minimal parser code to `native/hsd/model.c` with bounds checks. Keep
the Python probe only if it is a test; otherwise delete it.

## 6. Verify with the tool

```sh
./build/native/melee --inspect --list-parts
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 3 \
    --screenshot /tmp/asset.bmp
magick /tmp/asset.bmp /tmp/asset.png
```

Inspect the image. Use `--part-mode only --part N` to isolate suspicious parts.

## 7. Record

- Format facts → `learnings/`.
- Traps → `gotchas/GOTCHAS.md`.
- Numbers that must not regress → `STATE.md`.

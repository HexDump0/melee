# Workflow: debug rendering

Work from the data outward, not from the screen inward. Most "renderer bugs"
are decode bugs.

## 1. Classify the symptom

| Symptom | Likely layer |
|---|---|
| No geometry at all | parser / display list framing (G-001, G-011) |
| Spikes, shards, stretched seams | vertex stream desync or wrong matrix (G-003, G-005) |
| Model is a blob | skinning space (G-006, G-007) |
| Correct shape, wrong colors | textures / materials / UVs (G-013) |
| Some parts invisible | culling, material alpha, or part visibility (G-017) |
| Correct single frame, wrong in motion | per-frame state, not geometry |

## 2. Data-first checks

```sh
./build/native/melee-demo --inspect
./build/native/melee-demo --inspect --list-parts
./build/native/melee-demo --dump-tev        # per-batch GX material state
```

- Triangle count and bounds must match `STATE.md` for Mario (bounds include the
  `model_scaling` factor).
- Does the part list show geometry where you expect it?
- Does the batch's `rm`/`flags`/colormap match the decomp's `MObjMakeTExp`
  branch? `learnings/hsd_tev_materials.md` has the mappings (G-042..G-044).

## 3. Isolate

```sh
# one part, textured
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view \
    --part 12 --part-mode only --frames 3 --screenshot /tmp/p12.bmp

# hide a suspect part
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --view \
    --part 12 --part-mode hide --frames 3 --screenshot /tmp/nohide.bmp
```

Also try `W` (wireframe), `L` (lighting off), `T` (textures off) in the
interactive viewer; each removes one variable.

## 4. Compare against the decomp

When in doubt, read the original: `src/sysdolphin/baselib/pobj.c`
(`PObjSetupMtx`, `SetupEnvelopeModelMtx`), `jobj.c`
(`HSD_JObjMakeMatrix`), `mtx.c` (`HSD_MtxSRT`). The port must match these, not
a plausible reinventation.

## 5. Verify a hypothesis in Python

If a formula is suspect, replicate it against raw bytes in a throwaway script.
Especially useful for: pointer bases, stride, matrix conventions. Never guess.

## 6. Regression guard

After the fix:

```sh
./build/native/melee-demo --inspect                       # numbers
SDL_VIDEODRIVER=offscreen ./build/native/melee-demo --scripted --frames 240 \
    --screenshot /tmp/regression.bmp
```

If numbers changed intentionally, update `STATE.md` in the same commit.

## 7. Write it down

Add a `gotchas/GOTCHAS.md` entry with the symptom so the next agent greps
instead of debugs.

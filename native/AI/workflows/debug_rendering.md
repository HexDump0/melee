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
./build/native/melee --inspect
./build/native/melee --inspect --list-parts
./build/native/melee --dump-tev        # per-batch GX material state
```

- Triangle count and bounds must match `STATE.md` for Mario (bounds include the
  `model_scaling` factor).
- Does the part list show geometry where you expect it?
- Does the batch's `rm`/`flags`/colormap match the decomp's `MObjMakeTExp`
  branch? `learnings/hsd_tev_materials.md` has the mappings (G-042..G-044).

## 3. Isolate

```sh
# one part, textured
SDL_VIDEODRIVER=offscreen ./build/native/melee --view \
    --part 12 --part-mode only --frames 3 --screenshot /tmp/p12.bmp

# hide a suspect part
SDL_VIDEODRIVER=offscreen ./build/native/melee --view \
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
./build/native/melee --inspect                       # numbers
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240 \
    --screenshot /tmp/regression.bmp
```

If numbers changed intentionally, update `STATE.md` in the same commit.

## 7. Write it down

Add a `gotchas/GOTCHAS.md` entry with the symptom so the next agent greps
instead of debugs.

## Finding the one draw that is wrong (P-849)

When a stage or model looks broken, isolate before theorising:

```sh
# Which draw is doing the damage?  Sweep and diff against the unmodified shot.
for n in $(seq 0 63); do
    ./build/native/test_decomp_render --stage GrNBa.dat --no-fighter \
        --stage-cam --hide-draw $n --shot /tmp/h$n.bmp
done
```

`--only-draw N` renders just that one.  `--wire` answers a different question
and is worth asking first: if the wireframe is intact, the joint tree and the
vertex arrays are fine and the fault is in the display list, the material or
the blend, not in the geometry.

Then compare the port's vertex count for the draw against what the archive
says: `HSD_PObjDesc`'s display list is `n_display << 5` bytes, and its
primitives give an exact triangle count.  A renderer producing *more* vertices
than the data holds is reading a malformed primitive header.

```sh
MELEE_DL_TRACE=1 ./build/native/test_decomp_render --stage GrNBa.dat \
    --no-fighter --stage-map 6 2>&1 | grep -A12 nbytes=2656
```

prints every primitive with the bytes it consumed; the vertex size per
primitive is constant within a POBJ, so the row with a different one names the
damaged word.  See G-222 for the walk from there to the converter walker that
wrote it.

# S2: compiled HSD + GX HLE character render (P-606)

Written 2026-09-12. This is the durable result of milestone S2
(`ROADMAP_DETAILS.md`): a retail `PlMrNr.dat` is loaded by the compiled
decompilation (`HSD_ArchiveParse` + `HSD_JObjLoadJoint` + the DObj/PObj/MObj/
TObj display path), its GX command stream is captured through a real GX HLE,
and the result is rendered with GLES3 into a BMP that matches the prototype
viewer's screenshot.

Evidence:

- Harness: `native/tests/test_decomp_render.c` (32-bit, ADR-0012), ctest
  `decomp_render`.
- Backend: `native/decomp/gx/gx_hle.{c,h}` (GX state + display lists +
  vertex/channel/texgen), `native/decomp/gx/gx_gl.{c,h}` (EGL/GLES3 TEV
  renderer), `native/decomp/hsd/hsd_scene.{c,h}` (asset bridge + bootstrap +
  part visibility).
- Log: `native/AI/logs/2026-09-12-S2-render.md`.
- Prototype untouched and still the oracle (`--inspect` tail unchanged,
  `--view`/`--scripted` deterministic).

```
./build/native/test_decomp_render --width 1280 --height 800 \
    --shot /tmp/compiled.bmp
decomp_render: hidden DObjs=16
decomp_render: draws=52 vertices=17724 textures=88 display_lists=52
               primitives=292 skipped=0 degenerate=1826
decomp_render: PASS
```

## Pipeline

1. **Bootstrap** (`hsd_scene_boot`): `OSInit` (S1 platform layer), the heap
   descriptors via `OSInitAlloc`/`OSCreateHeap` exactly like `HSD_OSInit`
   (`initialize.c:161`), the HSD `*InitAllocData` pools, `HSD_IDSetup`,
   `JObjInfoInit`.
2. **Asset bridge** (`hsd_scene_load`): the data section is *not* blanket
   swapped.  Only the header/tables and the descriptor graph are converted
   field-by-field; every byte-defined range (display lists, vertex arrays,
   textures/TLUTs, FObj streams, strings) keeps its original big-endian
   bytes, which the GX HLE reads as BE.  This follows the P-605 spec
   (`learnings/decomp_assets.md` §7) but only covers the paths S2 needs:
   `HSD_Joint` (including the 12-float inverse-bind `mtx`), `DObjDesc`,
   `MObjDesc`, `Material`, `TObjDesc`, `ImageDesc`, `TlutDesc`,
   `TexLODDesc`, `TObjTevDesc`, `PObjDesc`, `VtxDescList` and the envelope
   descriptor arrays.  `robjdesc` pointers are nulled on the parsed tree
   (S0 behaviour); FObj/FigaTree conversion is S3.
3. **Camera + lights**: the harness builds a compiled `HSD_CObj` with the
   prototype viewer's framing math (`viewer_frame_bounds` + fovy 0.7 rad,
   zfar = distance*8+100) and builds compiled `HSD_LObj` objects from the
   same `MnSlChr` scene data the prototype's `native/hsd/light.c` reads.
4. **Display**: `HSD_StartRender(HSD_RP_SCREEN)` + `HSD_CObjSetCurrent` +
   `HSD_JObjDispAll(root, NULL, HSD_TRSP_ALL, 0)` with the
   `Fighter_UpdateModelScale` root scale from `ftData.model_scaling`
   (`src/melee/ft/fighter.c:213`).  A first identity-projection pass captures
   world bounds for the camera.
5. **Part visibility**: `hsd_scene_apply_visibility` reads the `Pl<Char>.dat`
   `ftData` vis tables (same offsets as `native/hsd/parts.c`) and sets
   `DOBJ_HIDDEN` on the compiled `HSD_DObj` chain in JObjDisp order; slot 0 /
   variant 0 hides 16 of 59 DObjs, matching the prototype's
   `Parts visibility: 16 of 59 objects hidden`.
6. **GX HLE** (`gx_hle.c`): state recorders for the full command surface HSD
   uses; `GXCallDisplayList` decodes the PObj byte stream (verified against
   `native/hsd/model.c`), reads indexed/direct attributes from the original
   BE arrays, applies the XF position/normal/texture matrices and the GX
   channel (diffuse + a Blinn-Phong specular channel approximation), and
   appends triangles to a per-frame draw list with a per-draw state snapshot.
7. **GLES3** (`gx_gl.c`): EGL pbuffer context, one streaming VBO, an ES3
   fragment shader that evaluates the captured TEV stages (args, ops,
   bias/scale/clamp, constants, swap tables, up to 4 stages), textures via
   `native/gx/texture.c`, GX blend/depth/cull/alpha-test state, and a BMP
   writer.

## Parity

The prototype screenshot and the compiled screenshot at 1280x800 with the
same camera and lights differ by:

| Metric | Value |
|---|---|
| RMSE over the model area (HUD/footer excluded) | **10.53 / 255** |
| RMSE over pixels the prototype renders | 28.3 / 255 |

Two TEV/GX semantics bugs were fixed after the first S2 pass (they made
Mario's boots grey, Luigi look "creepy" again and Link black when lighting
came on):

1. **`out_reg` does not clobber the previous-stage chain.**  HSD's specular
   graphs write the spec map to C2 (`GXSetTevColorOp(..., GX_TEVREG2)`) and
   then compute `CPREV + RASC*C2`, expecting CPREV to still be the diffuse
   from the stage before.  The shader used to set `prev = out` on every stage
   and returned the spec map instead of `diffuse + spec`.  `prev` now only
   updates when the stage's out register is `GX_TEVPREV`; register writes
   leave the chain untouched.
2. **Resetting the GX HLE state requires invalidating the engine caches.**
   The bounds/camera passes call `gx_hle_begin_frame`, which resets the
   backend; the compiled HSD keeps its own caches (channel registers, TEV
   stages, vtx descriptors) and would skip re-emitting them for the next
   model, leaving the raster black.  `render_scene` now calls
   `HSD_StateInvalidate(-1)` after the reset; cycling through all 33 models
   and back renders byte-identically to a fresh load.

A third decoder bug caused the owner-visible triangular holes (Mario's hat,
Link's leg, Bowser's horns, Giga Bowser's spikes): the streaming primitive
assembler reused a 4-slot ring for TRIANGLES and QUADS, so every group after
the first read stale slots and permuted vertices.  Only strips were safe
(they need just the last three vertices).  `exec_primitive` now keeps the
current group and the fan origin; the world-vertex differential against the
prototype's own parser (`--dump-verts`/`--dump-raw` in the prototype,
`--dump-world` in `test_decomp_render`) is exact: 0 mismatched vertices of
17,724, worst 1.9e-06.

The remaining residual is the channel-1 specular approximation (the prototype
uses Blinn-Phong `pow(N·H, shininess)`, GX hardware a rational polynomial);
boots, Luigi's gloves/face and Link now match the prototype.  Giga Koopa's
limbs still show a noise texture (P-610, POBJ_SKIN path).

Other documented deviations:

- **Fog** is captured and evaluated in the shader (type/start/end/color); the
  character-select fog (linear 500..1000) simply does not reach the model at
  the viewer's distance, so it has no effect on the parity image.
- **Direct-mode GX** (`GXBegin` + `GXPosition3f32`/... ) is not captured: the
  compiled calls are static-inline writes to the hardware FIFO address, which
  the platform maps as scratch.  HSD's character path uses display lists; the
  HUD/particles/shape-anim paths that use direct vertices are S4.
- **POBJ_SHAPEANIM** models (Kirby variants) are not exercised; Mario is all
  envelope.
- GX `GX_BM_LOGIC` is rendered as opaque (GLES3 has no logic ops); fighter
  materials do not use it.

## Gotchas added by this work

- `GX_CULL_*` values are `NONE=0, FRONT=1, BACK=2` (the prototype parser's
  `cull` field uses 1=front/2=back, so the numbers must not be conflated).
  Swapping them renders the inside of the model and breaks channel lighting.
- Texture matrices: HSD puts the TObj matrix in the *post* matrix
  (`GX_PTTEXMTX0 = 64`), not `GX_TEXMTX0 = 30`; both banks must be recorded
  (`tobj.c:setupTextureCoordGen`, `HSD_TexMapID2PTTexMtx`).
- `GXClearVtxDesc` must reset every attribute's type, or the next PObj's
  descriptor order is empty and the display list desyncs.
- This EGL pbuffer accepts only `GL_RGBA` readback (`GL_RGB` gives
  `GL_INVALID_OPERATION` and zeros).
- The game's own `powf`/`expf` (`src/melee/lb/lb_00CE.c`) are series that do
  not converge for large exponents and hang under `-O0`/ASan; the GX HLE uses
  double `pow` instead.
- GLES has no client-side vertex arrays: the frame must be uploaded to a VBO
  before `glVertexAttribPointer`.

## Sanitizers

`-DMELEE_SANITIZE=ON` (32-bit ASan/UBSan) is clean for:

```
./build/native-asan/melee_decomp_boot --boot-frames 2
./build/native-asan/test_decomp_hsd
./build/native-asan/test_decomp_render --no-gl
./build/native-asan/test_decomp_render --width 640 --height 480 --shot /tmp/a.bmp
```

## Reproduce

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
ctest --test-dir build/native --output-on-failure          # 5 tests
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 1 --no-grid \
    --screenshot /tmp/proto.bmp
./build/native/test_decomp_render --width 1280 --height 800 \
    --shot /tmp/compiled.bmp --dump
```

The render test SKIPs without the disc image (`iso/…ciso`).

## What S3 should replace

The focused `hsd_scene.c` converter becomes the real per-format asset
pipeline (AOObj/FObj streams, shape sets, REL-free containers), and the GX HLE
should gain: direct-mode capture, a faithful hardware specular polynomial,
fog, and texture/TMEM residency instead of per-frame decode.

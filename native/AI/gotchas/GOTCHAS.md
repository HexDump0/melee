# Gotcha list

## G-001: HSD pointers are data-relative, not file offsets

**Symptom:** model parses into nothing, or "valid" pointers land 0x20 bytes
early and produce garbage geometry.
**Cause:** every pointer field in an HSD archive is an offset from the data
section base (`0x20`), not from the file start.
**Fix:** `file = stored_u32 + 0x20`. See `learnings/hsd_archive_format.md`.

## G-002: zero pointer is sometimes valid

**Symptom:** the first vertex array or display list is skipped (`vertex == 0`).
**Cause:** `0` means NULL for link pointers, but it also means "data offset 0"
for array-base pointers.
**Fix:** use the NULL-aware accessor for links and the base-aware accessor for
arrays. `hsd/model.c` has both (`rptr`/`rbase`).

## G-003: direct attributes are read relative to a moved pointer

**Symptom:** model renders as spikes/shards; only direct (non-indexed)
attributes affected; matrix indices wrong.
**Cause:** code did `source = d + cur; offset = cur;` then indexed
`source[offset]`, reading `d[cur + cur]`.
**Fix:** for direct attributes set `offset = 0` because `source` already points
at the data. Commit `f70d50cce`.

## G-004: matrix-index attributes are always one byte

**Symptom:** stream desync after the first vertex; opcodes become garbage.
**Cause:** the descriptor says `GX_F32` for `PNMTXIDX`, but GX always writes a
single byte inline for matrix indices.
**Fix:** special-case `attr <= 8` to 1 byte in direct mode.

## G-005: `PNMTXIDX` selects a GX matrix slot, not a group index

**Symptom:** majority of vertices transform by the wrong joint; model becomes a
spiky blob.
**Cause:** HSD loads envelope groups at `GX_PNMTX0..PNMTX9` = slots
`0, 3, 6, ...`. The vertex byte is the slot.
**Fix:** `group_index = pnmtxidx / 3`.

## G-006: envelope groups are rigid vs blended

**Symptom:** some parts sit correctly, others are in the wrong space.
**Cause:** groups whose first weight is `1.0` transform by the joint's bind
world matrix; blended groups use `sum(w * M * inverseBind)`, which is the
identity at bind pose.
**Fix:** implement exactly `SetupEnvelopeModelMtx`'s two branches. See
`learnings/hsd_models_and_skinning.md`.

## G-007: `HSD_Joint.mtx` is the inverse bind matrix

**Symptom:** applying the stored matrix directly explodes the model.
**Cause:** the field is `inverse(bind world)`, used for `M * E` skinning.
**Fix:** verify with `bind_world * stored == I` (holds for all 53 joints in
`PlMrNr.dat`) before trusting your matrix convention.

## G-008: `HSD_MtxSRT` has a parent-scale correction

**Symptom:** joints with non-uniform/accumulated scale land in the wrong place.
**Cause:** HSD corrects each scale component by the parent's accumulated scale
when building the local matrix.
**Fix:** copy the formula verbatim from `mtx.c` rather than using a generic
Euler matrix. Also inherit accumulated scale when the joint's flag `0x8` is set.

## G-009: `n_display` is a count of 32-byte blocks

**Symptom:** display lists truncate early or read into the next object.
**Cause:** `pobj->n_display` is in blocks; the byte length is `n_display * 32`
(`GXCallDisplayList(pobj->display, pobj->n_display << 5)`).
**Fix:** multiply by 32 and bounds check.

## G-010: draw opcode low three bits are the vertex format

**Symptom:** opcode histogram contains odd values like 0x91/0x9B.
**Cause:** the byte is `primitive | (vtxfmt << 3)`; the primitive is `byte &
0xF8`.
**Fix:** mask, then dispatch on 0x80/0x90/0x98/0xA0/0xA8...

## G-011: display list ends at opcode 0x00

**Symptom:** parsing walks off the end or reads padding as data.
**Cause:** lists are padded to 32-byte blocks with zero bytes.
**Fix:** treat `op == 0x00` as the terminator. Never assume the data length
matches the header block count exactly.

## G-012: never skip vertex bytes for unknown opcodes

**Symptom:** desync at the first unknown primitive.
**Cause:** assuming an opcode has no attributes.
**Fix:** every draw command in these lists is followed by `count` vertices in
descriptor order; consume them even when you do not emit triangles.

## G-013: texture image descriptor offsets

**Symptom:** no textures decode; garbage or truncation errors.
**Cause:** wrong field offsets in `HSD_ImageDesc` / `HSD_TObjDesc`.
**Fix:** `TObjDesc.imagedesc` is at +0x4C; `ImageDesc` is `image_ptr +0,
width +4, height +6, format +8`. See `learnings/gx_textures.md`.

## G-014: CI4/CI8 need a palette that is not in the model archive

**Symptom:** some parts (eyes, effects) render flat material color.
**Cause:** the texture's `tlutdesc` is present but the palette data lives
elsewhere; decoding without it yields indices, not colors.
**Fix:** currently skip, fall back to material color. Real fix is P-203.

## G-015: CISO virtual offsets may exceed the physical file

**Symptom:** `fread` fails or you read the wrong data near the end of the FST.
**Cause:** unused blocks are omitted from the CISO file, so the virtual disc is
larger than the file.
**Fix:** clamp dynamic reads to `d->file_size` and treat a short read as
corruption. Also precompute the block ordinal prefix (G-016).

## G-016: CISO block lookup is O(blocks) if done naively

**Symptom:** FST enumeration takes minutes; `--list-models` feels hung.
**Cause:** computing the physical block ordinal by scanning the map from zero
on every read.
**Fix:** precompute a prefix-count array once in `disc_open` (`block_ordinal`).

## G-017: face expression meshes overlap in bind pose

**Symptom:** eyes/mustache smeared or doubled on Mario's face.
**Cause:** Melee hides alternate expression DObjs via the animation-driven part
visibility system (`ftParts`, `FtPartsVis`). With no animation, all are drawn.
**Fix/workaround:** viewer part isolation (`[` `]` + `V`). Real fix: P-201
(animation) or parse the part visibility table.

## G-018: root symbol selection

**Symptom:** model decode fails or produces nothing.
**Cause:** picking the first public symbol blindly.
**Fix:** prefer the first public whose name ends `_joint` and excludes
`matanim`; fall back to the first public.

## G-019: texture binding inside a GL display list is fine, but...

**Symptom:** batch lists render with the wrong texture or crash.
**Cause:** `glBegin`/`glEnd` cannot span `glCallList`; a batch list must contain
a complete texture-bound run.
**Fix:** compile one self-contained list per part batch and call them in order;
the previous monolithic list behavior is preserved by calling batches
sequentially.

## G-023: `ftData->x8` value 0 can be a valid pointer

**Symptom:** part visibility silently unavailable even though the archive has
the tables.
**Cause:** `ftData +0x08` is 0 in the raw file but is in the reloc table, so at
runtime it becomes `data + 0`: the parts descriptor lives at data offset 0.
**Fix:** treat a zero pointer as the start of the data section for this field
and sanity-check `model_num`/`vis_table` before using it.

## G-024: CI textures are paletted and the palette is in the archive

**Symptom:** eyes and some effects render untextured or white.
**Cause:** CI4/CI8 store palette indices; the palette lives in the model's
`HSD_TlutDesc`, not separately. Skipping it loses the eye atlas.
**Fix:** parse `TObjDesc+0x50`, expand the palette, use
`gx_texture_decode_ci`. See `learnings/gx_textures.md`.

## G-025: visibility indices are DObj indices, not PObj/batch indices

**Symptom:** wrong parts disappear when applying `FtPartsVis`.
**Cause:** the tables index `fp->dobj_list`; one DObj may own several PObjs.
**Fix:** record `dobj_index` on every `HsdBatch` and test visibility via
`hsd_model_batch_visible`.

## G-026: the `right` matrix must not apply to the skeleton root

**Symptom:** applying `right` to every PObj distorts characters whose DObjs are
on the root (Mario).
**Cause:** `_HSD_mkEnvelopeModelNodeMtx` returns NULL when the joint has
`JOBJ_SKELETON_ROOT`.
**Fix:** return "no right" for skeleton roots; only non-root DObjs get it.

## G-027: slots 1..4 of the visibility table are off in normal rendering

**Symptom:** showing all visibility lists re-creates the face smear.
**Cause:** `ftDrawCommon_800805C8` enables slot 0 (or slot 2 for metal) and
explicitly disables slots 1, 2, 4.
**Fix:** hide everything listed, then show only slot 0 variant 0 for the
neutral pose.

## G-028: GX color comp_type is the colour enum, not the scalar enum

**Symptom:** models made of vertex-coloured pieces explode into spikes; a huge
rainbow triangle appears near a character's head; all-vertex-colour characters
like Mr. Game & Watch are unreadable.
**Cause:** for `GX_VA_CLR0/CLR1`, `comp_type` is `GX_RGB565=0`, `GX_RGB8=1`,
`GX_RGBX8=2`, `GX_RGBA4=3`, `GX_RGBA6=4`, `GX_RGBA8=5` (2/3/4/2/3/4 bytes), not
`GX_U8..GX_F32`. Reading it as a scalar makes each vertex 1 byte too long and
desyncs everything after it.
**Fix:** size and decode colours with the colour enum (`color_attribute_size`,
`decode_color` in `hsd/model.c`).

## G-029: POBJ_SKIN has two matrix slots, SHAPEANIM is rigid

**Symptom:** characters that mix PObj types (Kirby, Link, Game & Watch,
Captain Falcon) have parts in the wrong place.
**Cause:** `PObjSetupMtx` only uses envelope groups for `POBJ_ENVELOPE`.
`POBJ_SKIN` uses the current joint, or two joints selected per vertex via
`PNMTXIDX` (0 = current, 3 = `u.joint`). `POBJ_SHAPEANIM` uses the current
joint only. `right` applies to envelope PObjs only.
**Fix:** dispatch on `(flags >> 12) & 3`.

## G-030: blended envelope groups scale by the weight sum

**Symptom:** parts are uniformly half/double size, or collapse to the origin.
**Cause:** `sum_i w_i * (M_i * inverseBind_i)` equals `(sum w_i) * I` at bind,
not necessarily `I`. Apparent "identity" only holds when the weights sum to 1.
**Fix:** sum the group's weights and scale positions by that value; start the
sum at 0, not 1.

## G-031: cull modes are per PObj and GX front faces are clockwise

**Symptom:** gloves and other shells look transparent/streaky (Master Hand).
**Cause:** the game culls back faces per PObj; drawing both sides shows the
inside of the mesh through z-fighting.
**Fix:** map `flags & 0xC000` to GL culling, set `glFrontFace(GL_CW)`, and skip
PObjs with both cull bits. `0xC000` means "do not draw".

## G-032: hidden joints still exist in the model graph

**Symptom:** a decal is partially covered by the surface under it (Mario's cap
"M"), or a character has extra geometry (Game & Watch's flat pieces).
**Cause:** `HSD_JObjDispDObj` skips DObjs of joints flagged `JOBJ_HIDDEN`
(1<<4) while still recursing into children. Hidden duplicate/helper meshes
overlap the visible ones.
**Fix:** skip the DObj loop for hidden joints; keep recursing. This completes
the static visibility picture together with `FtPartsVis` (G-025).

## G-033: honour material z-mode bits

**Symptom:** decals/effects fight with the surface they sit on.
**Cause:** `HSD_MObjDesc.rendermode` may set `RENDER_ZMODE_ALWAYS` (1<<27) or
`RENDER_NO_ZUPDATE` (1<<29); the port ignored them.
**Fix:** map them to `glDepthFunc(GL_ALWAYS)` / `glDepthMask(GL_FALSE)` while
drawing the batch, restoring afterwards.

## G-034: repeat_s/repeat_t scale texture coordinates

**Symptom:** half of a mirrored logo is missing (Mario's cap "M"), or tiled
textures appear at the wrong size.
**Cause:** `TObjDesc.repeat_s/repeat_t` feed `MakeTextureMtx`
(`scale = repeat/scale`, plus a mirror offset in T), so they scale the UVs.
The port decoded UVs raw and ignored them.
**Fix:** build HSD's texture matrix per batch and apply it with the GL texture
matrix. See `learnings/gx_textures.md`.

## G-035: copy the Visual only after GL resources exist

**Symptom:** player 2 is invisible while player 1 renders.
**Cause:** `visuals[1] = visuals[0]` happens before `compile_model`, so the
copy's `batch_lists` array is all zeros; the per-batch draw loop then calls
`glCallList(0)` for P2.
**Fix:** copy the `Visual` after `compile_model`, or share the first one's
list IDs explicitly.

## G-020: do not judge geometry from a flat-color render

**Symptom:** hours lost thinking the parser is broken.
**Cause:** untextured, low-resolution renders are near-useless for diagnosing
skin/UV bugs.
**Fix:** use the interactive viewer with textures, the ground grid, wireframe
toggle and part isolation before concluding anything.

## G-021: `native/AI` must stay trackable

**Symptom:** AI docs silently not committed.
**Cause:** `.git/info/exclude` historically used the unanchored pattern `AI/`,
which also matched `native/AI/`.
**Fix:** keep the pattern anchored (`/AI/`). If `git status` looks empty after
editing docs, run `git check-ignore -v native/AI/README.md`.

## G-022: ASan leak reports come from the GL driver

**Symptom:** sanitizer run "fails" with leaks in mesa/driver frames.
**Cause:** the GL implementation does not free everything at exit.
**Fix:** run with `ASAN_OPTIONS=detect_leaks=0` (see `TESTING.md`); leaks in
port allocations are still a bug and must be fixed.

## G-036: do not reinterpret the FObj stream statelessly

**Symptom:** animations look fine early, then a joint explodes near the end of
a clip (values like `-6e32`), or KEY/SPL tracks never match the game.
**Cause:** rewriting `fobj.c`'s compressed curve playback as a "parse once,
evaluate at t" model. The stream is read incrementally; pack counts, waits,
slope bookkeeping and the end-of-data state 6 all matter.
**Fix:** port the state machine literally (see `hsd/aobj.c`). Seek with
`ReqAnim(frame)` + `Interpret(rate = 0)`, which is exactly what
`HSD_JObjReqAnimAll` + `HSD_JObjAnimAll` do. Differential-test against a
transcription of `fobj.c` if you change it.

## G-037: FigaTree nodes map to joints, not ftParts slots

**Symptom:** characters with `Fighter_804D6540` entries (Kirby, Link, Zelda)
animate with wrong/offset limbs if the skip list is treated as a joint filter.
**Cause:** the skip list inserts phantom part slots for item attachments; the
i-th figatree node still drives the i-th joint in HSD traversal order.
**Fix:** bind node i to model joint i (see `anim_set_clip`); use the skip
list only for part-index bookkeeping.

## G-038: animation time must step on the 60 Hz tick

**Symptom:** animations play too fast/slow depending on the monitor (e.g.
180 Hz).
**Cause:** advancing animation frames by render `dt` directly couples playback
to the refresh rate.
**Fix:** accumulate wall time into a fixed 1/60 step before advancing, as the
game does (`HSD_AObjSetRate` is frames per 60 Hz tick).

## G-039: `--model` alone opens the interactive sandbox

**Symptom:** running `melee --model PlKbNr.dat` from a terminal pops a
window instead of printing.
**Cause:** without `--inspect`, `--view`, `--list-clips`, etc. the default mode
is the playable sandbox.
**Fix:** always pass a headless mode flag when probing assets.

## G-040: `glFrustum`'s bottom-right element is 0, not 1

**Symptom:** after moving the camera matrices to the CPU (core profile), the
whole scene is shifted about one pixel; every edge shows a one-pixel halo and
the parity RMSE is ~0.05 (normalized) even though the geometry looks right.
**Cause:** `glFrustum` produces a matrix whose `m[15]` (column 3, row 3) is 0,
because the perspective divide is `w' = -z`. A hand-written `m4_frustum` that
starts from identity leaves `m[15] = 1`, so `w' = 1 - z` and the projection
shifts.
**Fix:** set `m[15] = 0` in the frustum helper. Verify by comparing against
`glGetFloatv(GL_PROJECTION_MATRIX)` from a compatibility context (a throwaway
differential test is the fastest way to catch this class of bug).
See `learnings/gl_shaders.md`.

## G-041: fixed-function lighting clamps the lit colour before texture modulate

**Symptom:** with a custom shader that reproduces `GL_LIGHT0`/`glColorMaterial`
with an ambient + diffuse term, textured surfaces (Mario's cap, clothes) are
much brighter than the fixed-function build; the parity RMSE stays around 0.05
after the projection is fixed.
**Cause:** the GL lighting equation clamps the computed per-vertex colour to
`[0,1]` *before* the texture environment multiplies it. A shader that leaves
the lit colour in `[0, ~1.7]` and clamps only at the framebuffer produces
values above 1 that survive scaling by texture values < 1.
**Fix:** `clamp(color * (ambient + diffuse * NdotL), 0.0, 1.0)` per vertex, not
per fragment. Same clamp for the two-sided back colour.

## G-042: the GX TEV equation is `d + (1-c)*a + c*b`, not `(a-b)*c + d`

**Symptom:** TEV combiner modes come out wrong: MODULATE results in
`-prev*texture`, ALPHA_MASK looks like a subtraction.
**Cause:** copying a generic `(a-b)*c+d` combiner. GX evaluates
`out = d + (1-c)*a + c*b` (with `GX_TEV_SUB` subtracting the middle term),
which is why `TObjMakeTExp`'s MODULATE (`a=ZERO, b=prev, c=TEXC`) means
`prev*texture`.
**Fix:** use the GX ordering in the shader (`mix(prev, texture, c)`); see
`learnings/hsd_tev_materials.md`.

## G-043: GX channel lighting uses the registered material colour, not CLR0

**Symptom:** characters render with wrong tints; e.g. Mario's gloves looked
cream instead of the texture's blue-white, and colours shifted everywhere.
**Cause:** the renderer multiplied `CLR0` into every material. In GX the
raster colour is `mat_ambient * light_ambient + Σ light*N·L` for
`rendermode & 7 == 4` (`HSD_SetupChannelMode` case 4, white `mat_color`), and
the initial TEV stage uses the material `diffuse` constant unless
`RENDER_VERTEX` (1<<1) is set. Most fighter materials do **not** set
`RENDER_VERTEX`, so per-vertex colours are ignored.
**Fix:** derive `channel_lit`/`initial_ras`/`diffuse_mul` from `rendermode`
and evaluate exactly that; per-vertex colour is only the raster when the
channel is unlit. See `native/hsd/model.c:parse_material`.

## G-044: a TObj's lightmap flag selects its TEV phase, not just its texture

**Symptom:** Luigi's face/gloves and Mario's pocket shells render grey and
metallic-looking on normal costumes; the character looks like a half-finished
metal variant even though the model file has no metal state.
**Cause:** `TObjMakeTExp` routes textures by `TEX_LIGHTMAP_*`: DIFFUSE/
AMBIENT modify the diffuse accumulator, SPECULAR (0x20) modifies the specular
accumulator (`mat.specular` blended with the map, then multiplied by the
specular light channel and added), EXT (0x80) is a reflection map applied
last. Treating a SPECULAR map as a normal blend texture mixes its grey
highlight map straight into the lit colour.
**Fix:** classify each TObj into a phase (0 diffuse, 1 specular, 2 ext) and
route it in the shader; see `learnings/hsd_tev_materials.md`.

## G-045: the viewer grid shows through `RENDER_NO_ZUPDATE` parts

**Symptom:** translucent model parts (Master Hand's wrist/forearm connector)
appear "broken" because the floor grid is visible through them, even though the
grid is behind the model.
**Cause:** `RENDER_NO_ZUPDATE` materials draw with `glDepthMask(GL_FALSE)`
(matching GX). The viewer grid is drawn after the model, so it passes the depth
test wherever the translucent part did not write depth. This is a viewer
artifact; the game has no grid, so the same part blends correctly there.
**Fix:** use `--no-grid` (or `G` in the viewer) when inspecting translucent
parts, and don't "fix" the blend state — it matches `HSD_SetupPEMode`.

## G-046: cheap gamepads report a stuck axis from power-on

**Symptom:** in the sandbox, P1 walks left forever and `A`/`D` do nothing,
even though the keyboard works. Reproduces with a "shanwan Android GamePad"
(`/dev/input/js0`): `SDL_GameControllerGetAxis(LEFTX)` is `-32768` from the
moment it is opened and it emits **zero** `SDL_CONTROLLERAXISMOTION` events.
**Cause:** the loop read the stick every frame and let any value outside a 0.2
deadzone override the keyboard, so the stuck axis won.
**Fix:** only use the stick after at least one axis-motion event has been seen,
and let keyboard movement win while a key is held. `--no-controller` disables
pad input entirely. See the sandbox input block in `native/main.c`.

## G-047: the SDK math pairs are Metrowerks asm and cannot be compiled

**Symptom:** trying to compile `extern/dolphin/src/dolphin/mtx/mtx.c` or
`vec.c` (or planning to) produces `expected '(' before 'void'`,
`unknown type name 'psq_l'`, `nofralloc`, stray `@` and dozens more.
**Cause:** those files are hybrid C/asm from the MWCC era: function-level
`asm void PSMTXCopy(...) { psq_l ... }` and inline `asm { ... }` blocks inside
plain C functions (`PSMTXIdentity`, `PSMTXTrans`, ...). The pure-C `C_MTX*`/
`C_VEC*` twins live in the same TUs, so they are unreachable too.
**Fix:** never compile those TUs on PC. Provide the ~15 primitives the HSD
layer calls in a portable backend under `native/decomp/` (ADR-0011 rule 3).
This is exactly why P-301 could only land `HSD_MtxSRT` from
`src/sysdolphin/baselib/mtx.c`.

## G-048: the decomp does compile on the host, but three mechanical classes block it

**Symptom:** the GCC census (834/1034 `src/*.c` clean) reports `unknown type
name 'intptr_t'/'uintptr_t'`, `static assertion failed: offsetof(struct
ToyED8Data, ...)`, or `initialization of 'void (*)(int)' from incompatible
pointer type 'void (*)(_Bool)'`.
**Cause:** (1) `intptr_t`/`uintptr_t` come from the GC MSL headers (32-bit) or
are missing; on the host they must be 64-bit. (2) The decomp asserts GameCube
32-bit struct offsets at compile time; they are wrong on a 64-bit build and
must be disabled, not "fixed" by moving fields. (3) `dolphin/types.h` defines
`BOOL` as `int`, `platform.h` uses C `bool`, and some callbacks mix them.
**Fix:** for the port build, add `<stdint.h>` via `decomp/`'s shim, disable the
layout assertions, and decide the `BOOL`/`bool` convention per ADR-0011. Record
every `src/` patch in `learnings/decomp_port.md`. The syntax census and current
numbers live there too.

## G-049: `GX_CULL_*` values are not the prototype's cull numbering

**Symptom:** the compiled S2 render shows the inside of the model, textures
mirrored/garbled, and channel lighting collapses to ambient-only (all back
faces have normals pointing away from the light).
**Cause:** GX enums are `GX_CULL_NONE=0, GX_CULL_FRONT=1, GX_CULL_BACK=2,
GX_CULL_ALL=3`, but `native/hsd/model.c` stores `(pobj_flags & 0xC000) >> 14`
where 1 = `POBJ_CULLFRONT` and 2 = `POBJ_CULLBACK`.  The numbers happen to
overlap with opposite meaning.
**Fix:** map GX values directly (`GX_CULL_FRONT -> glCullFace(GL_FRONT)`,
`GX_CULL_BACK -> glCullFace(GL_BACK)`) after `glFrontFace(GL_CW)`, and do not
copy the prototype parser's numbering into GX code.

## G-050: HSD texture matrices are *post* texture matrices (GX_PTTEXMTX0 = 64)

**Symptom:** compiled HSD textures land on the wrong part of the atlas (face
looks like a different region, logos missing) even though the texture images
decode correctly.
**Cause:** `tobj->mtxid = HSD_TexMapID2PTTexMtx(tobj->id)` is `GX_PTTEXMTX0 =
64 + 3*map`, loaded by `GXLoadTexMtxImm(tobj->mtx, tobj->mtxid, ...)` and
passed as `pt_texmtx` to `GXSetTexCoordGen2`; the `mtx` argument is
`GX_IDENTITY` in the common path (`tobj.c:setupTextureCoordGen`).
**Fix:** record both the `GX_TEXMTX0..9` (30..57) and `GX_PTTEXMTX0..7`
(64..85) banks in the GX HLE and apply `post * mtx` in texcoord generation.

## G-051: the game's own `powf`/`expf` can hang

**Symptom:** `test_decomp_render` spins forever under `-O0`/ASan inside
`expf`/`powf` (`src/melee/lb/lb_00CE.c:51/84`).
**Cause:** the decomp's `powf` is `expf(y * 2*atanh(x))` and its `expf` sums a
Taylor series until the float stops changing.  For large exponents/high
`atanh(x)` the series diverges and `var_f3 != temp_f5` never becomes false.
**Fix:** never call the game's float `powf`/`expf` from port code (the linker
prefers them over libm).  Use double `pow`/`exp` explicitly and cast, or a
bounded implementation.

## G-052: GLES has no client-side vertex arrays and this pbuffer only reads RGBA

**Symptom:** `glDrawArrays` segfaults inside Mesa when the S2 renderer passed a
plain memory pointer to `glVertexAttribPointer`; after switching to a VBO,
`glReadPixels(..., GL_RGB, ...)` returns `GL_INVALID_OPERATION` and zeros.
**Cause:** OpenGL ES requires buffer objects (no client arrays); the EGL
pbuffer config here does not accept RGB readback.
**Fix:** upload the captured frame to a streaming VBO and read back
`GL_RGBA`, converting to 24-bit BGR for the BMP writer.

## G-053: TEV `out_reg` writes do not replace the previous-stage chain

**Symptom:** HSD specular materials render as the light spec map only:
Mario's boots grey instead of brown, and Luigi's face/gloves go grey/metallic
(the G-044 look returns) even though the TObj phases are correct.
**Cause:** the compiled TEV graph writes the specular accumulator to
`GX_TEVREG2` (`GXSetTevColorOp(..., GX_TEVREG2)`), then computes
`CPREV + RASC*C2`.  A shader that sets `prev = stage_result` unconditionally
returns the spec map from the register-writing stage, so the diffuse saved two
stages earlier is lost.
**Fix:** emulate `out_reg`: only update the previous-stage value when the
stage's output register is `GX_TEVPREV`; writes to C0/C1/C2 leave the chain
untouched (`native/decomp/gx/gx_gl.c` fragment main).

## G-054: resetting the GX HLE state must invalidate the engine's caches

**Symptom:** after switching models in the compiled viewer, lighting turns
everything black (textures-on, lights-on); the first model looks fine.
**Cause:** `gx_hle_begin_frame` zeroes the backend state, but the compiled HSD
keeps its own caches (channel registers, TEV/state setters, vtx descriptors)
and skips re-emitting unchanged state for the new model.
**Fix:** call `HSD_StateInvalidate(-1)` after resetting the backend
(`native/decomp/render/render_scene.c:compute_bounds`).

## G-055: a shared ring buffer silently permutes TRIANGLES/QUADS

**Symptom:** clean triangular holes in otherwise solid models (Mario's hat,
Link's leg, Bowser's horns, Giga Bowser's spikes) that survive disabling
culling and alpha test; vertex counts still match the prototype.
**Cause:** the GX display-list assembler stored every vertex in a 4-slot ring
and emitted `win[0..2]` for each triangle/quad group.  That is correct for the
first group only; group two reads slots that now hold the next group's
vertices, permuting connectivity.  Strips are safe because they only need the
last three vertices.
**Fix:** index the window by `i % stride` and emit the current group's slots
(`gx_hle.c:exec_primitive`).  Verify with the world-vertex differential:
prototype `melee --dump-verts f` vs `test_decomp_render --dump-world f`
(0 mismatches expected).

## G-056: viewer lights-off must zero the specular channel

**Symptom:** toggling `L` (lights off) makes several models look *worse* than
the prototype — blown-out, metallic or grey, especially shiny parts.
**Cause:** the flat-raster override forced both GX channels to white, so every
specular TEV stage added `C2 * 1` (the spec map at full strength).  The
prototype's `u_lighting = 0` both whitens the diffuse channel and zeroes the
specular light.
**Fix:** when `u_ras_flat` is set, channel 0 becomes white but channel 1/alpha1
becomes black (`native/decomp/gx/gx_gl.c`).

## G-057: `glClear` inherits the previous frame's write masks

**Symptom:** rotating the camera around Master Hand (viewer `--spin`) makes the
model vanish for part of the orbit; a static frame looks correct.
**Cause:** GX draws can end a frame with `z_update = 0` (`RENDER_NO_ZUPDATE`)
or alpha writes masked, and those settings were left in the GL pipeline.  The
next frame's `glClear(GL_DEPTH_BUFFER_BIT)` then wrote nothing, so depth (and
alpha) accumulated until the depth test rejected everything.
**Fix:** restore `glColorMask(TRUE...)` **and** `glDepthMask(GL_TRUE)` before
`glClear` (`gx_gl.c:gx_gl_render_frame`).  Regression check:
`--frames 2 --shot` static vs `--frames 2 --spin 360 --no-hud --shot` is
RMSE ~0.003 (max 1/255; the residue is HSD lookat float rounding).

## G-058: TEV stages can order a texcoord generated from another texcoord

**Symptom:** Giga Koopa's arms/legs show a high-contrast gold noise texture
instead of smooth skin; Falcon unaffected.
**Cause:** the material's bump-style TEV pair names `GX_TG_TEXCOORD1` as the
source of coord 2 (type `GX_TG_MTX3x4`, identity matrices), i.e. coord 2 is a
passthrough copy of coord 1.  At the time the fragment stage carried only
`v_uv0`/`v_uv1` and treated coord 2 as coord 0, so the add/sub pair sampled
different UVs and the difference term never cancelled.
**Historical fix:** fold `order_coord` down through identity-matrix
`GX_TG_TEXCOORDn` chains onto the varyings they alias before snapshotting the
draw (`gx_hle.c:resolve_stage_coords`).  P-736 later added native varyings for
all eight coordinates; chain resolution remains valid and avoids redundant
generation, but is no longer a substitute for the complete hardware range.

## G-059: capture up to 8 TEV stages (Master Hand uses 6)

**Symptom:** materials with more than four stages lose their extra stages
(Master Hand's cape/hand blend), while the rest of the model looks right.
**Cause:** `GX_HLE_MAX_STAGES` and the GLES fragment shader were fixed at 4.
**Fix:** raise `GX_HLE_MAX_STAGES`/`MAX_TEV_STAGES` to 8 and size the shader
uniform arrays/loop accordingly (`gx_hle.h`, `gx_gl.c`).  Draws are clamped to
the limit, so increasing it is safe for every existing model.

## G-060: adding a shim header that shadows an existing include needs a clean rebuild

**Symptom:** P-608's `native/decomp/shim/dolphin/gx/GXVert.h` had no effect on
the compiled game: `displayfunc.o` still referenced the SDK inline FIFO writes
(GCC silently kept using the extern header), and only `test_decomp_render`,
which was recompiled for an unrelated edit, used the shim.
**Cause:** the shim directory was already on the include path, so adding a file
there changes no compile command and make's dependency graph has no edge to the
new file. 932 objects include `GXVert.h` transitively and none of them are
rebuilt.
**Fix:** after adding/removing a shadowing header, force the TUs that include
it to rebuild (e.g. `rm -rf build/native/CMakeFiles/melee_decomp_game.dir`
followed by `cmake -S native -B build/native` to regenerate `build.make`, then
build).  Verify with
`objdump -dr .../displayfunc.c.o | grep GXPortWGFifo` and
`.o.d` showing the shim path.

## G-061: the S2 harness never set the VI render mode, so camera setup got zero scales

**Symptom:** after implementing `GXSetScissor` (P-612), every draw was clipped
away (blank frame).  `setupNormalCamera` computes
`x_scale = fbWidth / viWidth` from `HSD_VIGetRenderMode()`; with a zeroed
render mode that is NaN/0, so the viewport and scissor rects collapse to
`0,0,0,0`.
**Cause:** `HSD_CObjSetCurrent` reads `HSD_VIData.current.vi.rmode`, which
`HSD_InitComponent` fills via `HSD_VIInit`.  The S2 harness
(`hsd_scene_boot`) boots the pools by hand and never calls it, so the render
mode stayed zero.  Invisible until the port actually used viewport/scissor.
**Fix:** assign `HSD_VIData.current.vi.rmode = GXNtsc480IntDf` in
`hsd_scene_boot()` (`native/decomp/hsd/hsd_scene.c`).

## G-062: `GX_VA_NBT` is a distinct attribute with nine components

**Symptom:** shape-anim PObjs (Kirby-style shapes) decode garbage: positions
after the first vertex are wrong and the draw desyncs, because the reader
advanced four bytes per normal instead of thirty-six.
**Cause:** HSD's `setupShapeAnimVtxDesc` (pobj.c) sets `GX_VA_NBT` directly
(not `GX_VA_NRM` with `GX_NRM_NBT`), and the component-count table only knew
`GX_VA_NRM`, so the NBT stream was read as a single component.
**Fix:** `comp_count` returns 9 for `GX_VA_NBT`; the first three components are
the lighting normal, the remaining six (binormal/tangent) are skipped.  The
`--direct` self-test covers a three-vertex NBT stream (`ctest
decomp_gx_direct`).

## G-063: never defer a write to a DVD command block the caller may drop

**Symptom:** the ASan boot crashed with EBX=0 inside `platform_pump_completions`
(appearing as a PIC GOT clobber).  Non-ASan builds survived by accident.
**Cause:** `DVDClose` cancels the command of a `DVDFileInfo` that usually lives
on the caller's stack (`lbFile_8001634C`).  The host `DVDCancel` posted a
deferred completion that later wrote `block->state`/`transferredSize` into that
dead stack frame, zeroing saved registers (EBX is the i386 PIC GOT register).
**Fix:** `DVDCancel` marks any queued `DvdCompletion` for the block as
`canceled` (the pump frees it without touching the block) and updates the
block synchronously while the caller is still alive; `DVDCancelAsync` invokes
its callback synchronously.  General rule: a completion may only touch memory
the caller guaranteed to keep alive until the callback.

## G-064: the retail `.ssm` group copy overlaps its source and destination

**Symptom:** ASan aborts the boot in `HSD_SynthSFXSampleLoadCallback` with
`memcpy-param-overlap`; the bank load otherwise completes.
**Cause:** `synth.c:107` copies each group from `new + dnw` into `new + 8`;
the destination advances `(n<<6)+0x10` bytes per group while the source
advances `(n<<6)+8`, so the regions overlap by the end.  MWCC's memcpy
tolerated it; glibc's is undefined.
**Fix:** `#ifdef PORT_PC` `memmove` around that one copy (`synth.c:107`,
ADR-0011 rule 2, listed in `learnings/decomp_port.md`).  The copied fields are
re-patched below, so the result is unchanged.

## G-065: descriptor offsets are data-section-relative, not archive-relative

**Symptom:** the converter's first S3 revision produced spike geometry and
`skipped=42` draws; fields looked half-converted.
**Cause:** all HSD offsets (relocation entries, joint children, display
pointers) are relative to `archive->data` (file offset 0x20), but the
converter read/wrote `archive_base + offset`, i.e. 0x20 bytes early.  The
relocation table itself *is* at archive-relative `0x20 + data_size`, so both
bases are needed: keep `c->d` (archive) for the header/tables/public names and
`c->data` (archive + 0x20) for descriptors.
**Fix:** `Conv` carries both pointers; `rd32_abs` is the table reader and
`rd32`/`conv_u32`/`conv_u16` use `c->data`.

## G-066: in-place conversion walkers must be idempotent

**Symptom:** after fixing G-065, a second pass over shared lists still
double-swapped values; e.g. all 68 Mario PObjs point at one vtxdesc list, so
the list was converted 68 times and even-count visits left it big-endian.
**Cause:** S2 converted from a separate read-only copy, so re-visiting was
harmless; the S3 converter writes into the same buffer it reads.
**Fix:** every shared list/descriptor walker marks its entry in `c->seen` and
returns/breaks on a revisit (`conv_vtxdesc`, `conv_envelopes`,
`conv_rvalue_list`, leaf descriptors).  Relocation targets are exempt: they are
handled once by the table pass and skipped by `conv_u32` via `c->reloc`.

## G-067: bump `HSD_CONVERTER_VERSION` whenever conversion semantics change

**Symptom:** after adding the scene-data walkers, the boot still panicked on a
raw `projection_type` — the disk cache was serving images produced by the
previous converter, so the new walkers never ran.
**Cause:** the cache key is content hash + converter version, by design.
**Fix:** bump `HSD_CONVERTER_VERSION` in `hsd_convert.c` with every conversion
change (or set `MELEE_NO_ASSET_CACHE=1` while debugging).  A stale cache is
otherwise invisible: same hash, older semantics.

## G-068: `gl_FragDepth` is ignored inside the big TEV program (Mesa/radeonsi)

**Symptom:** `GXSetZTexture(GX_ZT_REPLACE)` wrote the right uniforms and the
fragment shader executed (verified with a debug color), but the depth buffer
never changed: a nearer quad with `GX_LESS` kept failing.
**Cause:** on Mesa 26.1.6 / radeonsi (GLES 3.2), writing `gl_FragDepth` from
the large multi-stage TEV fragment shader has no effect (a minimal shader
writing `gl_FragDepth` in the same EGL context works).  Not a spec violation
we could pin down; likely a driver shader-compiler interaction with the big
program.
**Fix:** run the Z-texture pass with a small dedicated depth-only program
(`ZTEX_FRAGMENT_SRC` in `gx_gl.c`): sample the bound texture, write
`gl_FragDepth`, discard color.  `gx_gl` switches programs for
`state.ztex_op != 0`.

## G-069: direct-mode vertices must be flushed before any draw-state change

**Symptom:** HSD's `HSD_EraseRect` (`GXBegin`/`GXEnd` with no following vertex
command) would have been decoded with whatever descriptors/state happened to
be current at the next flush; the P-615 test's erase quad was decoded with the
next quad's descriptors and lost its TEX0 bytes.
**Cause:** the GXVert shim cannot flush on `GXEnd` (it is an empty inline in
the SDK's `GXGeometry.h`), so the HLE flushed lazily at the next `GXBegin`/
`GXCallDisplayList`.  By then `gx.cur` may already hold the next draw's state.
**Fix:** every draw-affecting GX setter (`GXSetVtxDesc`, `GXSetVtxAttrFmt`,
`GXClearVtxDesc`, `GXSetArray`, `GXSetTev*`, `GXSetChan*`, `GXSetZ*`,
`GXSetBlendMode`, `GXSetAlpha*`, `GXSetCullMode`, `GXSetProjection`,
`GXLoad*MtxImm`, `GXSetCurrentMtx`, `GXSetFog`, `GXSetCopyClear`) calls
`flush_direct()` *before* mutating state, so the pending primitive decodes and
snapshots with the state it was written under.

## G-070: recycled `frame_draws[]` slots keep the previous frame's command kind

**Symptom:** after `GXCopyTex` ran in one frame, a primitive in the next frame
was swallowed by the EFB-copy path (`kind == GX_HLE_DRAW_COPY_TEX`), so it
never rendered or appeared in the uploads.
**Cause:** `gx_hle_begin_frame` resets `frame_dcount`, and
`begin_draw_snapshot` overwrote only `first_vertex`/`vertex_count`/`state`,
leaving `kind`/`copy_*` from the recycled slot.
**Fix:** `memset` the whole `GxHleDraw` at snapshot time before filling it.

## G-071: GX alpha channels share the colour channel state slots

**Symptom:** the first model in `melee_decomp_viewer` is lit, but after
switching models (`N`/`P`) the new model renders with a flat raster —
identical to the `L` lights-off toggle — and never recovers.
**Cause:** `GXSetChanCtrl`/`GXSetChanAmbColor`/`GXSetChanMatColor` collapsed
`GX_ALPHA0`/`GX_ALPHA1` (and every non-`GX_COLOR1` id) onto the colour-0 state
slot.  HSD emits both halves of case-4 materials per draw
(`HSD_SetupChannelMode`: `_60` COLOR0 with the diffuse mask, then `_90`/`_C0`
ALPHA0 with `enable=0, light_mask=0`), so the alpha write wiped the colour
light mask/enable/diff_fn.  On the first load this is masked: `compute_bounds`
runs `HSD_StateInvalidate`, `HSD_LObjSetupInit` then raises the diffuse mask
from 0 to 1, and the material's `HSD_SetupChannel` re-emits COLOR0.  After a
model switch the light masks are unchanged, HSD's `prev_ch` cache skips the
COLOR0 emit, and the reset slot (mask 0) survives.  The specular channel is
unaffected (its `GX_COLOR1` writes do not alias alpha).
**Fix:** keep four channel slots — COLOR0, COLOR1, ALPHA0, ALPHA1.  The A0/A1
ids write the colour slot and mirror their alpha into the matching alpha slot;
`channel_raster` reads the material alpha from slot `ch + 2`.  Regression:
the channel-slot block at the end of `test_decomp_render --direct`
(ctest `decomp_gx_direct`), which fails with the old mapping
(`mask=0x0 enable=0 diff=0`).  Verified: after-frames switch vs direct load
is pixel-identical for Mario->Mewtwo, Falcon, Kirby, Giga Koopa and Luigi
pairs.

## G-072: lit channel raster alpha was hardcoded 0

**Symptom:** Master Hand's / Crazy Hand's translucent wrist-forearm connector
is invisible with lighting on; with `L` (lights off) it appears as a flat
white haze instead of a shaded translucent piece.
**Cause:** `channel_raster` returned `out[3] = 0` for the lit path (RGB is
material * accumulated light, alpha was never evaluated).  HSD's character
TEV template multiplies by `GX_CA_RASA` (`mobj.c` `HSD_TExpAlphaIn(...,
GX_CA_RASA, ...)`, e.g. `(ZERO, APREV, RASA, ZERO)` for the hand connector),
so raster alpha 0 makes the piece fully transparent.  Lights off masked the
bug because the viewer's flat override forces `ras = vec4(1)`.
**Fix:** evaluate raster alpha from the paired alpha channel (`GX_ALPHA0/1`,
state slots 2/3) using that channel's own sources, light mask and diffuse
function, and use it for the diffuse, unlit and specular outputs.  Regression:
the `v->ras[3] == 1.0` check in `test_decomp_render --direct`.  Verified:
Mario, Kirby, Giga Koopa and Link screenshots are byte-identical; only the two
hand models change (the connector renders translucent again).

## G-073: `map_head` is a struct, not an archive, and its map ids are camera variants

**Symptom:** `Gr*.dat` loads through the asset sweep but the viewer's stage
path shows nothing: picking a `_joint` public symbol fails (stage archives have
none), and parsing the `map_head` symbol data as an HSD archive gives garbage
publics.
**Cause:** `Gr*.dat` stages put their geometry in the `map_head` public, which
is the `UnkStageDat` struct (`src/melee/gr/types.h`): a `maps` array of
`UnkStageDat_x8_t`, each with a joint tree + anim/matanim/shapeanim + camera +
light list + fog.  The archive's other publics are textures (`*_image`) and
stage data (`coll_data`, `map_plit`, ...).
**Fix:** `conv_stage_maphead` in `hsd_convert.c` (converter version 4) walks
the struct and the descriptor chains; `hsd_scene_load_stage` loads
`maps[map_id].joint`.  Note `map_id` is a **camera id**, not a stage id:
`Ground_GetStageGObj(map_id)` builds one GObj per id, some are empty
(GrNBa 0) and some are far background layers (GrNBa 1..5).  For display, pick
the smallest-bounds non-empty map.

## G-074: stage light anims and shape sets need their own converter walks

**Symptom:** cycling stages crashes in `MObjLoad` (GrNLa) or trips
`pobj.c:842 vertex_buffer_size >= shape_set->nb_vertex_index` (GrNSr); GrPs
segfaults inside the converter itself.
**Cause:** three separate gaps.
1. `LightList.anims[0]` is an `HSD_LightAnim` whose `WObjAnim.aobjdesc.obj_id`
   is a *JObj offset*, not an animation ID; `HSD_AObjLoadDesc` falls back to
   `HSD_JObjLoadJoint`.  Nothing walked that joint tree, so its MObjDescs
   stayed big-endian.
2. `POBJ_SHAPEANIM` `PObjDesc.u.shape_set` is an `HSD_ShapeSetDesc` (0x1C):
   its counts and two VtxDesc lists must be converted.
3. `map_head` placeholder entries use `0xffffffff` sentinels; the light-list
   walker read through them and went out of bounds.
**Fix:** `conv_aobjdesc_ref` (AObj → referenced joint), `conv_shapesetdesc`
(shape set → VtxDesc lists), and range checks on every `LightList`/`anims`
read.  Converter version 9.  Rule: when a converter walker follows a pointer,
check `in_data` (and treat `0xffffffff` as invalid) before dereferencing.

## G-075: a Melee stage is *all* `map_head` maps, not one

**Symptom:** the stage viewer showed only the platform or only a background
layer; the game shows both together.
**Cause:** `Ground_GetStageGObj(map_id)` is called once per map id by the
stage callbacks; each id is a separate Ground GObj (foreground platform,
background layers, sky) rendered in the same scene.  Choosing the "main" map
by smallest bounds is only for the camera/lights/fog source.
**Fix:** `hsd_scene_load_stage_all` loads every map joint into
`HsdScene.stage_roots[]` (bounded at 64), `compute_bounds`/`render_scene_draw`
iterate them, and the camera map only supplies camera/lights/fog.  `,`/`.`
still isolates one map (`--stage-map N`) for inspection.

## G-076: i386 aligns 8-byte fields to 4, so decomp struct layouts drift

**Symptom:** the compiled port writes into unrelated globals: the save-data
name init (`InitializePersistentNameData`) clobbered
`gmMainLib_8046B0F0.resetting`, so every scene exit took the reset path and
the pending game-mode change was discarded.  `sizeof(struct gmm_x0)` was
0x850C instead of the retail 0x8518, and `thing.x2FF8` sat 12 bytes early.
**Cause:** PowerPC EABI aligns `s64`/`double` inside structs to 8 bytes; i386
SysV aligns them to 4.  The decomp's `ASSERT_SIZE` checks are disabled on the
host (G-048), so the drift is silent.
**Fix:** compile every 32-bit compiled-decomp target with
`-malign-double` (`native/CMakeLists.txt`).  Verify with
`sizeof(struct gmm_x0) == 0x8518` and `offsetof(thing.x2FF8) == 0x2FF8`.
Do not add it to 64-bit-only targets (`melee_decomp_math` is shared).

## G-077: effect/stage PS banks self-relocate; convert endian only

**Symptom:** `psInitDataBankLocate` walks off the end of a bank
(`EfCoData.dat` during `gm_Scene_Vs_OnEnter`, `map_ptcl`/`map_texg` during
stage load), or triple-swaps group fields.
**Cause:** `Ef*.dat eff*DataTable` points at two bank blobs and
`Gr*.dat` exposes `map_ptcl`/`map_texg` directly; `particle.c` relocates the
bank's internal pointers *at runtime relative to the bank base* (they are not
in the HSD relocation table).  The converter must only bring the numeric
fields to host order.  The cmd-bank version word is a `u16` at +0; read it
big-endian (`be16`) before deciding the layout, and do not apply the
version-0 cmd-bank reloc layout to the tex bank (its group table starts at
+4, after `version|num_groups`).
**Fix:** `conv_ps_cmd_bank` (versions 0x40..0x43 and 0) and
`conv_ps_tex_bank` (version 0, `HSD_PSTexGroup` fields + texTable/palette
pointers), dispatched from `eff*DataTable`, `map_ptcl` and `map_texg`.
Partial conversions are worse than none: a half-swapped header makes the
runtime's control flow diverge from the console's.

## G-078: stage `coll_data` and `map_ptcl` overlap in the archive

**Symptom:** after converting `coll_data`, `psInitDataBankLoad` reports
"unknown version" for `map_ptcl`; the bank's version word reads 0x1E00.
**Cause:** in `Gr*.dat`, `MapCollData` (`coll_data`) is 0x2C bytes of real
data, and `map_ptcl` starts exactly at `coll_data + 0x2C` — the decomp's
inferred `x2C` tail field aliases the bank header.  Converting `+0x2C` as a
u32 corrupts the bank version.
**Fix:** `conv_coll_data` intentionally does not convert `+0x2C`.  If a
future consumer needs that field, the two formats must be reconciled first.

## G-079: check host-order reads when deriving converter decisions

**Symptom:** `conv_ps_cmd_bank` never matched version 0x42 and skipped the
whole bank.
**Cause:** the version test used `rd16` (host-order read) on a field that is
still big-endian, so 0x0042 read as 0x4200.
**Fix:** use `be16(c->data + off)` for endian-sensitive decisions taken
before the first conversion of that field.

## G-080: converter loops must stop at the real array count

**Symptom:** Link/Young Link's hidden-part counts came out as 0x01000000 and
the parts walk diverged; later a fixed-size table double-converted unrelated
fields.
**Cause:** `conv_ft_common_data` walked `pData[4]`/`pData[5]` (both indexed by
`FighterKind`, 33 entries) up to 64, then interpreted the neighbouring
`Fighter_804D6540_t` structs as array elements and converted their fields
again.
**Fix:** bound per-kind arrays to `FT_KIND_MAX` (33). When a walk has no count
field, use the index domain from the decomp (`Ft_Kind_Max`), never a round
number like 64. `conv_u32`/`conv_u16` now also track converted offsets in
`Conv.num`, so a legitimate overlap cannot cancel a previous conversion.

## G-081: `&static_a` casts rely on declaration-order layout GCC does not keep

**Symptom:** the VS camera eye/interest went NaN and `game_camera.translation`
became NaN on the first scene-enter frame; `lbVector_WorldToScreen` asserted.
**Cause:** `Camera_ApplyQuake` casts `&cm_803BCB18` (a `CameraModeCallbacks`)
to `{ CameraModeCallbacks; HSD_WObjDesc; HSD_WObjDesc;
HSD_CameraDescPerspective; }`, relying on `cm_803BCB18/3C/50/64` being
adjacent in declaration order. GCC places them apart (0x56b50800, 0x56b53a0c,
0x56b539f8, 0x56b539c0), so the cast read unrelated strings/pointers.
**Fix:** ADR-0011 `PORT_PC` patch in `src/melee/cm/camera.c` reading
`cm_803BCB64` directly. Watch for other address-adjacency casts in `src/`
(`ftdata.c` `ft_800852B0` is the other known one).

## G-082: MWCC packs bitfields MSB-first; archive command scripts need a repack

**Symptom:** the first DK landing action dispatched to
`ftAction_80071A9C` (set_hurt_state) with `bone_idx=3, state=0x1010` and then
asserted `ftcoll.c:3171 "illegal parts!"`; many other action commands would
mis-dispatch the same way.
**Cause:** the fighter action scripts are archive data (`Fighter_WaitAnimData.xC`
in `Pl*.dat`). MWCC on PowerPC allocates bitfields from the MSB of the
big-endian word, so `{opcode:6; ...}` lives in `word >> 26`. GCC on x86
allocates from the LSB, so the host reads `word & 0x3f` and dispatches the
wrong action. The decomp's `/// Bits 0~5` comments describe source bit
positions, not host memory order.
**Fix (next task):** walk each script with `opcode = be32(word) >> 26` and the
advance counts in `ftAction_803C0870[]`, then repack each command word from
MSB-first to LSB-first field order using the `CmdUnion` structs in
`src/melee/lb/types.h`. A plain byte swap is **not** the transform (it maps
the opcode to bits 24..29). Pointer words (Goto/Subroutine) are relocation
targets and are already host order.

**Alternative lead (may be cleaner):** GCC supports
`__attribute__((scalar_storage_order("big-endian")))`, which makes bitfield
loads read MSB-first from the raw bytes. A two-line test on the actual DK
landing word reads `opcode=55 bone=0 state=0x404` (the console values) with no
data conversion. Adding the attribute (under `PORT_PC`) to the `CmdUnion`
member structs in `src/melee/lb/types.h` / `src/melee/it/*` would let the
engine read archive scripts in place. Caveats: do not annotate pointer-only
members (Command_05/07 `ptr`; they are host pointers fixed by `Locate`),
expect `-Wscalar-storage-order` warnings at mixed-union accesses, and this is
a large ADR-0011 header patch (list it in `decomp_port.md`). Try this before
the walker.

## G-083: `Ground_801C34AC` pair count counts pairs, and `pairs` may be offset 0

**Symptom:** Final Destination's spawn joints (`stage_info.x280[]`) were all
NULL, so `Player_80032768` stored an uninitialized stack vector and the
fighter's first collision produced a ~1e14 position.
**Cause:** two bugs in `conv_stage_maphead`'s entry loop.  The retail
`Ground_801C34AC` entry `{void* joint; s16* pairs; s32 count}` stores the
number of `(joint,target)` *pairs* in `count` and advances `pair += 2`; the
converter swapped only `count` u16s.  And the `pairs` pointer may point at
data offset 0, which `pairs != 0` mistook for NULL even though it is a
relocation target (G-023 applies here too).
**Fix:** bound the u16 loop by `pair_count * 2` and accept `pairs == 0` when
`c->reloc[e + 4]` is set.

## G-084: compiled-data bitfields with explicit masks need host bit positions

**Symptom:** `Ground_801C466C` never selected a per-map light list
(`callbacks->flags_b0 == 1` never matched) and fell back to the default list,
which then asserted in `Ground_801C43C4`.
**Cause:** `grLast_StageCallbacks[].flags` is initialized with `0x80000000`;
MWCC packs the `u8 flags_b0:1` aliases MSB-first, so `flags_b0` is bit 31,
while GCC puts it at bit 0.
**Fix:** under `PORT_PC`, declare the alias fields at the matching host bit
positions (24 bits of pad, then `flags_b7..flags_b0`) in `StageCallbacks`.
This is not `scalar_storage_order` (which does not reorder bitfields inside a
byte).  Check other compiled structs whose initializers use bit-31 masks.

## G-085: card work area is one 0x1510-byte `CardContext`, not 0x10 bytes

**Symptom:** ASan: `SEGV on unknown address 0x20` in `hsd_803AAA48`
(`hsd_3A94.c:1071`) on every game-mode change; release clobbered adjacent
globals silently.
**Cause:** `hsd_4D11.c` declares `u8 hsd_804D1138[0x10]`, but `hsd_3A94.c`
casts it to `CardContext` (0x1510 = 4 header words + `CardCmd[128]` +
`HsdCmdEntry[32]`).  On the console the three card/JPEG symbols are
contiguous (`0x4D1138 + 0x1510 == 0x4D2348 + 0x300`); GCC may order them
differently.
**Fix:** `hsd_4D11.c` defines `hsd_804D1138[0x1510]` under `PORT_PC`.  The
card path is still S6 work, but the pump must not write out of bounds.

## G-086: completion queue held freed entries during callbacks

**Symptom:** ASan: heap-use-after-free in `dvd_mark_canceled`
(`native/platform/dvd.c`) when `DVDClose` cancels a command from inside a
completion callback.
**Cause:** `platform_pump_completions` invoked each callback while the
consumed `DvdCompletion*` was still in the queue; a callback that calls
`DVDCancel` visits the queue and reads the freed entry.
**Fix:** clear each queue slot (`fn`/`arg` = NULL) before invoking its
callback, and skip NULL slots in `platform_visit_completions`.

## G-087: the viewer inherited the boot triage's 60-frame budget

**Symptom:** `melee_decomp_viewer --match --frames 150` presented 60 frames,
printed nothing after frame 60, exited 0 and never wrote `--shot`; it looked
like a hang or the game exiting on its own.
**Cause:** `boot_triage_frame()` stops at the static default
`frame_budget = 60`; only `--boot-frames` sets it, and the viewer called
`boot_triage_init(..., 0, 0)` without touching the budget, so
`boot_triage_stop("frame budget reached")` called `exit(0)` from inside
`VIWaitForRetrace`.
**Fix:** `run_match` calls `boot_triage_set_frame_budget(limit + 240)` (0 =
unlimited when no `--frames` is given).  When a viewer match stops early, run
it under `gdb` with a breakpoint on `exit` or check the triage stop reason
before debugging the game.

## G-088: the GX HLE asset table was capped at 8 archives

**Symptom:** the VS-scene "GO!" logo rendered as a black quad while the log
spammed `gx_gl: CI texture decode failed: truncated CI texture data`; the
texture is 376x188 CI8 (70,688 bytes of tile data) but the decoder saw a
64 KB bound.
**Cause:** `gx_hle_register_asset` stored at most `GX_HLE_MAX_ASSETS` (8)
archives and silently dropped the rest.  A match parses far more than 8, so
the GO! archive was unknown, `gx_hle_asset_remaining` returned -1 and
`texture_for` fell back to its 64 KB bound — smaller than the texture.
**Fix:** the asset table grows with `realloc` (16 entries, doubling);
`gx_hle_reset_assets` resets the count only.  The decode failure log now
prints dimensions, format and available bytes so a wrong bound is obvious.

## G-089: bound texture decodes by the GX-declared size, not the archive

**Symptom:** the title screen spammed `gx_gl: texture decode failed:
truncated GX texture data` and drew black rectangles over the logo: 48x48 and
44x44 I4 textures reported only 943/732 bytes available.
**Cause:** `texture_for` bounded the decode by the remaining bytes of the
registered archive containing `t->image`.  Runtime `HSD_ImageDesc`s
(`HSD_ImageDescAlloc`), copied/streamed images and freed-then-reused archive
buffers are not those ranges, so the bound was unrelated to the texture (the
pointer merely happened to land near the end of a stale buffer).
**Fix:** the bound is the tile-aligned size the decoder reads
(`gx_texture_min_size`), exactly what the game's own `GXInitTexObj` draw
consumes; the archive lookup stays only as a fallback for unsupported
formats.  Do not restore the archive bound — it produces false truncation
failures for any texture outside a parsed archive.

## G-090: the debug title freezes the logo on its reveal card

**Symptom:** the title screen draws a large opaque grey card behind the
"SUPER SMASH BROS. Melee" logo; the retail title shows the logo floating on
the tunnel with no card.
**Cause:** the card is draw 13 of the logo reveal (IA4 448x128 texture,
material diffuse `179`, TEV alpha `A1 + TEXA*(K1_A - A1)` with
`A1 = K1_A = 1` -> opaque — the captured GX state is faithful).  The card
only exists in the logo animation's intro frames 0..~270: `gmtitle.c`
starts the logo at frame 400 and loops 400..1600
(`AnimLoopSettings {0, 1600, 400}`).  The S4 debug flow reaches `GM_TITLE`
without the opening movie and without the retail mode ordering, so
`gm_804D67EC == 0`, and `fn_801A1498` re-requests `gm_804D67EC - 5130`
(clamped to frame 0) every frame — the reveal card never animates away.
Forcing `GM_TITLE` from the harness black-screens, so this is not fixable
from `--match`.
**Fix (S6, P-624):** enter the title the way `gmboot`/`gmopening` do — mode
is `GM_TITLE` by scene on-enter and/or `gm_804D67EC` is past 5400, so
`gmTitle_801A165C` starts the logo at frame 400.  Do not "fix" this in the
GL layer.
**Resolution (2026-09-14):** the mode-ordering work that shipped the retail
frontend as the product (`05b034415`) already enters the title scene with
`gm_GetCurrentGameMode() == GM_TITLE` (traced at `gmTitle_801A165C`: mode 0,
scene 0, `gm_804D67EC == 0`), so the retail branch runs: logo requested at
frame 400, `gmTitle_801A1630` looping 400..1600.  The opening path still uses
`fn_801A1498` and reveals the logo as the movie frame count passes 5400.  The
grey card is gone in both flows; `ctest decomp_title` (`MELEE_TITLE_TEST=1`)
reads the link-9 title logo's `mn_8022F298` frame and requires it in
[400, 1600].  Flipping `isActiveTitle()` to true (the old wrong branch) makes
the probe read `logo_anim=4294962176` and fail the test.

## G-091: fighters were never drawn — `x21FC_flag` bit order

**Symptom:** the live match rendered the stage, HUD and effects but both
fighters were invisible (later, once forced, they were black and the frame
broke up).
**Cause:** `fighter.c` enables the fighter draw with
`fp->x21FC_flag.u8 = 1`; `ftdrawcommon.c` checks `x21FC_flag.b7`.  MWCC packs
`u8` bitfields MSB-first, so the raw `1` sets `b7`.  GCC packs LSB-first, so
`b7` stayed 0 and `ftDrawCommon_800805C8` returned before `HSD_JObjDispAll`.
**Fix:** `Fighter.x21FC_flag` is `FtStatusFlags` under `PORT_PC`, a union
declaring `b7..b0` in reverse so `b7` is host bit 0 (ft/types.h, ADR-0011
list).  Do **not** reverse the shared `UnkFlagStruct`: stage code
(`grbigblueroute`, `gricemt`) mixes raw `u8` tests with its aliases, and a
global reversal broke stage lighting/fog.

## G-092: GL texture cache overflowed and bound texture 0

**Symptom:** after fighters started drawing, they (and the Go! logo and some
effects) rendered solid black; the stage blew out white and a huge black quad
covered the screen.  No decode errors in the log.
**Cause:** `gx_gl`'s GL texture cache (`MAX_GL_TEXTURES` 256) is never reset
in match mode and accumulated textures across frames; once full,
`texture_for` freed the decoded RGBA and returned 0, so those draws bound
texture 0 (black).  A match frame already uses ~150 textures, so the overflow
hit the Go! logo and the fighters first.
**Fix:** LRU eviction: on a full cache, delete and reuse the least-recently
used entry (`last_used` stamp) instead of failing.  `--dump-draws FRAME`
(melee_decomp_viewer) lists a captured frame's draws with texture bindings
and NDC bounds for exactly this kind of hunt.

## G-093: walk/run animations froze after one cycle

**Symptom:** fighters slid in a fixed walk pose: `cur_anim_frame` stuck at the
clip end (45.56 for WalkMiddle) and never wrapped, so only one walk cycle ever
played.
**Cause:** `ftAnim_8006EBE8` sets `AOBJ_LOOP` only when `fp->x594_b1_loop`, a
byte-view bit of the word copied from
`Fighter_WaitAnimData.x10_animCurrFlags`.  `conv_waitanim_flags` repacks that
word for GCC's LSB-first bitfields but only for the word-view fields; the
byte-view flags were left shifted two bits up in the pad field, so the
console's `b1_loop` (= bit 30 of the big-endian word) landed where GCC never
read it.  Walk entries have `b1_loop = 1`; Wait has 0.
**Fix:** `conv_waitanim_flags` also bit-reverses the word's top byte into the
low byte (converter v57), so the byte-view flags read the console bits.
Verified: WalkMiddle/WalkFast frame counters now wrap (43 -> 7, 22 -> 2) and
`x594_b1_loop` reads 1 for anim 7/8/9 and 0 for Wait.  Bump
`HSD_CONVERTER_VERSION` whenever this word layout changes.

## G-094: `__frsqrte` is reciprocal square root, not square root

**Symptom:** Link's limbs folded into an impossible pose after KO/respawn and
then stayed corrupt for hundreds of frames.  During the same interval, game
work rose from ~2–5 ms to ~29 ms although draws, vertices, display lists and
GL render time were unchanged.
**Cause:** the non-Metrowerks fallback in `src/placeholder.h` defines
`__frsqrte(x)` as `sqrt(x)`.  The game treats the intrinsic as a reciprocal
square-root estimate and applies three Newton-Raphson refinements.  With the
wrong starting function, Link's foot/leg IK eventually passed NaN angles into
the JObj hierarchy.  The GX HLE then spent ~25 ms doing x87 matrix arithmetic
on 40k+ NaN matrix components; the apparent renderer hot spots were a
consequence, not the cause.
**Fix:** `native/decomp/shim/placeholder.h` shadows the upstream header and
redefines `__frsqrte(x)` as `1.0 / sqrt((double) x)`.  Keep this in the native
shim; do not modify the decomp source merely to supply host intrinsic
semantics.  `decomp_gx_direct` checks `__frsqrte(4) == 0.5`.
**Correction (P-627):** this fixed the slowdown but not the pose.  The IK still
received NaN angles because `ftData->x58` was never byte-swapped (G-096); the
legs only became coherent once that was converted.

## G-095: 32-bit GCC x87 excess precision can poison quaternion slerp

**Symptom:** after correcting `__frsqrte`, the first match initially stopped
at viewer frame 83 with `OSPanic` in `mpCollInterpolateECB`; the fighter's
collision joints had become NaN during animation blending.
**Cause:** the 32-bit build used GCC's x87 backend.  A `float` quaternion dot
product remained just below 1 in an 80-bit register, so
`HSD_QuatLib_8037EF28` selected its spherical interpolation branch.  Passing
the same value to `acosf` rounded it to exactly `1.0f`; `sin(acos(1))` was
zero and both interpolation weights became `0/0`.  PowerPC and normal SSE
evaluation round the expression to its declared `float` width before the
branch.
**Fix:** all 32-bit decomp targets compile with `-msse2 -mfpmath=sse`.  This
preserves the C float/double widths without `-ffloat-store`'s pervasive memory
traffic and is faster on the supported PC baseline.  `decomp_gx_direct`
regresses the exact near-identical quaternion pair that exposed the issue.

## G-096: `ftData->x58` leg-IK lengths were never byte-swapped

**Symptom:** Link's legs stayed missing/deformed in the live match even after
G-094/G-095 removed the sustained NaN slowdown.  At frame 720 the leg draws
(117–121, 135–139) had entirely non-finite transformed coordinates and the
`--dump-draws` NDC bounds stayed at the inverted `[1000000000,-1000000000]`
sentinel.  GDB showed Link parts 6–10 and 12–16 with finite
rotate/scale/translate but NaN `HSD_JObj.mtx` rotational components, so the
NaN had been written into the matrices and the finite rotations restored
without re-dirtying them; the legs simply weren't re-set-up in Wait.
**Cause:** `ft_80089B08` reads the two-bone leg lengths from
`((ftData_x58_t*) fp->ft_data->x58)->x4/xC/x18`.  The converter handled the
`x0/x1/x8/x9/x10/x11` part indices (bytes, no swap needed) but never converted
the three f32 fields.  The raw big-endian words read little-endian as
`x4=-490.4`, `xC=-1.01e27`, `x18=7.72e35`; the target position `pos3/pos4`
exploded to ~1e35, `lbBgFlash_80021410` computed `len_ac` from an overflowing
square and `acos` of NaN, and `fn_8002113C` wrote NaN rotations for parts
6–16.  NaN angles were present from frame 190 in every run; only some states
(e.g. Wait, LandingAirF) left the poisoned matrices dirty-less until render.
**Fix:** `native/decomp/assets/hsd_convert.c` v58 converts `x58+0x04`,
`x58+0x0C`, `x58+0x18`; the values now read `2.89 / 3.88 / 1.36` and the IK
stays finite.  Verified with `--dump-draws 720` (zero non-finite NDC bounds),
GDB matrix probes over frames 650–720 (all finite) and `--match --frames 1200`
(no leg draws above 10 ms game time).  Lesson: when adding a loader walk, list
*every* pointer field's pointee numeric payload — `x58` was the one ftData
sub-table with no walker.  `ftData->x1C` has the same omission (P-630).

## G-097: GC DSP-ADPCM is 8-byte / 14-sample frames, scale in the low nibble

**Symptom:** music and SFX render as **loud noise** through the mixer (peak
near full scale, spectral flatness ~0.27, no stereo image). Typical ear
damage if you had headphones on.
**Cause:** the decoder assumed 9-byte frames with 16 samples and
`scale = header >> 4`, `predictor = header & 0xF`. GC DSP-ADPCM is
**8 bytes = header + 7 data bytes = 14 samples**, with
`scale = 1 << (header & 0x0F)` and `coef_index = header >> 4`, decoded as
`((nibble * scale) << 11) + 1024 + coef1*hist1 + coef2*hist2) >> 11`, nibbles
high-first. (`AXPBADPCM.pred_scale` still equals the first frame's header,
which made the wrong interpretation look plausible.)
**Fix:** `native/audio/ax_mixer.c:voice_decode_frame`; frame addressing is
16 AX units per frame and the mid-frame position fraction is `/14`. Verify
with a fixed dump: flatness should be < 0.05 (tonal), not > 0.25.

## G-098: `*(u32*) &hi` over two u16 fields is big-endian-only

**Symptom:** SFX play from garbage addresses (`currentAddress` like
`0x73528a3d`) and stay silent, or every voice's pitch is 1/65536.
**Cause:** the engine stores `AXPBADDR` addresses and `AXPBSRC.ratioHi/Lo` as
two adjacent u16s and pokes them with a single u32 store/load (correct on
big-endian PowerPC). On little-endian the halves land in the wrong fields.
**Fix:** use the `MELEE_PORT_AX_GET/SET_U16PAIR` / `SET_RATIO` macros from
`decomp_shim.h` under `PORT_PC` (the S5 patch list in
`learnings/decomp_port.md`). Never "fix" this by swapping the struct fields —
the compiled SDK setters copy the fields one by one.

## G-099: `smash2.sem`, `.hps` and `.ssm` need format-aware endian conversion

**Symptom:** `AXDriver_8038DA70` segfaults right after loading `smash2.sem`;
the HPS stream starts with `voice_count = 0x02000000` and crashes in
`AXSyncVoiceMix`; SFX voices stay silent.
**Cause:** unlike the HSD archives there is no relocation table: the audio
files are raw big-endian structs that the compiled C reads as host values.
**Fix:** convert on the DVD read path, keyed by file extension —
`platform/sem.c` (5 count/list sections + every command-stream word),
`platform/hps.c` (stream header, per-voice AXPBADDR/AXPBADPCM, 0x20-byte page
tables), `platform/ssm.c` (header first, then u16 fields; address pairs are
Hi/Lo u16 pairs). The `.ssm` converter is stateful across reads and each read
has its own destination buffer: never pass the same buffer for two reads in a
test (the second call would convert `dst[0]` as the window start).

## G-100: synchronous loaders need the idle tick to pump completions

**Symptom:** after `AXDriver_8038DA70` is compiled for real, `gm_main` spins
in the `smash2.sem` loading screen calling `PADRead`/`CARDProbe` forever.
**Cause:** it waits on a DVD callback from `while (flag == 0) callback();`,
and its callback never reaches a `VIWaitForRetrace` or `OSRestoreInterrupts`
point; the host delivers deferred completions only at those points.
**Fix:** `boot_platform_idle_tick` (which `DVDGetDriveStatus` calls, the only
hardware poll in that loop) pumps the completion queue when interrupts are
enabled. Do not pump while interrupts are disabled (G-063's rule).

## G-101: `GXCopyTex` has more formats than RGB565/RGBA8

**Symptom:** Final Destination's platform top (and other shadow-receiving
surfaces) renders as a hard black band; the correct texture flashes for a few
frames when the camera zooms.  Isolating the draw (`--part N --part-mode
only`) shows a black quad.
**Cause:** the material's last TEV stage multiplies by a texture produced by
`GXCopyTex`; HSD's dynamic shadow map uses `GXSetTexCopyDst(w, h, 0x20)` =
`GX_CTF_R4` (`src/sysdolphin/baselib/shadow.c:108`).  `efb_copy_tex` only
implemented `GX_TF_RGB565` and `GX_TF_RGBA8`, so the destination buffer kept
its initial contents and the multiply went black.
**Fix:** implement the R4 copy in `native/decomp/gx/gx_gl.c:efb_copy_tex`: read
the EFB red channel, keep the high nibble, and write the 4-bit tiled layout
(8x8 px / 32-byte tile, first pixel in the high nibble) that
`native/gx/texture.c:decode_i4` expects.  `ctest decomp_efb` pass 3 covers it.

## G-102: `GXSetViewport` must be applied per draw

**Symptom:** character shadows appear far from the fighters, wrong size, or
not at all; the shadow map (a 256x256 R4 EFB copy) contains silhouettes that
don't line up with the world.
**Cause:** `GXSetViewport` was captured but never used when replaying draws.
The shadow pass renders through a 256x256 camera viewport; mapping its NDC to
the whole surface put the silhouettes in the wrong place (and the copy read a
different region).
**Fix:** `GxHleDrawState` carries `viewport[4]`/`depth_range[2]` (set by
`GXSetViewport`), and `gx_gl.c:apply_viewport` sets `glViewport`/`glDepthRangef`
per draw, converting GX's top-left origin to GL's bottom-left.  The Z-texture
pass applies it too.

## G-103: a backend state reset needs `HSD_StateInvalidate(-1)`

**Symptom:** the shadow map background is intermittently black (a black band
across shadow-receiving surfaces for a few frames), and other material state
can go stale after a reset.
**Cause:** `gx_hle_begin_frame` resets the captured GX state; HSD caches the
state it has *requested* (`prev_ch`, TEV stages, vtx descs) and skips setters
it thinks are unchanged (e.g. the shadow pass's white material colour after a
frame compiled with white).  Our reset then leaves the state at the default
(black) while HSD never re-emits it.
**Fix:** every target that resets the backend while the compiled engine is
running must call `HSD_StateInvalidate(-1)` right after (the live match viewer
and `bounds_pass` do).  Tests use `gx_hle_reset_state()` explicitly.  This is
the G-054 contract, now at the frame boundary.

## G-104: a material can reference multiple TEV texture maps (base + shadows)

**Symptom:** with two fighters on the ground only one character shadow shows;
which one depends on the material.
**Cause:** the platform's material compiles as `K0`, base texture (map 0),
fighter A shadow (map 1) and fighter B shadow (map 2).  The GL fragment path
had only `u_tex0`/`u_tex1` and silently dropped the map-2 stage.
**Historical fix:** a third texture unit (`u_tex2`, `u_tex_lod_bias.z`,
texmap[2]) restored the first observed two-shadow material.  The conclusion
that three was enough was false: Castle and Stadium use two base maps plus two
shadow maps, reaching map 3.  P-736 carries all eight hardware maps/coords;
see G-173.

## G-105: offscreen SDL video does not mute the match viewer

**Symptom:** an automated screenshot or frame-capture command unexpectedly
plays game audio through the user's speakers even though
`SDL_VIDEODRIVER=offscreen` is set.
**Cause:** SDL's video and audio drivers are independent.  The compiled match
viewer still opens the default audio device after the game initializes.
**Fix:** set both variables for every non-listening viewer run:
`SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy`.  Omit the dummy audio
driver only when the user explicitly asks for a listening test.

## G-106: relocated dynamics pointers do not convert packed solver numbers

**Symptom:** Link's cap/head sprouts a long, otherwise finite green polygon;
matrix and NaN probes look clean. **Cause:** `BoneDynamicsDesc.data` is
relocated, but its pointee is a packed array of `count` 0x3C-byte solver
records. `lb_80011710` reads 15 floats per record, which remained big-endian.
**Fix:** converter v59 swaps every packed record for fighter and item dynamics;
`test_decomp_assets` checks Link's node count and representative parameters.

## G-107: live R4 EFB copies must stay on-GPU without inherited scissoring

**Symptom:** stage shadow maps create recurring render stalls, or a direct GPU
blit makes Final Destination's platform top black. **Cause:** materializing R4
through `glReadPixels` is a synchronous GPU-to-CPU-to-GPU round trip; the fast
blit is itself clipped if the previous GX draw's scissor remains enabled.
**Fix:** the SDL-attached renderer keeps R4 copies in GPU textures, disables
and restores scissoring around the blit, and marks dynamic I4 sampling in the
TEV shader. The headless EGL test path still materializes bytes.

## G-108: GX TEV register uniform slots include PREV at zero

**Symptom:** Final Destination's rotating center effects blend with
`SRC_ALPHA/INV_SRC_ALPHA` but still form opaque black slabs; their animated
alpha never appears. **Cause:** `GXTevRegID` is `PREV=0, REG0=1, REG1=2,
REG2=3`. The HLE correctly stored `GXSetTevColor(REG0, ...)` in slot 1, but
the fragment shader initialized C0/C1/C2 from slots 0/1/2. Thus `GX_CA_A0`
read PREV's default alpha 1 instead of the animated REG0 alpha (often 0).
**Fix:** initialize C0/C1/C2 from uniform slots 1/2/3. The EFB regression
renders a REG0 quarter-alpha red quad over the clear color and checks the
blended pixel.

## G-109: combined GX channel controls also set the paired alpha channel

**Symptom:** stage effects that should fade out render fully opaque. Final
Destination's center glow is a hard white donut instead of a soft halo, and
Battlefield's under-stage panels stay flat and bright. **Cause:** those
materials draw with vertex colours whose **alpha carries the gradient**
(`v0.color = 255,255,255,0`) and select the source with
`GXSetChanCtrl(GX_COLOR0A0, ..., mat_src=GX_SRC_VTX)`. The HLE stored that
control only in colour slot 0; the fragment shader reads raster alpha from the
paired alpha slot 2, which still held the default register alpha 1.0. On
hardware a combined `COLOR0A0` control applies to both. **Fix:**
`GXSetChanCtrl` mirrors `COLOR0A0`/`COLOR1A1` writes into the paired alpha
slot; a later separate `ALPHA0`/`ALPHA1` write still overrides it.
`ctest decomp_gx_direct` checks the mirrored `mat_src` and a zero vertex alpha.

## G-110: stage maps carry three animation arrays, not one

**Symptom:** stage material animation (colour/alpha fades) never runs, while
joint animation (rotation) does. **Cause:** `UnkStageDat_x8_t` holds
parallel `AnimJoint**`, `MatAnimJoint**` and `ShapeAnimJoint**` arrays
(`+4/+8/+C`) indexed by joint; `grAnime_801C7C1C`/`grAnime_801C6C0C` load all
three at stage load. The converter only walked the first, so material AObjDesc
`end_frame`/`flags` reached `HSD_AObjLoadDesc` big-endian (denormal durations
stop the AObj immediately). **Fix:** converter v61 walks all three arrays with
the relocation table as the array bound (NULL slots are legal gaps, not the
end) and `test_decomp_assets` verifies the converted AObjDesc durations for
`GrNBa`/`GrNLa`.

## G-111: viewer cameras must keep the GX 640x480 viewport space

**Symptom:** the model/stage viewer looks zoomed in and shows only a slice of
the scene (top-right or bottom, depending on the window), while the match
viewer is fine. **Cause:** the GX HLE maps GX viewport/scissor rects from
640x480 EFB pixels onto the real window. `render_scene_update_view` also wrote
the window's pixel size into `HSD_CObjSetViewport`/`HSD_CObjSetScissorx4`, so
the HLE scaled it a second time (2x at 1280x800) and cropped the scene.
**Fix:** keep the viewer camera's viewport/scissor at the 640x480 authoring
rect; `HSD_CObjSetPerspective` already carries the window aspect.  The viewer
also polls `SDL_GetWindowSizeInPixels` every frame (not just on resize events)
so a tiling WM mapping the window late or a live drag stays in step.

## G-112: the DOL is not an FST entry; the FST stores basenames

**Symptom:** `platform_disc_load_file("sys/main.dol")` (used by the font
bootstrap, `native/decomp/fonts.c`) fails with `file not found` and the SisLib
atlases stay zero, so atlas-backed text is invisible.
**Cause:** on a GameCube disc the DOL is a raw region addressed by the disc
header at `0x420`, not a file in the FST; and the port's FST scan in
`native/platform/disc.c` compares the raw name strings, which are basenames
(`main.dol`, `Tyandold.dat`), not paths. Logging every `files[i].name` in
`dvd.c` shows 1209 entries and no `main.dol`.
**Fix:** read the DOL via `disc_image_read()` at the header's DOL offset
(compute the size from the DOL section headers) instead of an FST lookup.
**Do not:** try `"sys/main.dol"` or `"/sys/main.dol"` in `disc_load`.

## G-113: SIS text buffers are console big-endian and dispatch on the first byte

**Symptom:** compiling the real SisLib engine (`hsd_3A76.c`) segfaults in
`gx_texture_decode` on the first dialog glyph (`GXInitTexObj` image like
`atlas + 0x1FA4000`), or text is missing.
**Cause:** the renderer's parser reads `u8 opcode = *cursor;` and treats
`>= 0x20` as a glyph, then reads a `u16` glyph code; message tables
(`SdMsgBox`'s `SIS_MessageData`) store that code **big-endian**. Reordering
the buffers to host order (swapping pairs, normalizing at
`HSD_SisLib_803A6368`/`803A6478`) breaks the first-byte dispatch and/or
corrupts the font's kerning tables (indices 0/1 of the SIS array are the
kerning/texture tables, not messages).
**Fix:** keep buffers byte-identical and patch the reads in `hsd_3A76.c`
through `SIS_U16/SIS_S16/SIS_S32` (PORT_PC accessors in `sislib.h`). Watch for
the spaced cast `*(u16 *)` which is easy to miss in a mechanical replace.

## G-114: `HSD_TexAnim.id` is a GXTexMapID enum that must be byte-swapped

**Symptom:** title logo letters render as hollow outlines (no fire fill),
background effects look flat, and the main-menu 1-P preview panel stays
empty — although the textures, TEV stages, UVs and joint anims are all
correct and the fire image table (~30 CMPR frames) is in memory.
**Cause:** `conv_texanim` never converted `HSD_TexAnim.id` (+4, a 4-byte
enum, not a reloc target). An id of 1 stays `00 00 00 01` and reads back as
16777216, so `lookupTextureAnim` (`ta->id == tobj->id`) never binds any
nonzero-map TexAnim and TIMG image-sequence animation never runs. id 0
works by accident (0 swaps to 0), which is why TEXMAP0 TexAnims (and most
of the game) looked fine.
**Fix:** `conv_u32(c, off + 0x04)` in `conv_texanim` + bump
`HSD_CONVERTER_VERSION` (G-067). Verified in the v67 cache bytes
(ids read 1/0/1), in gdb (the letter's `tex1img` changes 450→550 in one
run), and visually (panel text bands appear). Debug technique: extend
`--dump-draws` (tex1 details, all-quad UVs, texgen matrix ids) rather than
guessing from thumbnails.

## G-115: coalesced AX sync bits drop `AXSetVoiceAddr`'s whole-struct copy

**Symptom:** an HPS stream plays its first ARAM page and then goes silent
(menu BGM after ~2 s); the page machine freezes with `pos` still in the start
slot and the node is torn down when both voices reach their mixer end.
**Cause:** `AXSetVoiceAddr` sets `AX_SYNC_FLAG_COPYADDR` (`__AXServiceVPB`
copies the whole 16-byte `AXPBADDR`), then `HSD_Synth_8038B120` adds
`COPYCURADDR`/`COPYENDADDR`/`COPYLOOPADDR`.  `__AXServiceVPB` gives the
per-field branch priority and only copies the named fields when both are
pending, so the shadow never receives the header's `loopFlag`.  On hardware
the DevCom bootstrap (header → page table → 64 KB page data) spans several
5 ms AX frames, so `COPYADDR` is serviced first; the host's synchronous DVD
completion chain finishes the whole bootstrap inside one frame.  GDB proof:
the first `__AXServiceVPB` for the stream voice sees `sync=0x0007d2b6`
(COPYADDR + field bits, no `COPYLOOP`) and the shadow `loopFlag` stays 0 while
the user PB reads 1.
**Fix:** `native/audio/ax_hle.c:ax_collapse_addr_sync` clears the per-field
address bits before `__AXSyncPBs` when `COPYADDR` is pending; the whole-struct
copy then applies the user PB's latest values (the field setters wrote them to
the same PB), which equals the console state after a frame boundary.

## G-116: HPS page loops target above `endAddress`; the end test must be crossing-based

**Symptom:** after the loopFlag fix, the music plays but a loud ~2.3 kHz
"beep" bursts out of the speakers for 5–15 ms at page handoffs (every ~0.9 s,
worst on slot0→slot1/slot1→slot2).
**Cause:** two wrong assumptions in `voice_decode_frame`.  (1) The HPS ring
moves `loopAddress` to the *next* page's ring slot before the game extends
`endAddress`; the next slot is usually a higher address, so `frame_addr >=
end_addr` fires on the first frame after the wrap and re-wraps on *every*
frame until `HSD_Synth_8038ADD0` runs, replaying the page's first 14-sample
frame as a 2.3 kHz tone.  (2) The DSP jumps to `loopAddress` whenever
`loopFlag` is set; it does not require `loopAddress < endAddress`.
**Fix:** trigger only when the address *crosses* the end
(`prev_frame_addr < end_addr && frame_addr >= end_addr`, tracked per voice)
and drop the `loop_addr < end_addr` test.  Verified with a `[wrap]` trace:
one wrap per voice per page instead of a storm, and zero
`peak>20000 && meanabs>6000` 5 ms windows.

## G-117: the HPS page table's `AXPBADPCMLOOP` contexts need a u16 swap

**Symptom:** audible click/step at every page seam even after G-115/G-116;
the seam sounds like a short discontinuous burst.
**Cause:** `hps_fix_read` only swapped the page table's three u32s
(`x0`/`x4`/`x8`).  The per-voice decoder continuation
(`AXPBADPCMLOOP.loop_pred_scale/loop_yn1/loop_yn2` at bytes `0xC+i*8`) stayed
big-endian, so the mixer loaded byte-swapped predictor history (`yn` off by
one byte) at each wrap.
**Fix:** `swap16` every u16 from `0x0C` to `0x20` in the page-table branch
(keep `swap32` for the three words — swapping the u16 halves of a u32 is not a
byte swap).  Validated by decoding the concatenated per-voice page data in a
throwaway Python probe: the stored context at every page boundary matches the
running decoder state exactly (`yn1`/`yn2`/`pred` identical, pages 1–11, both
voices), so applying it makes the seams bit-continuous.

## G-118: `ItemStateArray[8]` is not the serialized array bound

**Symptom:** random common items, especially Bob-omb, abort in
`HSD_PObjResolveRefs` because a skin PObj's referenced joint is absent from the
loaded model root.  Other item model counts/attach IDs remain byte-reversed.
**Cause:** the decomp type declares eight states, but `ItCo` serializes a
variable-length state array immediately before each `Article` (with alignment
padding).  Walking eight entries overruns short arrays into the article/model
metadata; animation walkers mark or mutate those descriptors before their real
converter runs.  Some articles also have more than eight states.
**Fix:** derive the state count as
`(article_offset - states_offset) / sizeof(ItemStateDesc)` and bounds-check it.
Converter v68 plus `test_decomp_assets` now loads all 40 common-item model roots
through `HSD_JObjLoadJoint`.

## G-119: `ftData->x1C` descriptor indices are `u16`, not byte data

**Symptom:** a fighter crashes when an action command applies a part animation;
the observed Classic-mode landing reached `ftAnim_80070904` with
`start=0x2900`, then indexed `Fighter.parts` into an invalid `HSD_JObj*`.
Link had the same latent error (`x0=0x1700`, `x2=3072`).
**Cause:** archive relocation converted the `ftData->x1C` table and descriptor
pointers, but the descriptors' `u16 x0` first-part and `u16 x2` part-count
payload remained big-endian.  The archive table is not five unconditional
pointers merely because `Fighter.x8B0` has five runtime slots: only its leading
relocation-backed entries are serialized, and adjacent words can resemble a
pointer.
**Fix:** converter v69 walks at most five entries while each slot is a real
relocation, and converts descriptor offsets `+0/+2`.  `test_decomp_assets`
checks all four serialized slots in both `PlMr.dat` and `PlLk.dat`.

## G-120: PowerPC callbacks cannot be invoked through smaller prototypes on i386

**Symptom:** Pokémon Stadium crashes during `grStadium_OnInit` when
`grAnime_801C77FC` applies the stage's looping AObjs; `fn_801C6F2C` receives
`aobj == fn_801C6F2C` and faults on the first write.
**Cause:** the retail `grAnime_801C6F50` dispatcher calls the AObj callback
through generic prototypes (`((Event) func)()` for `AOBJ_ARG_A`).  PowerPC's ABI
keeps the first integer argument in `r3` across that call, so a no-argument
callee still sees the `HSD_AObj*`; on i386 cdecl the callee reads whatever the
stack holds instead.
**Fix:** under `PORT_PC`, dispatch on the `AObj_Arg_Type` and call the exact
declared shape (`patches/src/melee/gr/granime.c.patch`, listed in
`learnings/decomp_port.md` S6).  The GC build keeps the retail calls.

## G-121: converter array bounds must not trust the next pointer alone

**Symptom:** Fox crashes or freezes the first Classic match right after
landing; `ftAnim_80070904` dereferences a bogus joint from
`ftData->x1C[slot]->x8[arg2]`.
**Cause:** `conv_ft_data` bounded the `Fighter_WaitAnimData` arrays (`xC`/`x14`)
by the closest `ftData` pointer value after the array start.  For Fox that
value sits past the part-animation pointer arrays, so the walk processed other
structures' words as records and `conv_waitanim_flags` overwrote a valid
`HSD_AnimJoint*` (raw `0x00700313`).
**Fix:** stop the walk at the first record whose `x0` is neither a relocation
target nor zero: real records carry a name pointer and empty slots are legal
because the runtime indexes by anim id.  Converter v70 makes the trim;
`test_decomp_assets` now diffs every relocation field against a raw copy of the
archive and validates the part-animation trees (P-652).

## G-122: unaligned bitfield overlays on command data read the wrong bit

**Symptom:** no attack ever damages anyone.  Fighters walk through each other
and the opponent's percent stays 0; `ftColl_8007ABD0` (hitbox activation) is
never called.
**Cause:** `ftAction_8007121C` tests `((struct spawn_hitbox_skip*) cmd)->xF_b4`
before spawning a hitbox.  The struct is a bare overlay on the command bytes
(`u8 _0[0xF]` then a `u32` bitfield), and MWCC packs the flags MSB-first from
bit 7 of byte 0xF, so `xF_b4` is bit 3 (`lbz r0,0xf(r4)` + `extrwi. r0,r0,1,28`
in the retail asm).  GCC placed the bitfield differently and read bit 4, which
is set in normal attack commands, so every `spawn_hitbox` command took the
`ftAction_800715EC` skip path.
**Fix:** under `PORT_PC`, `spawn_hitbox_skip` declares the flags in a single
console-ordered `u8` (`patches/src/melee/lb/types.h.patch`, listed in
`learnings/decomp_port.md` S6).  `ctest decomp_hit`
(`MELEE_HIT_TEST=1` on the boot match) asserts the opponent takes damage.

## G-123: ItemAttr's byte bitfields are MSB-first on the console

**Symptom:** item behavior reads the wrong flags: heavy items (crates) are not
treated as heavy, throwable/swingable/shootable classification (`x0_78`) and
the item camera kind (`x1_67_cam_kind`) come out of unrelated bits, and the
`x1_4`/`x1_5` flags copied into `Item.xDC8_word` are wrong.
**Cause:** `ItemAttr` (loaded verbatim from `ItCo.dat`; the converter only
touches `+0x04..+0x80`) declares two bytes of `u8` bitfields.  MWCC packs them
MSB-first, so byte 0 is `x0_is_heavy=0x80`, `x0_78=0x78`, `x0_hold_kind=0x07`
and byte 1 is `x1_1=0xB0`, `x1_3=0x20`, `x1_4=0x10`, `x1_5=0x08`,
`x1_67_cam_kind=0x06`, `x1_8=0x01`.  The retail asm pins this: `itIsHeavy` is
`lbz` + `extrwi r0,r0,1,24` (bit 0x80), `it_8026B30C` is
`extrwi r3,r3,4,25` (bits 0x78) and `itGetHoldKind` is `clrlwi r3,r3,29`
(bits 0x07).  GCC allocates LSB-first, so every compiled field read landed on
a different bit.
**Fix:** under `PORT_PC`, declare the fields in reverse order within each byte
(`patches/src/melee/it/types.h.patch`, listed in `learnings/decomp_port.md`
S6); the GC build keeps the retail declaration.  `test_decomp_assets` now
compares every one of the nine compiled field reads against the raw
console-ordered article bytes for all 43 common-item articles (199 mismatches
without the patch).

## G-124: ftData numeric pointees need their own walks (x40/x4C)

**Symptom:** held/picked-up items snap to the world origin instead of the
fighter's hands, and most per-character sound effects are silent or wrong.
**Cause:** `ftData->x40` points at `itPickup` (twelve grab-offset floats) and
`ftData->x4C_sfx` at `FtSFX` (eleven `s32` sound ids plus three `FtSFXArr`
{`num`, `sfx_ids`} tables).  Relocation only fixes the pointers; the converter
never walked either pointee, so compiled code read the big-endian floats as
denormals near zero (`ftpickupitem_80094150` then measures the grab box at the
origin) and the sound ids as `0xnn000000` values the synth bank cannot find.
The decomp types `FtSFX.x1C` as `int`, but the archive stores a third
`FtSFXArr*` there (it is a relocation target and `ftCo_Damage` assigns it to an
`UNK_T`), so the array walk keys on the relocation entry.
**Fix:** converter v71 walks `x40` (12 words), the eleven `FtSFX` ints and all
three `FtSFXArr` counts/id arrays.  `test_decomp_assets` compares every one of
those fields against a raw copy of the archive for Mario, Ness, Game & Watch,
Peach and Fox; before the fix each reports 34 mismatches.

## G-125: per-fighter x48 special-item Article arrays

**Symptom:** item specials spawned by Ness, Peach, Game & Watch, Link, … read
denormal throw speeds/damage and the wrong state trees (e.g. Ness PK Fire).
**Cause:** `ftData->x48_items` is an array of `Article*` (indexed by item kind,
with NULL holes) that the converter never walked.  Relocation fixed the six
pointers, but the pointees' `ItemAttr`, hurt-bone, state, model and dynamics
payloads stayed big-endian.
**Fix:** converter v72 walks the leading run: entries while the slot is a
relocation target (or zero), stopping at the first non-NULL slot that is not.
Only entries whose `attr` really looks like an `ItemAttr` are walked (`attr`
in range, the attr word itself not a relocated pointer, `x4_throw_speed_mul` /
`x60_scale` sane, state array before the article) because Kirby/Yoshi/Pichu/
Samus keep unrelated pointer tables after the run.  A shared `ItemAttr` (four
Ness kinds point at the same one) must skip the sanity probe once converted or
its host-order floats fail the BE check.  `test_decomp_assets` diffs all 31
`ItemAttr` words of every accepted entry against a raw copy for eleven
fighters; before the walk the first entry already fails.

## G-126: converter symbol dispatch must match the real symbol name length

**Symptom:** results-screen character names were wrong for every player; both
panels read the same (first) table entry.
**Cause:** the converter dispatched `TyDataf.dat`'s trophy tables with
`length == 15 && memcmp(name, "tyModelFileTbl", 15)` and `length == 17` for
`tyModelFileUsTbl`, but the symbols are 14 and 16 characters.  Neither branch
ever matched, both tables stayed big-endian, and `Toy_8030813C`'s
`*(s32*)ptr == id` scan only matched entry 0 (id 0) for every character.
**Fix:** correct the counts to 14/16 (converter v73); `test_decomp_assets`
loads `TyDataf.dat`, walks all 293+5 entries and requires every id to equal its
raw big-endian value.  Before the fix the first non-zero entry fails.

## G-127: converter root dispatch misses whole tables by symbol name

**Symptom:** credits ("staffroll") name models and Stadium spawn tables read
big-endian numbers; with the affected modes reached, models split/scale wrong
and spawn rows get absurd kinds/positions.
**Cause:** `convert_roots` dispatches every special walk by a literal public
symbol suffix.  `ScGamRegStaffrollNames_scene_modelset` ends in `_modelset`
(not `_scene_models`/`scemdls`) and the six `gmKumiteSystemTable*` symbols
matched nothing, so those archives' dynamic model descriptors and
`RegClearSpawnEntry` rows were never walked.
**Fix:** converter v74 adds a `_modelset` -> `conv_dynamic_models` branch and a
sentinel-bounded `conv_regclear_spawn_table` for `gmKumiteSystemTable*`.
`test_decomp_assets` checks the ten modelset joints' flags and every Stadium
spawn row (x0/x8/xC) against a raw copy; both fail without the branches.
Audit method: a one-off scan of `roots_unknown` over all 1,209 disc archives
lists the remaining unwalked publics (event level table, intro-easy table,
`standScene`/`cut*Scene`, debug tables); the per-fighter
`ftDemo*MotionFile*` symbols are strings and need no walk.

## G-128: converter bounds must not wrap, and walkers must respect relocation words

**Symptom:** heap corruption while converting effect archives; 167 relocation
fields across 22 `Ef*Data.dat` archives were rewritten with garbage (pointers
split across unaligned writes), and ASan caught a 1-byte heap-buffer-overflow
on the relocation map.
**Cause:** three holes in the converter's defensive checks:
1. `in_data(c, off, need)` was `(size_t) off + need <= data_size`, which wraps
   on the 32-bit product: a `0xFFFFFFE0` fogadjdesc offset passes `off + 0x44`
   and then indexes `c->num[]` before the allocation.
2. `conv_u16` did not skip words that are relocation targets, and neither
   `conv_u16` nor `conv_u32` rejected unaligned offsets.  A misidentified
   `HSD_Joint` with a garbage `mtx` (7) wrote a u32 at data offset 11, cutting
   across pointer fields.
3. `conv_ef_dat` bounded the `EF_EffectDesc` array by the first particle bank,
   but EfDk/EfPe/EfLk/EfNs have both bank pointers null, so it walked up to
   1024 entries into unrelated data and treated arbitrary words as models.
**Fix:** `in_data` compares against the remaining size; `conv_u32` rejects
unaligned offsets and `conv_u16` unaligned offsets or pointer words (the
`conv_u32` relocation check already existed); `conv_ef_dat` stops at the first
descriptor with no relocation-backed model pointer and reports
`stats.effect_descs`.  `test_decomp_assets` now converts every one of the 861
HSD archives on the disc and checks every relocation field against a raw copy
plus each effect table's descriptor count (EfCoData alone walks 50 entries
for 47 descriptors, and the old code corrupted 167 pointers; ASan clean
with the fixes).

## G-129: MWCC register leftovers are not part of the C semantics

**Symptom:** every player's HUD stock icon showed Captain Falcon (frame 0).
**Cause:** two decompiled functions in `gm_1601.c` relied on MWCC leaving
values in registers:
1. `gm_80168B34` declares `int base;` uninitialized and only assigns it in
   some branches; the retail asm keeps `ckind` in `r3` for the fallthrough
   (`ble .L_80168BCC`), but GCC used the uninitialized local and happened to
   pick the Popo constant (14), so every character returned frame 14.
2. `gm_80168BF8` computes `gm_80168B34(...)` without `return`; MWCC's float
   result survives the epilogue in `f1`, but GCC proves `gm_80168B34` has no
   side effects and deletes the call, returning `0.0f`.
   `ifStock_802F98E8` fed that 0 to `HSD_TObjReqAnimAll`, so every icon
   selected atlas frame 0 = Captain Falcon (`CKind_Captain == 0`).
**Fix:** under `PORT_PC`, initialize `base = ckind` and `return` the call
(`patches/src/melee/gm/gm_1601.c.patch`, listed in
`learnings/decomp_port.md` S6).  `ctest decomp_icons` (`MELEE_ICON_TEST=1` on
the boot match) requires `gm_80168B34(CKind_Mario)=8`,
`gm_80168B34(CKind_Donkey)=1` and different frames for the two live players;
before the patch it prints `mario=14 dk=14 p0=0.0 p1=0.0 distinct=0`.

## G-130: per-stage `yakumono_param` layouts are not interchangeable

**Symptom:** on Yoshi's Story (GrYt.dat), hitting a Lucky Block from below
stops the fighter dead in mid-air (CPU players too).
**Cause:** `grYt_804D6A20.x0 = Ground_GetYakumonoParam()` is a `YorsterParams`
(`gryorster.c:61`: four f32 then four s32).  The converter only understood the
Zebes layout (`desc == off - 0x24`) and left every other stage's parameters
big-endian.  `grYorster_802024F0` reads `x00` as the bump threshold, so the
raw word `0x3F4CCCCD` (= 0.8f) read as `-4.3e8` and every contact passed the
test, while `x10` (2) read as a denormal ~0 and `ftLib_80086A4C`/`ft_ITBump`
zeroed the bump velocity: the fighter reached the block and stayed there.
**Fix:** converter v76 selects the Yorster layout for the archive whose
publics carry the `GrdYorster*` texture names and converts the eight fields.
`test_decomp_assets` checks `x00=0.8, x10=2, x14=5, x1C=140` against the raw
archive (the first word fails before the fix).  Other stages with their own
`yakumono_param` structs still need their own field tables (P-662).

## G-131: `GXGetProjectionv` returns the packed XF form, not matrix diagonals

**Symptom:** match particle billboards use wrong axes (the `psdisp.c`
billboard matrix is built from the projection); HSD fog range adjustment would
also read garbage.
**Cause:** our GX HLE returned `projection[0][0], [1][1], [2][2], [3][3]`.
The SDK (`GXTransform.c:GXGetProjectionv`) packs
`{projType, A, B, C, D, E, F}` where perspective uses
`{m00, m02, m11, m12, m22, m23}` and ortho `{m00, m03, m11, m13, m22, m23}`.
`psdisp.c:1936` branches on `prj[0]` (our `m00` is a float like 0.5, so it
took the ortho branch) and then builds billboard axes from `prj[1..4]`.
**Fix:** `GXSetProjection` stores the packed coefficients, `GXGetProjectionv`
returns them, and `GXSetProjectionv` is implemented (P-678).  `ctest
decomp_gx_direct` now asserts both layouts and the pointer-fed round trip;
flipping the getter back to the diagonal form fails with
`GXGetProjectionv type=2 want 0`.

## G-132: the card work-area symbols overlap; separate arrays deadlock the pump

**Symptom:** with a card inserted (`MELEE_CARD_DIR`/`MELEE_CARD=1`), the boot
freezes on "Do not touch the Memory Card or POWER Button! Creating new Game
Data.": `hsd_804D799C == 2`, the dispatch queue reads empty
(`hsd_804D7990 == hsd_804D7994`) and `lb_8001BC18` spins because `x8AC == 1`.
Mount and check succeed; the first file command never runs.
**Cause:** G-085 gave `hsd_804D1138` its full 0x1510 bytes but left
`hsd_804D1148` and `hsd_804D2348` as separate arrays.  On the console those
symbols are *inside* the `CardContext`: the command ring is at
`hsd_804D1138 + 0x10` (`hsd_804D1148`) and the dispatch queue at
`hsd_804D1138 + 0x1210` (`hsd_804D2348`).  The queue writers in `hsd_3B27.c`
(`hsd_803B2550`, `hsd_803B286C`, ...) write into the context, but
`fn_803AA790` pops from the standalone `hsd_804D2348`, so the command is never
dispatched; `x8AC` (the game's pending-operation count) keeps waiting for a
completion that will never be posted.
**Fix:** under `PORT_PC`, `hsd_4D11.c` defines only `hsd_804D1138[0x1510]` and
aliases the other two symbols onto it (`__asm__(".set hsd_804D1148,
hsd_804D1138 + 0x10")`, `hsd_804D2348 + 0x1210`), which is the console layout.
`ctest decomp_frontend_card` runs the frontend twice over one card directory
(create then load) and requires the save file to exist after both.

## G-133: per-stage `yakumono_param` layouts need the archive's own symbols

**Symptom:** Peach's Castle (`GrCs.dat`) crashed on match entry
(`grCastle_801CD658 -> lb_8000FD48`); stages outside Yoshi's Story/Zebes read
their `yakumono_param` fields as denormals or huge ints.
**Cause:** each stage stores a different struct under the same
`yakumono_param` public.  The converter only knew the Zebes layout (a
self-check at +0x2C) and the Yorster layout, and the `Grd<Stage>` marker scan
stopped at the first hit.  That first-hit rule is wrong because stages reuse
other stages' textures: `GrVe.dat` carries both `GrdVenom*` and
`GrdCorneria*`, Big Blue carries `GrdCorneria*`, the adventure routes carry
`GrdDonkey*`/`GrdCastle*`.  `GrCs.dat`'s `dynamicsdata_flag*` publics (source
`DynamicsDesc`: `data`, `count`, `Vec3 pos`) were not walked at all; the
big-endian `count` 3/4/6 read as `0x0n000000`, so `lb_8000FD48` drained the
whole dynamics pool.
**Fix:** converter v77/v78 adds a marker table ordered most-specific-first
(`GrdVenomBase`, `GrdCorneriaAwbody`, `GrdIzumiBulbon`, `GrdDonkeyKareki`,
`GrdStory`, `GrdOnett`, `GrdInishie1`, `GrdYorster`) and per-layout field
tables for GrCn/GrIz/GrKg/GrSt/GrVe/GrOt/GrI1 (mixing u16 and u32 fields;
pointer fields stay with the relocation pass), plus `conv_dynamics_desc` for
`dynamicsdata_*`.  `data == 0` is a legal record base (`GrCs.dat` flag3), not
NULL.  `test_decomp_assets` diffs every listed field against the raw archive,
requires `GrNBa`/`GrFs`/`GrFz` (packed layouts) to stay raw, and pins the
three Castle dynamics counts 3/4/6.  Remaining unknown layouts (target-test
stages, adventure routes, GrBb/GrGb/GrKr/...) stay raw by design.

## G-134: the last unwalked public roots

**Symptom:** the unknown-root scan still listed a handful of public symbols
whose numeric fields stayed big-endian: trophy/cutscene scenes read garbage
camera transforms, Classic-mode intro placement was wrong, and event levels
would read their rule floats as denormals.
**Cause/fix (converter v80):**
- `GmRgStnd.dat` `standScene` and `GmRegEnd.dat` `cut{1,2,3}CanimScene` /
  `cut3BgScene` are `SceneDesc`s (models/cameras/lights/fogs) like the
  `pnlsce`/`flmsce` cases, but the dispatch only matched `_scene_data`; they
  now go through `conv_scene_desc`.  A length check must count the real symbol
  (`standScene` is 10 characters, not 9).
- `GmIntEz.dat` `gmIntroEasyTable` is the Classic-mode intro layout table
  (gm_1832.c:119): f32 slot rows, `ClassicCharLayout`/`ClassicTeamEntry`/
  `ClassicSplashRow` rows with unnamed pad runs.  `conv_intro_easy_table`
  swaps exactly the f32 fields (0x9B8-byte table) and leaves the pads.
- `GmEvent.dat` `sqEventInitDataLevelTbl` is 51 relocation-backed pointers to
  `gm_804D6900_t` levels; `conv_event_level_table` walks each level's evinit,
  evbonus, stage table and five player-init blocks.  `gm_evinit`'s first word
  is MWCC MSB-first bitfields (`x0_0:3 ... x1_5:3`), so
  `conv_event_init_flags` repacks the two bytes to GCC's LSB-first layout
  (0x2b800102 -> host bytes 0xD1/0x01) instead of a byte swap; the level
  `+0x04` pointer is dual-use (level-0 timer numerics vs a character-kind
  byte list) and is deliberately left raw.
- `DbCo.dat` `dbLoadCommonData` is three `char**` name tables with no numeric
  fields; the branch is explicit but only the relocation pass acts.
`test_decomp_assets` compares each walked field against the raw archive
(missing the branch fails on the first value) and ASan stays clean.

## G-135: the AX ITD shift ramps per 5 ms frame, and chorus's resampler rotates

**Symptom / scope:** the mixer's ITD moved `shiftL/shiftR` toward the target
once per *sample*, so any pan reached its target within 32 samples (< 1 ms)
and the game's separate snap path (`HSD_SynthSFXUpdateMix` sets `shiftL/R`
directly when `interpolate == 0`, otherwise only writes `targetShiftL/R` via
`AXSetVoiceItdTarget`) was pointless.  That the SDK keeps a target mechanism
at all only makes sense if the DSP ramp is slower than a frame.
**Fix:** `ax_mixer_frame` steps each shift by one toward its target once per
5 ms frame, before the sample loop; the frame uses that constant shift.  A
full pan now glides over up to 31 frames (~155 ms), which is what the
`interpolate == 0` vs target split in the game's own code implies.  There is
no retail oracle for the exact rate; the regression is the deterministic PCM
hash (`ctest decomp_audio`) and the mixer unit test.
**Separate chorus trap:** `do_src1`/`do_src2` index the 12 kHz resample table
with `rlwinm r10,r4,7,21,27` (rotate left 7, mask bits 21..27), **not** a
plain `(posLo >> 14) & 0x7f`.  The retail DOL carries the same encoding
(`54 8a 3d 76`) at 0x359b34 and 0x359ccc, so use the PPC rotate+mask
semantics; the 512-float table's phase ordering assumes it.

## G-136: CPU-updated textures need cache invalidation (THP movie)

**Symptom:** the opening movie played (frames advanced, audio, START skipped)
but the screen stayed on the first decoded frame — a black/grey card.
The GL draw dump showed the movie quad bound to the correct Y/U/V planes.
**Cause:** `gx_gl`'s decoded-texture cache keys on the CPU pointer.  The THP
player writes every frame into the same plane buffers (`THPDec_80331340`
copies into `MoviePlayer.unk_50/54/58`), so the cache kept serving the first
decode (black).  The EFB-copy path already invalidated; `GXInitTexObj` did
not.  The player re-inits its three texobjs every frame, which is the natural
dirty signal.
**Fix:** `GXInitTexObj` calls `gx_gl_invalidate_texture(image_ptr)`; the next
`GXLoadTexObj` re-decodes.  Static textures are initialized once, so there is
no per-frame cost.  `ctest decomp_opening` requires the captured movie frame
to have real pixels (flipping the invalidation off fails it).
**Related porting notes (P-685):**
- `extern/dolphin`'s `THPDec.c` is MWCC-only in practice: its `#ifdef
  __MWERKS__` blocks remove the control flow that the remaining C labels rely
  on (the Huffman decode falls through into the failure path).  Replaced by
  `native/decomp/thp_dec.c`, a C transcription of Aurora's `THPDec.cpp`.
- THP headers and each packed frame's size prefix are big-endian file words;
  the host reads the raw file, so `lbmthp.c` swaps them under `PORT_PC`.
- `ColorOverlay_x8_t`'s colanim opcode bitfields (`unk:6`, rot fields) are
  archive data read MSB-first on the console; they need the same
  `scalar_storage_order("big-endian")` treatment as the action commands
  (G-082).  Without it the title attract demo dispatches a garbage opcode
  into `ftCo_803C6AD0[opcode - 0x15]`.
- `ftData->x54` is a relocation-backed pointer to a five-int per-costume part
  table (`ftCo_8009F834` reads it for bone id 0x8D); the converter has to
  walk the pointee or the entries stay byte-reversed.

## G-137: a new translation unit can expose DOL-only data adjacency

**Symptom:** after the opening-movie decoder landed, both `melee --match` and
normal frontend VS matches stopped just after the Ready sequence, before the
fighters appeared.  The viewer exited cleanly because its triage log is hidden;
the headless boot showed `texp.c:1048 "clist->type == HSD_TE_CNST"`.
**Cause:** `ftmaterial.c` declares `ftMObj` as an `HSD_MObjInfo` but casts its
address to a larger private `struct ft_MObjInfo` to read a TEV descriptor and
TExp constant after it.  Those are actually separate globals whose DOL order
is `ftMObj`, `ftMaterial_803C69D0`, `ftMaterial_803C6A44`.  Adding the THP
translation unit changed host link layout; the cast then read unrelated data
and produced TExp type 0 instead of `HSD_TE_CNST` (4).
**Fix:** under `PORT_PC`, copy the two named template globals directly.  Keep
the adjacency expression for the GameCube build.  `decomp_match` retains its
exact frame-600 position, and `decomp_hit` plus a 240-frame live viewer match
exercise the fighter material path.  Do not treat link-layout-sensitive
failures as timing bugs until a backtrace checks for adjacent-data casts.

## G-138: `GXSetZTexture` draws still write the TEV colour (screen erase)

**Symptom:** the title screen's background base read black instead of the
scene's dark grey (the owner's "black bands" against Dolphin's filled
background); every background layer that should composite over the erase
colour sat on black instead.
**Cause:** HSD's screen erase (`displayfunc.c`, reached from
`gmTitle_801A18D4` via `HSD_CObjEraseScreen`) draws a full-screen quad with
`GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z8, 0)` and `color_update = GX_ENABLE`:
the hardware replaces depth *and* writes the TEV colour (the erase colour,
38,38,38 for the title).  The port's dedicated Z-texture program hard-coded
`frag = vec4(0.0)` -- it was written for the depth-only shadow passes -- so
the erase colour never landed.  `--part 0 --part-mode only` showed a black
frame even though the draw's vertices carry the erase colour.
**Fix:** the Z-texture fragment program writes the vertex (raster) colour when
the draw updates colour, black otherwise.  Melee's only colour-writing
Z-texture draws use `GX_SRC_VTX` material with a passthrough TEV, so the
vertex colour is the TEV result there.  `decomp_efb` stays green (the shadow
paths still write no colour) and the title's `(320,60)` readback is now
`36,36,36` instead of `0,0,0`.

## G-139: vertex integer channels expand by bit replication, and RGBX8 ignores X

**Symptom:** nothing theatrical -- a one-LSB colour error on every RGB565 /
RGBA6 vertex colour, and a wrong (often zero) vertex alpha for RGBX8 meshes.
The GUI debug viewer and the compiled path could also disagree if only one
decoder is fixed.
**Cause:** the HLE's `decode_color` (and the prototype's `model.c` copy) kept
the old `v * 255 / 31` / `v * 255 / 63` rounding while `native/gx/texture.c`
had already moved to bit replication (`ExpandTo8`), and it read the fourth
byte of `GX_RGBX8` as alpha although the reference ignores it
(`fetch_rgbx8` returns alpha 255).
**Fix:** bit-replicate 5/6-bit channels (`(v << 3) | (v >> 2)`,
`(v << 2) | (v >> 4)`) in both decoders and force RGBX8 alpha to 255.
`ctest decomp_gx_direct` asserts RGB565 (13,17,7) -> 107,69,57 and RGBX8
(10,20,30,0) -> alpha 255; both failed before the fix.

## G-140: a texture-coordinate texgen source is `(u, v, 1)` (q rows)

**Symptom:** subtle projection errors in reflection-style `GX_TG_MTX3x4`
texgens whenever the matrix's third row has a z coefficient or a translation:
the texture lands offset/scaled because q is wrong.  Everything with an
identity post matrix (including `GX_TG_MTX2x4`, which forces z=1 anyway)
looks fine, so it hides easily.
**Cause:** `texgen_coord` fed `(u, v, 0)` for TEX sources; Aurora builds
`vec4f(uv, 1.0, 1.0)`, so q = `m8·u + m9·v + m10·1 + m11`.
**Fix:** feed 1.0 (P-693).  P-736 additionally keeps the resulting STQ triple
through rasterization and divides S/T by Q per fragment; dividing at each
vertex gives affine interpolation and is not equivalent.  The
`decomp_gx_direct` MTX3x4 case uses q row `{2,0,2,1}` and now asserts the
pre-divide triple `(0.25, 0.625, 3.5)`.

## G-141: the TEV raster channel is not always rast0

**Symptom:** stages ordering `GX_ALPHA1` as their raster channel read the
COLOR0A0 material instead of COLOR1A1, and stages ordering `GX_COLOR_NULL` /
`GX_COLOR_ZERO` read the lit raster instead of black.  TEV graphs that blend
specular/alpha channels can therefore invert or over-brighten.
**Cause:** the fragment shader selected `v_ras1` only for channels 1 and 5;
Aurora `color_channel()` maps `GX_ALPHA1` to rast1, and `color_arg_reg`
returns zero for `GX_COLOR_ZERO`/`GX_COLOR_NULL`.
**Fix:** select rast1 for channels 1/3/5 and black for 6/255 (P-694).
`ctest decomp_efb` checks an ALPHA1 stage against the COLOR1A1 material
(green; red before the fix) and a NULL-channel stage (black; red before).

## G-142: PlCo `pData[8]` is a `{joint, animation}` pair, not a joint

**Symptom:** the respawn/rebirth platform is invisible while the entry/trophy
platform (slot 16) is fine.  Before/after match frames are byte-identical
except during the rebirth windows (470–520 and 580–600 in the scripted match).
**Cause:** `Fighter_804D6534 = pData[8]` (`fighter.c:196`) points at a
two-slot table: `[0]` is the joint for `ftCommon_SetAccessory`, `[1]` the
animation for `ftCommon_8007E690` (`ft_0D4D.c:139,148`).  Slots 16/20 are
direct joints, so `conv_ft_common_data` walked them but skipped 8; the slot-8
joint's flags and nine rot/scale/translate floats stayed big-endian and the
`1.0f` scales read back as `4.6e-41` denormals.
**Fix:** convert the pair with `conv_joint` + `conv_anim_joint` (converter
v82, P-689).  `ctest decomp_assets` compares the slot-8 joint flags/floats
against the raw archive and keeps slot 16 as the converted control; it fails
before the fix (`flags 08000030 want 30000008`, `scale 4.6e-41 want 1`).

## G-143: `SECTION_CTORS` is empty off-Metrowerks

**Symptom:** an MSL TU that relies on a `.ctors` initializer silently uses
zeroed tables.  Compiling `src/MSL/trigf.c` without running
`__sinit_trigf_c` leaves `__four_over_pi_m1` all-zero, so `sinf`/`cosf` take
the cruder argument reduction.
**Cause:** `src/Runtime/platform.h` defines `SECTION_CTORS` as the Metrowerks
`__declspec(section ".ctors")` only under `__MWERKS__`; on the host it expands
to nothing and `__sinit_trigf_c_reference` is an ordinary unused pointer.
**Fix:** run the initializer from a port `__attribute__((constructor))` TU
(`native/decomp/msl_port.c`, P-688).  Treat any `#ifdef __MWERKS__`-only
side effect as absent on the host until proven otherwise.

## G-144: asm behind `#ifdef MUST_MATCH`/`#ifdef MWERKS_GEKKO` with no `#else` is a silent no-op

**Symptom:** a function compiles on the port but does nothing, or returns
uninitialised data, with no warning.  Observed: Big Blue's cars never reach
state 10/4, the results screen's `player_standings[i].xE` came from
uninitialised `sp48_x`, and `__cvt_dbl_usll` compiled to a single `ret`.
**Cause:** the only implementation is the Metrowerks asm/body behind
`#ifdef MUST_MATCH` (`grbigblue.c` x5), `#ifdef MWERKS_GEKKO` (`gm_1601.c`
`fn_80166A8C`) or `#ifdef __MWERKS__` (`Runtime/runtime.c`
`__cvt_dbl_usll`), with no `#else` for other compilers.
**Fix:** `#elif defined(PORT_PC)` fallbacks in the P-687 patches; `ninja` in
`decomp/` still reports `main.dol: OK` with them applied.  When re-pinning,
check upstream #3456 first — merging it removes these patches.  `objdump` the
port binary after touching these TUs: the five `and $0x3` Big Blue inserts and
a real conversion body for `__cvt_dbl_usll` are the fingerprints.

## G-145: a decompiled function with no `return` returns garbage on GCC

**Symptom:** the game segfaults one frame after the "GAME!!" announcer at the
end of any 1P stage:

```
lb_800138D8 (gobj=0x0, size=1) at src/melee/lb/lbspdisplay.c:683
fn_80180630 (...) at src/melee/gm/gmregclear.c:1082
fn_8016D634 () at src/melee/gm/gmvs.c:1545
gm_Scene_Vs_OnFrame ()
```

**Cause:** `lb_800138EC` is declared `HSD_GObj*` and has **no `return`
statement**.  On the console MWCC leaves `gobj` in `r3` across
`GObj_SetupGXLinkMax` -> `GObj_GXReorder` (neither writes `r3`; verified in
`main.elf` at `0x8039075c`/`0x8039063c`), so retail returns the blur GObj.
GCC returns whatever is in `eax` — `NULL`.  `gmregclear.c:1081` stores that in
`state->x2C` and dereferences it two lines later in `lb_800138D8`.

**Fix:** `return gobj;` under `PORT_PC`
(`patches/src/melee/lb/lbspdisplay.c.patch`).  `ctest decomp_gameover`
(`MELEE_GAMEOVER_TEST=1` on the boot match) forces `OUTCOME_ELIMINATION` into
the 1P clear overlay and segfaults without the patch.

**Generalisation:** `melee_decomp_game` builds `src/` with `-w`, so this whole
class is invisible.  There are 45 such sites at pin `40012f51f`.  The census
recipe, the retail-asm method for deciding the correct return value, and a
per-site verdict table are in `learnings/decomp_port.md` ("P-695 missing-
`return` census").  Two traps:

- `-fsyntax-only` finds only 8 of the 45 — it disables the CFG pass that
  emits "control reaches end of non-void function".  Compile for real.
- Do not invent a default for a site where retail is *also* indeterminate.
  Read the DOL epilogue first; if `r3`/`f1` is never written on that path, the
  path is unreachable and the right change is none (AGENTS.md §0.1).

## G-146: an unconverted archive root shows up as a 400 ms frame, far from the cause

**Symptom:** the match runs at 60 Hz until any part of a player leaves the
camera, then drops to ~2.5 fps and recovers the moment they come back:

```
[match] frame 1590 ... render=4.09ms  frame=16.75ms
[match] spike frame=1598 interval=408.4ms render=405.31ms draws=320
[match] spike frame=1599 interval=407.9ms render=408.67ms draws=320
```

`draws`/`verts` barely move (302 -> 320), so it is not geometry.

**Cause:** the off-screen player magnifier.  `convert_roots` in
`hsd_convert.c` dispatches on the archive's public symbol name and silently
ignores names no rule claims, leaving that root's whole sub-graph big-endian.
IfAll.dat's `lupe` (and `tdsce`, `Stc_rarwmdls`) were in that hole, so the
magnifier's `HSD_ImageDesc` still held big-endian fields.
`ifMagnify_802FBBDC` -> `lb_800122C8` -> `HSD_ImageDescCopyFromEFB` feeds those
fields straight to `GXSetTexCopySrc`/`GXSetTexCopyDst`, so a 64x64 RGB5A3
target (`0x0040`, `0x00000005`) became a **0x4000 x 0x4000** copy in format
**0x05000000**.  `copy_tex_encode` then ran 268M iterations writing nothing,
because the byte-swapped format matched no `case` — pure wasted CPU, every
frame the magnifier was up.

**Fix:** `lupe`/`tdsce`/`Stc_rarwmdls` now route to `conv_dynamic_models`
(converter version 83), and `efb_copy_tex` refuses any copy larger than the
640x480 EFB with a one-line warning naming the format, so a future
unconverted descriptor costs a log line instead of 25 frames.
`ctest decomp_assets` (`check_ifall_hud_modelsets`) fails with the rule
disabled.  Worst-case render over a 300-frame match: **414.22 ms -> 13.57 ms**.

**Generalisation:** when a frame time or a dimension is absurd, read the number
in the other byte order first.  `16384` is `0x4000` = 64 byte-swapped;
`83886080` is `0x05000000` = 5.  Then find which root owns that data and check
it against the dispatch in `convert_roots` — `MELEE_ROOT_TRACE=1` lists the
ones nothing claims.  Beware near-miss names: the rule is
`name_ends_with(..., "scemdls")` and `Stc_rarwmdls` ends in `mdls`.
Full method and the remaining unhandled-root list in
`learnings/decomp_assets.md`.

## G-147: shape-set pools are the one vertex arrays HSD reads with the CPU

**Symptom:** the 1P clear screen's "STAGE CLEAR" banner is missing and its
black backdrop reads as a solid black box over the top quarter of the screen.
`--dump-draws` shows the banner's 536x58 texture bound and its UV slices
correct, but every quad degenerate: `ndc x[0.00,0.00]`.

**Cause:** the banner is a `POBJ_SHAPEANIM` mesh.  Ordinary vertex arrays stay
big-endian in the port because the GX display-list decoder reads them that way
(`learnings/decomp_assets.md`), but `drawShapeAnim` blends morph targets on the
CPU: `get_shape_vertex_xyz`/`get_shape_normal_xyz`/`get_shape_nbt_xyz`
(`pobj.c`) `memcpy` the `GX_F32` case straight into an `f32[3]` and cast the
`GX_U16`/`GX_S16` cases natively, then push the result through
`GXPosition3f32`.  A big-endian float read little-endian is a denormal near
zero, so the whole mesh collapses to a point.  `gdb` on
`interpretShapeAnimDisplayList` showed `vertex_buffer` full of `1.157e-41`.

**Fix:** swap on read, under `PORT_PC`, in those three readers
(`patches/src/sysdolphin/baselib/pobj.c.patch`) — the same thing the function's
own `GX_INDEX16` index reads a few lines above already do by hand
(`idx = (index_array[i*2] << 8) + index_array[i*2+1]`), which is the clue that
these arrays are meant to be read big-endian.  `ctest decomp_clear_banner`
counts non-black pixels inside the banner: **0** before the patch, **30690**
after.

**Do not fix this in the converter.** Byte-swapping the pool in
`conv_shapesetdesc` also works for the banner, but other PObjs in the same
model share those arrays and *are* decoded by the GX HLE, so it silently broke
the SPECIAL BONUS panel frame and the TIME REMAINING/DAMAGE fills on the same
screen.  The swap belongs at the one CPU reader, not in the shared data.

**Also worth remembering:** while trying the converter route, the array base
read back as `0` and an `if (base == 0) return;` guard rejected it — that is
G-002 (a base pointer of 0 is data offset 0, not NULL).  Ask
`c->reloc[field]` whether a field is a pointer; never test the value for zero.

## G-148: a NUL-terminated array walk needs the relocation table, not just != 0

**Symptom:** on the Classic splash screen (`GS_INTRO_EASY`) the row of
stage-marker models draws nothing — only the thin chain between them survives,
so the top of the screen reads as a bare zigzag line on black — and the big
red "VS" between the fighters is a few dark streaks instead of a solid glyph.

**Cause:** `conv_scene_desc` walked `SceneDesc.fogs` (and `.cameras`,
`.lights`) with `for (;;) { desc = rd32(p); if (desc == 0 || !in_data(...))
break; ... }`.  A NUL terminator is not the only thing that can follow such an
array: the next word may be unrelated archive data that still looks like a
plausible data offset, and the walk then converts whatever it lands on.  In
GmIntEz.dat it landed on an `HSD_PEDesc` and byte-swapped its first word, so
`flags = 0x29` read back as `0x00`.  `HSD_SetupPEMode` does
`HSD_StateSetColorUpdate(pe->flags & 1)`, so every draw with that material ran
with `GXSetColorUpdate(GX_FALSE)` and wrote no colour at all.

**Fix:** gate each slot on `c->reloc[p]` — every real entry in these arrays is
a relocated pointer, and the relocation table is the authority on which words
are pointers (converter version 84).  `ctest decomp_intro_markers` counts
non-black pixels across the marker row: **553** (just the chain) before the
fix, **14411** after.

**How to recognise this class:** the geometry is present and correctly
positioned in `--dump-draws` but nothing reaches the framebuffer.  Check
`colup=` in the dump before suspecting lighting or culling — a draw with
`color_update = 0` is invisible no matter how well lit it is.  Chasing it as
"collapsed geometry" (G-147's signature) wastes a lot of time; the draws here
had perfectly good vertex positions all along.

**Generalisation:** the same `!= 0` idiom appears in several other walkers.
Any array of pointers in an HSD archive should be walked with the reloc bit,
which also handles G-002 (a legitimate pointer to data offset 0 reads as 0).

## G-149: `vsnprintf(buf, -1, ...)` silently drops the last character on glibc

**Symptom:** every string the SIS text engine renders loses its final glyph and
picks up junk: the 1P character-select level reads `VERY EASE••` instead of
`VERY EASY`, `NORMAL` renders as `NORMAr` + junk, and the "TOTAL HIGH SCORE"
value shows eight digits instead of nine.

**Cause:** `HSD_SisLib_803A6B98`/`803A70A0` (`hsd_3A64.c`) and
`DevText_Printf` (`textlib.c`) call `vsnprintf(buffer, -1, fmt, args)`.  The
console's MSL reads `-1` as "unbounded", which is the intent.  glibc
documents sizes above `INT_MAX` as unsupported and writes one byte fewer than
asked.  Ten-line repro, 32- and 64-bit alike:

```c
char b[128];
vsnprintf(b, -1, "%s", "VERY EASY");   /* -> "VERY EAS" */
```

The engine's strings are Shift-JIS, two bytes per letter, so losing one byte
truncates mid-character: `HSD_SisLib_803A67EC` then fails its SJIS lookup for
the orphaned lead byte, emits nothing for it, and the parser reads on into the
bytes that follow — which is where the trailing `E••` came from.

**Fix:** pass `sizeof(buffer)` under `PORT_PC`
(`patches/src/sysdolphin/baselib/hsd_3A64.c.patch`,
`patches/src/melee/if/textlib.c.patch`).  Every destination is a fixed local
buffer, so the bound is exact and matches what the console does for anything
that fits.  `ctest decomp_classic_text` probes the leading digit of the
right-aligned score: **0** before the fix, **159** after.

**Generalisation:** grep for any `-1`, `~0`, `0xFFFFFFFF` or `SIZE_MAX` passed
as a size to a libc function the port compiles.  The console's MSL was
routinely laxer than glibc here, and the failure is silent and off-by-one —
which reads as a font or parser bug, a long way from the call.

**Debugging note:** do not trust a pixel probe on text inside a fitted box.
The CSS level box rescales its contents (`text->x88`), so the broken
11-glyph string and the correct 9-glyph one occupied almost the same pixels
(327..484 vs 330..482).  The right-aligned score value was the discriminator.

## G-150: the decomp's non-ASCII literals are UTF-8; the console gets Shift-JIS

**Symptom:** the Classic VS screen shows the two fighters but no names under
them.  Forced through a debug entry the names appear as nonsense kana
("いさwwv"); with the US name table they vanish entirely.

**Cause:** the character names live in `src/melee/gm/gm_1601.c` as full-width
literals (`"Ｍａｒｉｏ"`), stored in the source as **UTF-8** — `Ｍ` is
`EF BC AD`.  The GameCube build pipes the source through **sjiswrap**
(`decomp/configure.py --sjiswrap`), so MWCC emits the console's Shift-JIS
bytes (`82 6C`).  `HSD_SisLib_803A67EC` reads the string two bytes at a time
and looks the pair up in the SJIS table, so the UTF-8 bytes miss every entry:
no glyph is emitted, and whatever partial pairs do match come out as random
kana.

**Fix:** compile the decomp objects with `-fexec-charset=CP932`
(`native/CMakeLists.txt`, `melee_decomp_game`).  GCC converts the literals at
codegen, after parsing, so the classic "0x5C as a Shift-JIS trail byte eats
the quote" problem never arises — that is what sjiswrap exists to solve for
MWCC, and it does not apply here.  **Use `CP932`, not `SHIFT-JIS`:** iconv's
strict `SHIFT-JIS` rejects several characters the JP name table uses and the
build fails with "converting to execution character set".

Shift-JIS is ASCII-compatible, so every ordinary literal is unchanged; the
GameCube build is untouched (this is a host compile flag only).
`ctest decomp_classic_names` reads the first byte of `gm_80160980(0)` and
requires a Shift-JIS lead byte (0x81..0x9F): `name_lead=82 sjis=1` with the
flag, `name_lead=ef sjis=0` without.

**Do not test this with a pixel probe.** The broken build renders *garbage
glyphs*, not nothing, so a white-pixel count barely moves (927 vs 1054 over
the name row).  Check the encoding at the source instead.

## G-151: `match_boot`'s probe lines are silent unless `MELEE_VIEWER_TRIAGE=1`

Every `[boot]`/`[match]`/`[gameover]`/`[intro]`/`[classic]` line in
`native/decomp/boot/match_boot.c` goes through `boot_triage_note()`, and
`viewer_main.c:1063` initialises that stream to **`/dev/null`** unless
`MELEE_VIEWER_TRIAGE` is set:

```c
boot_triage_init(getenv("MELEE_VIEWER_TRIAGE") != NULL
                     ? stderr
                     : (devnull != NULL ? devnull : stderr),
                 0, 0);
```

A harness run with `MELEE_CLASSIC_TEST=1` alone therefore produces no probe
output at all, which reads exactly like "the harness never reached the scene".
It did; you just cannot see it.  Always set `MELEE_VIEWER_TRIAGE=1` alongside
the scene harness env vars.

The `ctest` cases set it already — this only bites interactive/ad-hoc runs.

## G-152: HSD splash-layout tables are indexed by a *count*, so the count must be right

`gm_1832.c` reads the Classic splash layout as

```c
lbl_804D6604->x57C[lbl_8047368C.xEF].x18[i]   /* xEF is a COUNT, not an index */
lbl_804D6604->x00[lbl_8047368C.xEF - 1].vals[i]
```

where `x57C` is `ClassicSplashRow[3]` and `x00` is `ClassicSlotVals[2]`.  The
game picks a *row per player count*, so a count that is one too large silently
reads the next table's bytes.

Two consequences when porting:

- A wrong count upstream does not crash and does not drop a draw — it produces
  a plausible-looking but wrong layout.
- The damage is invisible in the draw stream, because
  `HSD_SisLib_803A7548` (`hsd_3A64.c:481`) stores the scale as **8.8 fixed
  point**: `*p = (u8) scale; p[1] = (u8) (256.0f * scale);`.  Any scale below
  `1/256`, or `>= 256`, quantises to 0 and the glyphs draw at zero size — no
  error, no missing geometry, just absent text.

So when text is missing from a SIS screen, check the *count* feeding the
layout lookup before you go looking at the glyph atlas or the draw state.

## G-153: gdb is unusable for breakpoints deep in a menu walk (~0.6 fps)

Reaching a 1P screen means letting the game run several hundred frames through
the menus.  Under `gdb -batch` the compiled decomp manages roughly **0.6
frames per second** — a breakpoint 500 frames past boot is 10+ minutes away,
and a `timeout` around the run reports success while the breakpoint never
fired.  The same build reaches frame 900 in under a minute when run directly.

Probe scene state with a throwaway `printf` in the file that owns the static
(most of these are file-static, so the probe has to live there), rebuild
incrementally, run without gdb, then revert the decomp edit.  Keep gdb for
crashes and for breakpoints that are reachable in the first few frames.

## G-154: retail tables that rely on linker adjacency break under `-fdata-sections`

`fn_80160DE8` (`gm_1601.c`) picks the width for a fighter-name glyph string
with `lbl_803B75F8[ckind + 0x21]`, `+ 0x42` and `+ 0x63`.  `lbl_803B75F8` is a
33-entry `static const float` array, so those indexes are past its end — on
the console they land in the tables the linker placed immediately after it
(`lbl_803B767C` at +0x21, `lbl_803B7700` at +0x42, `lbl_803B7784` at +0x63).
GCC gives every `static const` array its own section, so the reads land in
padding and return `0.0`.

Same class as P-686 (fighter material templates): **a decompiled index that
only makes sense because of the retail link order cannot be compiled as-is.**
Under `PORT_PC`, name the array the offset resolves to
(`lbl_803B767C[tmp_ckind]`), which is what the sibling `gm_80160B40` /
`gm_80160C90` already do.

Why it hid for so long: only the **US** branch of `fn_80160DE8` uses the
out-of-bounds offsets; the JP path reads `lbl_803B75F8[ckind]`, which is in
bounds.  A JP save — or the debug harness without `MELEE_INTRO_US` — renders
names perfectly, so local checks looked green while the owner's US save showed
nothing.  When a data-adjacency hypothesis is on the table, test the branch
that actually reads the offset.

A zero width is then swallowed silently: `HSD_SisLib_803A7548` stores the
scale 8.8 fixed, so 0 draws every glyph at zero width with no error (G-152).

## G-155: `gx_gl_probe_nonblack` cannot see white text on a non-black backdrop

The VS splash's name row sits on a dark but non-black backdrop, and the white
"VS" logo sits at x~298..340 in the middle of it.  A non-black count over the
whole row is nonzero with *and* without the glyphs (6661 vs 7929 here), so any
threshold low enough to pass the fixed build also passes the broken one — the
test does not flip.  Use `gx_gl_probe_white` (`gx_gl.c`, per-channel `> 190`)
and choose a rectangle that excludes other white elements (here x 60..280):
the same row measured **0 without the fix and 2457 with it**.

The general rule: pick the probe statistic that isolates the thing under
test, then run the probe against the **broken** build before committing the
test.  A probe that stays above threshold in both states is not a regression
test.

## G-156: relocated CPU tables need numeric conversion, including offset zero

**Symptom:** CPU fighters move toward opponents but never attack. At close
range an action SFX can repeat rapidly while the CPU retries, producing the
reported frame stutter. The same behavior appears in the four-CPU title demo.

**Cause:** `PlCo.dat`'s `ftLoadCommonData` root is a 23-pointer array, and
`pData[22]` becomes `Fighter_804D64FC`, the CPU attack database. The generic
relocation pass fixed its pointers but no descriptor walk converted the
numeric pointees. Each `ftCo_AttackEntry` therefore read command `2` as
`0x02000000`, while ordinary weights such as `3.0f` read as denormals. The
attack selector either found a zero total weight or produced an unusable
command, so movement logic worked but attack command scripts did not.

**Fix:** converter v86 walks all seven FighterKind-indexed selection tables
(`x4..x1C`), converting 230 lists / 1,159 0x24-byte records, plus the 33
distance thresholds and six held-weapon reach bonuses. Command scripts at
`x0` stay byte streams.

**Offset-zero trap:** Mario's ground-attack list is the first object in the
archive, so its serialized pointer value is zero. It is not NULL: the slot is
in the relocation table and becomes the data-base address during archive
location. Gate pointer walks on `c->reloc[slot]`, never on `value != 0`.

**Evidence:** `ctest decomp_assets` compares every converted CPU word to the
raw big-endian archive and reports `230 lists/1159 entries ok`. The idle
title-demo part of `ctest decomp_opening` checks all four live CPU tables,
observes attack-state entries and requires real damage without PAD input.
Disabling the pData[22] walk makes both regressions fail.

## G-157: a stage's `yakumono_param` public must be converted or its intro timers stay big-endian

**Symptom:** the Peach's Castle title-demo match (and any Castle match) plays
the `castle.ssm` ambient loop (`id 340005`, slot `0x53025`) for the whole run.
The owner's SFX capture showed one request at scene entry, a ~0.7 s loop that
never stopped (7.6 s and still alive at exit) and the mixed PCM clipping at
`peak=32768` for most of the demo.

**Cause:** `grCastle_801CE260` starts the loop and `grCastle_801CE578`'s
countdown stops it through `Ground_801C5544`, but the countdown is
`yakumono_param->entries[map_id - 8].x0` and the `GrdCastleCast` public in
`GrCs.dat` was never converted (P-662 covered
GrCn/GrIz/GrKg/GrSt/GrVe/GrOt/GrI1 only). Big-endian, the 405/600/720-frame
timers read as 22530/38145, so the intro animation never runs and the loop
never gets its stop call.

**Fix:** converter v87 routes `GrdCastleCast` to `conv_castle_param`, which
converts the 0x144-byte `grCastle_YakumonoParam`: the s16/f32 scalars, the
nine 0x14-byte `entries` (s16 countdown + four f32), `x110`, `x118..x124`,
`x12C[4]` and `x134..x140`. The `x114` pointer is left to the relocation
pass. The marker must be `GrdCastleCast`, not `GrdCastle` — `GrNKr.dat` /
`GrNSr.dat` carry `GrdCastleWater1_*` symbols that the shorter prefix also
matches.

**Evidence:** `ctest decomp_assets` compares the nine countdowns to the raw
archive (`405/600/600/720/575/720/575/600/600`) and fails with the walk
disabled (`entries[0].x0=38145 want=405`). After the fix the owner confirmed
the sound no longer glitches.

**Class:** every `Gr*.dat` whose `gr*.c` reads a `yakumono_param` public needs
its own descriptor; the unconverted VS-legal stages are tracked as P-708.

## G-158: GQR3 = 0x00050005 means `psq_st` stores a quantized u16, not a float

**Symptom (latent):** the P-687 fallback for `fn_80166A8C` writes a 4-byte
float, but every caller reads a u16 back (`gm_80166378` does
`player_standings[i].xE = *(u16*) &sp48_x`), so `MatchPlayerData.xE` receives
float mantissa bits instead of the joystick-activity score.  `xE` feeds
`gm_801688AC`/`gm_80168940` -> `gm_8016247C` high-score accumulation.

**Cause:** `init_spr_unk` (`decomp/src/melee/gm/gmmain.c:107-120`) sets
GQR2..GQR5 to `0x00040004/0x00050005/0x00060006/0x00070007` (`li` + `oris`
with the same immediate).  GQR store type 5 is U16 with scale 0, so
`asm { psq_st x, Vec3.x(dst), 1, qr3 }` writes a clamped 0..65535 halfword
and leaves the float in `f1`.  Upstream `doldecomp/melee#3456` and its fork
branch describe this `psq_st` as "a single-element float store" — that is
wrong, and the P-687 patch inherited the error.

**Fix (landed, P-709):** `patches/src/melee/gm/gm_1601_ml_fallback.patch` now
clamps and stores a u16
(`*(u16*) dst = (u16) (x < 0.0f ? 0.0f : (x > 65535.0f ? 65535.0f : x));`) and
still returns the float.  Reference implementation at the same upstream pin:
`999sian/melee-pc` `src/melee/gm/gm_1601.c:3094-3106`.  Check it with
`objdump -d` on the inlined copy in `gm_80166378`: the store at
`player_standings[i].xE` must be `mov %ax,0x66(%edi)` (2 bytes) preceded by a
`comiss` 0-clamp and a `$0xffff` saturate, never a `movss`.

**Generalise it:** any `psq_st`/`psq_l` in the tree is quantized by whichever
GQR the instruction names, and `init_spr_unk` is the only place the game loads
them.  Before writing a `PORT_PC` fallback for one, read the GQR type field
there rather than assuming the float default (type 0) — and do not trust
upstream #3456 on this point.

## G-159: `ftAnim_8006F3DC` falls off the end on the not-found path

**Symptom (latent):** `fp->cur_anim_frame = ftAnim_8006F3DC(gobj)`
(`decomp/src/melee/ft/ftanim.c:371-376`) receives whatever `eax`/`xmm0` holds
when `x8A4_animBlendFrames == 0` and no part matches the flag test, or a
matching part has no `HSD_AObj`.

**Cause:** the loop over `ftPartsTable[fp->kind]->parts_num` exits without a
`return`.  Retail writes nothing to `f1` either (it returns the caller's
`f1`), which is why the P-695 census classified it "left alone — never
observed"; but the host value is indeterminate rather than inherited, and
`999sian/melee-pc` fixed the same function in its
"functions that fell off the end returning garbage on x86-64" commit
(`21da73a09`) to return `0.0f`.

**Fix (landed, P-709):** `patches/src/melee/ft/ftanim.c.patch` adds a
`PORT_PC` `return 0.0f;` after the loop (the same patch already handles
`ftAnim_8006F994`).  Deterministic beats indeterminate even where retail is
indeterminate, because the host's garbage is unrelated to the caller's `f1`.
Note this is an exception to the P-695 census's outcome 3 ("indeterminate and
unreachable -> leave it"): the rule holds unless a consumer stores the result
into live state, which `fp->cur_anim_frame` is.

## G-160: "no caller reads the result" must include function-pointer tables

**Symptom:** the Chansey egg (`itKyasarinegg`) in motion state 4 was destroyed
at random, and the Sound Test menu swallowed or double-handled key presses.

**Cause:** the P-695 missing-`return` census bucketed 14 of its 45 sites as
"declared non-void but no caller reads the result (fake return type)".  That
judgement was made by grepping for `name(` — i.e. **direct calls only**.  Four
of those functions are never called directly at all; they are installed in
callback tables and invoked through a pointer, and the engine does read the
result:

| Function | Installed as | Consumer |
|---|---|---|
| `itKyasarinegg_UnkMotion4_Anim` | `ItemStateTable.animated` (`itkyasarinegg.c:21`) | `Item_80269528` (`item.c:1303`) destroys the item when it returns true |
| `un_802FF934`, `un_80300758`, `un_80300790` | `un_80304138_objalloc_t_x8.x4` (Sound Test menu rows) | `un_80302E00` (`textlib_1.c:33`) forwards the key to the parent handler **only** when the row's callback returns 0 |

A grep for `\bname\b` that *excludes* `name(` finds these in one pass:

```sh
grep -rn "\b$fn\b" --include=*.c --include=*.h decomp/src | grep -v "$fn *("
```

**Fix:** all four are census outcome 1 (retail's `r3` provably holds a specific
expression), fixed in P-710 — see `learnings/decomp_port.md`.

**Rule:** before writing off a fall-off-the-end site as unread, check for
table membership as well as direct calls.  In this codebase the state machines
(`ItemStateTable`, `ftState`, menu row tables, `HSD_GObjPredicate` slots) are
where the *consumed* return values live, and they never appear as `name(`.

## G-161: a `void` callee still decides the caller's `r3`

**Symptom:** `un_80300758`/`un_80300790` look like they must return 0 on the
`arg0 == 1` path — the reference port `999sian/melee-pc` patched them that way
(`58cf731`) — but retail returns **4**.

**Cause:** the path ends in `un_802FFCD0(4, ptr)`, which is `void`.  Its retail
body (`0x802ffcd0`) reads `count` out of `r3` and does all its work in
`r0/r5/r6/r7`, so it **never writes `r3`** and the argument survives the call.
`cmpwi r3, 1` does not write `r3` either, so the other path returns the
incoming `arg0`.  Both paths are therefore fully determined, not garbage.

**Rule:** when deciding what a fall-off-the-end site returns, do not stop at
"the last call was `void`, so it is indeterminate".  Disassemble the callee and
check whether it writes `r3` at all; a small leaf function often does not, and
then the *caller's* argument is the return value.  This is how the P-710 sites
turned out to be outcome 1 rather than outcome 3.

## G-162: "`x` is used uninitialized" in `src/` usually means a type pun, not a missing assignment

**Symptom:** a `-Wuninitialized` sweep of the decompilation names three
variables that are plainly assigned one line earlier:

```
particle.c:539       'abs_z' is used uninitialized
gmresultplayer.c:609 'abs_stick_y' is used uninitialized
ft_0892.c:44         'spC' is used uninitialized
```

**Cause:** the idiom is a sign-bit clear done through an `int` lvalue —

```c
f32 abs_z = vz;
*(s32*) &abs_z &= 0x7FFFFFFF;
```

— so the `f32` object is written and the `s32` object is not.  With strict
aliasing on, GCC treats them as distinct objects, and the read of the `s32` one
really is a read of something never written.  The practical consequence is
worse than the warning sounds: the mask can be optimised away.

**Fix:** ADR-0019 compiles every decomp target with `-fno-strict-aliasing`
(`MELEE_DECOMP_UB_OPTIONS` in `native/CMakeLists.txt`).  Do **not** "fix" these
by rewriting the puns — there are 72 of them, `src/` is read-only, and the
sites GCC does not warn about are the dangerous ones.

**Read the warning text carefully.** `is used uninitialized` (3 sites) and
`may be used uninitialized` (181 sites) are different problems: the first is
this aliasing class, the second is genuinely lost assignments (P-713).  A
filter that greps for only one of the two strings returns a confident wrong
answer — melee-pc hit exactly this trap from the other direction on its Venom
crash, where the assert macro demoted a real bug to "may be used".

## G-163: the decomp's own layout assertions were switched off twice over

**Symptom (latent, and one live):** nothing in the port ever verified that its
structs match the console, and `struct TmData` (the `gm_804771C4` Tournament
Mode global) was `0x5F8` instead of `0x574`, with every field after `x37[64]`
shifted 132 bytes.

**Cause:** two independent switches, both off.

1. `Runtime/platform.h:120` gates `ASSERT_SIZE`/`ASSERT_OFFSET` on
   `#if defined(MUST_MATCH) || defined(LINT)`.  The port defined neither, so
   all 189 of them expanded to nothing.  The same condition also gates a
   `#pragma pack(push, 1)` in `melee/gm/types.h` — so `LINT` is not only
   assertions, it is **layout**.
2. `native/decomp/shim/Runtime/platform.h` additionally `#undef`ed
   `STATIC_ASSERT` itself, killing even the raw
   `STATIC_ASSERT(offsetof(...))` assertions that are not behind that gate.
   Its reason — "false on a 64-bit host" — predates ADR-0012, which made every
   compiled target 32-bit.

**Fix:** ADR-0020 — `-DLINT` on every decomp target
(`MELEE_DECOMP_LAYOUT_OPTIONS`) and the shim's `#undef` deleted.  All 993 TUs
pass.

**Two traps worth remembering:**

- **Validate the instrument before believing a zero.**  The first run of this
  check reported 0 failures and meant nothing: the shim was still neutralising
  `STATIC_ASSERT`, so the pass was measuring air.  Breaking
  `ASSERT_SIZE(struct Fighter, 0x23EC)` on purpose and watching it fire is
  what proved the check was live.  Do this every time.
- **`LINT` changes struct packing, so it is all-or-nothing.**  A build where
  some TUs define it and others do not gives one struct two layouts, which is
  worse than the original bug.  If you add a target or change its includes,
  re-run the check in ADR-0020 that no non-`LINT` TU reaches a LINT-sensitive
  header.

**Do not assume a reference port's retraction applies to us.**
`999sian/melee-pc` explicitly withdrew this idea (`51965c2`) after getting 94
failures — correctly, for a 64-bit target where pointer-bearing structs cannot
match PowerPC sizes.  Being 32-bit (ADR-0012) is exactly what turns their dead
lever into our live one.  See also G-160/G-161, where copying that port's
conclusions would have been wrong for different reasons.

## G-164: a byte read past an array can change meaning with endianness

**Symptom:** in `gm_1601.c`, `team_count` was incremented for a player who had
any self-destructs at all.  Retail increments it essentially never.

**Cause:** the loop is `for (i = 0; i < 6; i++)` over
`team_standings[GM_MAX_TEAMS]`, and `GM_MAX_TEAMS` is 5.  The sixth iteration
reads one byte past the array.  That byte is not padding — it is
`player_standings[0].self_destructs`, a **u16** of natively-stored runtime
data, at `MatchEnd+0x62`.

On the console's big-endian layout the byte at that address is the u16's
**high** half, so the branch needs 256+ self-destructs.  On a little-endian
host the same address is the **low** half, so it fires on the first one.  Same
address, same C, opposite behaviour.  `team_count` is then added into
`is_big_loser`/`is_small_loser`, so it reaches the results screen.

**Rule:** when porting an out-of-bounds read that lands on a live neighbour,
the byte offset is not enough — work out which *half* of the neighbouring
field the console was reading.  A stray read of a `u8` neighbour ports
directly; a stray read into a `u16`/`u32` neighbour does not.  This applies to
runtime data; disc data is already handled by the converter.

**Related trap:** the decomp's `/* 0xNN */` offset comments are commentary, not
contract.  `team_standings` is commented `/* 0x1B */` and actually sits at
`0x1C` (the preceding `u8[5]` ends at 0x1B and `MatchTeamData` needs 4-byte
alignment); `gm/types.h` even notes "offset by 1 because of the previous
struct" further down.  **Measure offsets with `offsetof`, and pin the result
with a `STATIC_ASSERT`** so a re-pin breaks the build instead of the game —
arithmetic done from the comments put this fix on the wrong field first, and
the assertion is what caught it.

## G-165: narrow type-puns in the decomp are self-consistent — checked, not a bug class

**Context:** after G-164 (a stray read that landed on half of a neighbouring
`u16` and so flipped meaning with endianness), the obvious follow-up is that
*deliberate* narrow puns might do the same.  They do not.  Recorded so the
sweep is not repeated.

The tree has 24 `*(u8|s8|u16|s16*) &field` sites.  Every one checked resolves
to one of three harmless shapes:

- **The field is really a buffer head.**  `*(u8*) &toy->x194` looks like a byte
  write into an `s32`, but `x194` is the first 4 bytes of a byte-addressed
  region (`memzero(&userData->x194, 0x25A)` right above it, and `(u16*)(base +
  0x194)` indexing below).  Byte 0 is byte 0 on either endianness.
- **Same address, same width, both ways.**  `game_camera.x368` is a `Vec3`
  whose `.x` doubles as a 16-bit distance slot in camera mode 2:
  `camera.c:3428` writes `*(s16*) &x368` and `camera.c:2968` reads it back.
  The console puts that `s16` over the float's sign/exponent bytes and the
  host over its low mantissa bytes, so the *bit pattern* of `x368.x` differs —
  but nothing reads `.x` as a float in that mode (`.y`/`.z` are the angles),
  so the two never meet.  `grzebes.c` `zebes5.xF8` and `grcastle.c`
  `castle2.xC4` are the same shape.
- **The target is already that width**, so the cast is a no-op.

**The rule that separates this from G-164:** a pun is safe when *every* access
to those bytes uses the same address and the same width.  It is a bug when the
same storage is reached at two different widths — a narrow write and a wide
read, or a stray access landing on part of a wider neighbour.  Check for the
*mix*, not for the cast.

## G-166: `-fdata-sections` breaks every "one base, two symbols" overlay

**Symptom:** `soundtest.c` reaches `un_803FA258` by indexing off
`un_803FA128`, a 304-byte array, at offsets up to `0x227` — 244 bytes past its
end, into whatever the linker happened to place next.  `-Wstringop-overflow`
is what surfaced it ("writing 4 bytes into a region of size 0").

**Cause:** the console linker placed `un_803FA258` immediately after
`un_803FA128` (which is exactly `0x130` bytes), and the original code addresses
both blocks from one base — the decomp says so in a comment on
`struct un_803FA128_t`.  The port builds with `-fdata-sections`, so GCC gives
each object its own section and the linker may order them however it likes.

**This is the second instance of the same class.**  The first was G-154: the
four name-width tables at `803B75F8/767C/7700/7784`, which retail indexes as
`lbl_803B75F8[ckind + 0x21]`, and which P-704 fixed by naming the real arrays.
The fix here is the same idea — `PORT_FA128_BASE` rebases the overlay onto the
symbol that actually holds the fields — with `STATIC_ASSERT`s pinning the
mapping (`x220` == `un_803FA258.xF0`, `x224` == `.xF4`, and
`offsetof(x130) == sizeof(un_803FA128)`).

**Go looking for the rest.**  Any decomp comment mentioning that one symbol is
addressed from another, and any `(struct X*) some_other_symbol` cast, is a
candidate.  The tell at compile time is `-Wstringop-overflow` /
`-Warray-bounds` reporting a "region of size 0" or a size that matches one
symbol while the access offset clearly belongs to the next.  Adjacency that the
console's linker guaranteed is never guaranteed here.

## G-167: a stray read of a *pointer* is negative on the console and positive here

**Symptom:** `fn_803AF3F0` (memory card) could run
`for (i = 0; i < file_blocks; i++) block_map[i] = -1;` with a garbage
`file_blocks` over a 64-entry **stack** array.  The console never does.

**Cause:** `CardState::file_sizes` is `int[9]`, and this file routinely carries
a file index of **9** — it is expected, not accidental:
`fn_803AC6B8_blocks_before` opens with `if (file_idx >= 9) return 0;` and
`fn_803ACC50` tests `file_idx + 1 >= 9`.  The size reads have no such guard, so
`file_sizes[9]` lands on `CardState::file_data[0]` at offset `0x70` — a
pointer.

Then the platforms diverge on the **sign**:

| | `file_data[0]` as `s32` | `file_sizes[...] <= 0` guard | result |
|---|---|---|---|
| GameCube | MEM1 address `0x8xxxxxxx`, or NULL | negative or zero -> fires | file treated as empty, nothing happens |
| 32-bit host | typical heap/bss address **below** `0x80000000` | positive -> does **not** fire | block count computed from an address, stack smash |

So the console is saved by an accident of its address space, and the port is
not.  This is the sharper form of G-164: there the stray byte changed meaning
with *endianness*, here the stray word changes meaning with the *sign of a
pointer value*.

**Fix (P-720):** `port_file_size()` returns 0 for an out-of-range index, which
is the outcome retail reaches anyway.  It is a `static inline` under
`PORT_PC` and a macro expanding to the original expression otherwise, so the
15 call sites need no `#ifdef` and the GameCube build stays byte-identical.

**Rule:** whenever a stray access lands on a *pointer* field, check the sign
and magnitude the console's address space gave it, not just the offset.
`0x8xxxxxxx` read as a signed int is negative, and a surprising amount of
retail code is protected by exactly that.  Writes are worse still — a write to
`file_sizes[9]` clobbers a live pointer on both platforms; those sites are
left alone here because no caller is proven to reach them (noted in P-717).

## G-168: a panic that reaches no stream is indistinguishable from a clean quit

`OSPanic` wrote its message to `boot_triage_out()`, and the viewer points that
at `/dev/null` unless `MELEE_VIEWER_TRIAGE` is set.  It then called
`boot_triage_stop("OSPanic")`, which `exit(0)`s when no harness has installed a
`siglongjmp` target — so the `abort()` on the next line never ran.

The result: every failed `HSD_ASSERT` in normal play looked exactly like the
user closing the window.  No message, status 0, "exited normally" under gdb,
no core, no stack.  The owner reported "a lot of bugs/crashes that just close
like this" across the whole game, and they were all panics with the evidence
discarded.

The same applied to `OSReport`.  The game narrates its own failures — `"****
Not Found Toy Model!(%d)"`, `"*** BG data aren't being loaded!"`, `"Cannot find
symbol %s."` — and the line before a panic is usually the one that names the
bug.  All of it went to `/dev/null`.

**Rule:** a diagnostic path may be quiet, but it may never be silent *and*
exit successfully.  Report on stderr regardless of where the log is pointed,
and leave through `abort()` so the shell sees a failure and a debugger keeps
the frame.  Concretely, in this port:

- `OSPanic` echoes to stderr, dumps the last 64 `OSReport` lines, prints a
  symbolised backtrace (`-rdynamic` is already on, so `dladdr` names game
  functions), and aborts.
- `MELEE_LOG_REPORTS=1` echoes every `OSReport` live.

The first run after this landed named its own bug in one line: `**** Not Found
Toy Model!(3073)` with `Toy_8030813C <- Toy_80310324` above it.  Before it,
the same failure was a blank exit.


## G-169: the console's spare stack slot is the host's live local

`Toy_80310324` declares `UNK_T sym[1]` and passes `sym + 4` as an out-pointer,
so the callee writes 16 bytes past a one-element array.  On the console that
landed on another slot of the same frame and nothing ever read it back — the
value is re-fetched later with `HSD_ArchiveGetPublicAddress` — so it was free
scratch, and the decompilation faithfully reproduces it.

The host's frame layout is its own.  That write lands on whichever local GCC
put there, and it is a silent corruption of an unrelated variable, not a
crash — so it shows up later, somewhere else, as nonsense data.

**Rule:** this is the stack-resident sibling of G-166.  When a decompiled
function indexes a local array out of range, it is not undefined-behaviour
pedantry: the console frame made that address mean something, and here it
means something else.  Look for `arr + N` and `&arr[N]` where `N >=` the
declared extent, the same way the `-Warray-bounds` sweep looks for the `.data`
version.

## G-170: a byte-swapped small integer is another plausible small integer

`TyDatai`'s seven trophy tables were never converted — no branch of the
converter's name dispatch claimed them — and nothing looked wrong until the
Trophy Gallery panicked with `**** Not Found Toy Model!(3073)`.  3073 is
`0x0C01`; the real trophy id was 268, `0x010C`.

That is the whole hazard.  Descriptor structures fail loudly when they stay
big-endian (absurd dimensions, garbage pointers, G-146).  A table of small
integers fails *quietly*: every field still reads as a small integer, indices
still land inside arrays, loops still terminate.  It surfaces much later as
"that menu is empty" or "that lookup missed".

Two rules follow.

**Do not guess entry counts.** My first fix used `TY_TROPHY_COUNT` (293) for
all three fixed-size tables.  The real extents are the gaps between the
archive's public symbols: `tyInitModelDTbl` holds **six** entries, not 293.
Sweeping 293 ran straight through its neighbours and converted them a second
time at `u32` granularity — which *transposes each `u16` pair* rather than
corrupting it, so the values stay small and plausible and every
sanity check still passes.  Clamp to the next public symbol
(`next_public_after`), or use the table's own `-1` terminator.

**Test against the raw bytes, not against plausibility.** A check like "every
id resolves in the other table" passed in all three states: correct,
unconverted, and transposed.  The only check that distinguishes them compares
each converted field with a big-endian read of the original buffer, which is
by definition what the console sees.  That is the P-645 pattern, and it should
be the default for any flat data table.

**And clear the right cache when testing this.** `decomp_assets` sets
`MELEE_ASSET_CACHE` to `<build>/asset-cache`, not `~/.cache/melee/assets`, so
a stale entry from an earlier build silently served converted bytes while the
converter itself never ran — which cost most of the time spent finding this.
`MELEE_NO_ASSET_CACHE=1` is the reliable way to force conversion.

## G-171: GX state setters must flush pending geometry, without exception

`GXSetTevColor` and `GXSetTevColorS10` wrote `gx.cur.tev_color[]` without
calling `flush_direct()`.  Every other TEV setter in `gx_hle.c` flushes --
`GXSetTevColorIn`, `GXSetTevAlphaIn`, `GXSetTevColorOp`, `GXSetTevAlphaOp`,
`GXSetTevOp`, `GXSetTevOrder`, `GXSetTevSwapMode`, `GXSetNumTevStages`,
`GXSetTevKColorSel`, and `GXSetTevKColor`.  These two were the exceptions.

The consequence is not a wrong colour on one draw; it is a *run* of draws
collapsing onto one colour, because geometry queued before the register
changed is emitted afterwards with the new value.  It shows up wherever the
game alternates state and geometry in a tight sequence, which is exactly what
text does: the SIS renderer sets TEVREG0 to the panel colour, draws the panel,
then draws each glyph.

**Rule:** in the GX HLE, any function that mutates draw state must flush
first.  The batching is only valid while the state is constant.  When
auditing, compare against the neighbours -- a setter that does not flush while
every sibling does is the bug, and that asymmetry is the cheapest way to find
these.

## G-172: never conclude from the first hit of a deduplicated trace

Hunting P-732 I twice published a wrong root cause, and both times the error
was the same shape.

I instrumented the text renderer with a trace that deduplicated its output, so
each interesting combination printed once. I then read the first line that
appeared and treated it as the steady state. It was not:

- "The entire render pass runs twice per frame" — the counter that grouped the
  calls (`boot_triage_frames()`) ticks inside `VIWaitForRetrace`, which does
  not tick on loading frames, so two real frames merged into one key. Printing
  passes-per-retrace raw showed `passes=1` everywhere but startup.
- "Each text object renders twice per frame" — same cause. The raw trace showed
  frame 60 twice and frames 61..82 once each.

Both claims survived only because the dedupe hid the distribution. The moment
the same data was printed raw, both died immediately.

**Rules.**

1. **Print raw first, dedupe second.** Deduping is for reducing volume once you
   know the shape. Use it to confirm a distribution, never to discover one.
2. **A dedupe key must cover every field you might care about.** Mine keyed on
   `(object, glyph count, x88)` and would silently have swallowed a change in
   scale, font size or line height — exactly the values being compared.
3. **Check what your grouping key actually counts.** A "frame" counter that
   only advances when the game waits for retrace is not a frame counter during
   loading.
4. Corollary to G-168: an instrument that can lie is worse than none, because
   it produces confident wrong answers. Verify the instrument against a case
   whose answer is already known before trusting it on the case that is not.

## G-173: GX has eight texture maps and coordinates; never alias an unsupported map

**Symptom:** Peach's Castle roof appears duplicated, rotates or swims with a
fighter, and intermittently darkens.  Pokémon Stadium similarly shows a dark,
transparent Pokéball following one fighter; it disappears when that fighter
leaves the stage.

**Cause:** the receiver material has four active TEV inputs: two ordinary
textures on maps 0/1 and two 256x256 I4 fighter-shadow copies on maps 2/3.
The HLE captured eight map slots, but the GL shader and vertex layout carried
only 0..2; `gx_sample_map(3, ...)` fell through to map 0.  The second fighter's
projective shadow coordinate consequently sampled and projected the stage's
roof/Pokéball image rather than its shadow map.  Which fighter it followed and
when it darkened were direct consequences of the live map-3 shadow pass.

**Fix:** carry three-component TEXCOORD0..7 varyings, sample `u_tex0..7`, and
upload size/LOD/dynamic-copy state for every map.  Resolve all GL texture names
before binding any unit: `texture_for()` may upload and bind on the current
unit, so interleaving lookup and binding can overwrite an earlier unit.
`decomp_efb` pass 6 selects a white map 3 while map 0 is black and fails on
the old fallback.  Rule: implement the complete GX register range; silently
aliasing an unsupported index converts missing support into moving corruption.

## G-174: partial GX texture-copy rows advance by the rounded-up tile count

**Symptom:** Pokémon Stadium's 250x160 monitor capture is dense coloured
noise even though its text overlay is readable.  Power-of-two EFB-copy tests
remain green.

**Cause:** RGB565/RGB5A3/IA8 use 4-pixel-wide tiles.  Their encoder advanced
tile rows by `dst_w / 4`; width 250 needs 63 tiles, but integer division used
62, so the last tile of one row overlapped the first tile of the next.  The
error accumulated down the captured image.

**Fix:** use `(dst_w + 3) / 4` consistently in the offset and allocation
calculation.  `decomp_efb` copies a 6x8 RGB565 image and asserts that the first
pixel of the second tile row remains green; the broken stride overwrites it.

## G-175: `GXCopyTex(clear=true)` resolves first and then clears the EFB

**Symptom:** later screen-texture and shadow passes can inherit colour/depth
that console GX would have erased after a capture, producing state-dependent
compositing differences.

**Cause:** `GXSetCopyClear` discarded its arguments and `GXCopyTex` ignored
the clear flag.  The flag is not a destination initializer: GX resolves the
requested EFB rectangle into texture memory, then clears that source rectangle.

**Fix:** snapshot the clear RGBA/Z registers with the draw, capture first,
then scissored-clear the source rectangle.  Respect `GXSetColorUpdate`,
`GXSetAlphaUpdate`, and the Z update bit when selecting clear components.
`decomp_efb` asserts both the copied pre-clear pixels and the post-copy EFB
clear colour.

## G-176: `tydisplay.c` reaches its second and third name table by offset

**Symptom:** every VS match dies just after the character-name splash with
`file isn't exist S<garbage>.usd = -1` and
`assertion "entry_num != -1" failed in .../lb/lbfile.c on line 114`.
Backtrace: `lbArchive_LoadSymbols` <- `tyDisplay_8031C454` <-
`Ground_801C0754` <- `Stage_802251E8` <- `gm_Scene_IntroEasy_OnEnter`.

**Cause:** `ty/tydisplay.c` defines three 43-entry `TyDspArchNames` tables --
joint names (803B8988), matanim names (803B8A34) and archive filenames
(803B8AE0) -- each 0xAC bytes.  `tyDisplay_8031C454` and
`tyDisplay_8031C5E4` cast the address of the *first* to `TyDspNameTables*`
and read `->matanim_names` and `->arch_names`, i.e. 0xAC and 0x158 past it.
That works only because the console linker emitted the three back-to-back.
The port builds with `-fdata-sections`; in the linked binary, 0x158 past
`_tyDisplay_803B8988` is a **function pointer**, so the filename handed to
`lbArchive_LoadSymbols` was executable code read as a string.

**Fix:** read each table through its own symbol (`TYDSP_*_TABLE` macros,
`PORT_PC` only).  Exact rather than approximate: the console's two offsets
land on precisely those two objects.  This is the `soundtest.c` treatment
(P-718/G-166) rather than the `toy.c` one (P-721), because nothing here reads
*across* a table boundary -- each access is wholly inside one table.

**Rule.** This is the sixth instance of the class and the first one that was
*fatal* rather than cosmetic.  The file even documents the adjacency in a
comment above the tables; a doc comment saying "these are emitted
back-to-back" is a defect report, not a reassurance.  When auditing P-721,
grep for that phrasing as well as for `-Warray-bounds`: this site produces no
warning at all, because the cast is to a complete type and GCC cannot see
that the object it points at is smaller.

## G-177: Pokémon Stadium's whole stage ran on big-endian parameters

**Symptom:** the jumbotron is dense coloured noise from the moment the match
starts.  `MELEE_EFB_TRACE` shows the 640x406 live feed is **never captured**;
`MELEE_STADIUM_TRACE` shows the display state flipping between 7 and 8 every
frame with `timer=-1341904928`, a different garbage value each time.

**Cause:** `GrPs.dat`/`GrPs3.dat`'s `yakumono_param` had no converter
descriptor (P-708's list), so `grPStadium_YakumonoParam` was read
big-endian.  `grStadium_801D2528` seeds the display countdown from
`randi_between_2(x38, x3C)`; 600 and 1200 read as `0x58020000` and
`0xB0040000`, so the countdown starts at a garbage negative.
`grStadium_801D2344`'s state 7 is:

```c
case 7:
    if (gp->u.display.xE0-- < 0) {
        grStadium_801D2A60(gobj);   /* pick a new state */
        break;                      /* <-- before clearing the flag */
    }
    GET_WRAPPER(gp->u.display.xD8)->flag = false;
```

The early branch fires on the first frame and **breaks before clearing the
flag**, so `grStadium_801D2FD0` never reaches its `GXCopyTex` and the monitor
samples the buffer `HSD_MemAlloc` returned -- uninitialised heap decoded as
RGB565.  State 8 has the same shape, which is why the 124x80 close-up
captured once (its wrapper is created with `flag = false`) and then froze.

**Fix:** converter v90 walks the layout: seven s32, the `{ u8 r, g, b }`
monitor tint plus a pad byte at +0x1C (**must not** be swapped), ten u32 and
five s16.  Marker `GrdPStadiumSteelK`, which is a **public**; the scan walks
`nb_public` only, so the obvious `GrdPStadiumRock_TopN_joint` is invisible to
it.  `GrHr.dat` also carries `GrdPStadium*` names -- the Home-Run Contest
reuses the textures -- and keeps its existing `GrdYorster` match.

**Scope is the whole stage, not the monitor.** The same block carries the
3600/3800-frame interval between transformations (`x0`/`x4`), the
transformation rise/fall timings (`x10`/`x14`/`x18`) and the 5/2/2/0 weights
that choose which transformation runs (`x48`..`x50`).  All of them were
garbage.

**Two rules.**

1. **An unconverted parameter block does not look like an endianness bug.** It
   looks like a renderer bug, and it cost P-736 a whole pass at the GX layer
   (rounded-up tile counts, copy-clear semantics -- both real fixes, neither
   related).  When a stage misbehaves, check `P-708`'s list of stages with no
   `yakumono_param` descriptor *first*: PStadium is now done, but MuteCity,
   Icemt, RCruise, Garden, Shrine, Kraid, Pura, BigBlue, Inishie2, Battle,
   OldPupupu, OldYoshi and OldKongo are still raw.
2. **Editing a converter without bumping `HSD_CONVERTER_VERSION` poisons the
   cache**, and so does running the tests against a half-built binary: the
   `build/native/asset-cache` entry is written from whatever the converter did
   at that moment and read back forever after.  A converter change that seems
   not to take effect is this, every time; `rm -rf build/native/asset-cache`
   before concluding anything.

## G-178: `0x424` is the fighter attribute *allocation*, not the struct

**Symptom:** no character can produce a projectile.  The neutral-special
animation plays, the fighter enters the right action, held articles that are
created from C code appear (Link's bow, Fox's gun), but nothing that the
animation script has to trigger ever happens -- no arrow, no laser, no
fireball, and no damage.

**Cause:** `conv_ft_data` byte-swapped `0x424` bytes starting at
`ftData->x0`.  `0x424` is the size `fighter.c:146` gives
`fighter_dat_attrs_alloc_data`, the runtime **backup** block; the struct in
the archive is `ftCo_DatAttrs`, which is `0x184`.  The extra `0x2A0` bytes ran
through `ftData->x4` (the per-character `ft??_DatAttrs` -- which is why the
character attributes came out converted at all) and then off the end of it.

What follows in every `Pl*.dat` is the fighter's **special-move command
scripts**.  For `PlLk.dat`, `ftDataLink->x0` is `0x33DC`, so the walk reached
`0x3800` and swapped the scripts at `0x363C` (SpecialNStart) and `0x36F0`
(SpecialNEnd).  Those structs are `CMD_BE` -- the engine reads the raw
big-endian command words -- so the interpreter saw:

```
broken: 030000d0 05000008 0000008c 0100004c   -> opcode 0, stop
fixed:  d0000003 08000005 8c000000 4c000001   -> 52, 2, 35, 19
```

`4c000001` is opcode 19, `set_cmd_var idx=0 value=1`, which is exactly what
`isDrawn()` in `ftlinkspecialn.c` waits for before spawning the arrow.  Every
special's script terminated on its first word, so no subaction event in any
special move ran for any character.

**Fix:** clamp `ftData->x0` to `sizeof(ftCo_DatAttrs)`, and convert
`ftData->x4` explicitly -- it used to be converted only by the overrun, and
for Kirby (ext size `0x424`) it was being *truncated* by the same walk.

**`next_pointed_at_after`.** The per-character attribute struct has no single
declared size, so the walk is bounded by the next data offset anything in the
archive points at.  An object cannot extend past the next object someone holds
a pointer to, so that is exact, and it reproduces the decompilation's own
sizes: `0x184`/`0xDC` for `PlLk.dat` = `sizeof(ftCo_DatAttrs)`/
`sizeof(ftLk_DatAttrs)`, `0x84` for `PlMr.dat` = `sizeof(ftMario_DatAttrs)`.
Measured across all 32 fighter archives.

**Rules.**

1. **An allocation size is not a struct size.** `HSD_ObjAllocInit(..., 0x424, ...)`
   sizes a pool entry that may hold more than the thing it is named after.
   Take sizes from the type, and check them against the archive.
2. **`~/.cache/melee/assets` is the default asset cache** and it is keyed by
   `HSD_CONVERTER_VERSION`.  Three of my measurements here were wrong because
   a stale entry answered instead of the converter: the "sensitivity" build
   that should have failed passed, twice.  Clearing
   `build/native/asset-cache` is not enough -- the game binary uses the one in
   `~/.cache`.  Second time this has cost a detour (G-177).

## G-179: three more layers behind the projectiles, and what each one looked like

Fixing the special-move command scripts (G-178) made every character's
neutral-B run its script for the first time.  Each layer under it then failed
in turn, and **none of the three looked like what it was**.

### 1. The particle bank's `HSD_PSCmdList` headers were big-endian

**Symptom:** holding B panics on `assertion "adr" failed` in `memory.c:23`,
stack `psGenerateParticle0` <- `hsd_8039DAD4` <- `efLib_particles_proc_main`.
Other characters freeze instead.

**Cause:** `conv_ps_cmd_bank` converted exactly one field of each descriptor,
`kind` at +0x08, and left the other 0x34 bytes raw.  So `life` 12 arrived as
`0x0C00` = 3072, `size` and `random` as denormals, and `texGroup` -- which
indexes `psTexGroupArray[bank][]` -- as a byte-swapped u16.  `generator.c`
seeds `gen->count` from `random` and then runs
`while (gen->count >= 1.0F) { ...; gen->count -= 1.0F; }`, so one effect asked
for ~4.3e8 particles.  `HSD_ObjAllocAddFree` grows the pool 152 bytes at a
time out of the HSD heap, which went from 10 MB free to 128 bytes in well
under a second.

**Not** heap exhaustion in the ordinary sense, which is what the assert looks
like: the request that failed was 152 bytes.

### 2. `psdisp.c` wrote the hardware FIFO address directly

**Symptom:** with the descriptors fixed, a clean `SIGSEGV` in
`psDispSubMakePolygon` on `GXWGFifo.u8 = tex_base;`.

**Cause:** the `GXVert.h` shim routes the SDK's *inline* vertex helpers
(`GXPosition3f32`, `GXColor4u8`, ...) into the GX HLE, and its own comment
said the raw `GXWGFifo` address "is only touched if such a function actually
runs, which the S2/S4 paths do not".  That was true only while particles never
rendered.  `psdisp.c` hand-inlines 35 such stores, `gm_1832.c` 12 and
`hsd_3915.c` 2.

**Fix:** a `WGFIFO_F32`/`WGFIFO_U8` macro pair per file, `PORT_PC` routing to
`GXPortWGFifo*` and otherwise expanding to the original expression, so the
GameCube build is unchanged (`main.dol` still 100.00% matched).

### 3. `Article::x4_specialAttributes` was converted for food items only

**Symptom:** the arrow spawns, flies nowhere and hits nothing.

**Cause:** `conv_article` only walked `x4` when `item_kind == ITEM_KIND_FOODS`.
Every projectile therefore read its attributes big-endian: traced over its
lifetime, Link's arrow launched with `vel=(2.67e23, 2.33e22)`.

**Fix:** bound the block with `next_pointed_at_after()` (G-178's rule) and
convert it as dense 4-byte fields.  Only two `*Attributes` structs in `it/`
are not uniform 4-byte -- `itWhispyAppleAttributes` and
`itOctarockAttributes` -- and both are spelled out rather than skipped,
because a u32 walk over `u8 x0[4]` reverses four independent bytes.

**Rule.** A fix that unblocks a dead code path is not finished when it lands;
it is finished when something *drives* the path.  Each of these three was
invisible until the one above it was fixed, and each new symptom pointed at
the wrong subsystem -- the allocator, the renderer, the item state machine.
`ctest decomp_projectile` now asserts **damage**, not a spawn, because the
arrow spawned long before it could fly and flew before it could hit; and it
carries a `FAIL_REGULAR_EXPRESSION` for the panic, because ctest's
`PASS_REGULAR_EXPRESSION` ignores the exit status and the crash happens after
the first hit.

## G-180: `UnkFlagStruct` — one union, every invisible model

**Symptom:** Link's bow and arrow, and Mario's fireball, are invisible.  They
exist, they move, and the arrow deals damage: only the model is missing.

**Cause:** `UnkFlagStruct` (`gm/types.h`) is a `u8 byte` unioned with eight
1-bit fields `b0..b7`, and the codebase writes the byte and reads the bits.
MWCC allocates the first bitfield at the **MSB**, so retail's `byte = 1` sets
**b7**.  GCC allocates LSB-first, so it set b0 and left b7 clear.
`item.c:719` does exactly `item_data->xDAA_byte = 1`, and `it_8026EECC` tests
`ip->xDAA_flag.b7` before it draws anything, so every item model was skipped
while its hitbox, physics and lifetime carried on normally.

**Fix:** declare the aliases in reverse under `PORT_PC`, so the raw byte and
the named bits agree -- and so does any flag byte that came out of an archive.
This is the `ItemAttr` treatment (P-654/G-123) applied to the union itself.

**This bug had already been found once and fixed in the wrong place.**
`patches/src/melee/ft/types.h.patch` introduces `FtStatusFlags`, a private
copy of `UnkFlagStruct` with exactly this reversal, for one Fighter field --
and its own comment says "GCC's LSB-first layout would set b0 and leave b7
clear, **hiding the model**".  Same union, same mechanism, same symptom, and
the general case was left in place for every other user.

**Rule.** When a fix needs a private copy of a shared type to change how that
type is laid out, the shared type is what is wrong.  Fixing the copy hides the
bug from everyone else who uses the original, and the next person pays full
price to rediscover it.  Before adding a `Foo2` that differs from `Foo` only
in layout, check who else uses `Foo`.

**How it was found, since none of the obvious checks pointed at it.** The
model tree was present (3 joints, 1 DObj, materials, display lists), nothing
was `JOBJ_HIDDEN`, the render callback was installed, GX link 6 *was* in the
camera's mask, and the root carried `JOBJ_ROOT_OPA|XLU|TEXEDGE` so it matched
every pass.  What settled it was scaling the model 30x from outside and
diffing the frame against an unscaled run: **zero differing pixels** says
"never drawn", not "drawn wrong", and that turned the search from the renderer
to the caller.  Then one print at the top of `it_8026EECC` showed `b7=0`.

## G-181: use `PORT_BF_BE`, not a reversed field order

G-180 fixed `UnkFlagStruct` by writing its eight bits out backwards.  That
works, but only because the group fills its storage unit exactly.  **Reversing
a partial group is wrong:**

```c
struct { u8 b2 : 1; u8 b1 : 1; u8 b0 : 1; } f;  /* three bits, reversed */
f.b0 = 1;   /* -> 0x04, not 0x80 */
```

Five bits of padding are needed to push `b0` to the top, and nothing in the
declaration reminds you.  `grCorneria_GroundVars::xC4` is exactly that shape.

GCC's `scalar_storage_order("big-endian")` gives MWCC's allocation directly,
with the fields still declared in their natural order and no padding to get
right.  `Runtime/platform.h` now carries it as `PORT_BF_BE`:

```c
union {
    struct PORT_BF_BE { u8 b0 : 1; u8 b1 : 1; u8 b2 : 1; } flags;
    u8 value;
} xC4;
```

It is the same mechanism as `CMD_BE` in `melee/lb/types.h`, which the
subaction command structs have used since G-082; `PORT_BF_BE` is reachable
from everywhere because `Runtime/platform.h` is.  Note the attribute also
byte-swaps *scalar* members of the struct it is applied to, so put it on the
bit-field struct inside the union, never on the union itself.

**Which groups need it.** Only those whose storage is also touched as a whole:
a union alias written or read as a non-zero scalar, or bytes that came out of
a `.dat`.  A group that is only ever accessed by field name is self-consistent
either way, and `= 0` is order-independent, so most are fine.  The scan that
found Corneria was: collect every union that pairs a scalar with a bit-field
group, then look for a **non-zero** whole-scalar read or write of that member.
Beware member-name collisions when you do this -- `value`, `flags`, `x3` and
`xC4` appear in dozens of unrelated structs, and most of the hits are noise.

Fixed so far: `ItemAttr` (P-654), `Fighter::x21FC_flag` (`FtStatusFlags`),
`UnkFlagStruct` (G-180), `grCorneria_GroundVars::xC4` (G-181).
`decomp_assets` asserts the first, third and fourth, and needs no disc image.

## G-182: the guard blend pose was never converted

**Symptom (owner):** holding shield leaves the fighter invisible and no shield
bubble is drawn.

**Half of that is correct.** Retail hides the body while shielding
(`ftAction_80071FA0` sets `fp->x221E_b5`, and `ftdrawcommon.c:320/348` skip
it) and the bubble is what you are meant to see.  There was one bug, not two.

**Cause:** `ftData->x20` is the guard blend pose --
`ftData_x20 { HSD_Joint** x0; f32 x8; }` (ft/types.h:700) -- and
`conv_ft_data` never walked it.  The joint tree stayed big-endian: every
joint read `scale = 4.6006e-41`, which is `1.0f` byte-reversed.

`ftCo_Guard.c` blends that raw `HSD_Joint` into the fighter through
`ftAnim_80070108`/`ftAnim_8006FA58` -> `lb_8000C868`, which reads
`position`/`rotation`/`scale` off the descriptor directly.  The result lands
in the anim skeleton, then `ftAnim_8006FE9C` -> `lb_8000C490` blends *that*
into the live shield joint -- and because `lb_8000C490`'s destination is also
its second source, the garbage fed back and drifted each frame.
`efLib_Update` then takes the bubble's scale from that joint's matrix, so the
bubble came out scaled to nothing.

**Fix:** `conv_joint` the tree from `ftData->x20->x0`.  Note the decomp types
`x0` as `HSD_Joint**` and reads `x0[2]`; that is the root joint's own `child`
field at +0x08, not a third array entry, so converting from the root covers
it.

**How it was found, and the one measurement that mattered.**  The previous
agent's handover already had it: `scale = -nan` on the bubble at draw time and
`translate.z = -3.09e30` on the attach joint.  From there a **gdb watchpoint
on that one float** named the writer in one run -- `lb_8000C490`, from
`ftAnim_8006FE9C` -- and a second watchpoint on *its* source named
`lb_8000C868` and the raw joint behind it.  Two watchpoints beat any amount of
reading: the value is written from three different places across two joint
trees, and the chain is not visible from the call graph.

**Two traps in asserting this.**

1. `1e31` in a translate did **not** make the matrix infinite.  It made every
   row a denormal near `1e-40` -- a matrix that passes "is finite" and still
   scales the bubble to nothing.  The first version of `decomp_shield` passed
   on the broken data for exactly that reason.  Assert the row **magnitudes**,
   which is what `HSD_MtxGetScale` actually reads, not finiteness.
2. Anything reached only through `ftData` needs its own walk.  `x1C` (P-630),
   `x40`/`x4C` (P-655), `x48` (P-656) and now `x20` were each invisible until
   something used them.  When adding a walk, check the neighbouring `ftData`
   fields at the same time.

## G-183: a big-endian `flags` word invents `JOBJ_INSTANCE`

**Symptom:** grabbing anyone kills the game with `assertion "jobj->child"`
failed at `jobj.c:694`, through `it_802A2568` -> `HSD_JObjLoadJoint` ->
`HSD_JObjResolveRefs`.

**Cause:** Link's hookshot chain joints live in the article's special
attributes (`itLinkHookshotAttributes.x54/x58/x5C`), reached from
`ftData->x48_items`.  Nothing converted them, so `flags` kept its archive
value `0x40100080`; read little-endian that is **`0x80001040`**, and
`JOBJ_INSTANCE` is `1 << 12`.

`HSD_JObjResolveRefs` treats an instance joint's `child` as an **ID** rather
than a pointer, looks it up with `HSD_IDGetDataFromTable`, gets nothing
(the real `child` is 0) and asserts.  So a byte-swapped flags word did not
merely produce a wrong-looking joint -- it sent the loader down a completely
different branch.

**Rule.** When flags are byte-swapped, look at which *bits* that turns on, not
just at the number.  Every `0x…40` in a big-endian byte becomes `0x40…` and
vice versa, so byte-swapping quietly moves bits across the whole word and can
enable a mode the data never asked for.  The crash is then nowhere near the
data.

**Fix, and why it is not a table.** The per-kind table (`article_joint_fields`)
covers articles reached with a known item kind.  The ones under
`ftData->x48_items` arrive with kind `-1`, so for those the converter scans the
attribute block for relocation-target words and converts any pointee that
passes `looks_like_unconverted_joint`: link fields null or relocations, flags
not a relocation, and the scale triple read **big-endian** landing in
`(1e-4, 1e4)` -- it is `(1, 1, 1)` for every joint in these blocks and
essentially never that for anything else.  `conv_joint` does not validate what
it is handed, so a plain "convert every pointer here" would corrupt rather
than skip.  A joint another root already converted fails the test and is
skipped, which is right; `mark()` would refuse it anyway.

Fourth member of the "reached only through `ftData`" family, after `x1C`
(P-630), `x40`/`x4C` (P-655), `x48` (P-656) and `x20` (P-747).

## G-188: an out-of-range-looking index that was right all along

**Symptom:** SIGSEGV in `ftParts_8007506C(ftkind=Ft_Kind_None, part=256)`.
`Ft_Kind_None` is 33 and is also `Ft_Kind_Max`, so the index looked obviously
corrupt and the search went after whatever produced it.

**It was not corrupt.** `ftAnim_8006FE08` deliberately takes the
`fp->kind != fp->x597_bits` branch when a fighter animates another fighter's
tree -- Kirby with a copy ability -- and `Fighter_804D6540` has a real slot at
33 for exactly that: entry[32] is NULL, entry[33] is a valid
`{ list*, count }`. The converter walked `i < FT_KIND_MAX`, stopping one short,
so that entry's count stayed big-endian at `0x01000000` and the loop ran
16,777,216 times off the end of memory.

**The lesson is about the diagnosis, not the fix.** Two plausible theories
were wrong before the boring one was right:

1. *"A 6-bit field reading 33 is the MWCC bit-field bug"* (G-180/G-181). It
   fits, and `PORT_BF_BE` went on the fp+594 union. That union aliases one
   word **as bytes and as a value, from opposite ends** -- eight `u8`
   bit-fields over the first byte, a 32-bit group whose last member is the
   kind -- so reordering it moved `x594_bits`, the mask that decides which
   bones an animation drives, and every fighter's skeleton came apart. The
   owner caught it in the running game. `conv_waitanim_flags` already repacks
   that word into GCC bit positions; the field was never wrong.
2. *"Then read the console bit position directly from the byte."* Also wrong,
   and in an instructive way: **the same word arrives in both orders**
   depending on the path, so the accessor fixed one and broke the other.
   Measuring a second instance is what showed it -- one fighter had the byte
   view right, another had the value view right.

**Before "fix" the producer of a suspicious value, check whether the consumer
is entitled to it.** Printing `Fighter_804D6540[33]` took one command and
showed a well-formed entry with a byte-swapped count -- the whole answer.
A count of `0x01000000` is `1` byte-swapped; that shape means unconverted
data, never a bad index.

## G-187: find the shape when you cannot find the root

**Symptom:** `HSD_TlutLoadDesc` segfaults on a pointer like `0x03000300`
hundreds of frames into a match, and only on some RNG streams.

**Cause:** an `HSD_TexAnim` whose *pointers* were all relocated correctly but
whose `u16` counts at +0x14 were never byte-swapped, so `n_tluttbl` read 768
instead of 3 and `HSD_TObjAddAnim` walked 765 entries past the end of the
table. Correct pointers with a wrong count is the signature of a descriptor
the **relocation pass reached but the descriptor walk did not**.

**What made it hard:** the owning `Article` in `ItCo.usd` is named by nothing
in the archive -- not in any of `itPublicData`'s three article arrays, and a
relocation-table search for its offset finds no referrer at all. The game
reaches it by arithmetic the converter cannot model, so there is no root to
add.

**Fix:** when the root cannot be found, match the *shape* instead. Every
relocation target is a plausible struct start, so test the unwalked ones
against a multi-level signature -- here `MatAnimJoint` -> `MatAnim` ->
`TexAnim`, each pointer word null-or-relocation, ending in counts that are
small read big-endian with a zero low byte -- and walk the matches. Three
chained layouts agreeing by accident is not a real risk; one layout is.
A blanket "convert every relocation target" is **not** safe (P-748 showed
`conv_joint` does not validate its argument), a validated scan is.

**Pin the count, not just the absence of a crash.** The failure mode is
silence: without the scan the archive converts "successfully" and the damage
only appears in a match. `check_orphan_matanims` asserts the scan still finds
its three trees in `ItCo.usd`, so deleting it fails a test rather than a
playthrough.

## G-186: zero-latency I/O let a completion callback run before its caller finished

**Symptom:** SIGSEGV deep in the sound engine (`HSD_Synth_80389334`, a NULL
voice), hundreds of frames after anything audio-related happened, and only on
some RNG streams.

**Cause, from the bottom up:** the port's DVD read finishes in microseconds,
so `platform_pump_completions` at the end of `HSD_SynthSFXLoad`'s own critical
section delivers the load callback **inside the call**, before the caller
stores the return value:

```
lbl_80433A64[slot] = HSD_SynthSFXLoad(...);   <- assignment has not happened
                     +-- OSRestoreInterrupts
                          +-- platform_pump_completions
                               +-- fn_80026C04   <- searches lbl_80433A64
```

The callback marks the finished slot by *searching that table* for the
entrynum, finds nothing, and the loader hands out the same slot again. The
`.ssm` loads twice, both copies register the same SFX ids, and unloading one
unlinks the other's nodes -- leaving a node linked into a freed block.

**The shape to remember:** on console the read takes milliseconds, so the
assignment always wins the race. Any port that makes I/O instant turns
"obviously ordered" into "reordered", and the damage surfaces far away in
both time and code.

**Fix:** publish the entrynum before the call (`PORT_PC`), so the table is
consistent whenever the callback runs.

**What not to do:** the general fix -- hold every completion until a later
pump -- is more faithful and *breaks the game*. The engine's synchronous
loaders (`AXDriver_8038DA70`, `HSD_SynthSFXWaitForLoadCompletion`) spin on a
flag with **no pump point inside the loop**; they only work because the
completion is delivered inline by the call that issued the read. Both a
virtual-tick deadline and a generation counter deadlocked the boot at
`mode=40`. A platform whose waits have no pump point cannot also have deferred
completions: fix the caller that races, not the delivery rule.

## G-185: a deterministic clock froze the game's only source of randomness

**Symptom (owner):** after G-184 fixed the Classic shuffle, every 1P run still
played out identically -- "now it is always just link and hyrule."

**Cause:** Melee seeds `HSD_Rand` exactly once, at `gmmain.c:156`:
`*HSD_RandSeedPtr = OSGetTick()`.  Everything else is the fixed LCG
`seed * 214013 + 2531011`.  The port's `OSGetTick` reads a virtual timebase
that starts at zero and advances in fixed steps, which is *correct and
deliberate* -- tick deltas and the boot log have to be reproducible -- but it
also makes that one sample a constant, so the whole stream repeats.

**Fix:** put the entropy at the seed, not in the clock (`PortRandomSeed`,
`PORT_PC`-gated).  The tick is still read, so its clock side effect survives;
`HSD_Rand` is untouched, because it is console-exact and "fixing" it would be
a reinterpretation.

**The general shape:** a port replaces hardware with something deterministic
for good reasons, and a *single* place where the game harvested hardware
noise quietly becomes a constant.  When the owner reports "it is always the
same X", look for the one call that samples the machine, not for the
generator.

**Corollary -- a frozen stream hides bugs.**  Unfreezing it immediately
segfaulted the sound engine (P-752) on a path CI had never taken in months of
runs.  So tests must pin the seed rather than inherit the game's entropy:
`MELEE_RNG_SEED=tick` is the old value, applied to the whole suite with one
`ENVIRONMENT_MODIFICATION`, so a new harness is deterministic by default and
nobody has to remember.  Pinning to a *new* arbitrary constant instead would
have moved every harness onto an untested stream at once -- three tests fail
at `0x13371337` -- which is a bug report, not a baseline.

## G-184: the Classic matchup shuffle wrote past the intro buffer

**Symptom (owner):** "every single time I go in single player it's always
Yoshi on Yoshi's Island."

**Cause:** `gmClassicIntroDataBuffer` (0x80490880, 0x20) and `gm_804908A0`
(0x804908A0, 0x70) are adjacent on the console with nothing between, and
`gmClassicRuntimeData` (0x90) is exactly that region typed.
`gm_Mode_Classic_OnLoad` casts `&gmClassicIntroDataBuffer` to it and shuffles
the matchup order through `runtime[order_offset]`, where offset 0x80 is
`gm_804908A0[0x60]` -- the same bytes as the `order` argument.  It is an
ordinary Fisher-Yates written through the overlay.

`-fdata-sections` separates the two objects, so the swap read and wrote 0x60
bytes **past** the intro buffer instead of into the order array.  The order
stayed at its initial `order[i].idx = i`, so the same matchup came out every
time, and the writes landed in whatever the linker put next.

Measured: before, `order[0x60..]` reads `0 0 0 0 3 4 0 0 …`; after, a real
permutation (`5 3 6 8 0 4 7 1 2`).

**Fix:** the block treatment -- one struct with both at the console's offsets,
`STATIC_ASSERT`ed.  The assertion is the regression here: it makes the
adjacency a compile-time property, which no runtime test can do as well.

**Why this one hid.** `gmclassic.c` already had a `PORT_PC` patch for a
*different* overlay in the same file (the `scene_data` matchup tables), so it
looked handled.  A file having a patch says nothing about a second overlay in
it -- and this one has no `-Warray-bounds` signature either, because the cast
is to a complete type (G-176, P-745).

**The RNG is not the bug and should not be "fixed".** `HSD_Rand` starts at
seed 1 and advances per call, with no entropy; that is console-faithful, and
Melee's apparent randomness comes from how many calls have happened by the
time you get there.  A port that boots deterministically will repeat a
sequence when the input timing repeats, and that is correct.

## G-189: initialize alpha when expanding RGB565/CMPR endpoints

**Symptom:** foliage and waves on Yoshi's Story/Yoshi's Island, plus moving
background sprites such as the Bullet Bill, look transparent and break into
individual coloured dots. The same pattern appears across unrelated textures
and stages.

**Misleading first leads:** authored mip chains and the missing display-copy
filter were both real parity gaps fixed by P-763. Neither fixed the
owner-reported artifact, and `GrYt.dat` reproduced it with mipmapping disabled.
Disabling alpha compare and culling also left it unchanged.

**Cause:** `rgb565()` wrote only `out[0..2]`. The direct RGB565 decoder and
CMPR palette builder then copied `out[3]` as alpha, so opaque endpoint texels
received whatever byte happened to be on the stack. The affected stage
materials enable source-alpha blending, turning the random alpha into sparse
foliage, waves and sprites. This common decoder path explains why unrelated
textures and stages shared the signature.

**Fix:** every RGB565 expansion writes alpha 255. CMPR's transparent selector
still clears alpha, but retains the GX interpolated RGB rather than desktop
DXT1's transparent black.

**Regression:** `decomp_render --direct` checks all four decoded RGB565
channels and a CMPR block containing both an opaque endpoint and a transparent
selector. Full-match headless captures on `GrSt` and `GrYt` confirm the
foliage and grass are solid. P-763's separate EFB and authored-mip regressions
remain valid.

## G-190: a backtrace name can belong to a neighbouring symbol

`dladdr` resolves to the nearest *exported* symbol at or before the address,
so a static function is reported under whatever public symbol precedes it.
P-794 already fixed the half of this where the name was missing entirely; the
half that remains is worse, because a **wrong** name is confident.

P-800 arrived as `it_802B1FE8` <- `ftPk_SpecialLw_8012765C` <- Pikachu's down
special, next to a fighter dump listing **Peach and Fox and no Pikachu**. The
natural reading is that the symbols are misattributed and the real functions
belong to Peach, whose down special also spawns an article. Half an hour of
reading Peach's code would have followed.

`addr2line -f -e build/native/melee <offset>` on the `[module+0x...]` offset
the reporter prints settles it in seconds, and here it resolved all four
frames exactly -- file and line -- so the names were right.

**And the dump was right too.** `Fighter::kind` is a `FighterKind`, not a
`CharacterKind`; the two enums order the roster differently and
`Ft_Kind_Pikachu` is 12, so the fighter I read as Peach *was* the Pikachu.
Two instruments were correct and the reading of one of them was not, which is
worse than either being broken: it produced a confident theory about gobj
teardown and a Training-mode character swap, all of it fiction. The dump
prints the kind by name now. When two pieces of evidence contradict each
other, suspect the interpretation before the instruments -- and check what the
number you are reading is actually an enum *of*.

## G-191: a fixture can encode the same wrong assumption as the code it guards

`decomp_efb` asserted that `GXSetPointSize(5, ...)` produces a 5 px point, and
the backend passed the raw byte to `gl_PointSize`. Both were wrong in the same
direction -- the argument is in **1/6 pixel** units -- so the test agreed with
the bug and stayed green while every point sprite in the game rendered six
times too large (P-799).

A test written from the same misreading as the implementation proves only that
they still agree. When a fixture fails on a fix, the first question is which
of the two is wrong, and the answer has to come from outside both: here from
`psdisp.c`'s own `w = (pp->size > 42.5) ? 255.0f : 6.0f * pp->size`, where
255/6 is exactly 42.5 and names the unit beyond argument.

## G-192: count it before theorising about it

P-799 looked exactly like a texture-coordinate bug -- flat uniform squares,
and `GXEnableTexOffsets`, which is what makes GX generate a coordinate across
a point sprite, really was unimplemented. Implementing it changed nothing.
Forcing the offset on for every point draw in the frame changed nothing.

One counter in `exec_primitive` ended it: **of ~7.1M point vertices in a
400-frame match, `TEX0` is present on zero of them.** Every point sprite in the
game is untextured, so no texture-coordinate bug could be what was wrong with
them, and their size had to be. Quads, for contrast, were textured 1.19M to
5k -- the same counter proved the code path was live.

The census cost one `if` and a `printf` and replaced a day of plausible
reasoning. When an artifact has a shape that suggests a mechanism, count how
often that mechanism actually occurs before building on it.
## G-193: an attribute that one compiler honours and another ignores in silence

**Symptom:** the WebAssembly build decoded the subaction command word
`0x44000000` -- a real "play sound" -- as **opcode 4 instead of 17**, and
compiled without a warning.  Everything downstream of the command interpreter
(sounds, hitboxes, GFX, throws, item animations) was therefore running other
people's instructions, on a build that otherwise looked healthy: it booted,
rendered, played audio and held 60 fps.

**Cause:** `CMD_BE` expanded, on the host, to
`__attribute__((scalar_storage_order("big-endian")))`.  GCC implements it.
**Clang does not implement it at all** and drops it with
`-Wunknown-attributes`, which the port's `-w` hides.  There is no error, no
link failure and no runtime fault -- the bit-fields simply come out
little-endian and LSB-first.

**The trap is the class, not this attribute.** A portability fix that relies on
a *compiler extension* is only as good as the compilers you have tried, and the
failure mode when one of them declines is silent wrong data rather than a
diagnostic.  Two of these were in the tree (`CMD_BE`, `PORT_BF_BE`) and neither
announced itself when a third compiler arrived.

**Fix (P-502):** state the layout instead of asking for it.
`native/decomp/shim/decomp_cmd_bits.h` declares all 76 command structs over one
32-bit word in *host* order -- the console's fields reversed and padded to the
top -- and `CMD_U(cmd)` byte-swaps the word where it is read.  No attribute, so
every compiler agrees.  `PORT_BF_BE`'s two groups live in a single byte, where
MSB-first allocation is the whole of it and padding plus reversal is enough on
its own.

**Two things that look like fixes and are not:**

- *Reversing the field order.* Only right for a group that fills its storage
  unit; 13 of the 76 do not (G-180).  The generator pads to the top of the
  word first.
- *Byte-swapping the command stream at load*, which the P-502/W0 note proposed
  on the grounds that `sizeof(CmdUnion) == 4` makes a script "a uniform array
  of 32-bit words".  **It is not one.**  `Command_05` (Subroutine) and
  `Command_07` (Goto) carry a **relocated host pointer** in the following word
  (`lbcommand.c`), so a blanket swap corrupts every jump.  The converter also
  has no way to find where the scripts start and end -- finding them by
  accident is exactly what G-178 was.

**Regression:** `ctest bit_order` walks every field of every command struct,
122,884 reads, comparing the port's decode against the console's arithmetic.
It is attribute-free, so it runs identically under GCC and Emscripten, and
`native/tools/wasm_census.sh` runs it under node after linking.
`native/tools/gen_cmd_bits.py --check` fails if the generated header drifts
from the declarations in `<melee/lb/types.h>`.

## G-194: a diagnostic that cannot reach the screen it is diagnosing

**Symptom:** the browser build stopped dead after `[frontend] frame 0` with no
output at all. Two rounds of diagnostics were added to `devcom.c` and
`lbfile.c`, rebuilt, and run -- and produced **nothing**, which was read as
"that code never executed."

**Cause, both halves:**

1. **`OSReport` does not print.** `native/platform/os.c` writes it into a ring
   buffer and only echoes to stderr when `MELEE_LOG_REPORTS` is set. A browser
   has no environment to set it in, so every `OSReport` diagnostic went into a
   buffer that is only dumped on a panic. Use `fprintf(stderr, ...)`: the page
   shell beams `printErr` to the dev server.
2. **A busy-wait blocks the reporter.** `lbfile.c:waitForDisc` is
   `do {} while (!discIsDone());`. In a browser that pins the only thread, so
   the `fetch()` beacons carrying the log cannot run. **Anything printed after
   entering that loop is lost**, however it is printed.

**The rule that falls out:** in a browser, print *before* you block, and print
to stderr. The last line that reaches the server is then the operation that
hung, which is exactly the datum you need.

**A third trap in the same family:** the run that "produced nothing" was also
served by **someone else's dev server**. A backgrounded `python3 -m
http.server` on a taken port dies with `Address already in use` into its own
log, and `curl` then answers 200 from a stale directory. Compare the byte size
of `melee.wasm` over HTTP against the file on disk before believing any run.

## G-195: 130 function-pointer casts, and why the emulation flag is the fix

**Symptom:** `RuntimeError: indirect call signature mismatch`, thrown on the
Asyncify rewind after a disc read.

**Cause:** the decompilation stores heterogeneous callbacks in one table by
laundering them through `Event`:

```c
GObj_SetupGXLinkMax(gobj3, (GObj_RenderFunc) (Event) fn_801852FC, 0);
```

PPC and x86 ignore surplus or missing arguments. wasm's `call_indirect`
compares the callee's type with the table entry and **traps**.

**The measurement that matters.** `-Wincompatible-function-pointer-types` sees
only *implicit* conversions and reports **one** site. That one is a red
herring: it round-trips through the port's AR backend to a zero-argument call
and matches. `-Wcast-function-type-strict` also sees explicit casts and
reports **130 across 49 files**. Three rounds of "fix the one the compiler
named" were wasted before running the right census.

**Decision:** `-sEMULATE_FUNCTION_POINTER_CASTS=1` is the port's answer, not a
diagnostic to be removed. It makes wasm tolerate what the other two targets
tolerate. 130 sites of an idiom cannot be rewritten inside a tree ADR-0011
keeps read-only. The W0 handoff's "fix the three declarations and remove the
flag" rests on the undercount.

Three of them were still worth fixing, because those three would have received
**garbage** rather than merely surplus arguments (`synth.c`'s DevCom
callbacks: `(u32, uintptr_t)`, `(void)` and `(void)` handed to a
`(int, int, void*, bool)` slot). See the P-808 commit.

## G-196: the DMA alignment x86 was passing by luck

**Symptom:** `assertion "dest % 32 == 0" failed` in `devcom.c`, browser only,
`dest=0x00118c50`.

**Cause:** `static u32 hsd_SynthSFXLoadBuf[0x20 / 4];` is a DVD/ARAM DMA
destination and the hardware requires 32-byte alignment. On the console it
lands in MEM1 where the section alignment made that true, so the declaration
never had to say so. A host compiler aligns a `u32[8]` to **4** and takes
whatever address the linker gives it. GCC happened to give an aligned one for
years; wasm-ld gave a 16-byte boundary.

**The lesson is not "wasm is different".** It is that the port had a real
alignment bug on every target and only ever passed by accident. `ATTRIBUTE_ALIGN(32)`
now states it, here and on `lbl_804C4540` which has the same role.

**Where else to look:** any static buffer passed as `dest` to
`HSD_DevComRequest`, or to `DVDRead*`/`ARQPostRequest`. Heap destinations are
safe -- `HSD_MemAlloc` goes to `OSAllocFromHeap`, which is 32-aligned by
construction.

## G-197: the browser build had never been optimised

**Symptom:** the first playable browser build ran at ~34 fps with choppy audio.

**Cause:** `native/tools/wasm_census.sh` began as a "does it compile" census
and **never passed an `-O` flag**. Every browser build up to that point was
`-O0`, against a native build that is RelWithDebInfo. The frame line said
`game=6ms render=15ms` where the desktop did the same scene in
`game=8.3ms render=8.6ms` -- the simulation was *faster* than native, so the
cost was all in rendering.

**Two things bite when you fix it:**

- `wasm-opt --fpcast-emu` (the pass behind `EMULATE_FUNCTION_POINTER_CASTS`)
  **miscompiles under `-O2`** and the link dies in the validator with
  `unexpected false: call* param number must match`. Running the two as
  *separate* `wasm-opt` invocations -- emulation at link, then `-O2` as a
  second pass over the output -- validates and works. 14.4 MB -> 9.2 MB.
- Clang's `-O2` at compile time is where most of the win is anyway; the link
  optimiser is second-order.

**The real bottleneck was call count, not code quality.** See G-198.

## G-198: 24,000 GL calls a frame, and why only the browser cares

**Symptom:** `render=15ms` in the browser against `8.6ms` on the desktop, for
the *same scene and the same code*.

**Cause:** `upload_draw_uniforms` in `gx_gl.c` issues **52 `glUniform*` calls**
and runs **once per draw**. A measured frame was 461 draws: ~24,000 GL calls.
Native GL costs ~20 ns each and does not notice. In a browser every one
crosses the JS/wasm boundary at roughly half a microsecond -- ~12 ms, which was
essentially the whole of the browser's render time.

**Fix:** a shadow copy per uniform location, skipping the call when the bytes
are unchanged. Measured over 400 frames of a real match: **94.8% skipped**
(382,536 uploads issued of 7,392,788 requested). Desktop render 8.56 -> 6.15 ms;
the browser should see far more, because its calls are ~25x dearer.

**Proving it is safe is the whole job.** `MELEE_GX_UNI_CACHE=0` forces every
upload, and a seeded match renders **pixel-identical** with the cache on and
off. Keep that switch: a renderer cache that is 99% right looks fine in a
frame-time graph and wrong on one material.

**A uniform buffer object was the obvious next step and is not worth it.** With
94.8% of the calls already gone, a UBO's remaining upside is under a
millisecond, against restructuring the shader's uniform block and matching
`std140` packing by hand -- the kind of change that corrupts rendering
invisibly. The bigger remaining cost is `glBufferData` **per draw**, which
reallocates the vertex buffer 461 times a frame.

## G-199

**Symptom.** `PANIC ... dobj.c line 312`, with
`mobj has unexpected blending flags (0x31001060)` just above it, the first
time a fighter uses one particular move. Owner-reported for Samus's throw;
the stack is `ftCommon_SetAccessory` -> `HSD_JObjLoadJoint` -> `JObjLoad` ->
`HSD_DObjLoadDesc`.

**Cause.** A model the converter never walked. `DObjLoad` switches on
`mobj->rendermode & 0x60000000` and panics on the one combination the three
`case`s do not cover, so the printed number is the diagnosis: `0x31001060`
byte-reversed is `0x60100031`, an ordinary rendermode that lands on
`case 0x60000000`. **A rendermode whose reverse is a legal blending mode
means the whole descriptor tree is still big-endian**, not that the data is
corrupt.

**Why the walker missed it.** `ftData->x48_items` is walked as an `Article*`
array, and five fighters keep something else in a slot. The walk stopped at
the first slot that failed the Article test, which is correct as far as it
goes -- but everything behind that slot is then reachable by nothing. G&W's
`items[10]` already had a named-slot rule for the same reason (P-765); the
four model slots did not.

**Two things this hid, and both are general.**

1. **`MELEE_UNWALKED` cannot see an unwalked joint tree.** It only reports a
   relocation target whose *own first word is a relocation* -- its test for
   "descriptor rather than payload". An `HSD_JObjDesc` root starts with
   `class_name`, which is NULL on every joint in a fighter archive, so the
   whole tree is invisible to the report. `PlSs.dat` listed **zero** unwalked
   descriptors while carrying this bug. Do not read a clean `MELEE_UNWALKED`
   as "this archive is fully walked".
2. **`MELEE_NO_ASSET_CACHE=1` or the converter does not run at all.**
   `hsd_asset_convert` checks `~/.cache/melee/assets` *before* converting, so
   a probe added to a walker prints nothing and the walker looks dead. This
   costs the same fifteen minutes every time; the house rule about bumping
   `HSD_CONVERTER_VERSION` is the other half of it.

**Fix.** Name the slots (`ft_x48_named_slot`) and walk them, and clamp the
array with `next_public_after`. The clamp is worth having on its own: in
**every one of the 21 fighters that reach it**, the array ends exactly at the
next public symbol, and that symbol is `ftData` itself -- so without the
clamp the loop's last act is to offer `ftData->x0` to the Article test and
rely on a heuristic to reject it. P-815.

## G-200: a hash index cleared out of step with the table it indexes

**Symptom.** None, except frame time. The owner reported ~38 fps with a full
house (`game=13.8ms render=12.2ms draws=780 verts=145k`) while two players ran
fine. A steady-state profile put 7.5% of all cycles in `frame_tex_find`'s
probe loop and `tex_key_equal`'s `memcmp`.

**Cause.** `gx_hle_begin_frame` reset `frame_tcount` but never cleared
`frame_tex_hash`. `gx_hle_discard_geometry`, three hundred lines away, always
cleared both.

**Why it was invisible.** It was never *unsafe*: `frame_tex_find` rejects
`idx >= frame_tcount`, so a stale entry is skipped rather than believed. The
guard that made it correct is exactly what stopped it ever being noticed.

**How it degraded, which is the part worth carrying.** A stale index does not
just waste a probe, it defeats the structure twice over:

1. A lookup for a **new** key stops hitting an empty slot early and instead
   probes the whole stale cluster, `memcmp`-ing 48-byte keys the entire way.
2. `GXLoadTexObj` records a key only when `frame_tex_find` hands back an
   **empty** slot. Once the table is dense with stale entries, insertions stop
   happening at all -- so the index covers less and less of the frame, which
   makes (1) worse, which is a feedback loop.

So the cost grew with how much history the table had accumulated, not with the
scene. That showed up as a **measurement** artifact before anyone read it as a
bug: run-to-run spread on a pinned seed was 0.35%, and fell to 0.02% once
fixed. **Unexplained variance in a benchmark is evidence about the program,
not noise to average away.**

**Fix.** One `memset` in `gx_hle_begin_frame`. 66.04e9 -> 53.03e9 instructions
over a 900-frame match, -19.7%, with geometry identical over all 900 frames.

**The general rule.** An index and the table it indexes have one lifetime.
If you find yourself resetting a count without resetting its index, the two
resets belong in the same function -- and if a second reset path already does
both, that path is the specification.

## G-201: the cache was not too small, it was being emptied

**Symptom.** CPU texture decoding (`decode_cmpr`, `put`) at ~7% of cycles in a
**steady-state** profile, long after loading. A working texture cache should
show zero there.

**The wrong diagnosis, which looked very good.** `MAX_GL_TEXTURES` is 256 and
`GX_HLE_MAX_TEXTURES` is 2048, so a frame can bind eight times what the cache
holds; past 256 the LRU evicts entries the same frame will ask for again. It
explains the symptom, it explains why it gets worse with more fighters, and it
is wrong. I wrote it into a task row and a message to another agent before
measuring it.

**What the counters said.** Over 300 frames: 26,187 misses, 26,187 decodes,
25,765 invalidations -- and **166 evictions**. Misses tracked invalidations
almost exactly and had nothing to do with the cache being full.

**Actual cause.** `GXInitTexObj` called the texture-invalidate hook on every
call, and HSD re-inits a texobj every time it binds a material. So the whole
working set was dropped and re-decoded once per frame: 18,198 of those decodes
were CMPR art read straight off the disc, which cannot have changed. P-685
added that hook for the opening movie's THP planes, which are `HSD_MemAlloc`'d
and CPU-updated in place. It was only ever needed for images the game
**writes to**.

**Fix.** Skip the invalidation when the image lies in a registered archive
range (`gx_hle_image_is_asset`, memoised direct-mapped and stamped with a
generation the range table bumps, because the range table has 125 entries and
this runs per `GXInitTexObj`). EFB copy destinations are deliberately not
covered -- `efb_copy_tex` invalidates its destination explicitly at all three
call sites. Decodes 26,187 -> 1,305; CMPR 18,198 -> 270, which is load time.
-37.1% instructions, two-player `render=` 4.0ms -> 1.0ms.

**Three things to carry forward.**

1. **Count the events that distinguish your hypotheses before you pick one.**
   "Cache too small" and "cache being flushed" have identical symptoms and
   opposite fixes, and one counter separates them. `MELEE_GX_TEX_STATS=1` now
   reports hits/misses/evictions/decodes/invalidations for exactly this.
2. **An invalidation hook added for one narrow case will be taken by every
   case unless you gate it.** P-685's commit message even says which case it
   was for.
3. **Keep the old behaviour behind a switch.** `MELEE_GX_TEX_INVALIDATE=all`
   restores it, which is what makes "byte-identical at frames 200/420/700" an
   A/B rather than an assertion -- the same argument as `MELEE_GX_UNI_CACHE=0`.

## G-202

**Symptom.** A soak cell fails reliably, but the `repro:` command the harness
prints beside it passes every time you run it. The bug reads as
ASLR-dependent or timing-dependent, and a session gets spent chasing that.

**Cause.** `soak.sh` runs each child with
`MELEE_MATCH_ITEMS="${MELEE_SOAK_ITEMS:--2}"` but the repro printer only
echoed the seed, the two fighters and the stage. An items-on sweep therefore
handed back a command for an **items-off match** — a different match, with a
different RNG consumption, different articles and different collisions.

**How much it mattered.** P-797's Brinstar Depths cell fails **3 runs out of
3** with `MELEE_MATCH_ITEMS=4` and **0 out of 5** without it. The previous
session recorded the bug as "ASLR-flaky, not deterministic" on the strength
of the printed command, and that conclusion was an artefact of the printer.

**Fix.** Print every variable the child was actually given. The general rule:
**a repro line is part of the instrument, and an instrument that emits a
command which does not reproduce is worse than one that emits none** — it
converts a deterministic bug into a phantom and sends the next reader looking
for nondeterminism that is not there.

**Related.** G-190 is the same shape one level up: an instrument was right
and the reading of it was wrong. Here the instrument itself was wrong, and it
was wrong in the direction that looks like flakiness.

## G-203

**Symptom.** A fighter's position becomes ~1e19 in a single frame and
`lbvector.c:397/398` asserts on the camera's position sanity check. `prev_pos`
is sane, so the value is *written*, not integrated. Reproduced on Brinstar
Depths (Roy vs Pichu) and Onett (Link vs Luigi).

**The chain, and why it is worth reading in full.** Every step was caught on a
hardware watchpoint rather than reasoned about, and **not one of the first
five steps is anywhere near the bug**:

1. the animation curve player hands `HSD_JObjSetTranslateX` **2.37e30**;
2. all six ECB bones' world matrices inherit it;
3. `mpColl_LoadECB_JObj` writes `desired_ecb.bottom.y = 1.48e29`;
4. `mpColl_80043754` does `s32 steps = x / 6.0F` on that, and the `float`->`int`
   conversion **overflows to `INT_MIN`**, so `1.0F / (steps - step)` is
   `-4.6566e-10` — exactly `-1/2^31`, which is how you recognise it;
5. `mpCollInterpolateECB` turns `ecb.bottom.y` into `-6.89e19`;
6. the floor snap adds it to `cur_pos.y`;
7. the camera asserts.

**Cause, seven steps up.** `conv_ft_data`'s per-costume TObj-index walk
iterates `for (k = 0; k < 8; k++)` over `ftData_x8_x8.xC` and used each slot
as a pointer without asking whether it is a relocation target. **Eight is the
cap, not the count.** For a fighter with fewer costumes the walk reads
ordinary data as an offset and byte-swaps `n_tobjs` u16 wherever it lands. In
`PlEm.dat` it landed on the **symbol string** at `0x18` and transposed its
first two halfwords:

```
wanted:  lPEymblem5K_Share_ACTION_WallDamage_figatree
archive: PlyEmblem5K_Share_ACTION_WallDamage_figatree
```

`ftData_80085CD8` DMAs the animation in from ARAM, parses it, then looks that
name up with `HSD_ArchiveGetPublicAddress`. The lookup is a plain `strcmp`, so
it returned NULL, `fp->x590` was left NULL, and `Fighter_ChangeMotionState`
skipped the rebind that is guarded by `if (fp->x590 != 0U)`. **The joints kept
the previous animation's `HSD_FObj`s while the DMA had already overwritten the
buffer they read from** — so the curve player decoded a new animation's bytes
with the old animation's lengths and fraction encodings. The giveaway in the
`HSD_FObj` was `ad - ad_head = 27` on a `length = 25` stream, with
`frac_value`/`frac_slope` of 0 on a track whose bytes only decode cleanly as
S16.

**Three lessons.**

- **A byte-swapped *string* is a walker bug that no struct check will catch.**
  Every field in sight was plausible; the corruption was four characters of a
  name, and it surfaced as a physics assert nine steps away.
- **`k < 8` over a costume table is the same mistake as a guessed count.** The
  file's own doctrine — bound by `c->reloc[]`, `next_pointed_at_after` or
  `next_public_after`, never by a constant — applies to inner tables too, not
  just the roots where it is written down.
- **Do not "fix" step 4.** Making the `float`->`int` conversion saturate the
  way PowerPC does turns `INT_MIN` into `INT_MAX` and trades the crash for a
  two-billion-iteration hang. It is a real port divergence and it is a
  symptom, not the bug.

**Also fixed by this.** The `lbvector.c:397` **x** twin on Onett, which had
been filed separately. One walker bound, two soak cells green. P-797.

## G-204

**Symptom.** A crash report is truncated: the signal handler's state dump
prints a line or two and then takes a second SIGSEGV of its own, so the
original crash is never described.

**Cause.** `match_boot_dump_fighters` walks GX link 5 and trusts
`gobj->classifier == HSD_GOBJ_CLASS_FIGHTER`. On the frontend a gobj passed
that test whose `user_data` was not a `Fighter`, and the dump faulted
following its `x890_cameraBox`.

**Fix.** MEM1 is a fixed 24 MB mapping at `0x80000000`, so every pointer the
dump is about to follow is checked against that window (and for alignment)
first, and a bad one is *printed* rather than followed.

**The general point.** A diagnostic that runs after something has already gone
wrong is, by construction, walking corrupt state — so it must treat every
pointer it reads as hostile. It is the one piece of code that cannot assume
its invariants hold, because it only ever runs when they have not. P-816.

## G-202: the vertex pipeline is arithmetic, not decisions or bandwidth

**Context.** After P-813/P-814 halved a match's CPU work, `render` is the whole
remaining frame cost, and a profile of a heavy scene puts ~50% of cycles in the
CPU vertex pipeline: `exec_primitive` 15.5%, `texgen_coord` 10.7%, the vertex
`memcpy` 10.6%, `read_comp` 8.2%.

Those four numbers suggest three obvious optimisations. **Two of the three are
worth almost nothing, and they were measured rather than argued:**

| Change | Reasoning that justified it | Measured |
|---|---|---|
| Hoist texgen matrix resolution out of the per-vertex loop (P-816) | two `tex_mtx_slot` classifications per coord per vertex, all loop-invariant | **-0.77%** |
| Shrink the vertex from 144 to 96 bytes (P-817) | `uv[8][3]` is 96 bytes, in-match texgen count never exceeds 3, 21 MB/frame | **-0.17%** |
| Inline `read_comp`, precompute `1/2^frac` (earlier, `gx_match_perf.md`) | a type switch and a shift per component per vertex | **0.01%, reverted** |

**Why the intuition is wrong.** A tight loop over a contiguous array is the
case modern hardware is best at: the branches are perfectly predicted, the
prefetcher covers the stride, and a bulk `glBufferData` is bandwidth the
machine has to spare. The profile attributes cycles to those lines because
that is where the *work* is, not because there is waste there. **A line being
hot does not mean it contains anything removable.**

**What this leaves.** The remaining cost is genuine per-vertex arithmetic, and
the only large win left is not doing it on the CPU at all -- transform and
texgen in a vertex shader, matrices as uniforms. That deletes all three rows
above at once, and it is the change that matters most for the browser target,
where CPU is scarcest.

**The procedural lesson.** Three profile-driven micro-optimisations, three
plausible arguments, ~1% between them. **Size the prize with a throwaway probe
before building the safe version** -- P-817 was answered in ten minutes by
changing `uv[8]` to `uv[4]` and breaking the load-time cases on purpose, which
is a measurement, not a patch. Had that been built properly first, it would
have been days of work for 0.17%.

## G-205

**Symptom.** `SIGSEGV in it_80270E30` at a wild address (`0xbffd00a0`, a stack
pointer), reproducible at frame 4518 of one soak cell.

**Cause.** `itcoll.c:815` does `temp_r29 = &it_804A0E70[index2]`, and `u32
index2` is **uninitialized**: it is assigned only inside
`if (knockback > max_knockback)`. When no damage-log entry beats the initial
`max_knockback` of `-1`, the index is whatever was left on the stack.

**Why the console survives it.** The leftover there is a register value and
the read lands somewhere inside 24 MB of mapped RAM, so retail silently
applies an arbitrary damage-log entry. On a 32-bit host the leftover is a
stack address and the dereference faults. **This is the third member of the
same family** — P-781's `FObjUpdateAnim` `default:` arm and `fn_8001E60C`'s
`fobj->next` are the others — and the family is worth naming: *an
uninitialized local that the console makes harmless because of what its stack
happens to contain.*

**Fix, and the principle.** `max_knockback == -1.0f` is an exact test for
"`index2` was never assigned" — it is the initial value, the only writer
raises it, and the comparison is strictly greater. With no entry selected
there is nothing to apply and **no correct index to invent**, so the guarded
path does nothing rather than guessing 0. Inventing a value here would trade a
crash for silently applying the wrong attacker, which is the worse failure.

**Finding more of them.** These do not show up in review, because the code
reads as if the assignment always happens. They show up when a soak runs long
enough for the "no entry matched" case to occur, which is why the 5400-frame
matrix found this and the 900-frame one never did. P-819.

## G-203: byte-identical captures are only meaningful at a fixed -O level

**What happened.** `-O3` was measured as a free 2.5% (67.65e9 -> 65.96e9
instructions on a 900-frame heavy match). Then the capture comparison that
this tree uses to prove "no behaviour change" failed on every stage tried:
0.04% to 0.27% of bytes differ, max delta 65. That is triangle edges moving by
a subpixel -- floating-point evaluation changing, not a gross error.

**It is not FP contraction.** GCC defaults to `-ffp-contract=fast`, so that is
the first suspect; `-O3 -ffp-contract=off` still differs. The remaining
candidates are `-O3`'s extra inlining changing where values round, its
vectorisation, or `-O3` exploiting undefined behaviour that `-O2` leaves
alone -- which is not a remote worry in a tree that compiles `src/` with `-w`
and already needs `-fno-strict-aliasing` because the decompilation type-puns
constantly (see the `MELEE_DECOMP_UB_OPTIONS` comment in CMakeLists).

**The consequence for how we verify things, which is the point of this entry.**
A byte-identical capture proves that *your change* did nothing, **holding the
compiler configuration fixed**. It does not prove the program is
configuration-independent, and it cannot be used to validate a codegen change
-- optimisation level, LTO, PGO, a compiler upgrade, or a different target.
For those, byte-identical is the wrong instrument and its failure is not
by itself evidence of a bug.

**So:**

- Keep proving ordinary changes with captures. It works, it is cheap, and it
  caught nothing false in this session across a dozen comparisons.
- For a codegen change, expect capture differences and decide on *magnitude
  and cause*, not on equality. 2.5% did not justify the investigation here.
- **If someone does want `-O3`, LTO or PGO, the first task is explaining the
  difference, not measuring the speedup.** The speedup is already measured and
  it is small.

## G-206

**Symptom.** Owner, from live play: "some characters feel like they have a
force field around them". Donkey Kong and Ness shove the Home-Run sandbag —
and other fighters — away from across the stage, while with every other
character you walk straight *through* the sandbag that Dolphin pushes.

**Cause.** `ftData->x50` is the fighter's player-nudge box, `Vec2 { x_offset,
radius }`. `ftCommon_8007DD7C` decides whether two fighters shove each other
with `ABS(dx) < a->x2C4.y + b->x2C4.y`, and `x2C4` is copied straight from
`x50`. The converter's walk took the array's length from `off + 0x54` —
but **`x54` is not a count**. The decompilation types it `int`, and it is a
relocation field in all 30 fighter archives; `conv_ft_data` already walks it
correctly as P-685's per-costume part-table pointer. Read as a count it gave
values like 28132, the `count <= 256` guard rejected them, and **the nudge box
was never converted for a single fighter.**

**Why it looks per-character, and why those two.** A big-endian float read
little-endian is not uniformly wrong — it depends on the bytes:

| character | on disc | bytes | as the port read it |
|---|---|---|---|
| Donkey Kong | 5.2 | `40a66666` | **2.72e+23** |
| Ness | 3.1 | `40466666` | **2.72e+23** |
| Mario | 3.3 | `40533333` | 4.2e-08 |
| Bowser | 8.0 | `41000000` | 9.1e-44 |
| Peach | 2.9 | `4039999a` | −6.3e-23 |

The only two characters whose radius ends in `66 66` reverse to `0x6666…` —
an effectively infinite radius — and **they are exactly the two the owner
named.** Everyone else reverses to a denormal, i.e. no push at all. One bug
produced two opposite-looking symptoms, which is why it read as several.

**The lesson.** *A struct field typed `int` in the decompilation is not
evidence that it holds a number.* The check that settles it is
`c->reloc[off + N]` over the real archives, and here it said "pointer" in all
30. Two walkers in the same function disagreed about the same word — one
treated `x54` as a pointer and one as a count — and that disagreement was
visible in the source the whole time.

**Watch for.** Re-enabling a mechanic that has been dead since the port began
*changes gameplay*: `decomp_match`'s pinned end position moved, which is a fix
and not a drift. Prove that kind of thing rather than asserting it — revert
only the suspect hunk, watch the old value come back exactly, then update the
pin and say why. P-824.

## G-207

**Symptom.** A new env-gated probe prints nothing when the owner runs it on
the real game, while working in every headless test — so the capture comes
back empty and it reads as "the code path never ran".

**Cause.** Arming a probe takes **two** edits, and only one is obvious.
`match_boot_init` installs the frame hook for whatever variable it sees, but
on the frontend path `viewer_main.c` only *calls* `match_boot_init` at all
when one of a hardcoded list of variables is set:

```c
if (getenv("MELEE_TITLE_TEST") != NULL ||
    getenv("MELEE_CPU_TEST")   != NULL || ... )
{
    match_boot_init(0);
}
```

A probe missing from that list is silently dead in exactly the place the
owner runs it. Every harness test still passes, because `melee_decomp_boot`
takes the other branch.

**How it was caught.** The owner's capture had no probe lines at all, not even
the harmless ones the probe emits for ordinary walking — and "not even the
boring ones" is the tell. Running the frontend headlessly with the variable
alone reproduced it, and adding `MELEE_STUCK_TRACE=1` made it fire, which
isolated the list as the difference in one step.

**Rule.** When you add a probe variable, add it to that list **in the same
commit**, and validate it the way the owner will run it —
`SDL_VIDEODRIVER=offscreen ./build/native/melee --frontend ...` — not only
through `melee_decomp_boot`. A diagnostic that works everywhere except where
it is needed costs a round-trip with the person who has the only repro. P-780.

## G-208

**Symptom.** A fighter freezes for good in a state it should leave after about
a second. Owner-reported: pick up the Home-Run bat, dash-swing, and the
character never recovers while the rest of the game runs on.

**How it was found, and the step that mattered.** The ordinary stuck detector
stayed silent, because it requires `motion_id` *and* `cur_anim_frame` frozen
together and here the animation counter kept moving. A probe keyed on the
motion id alone (`MELEE_ANIM_STALL`) printed the census that settled it in one
run:

```
[anim-stall] slot 0 kind 0 motion_id=127 for 90 frames
  part 2 ... flags=0x00000000 curr=0.00 end=46.00 rate=0.00
```

**`rate=0.00` is the whole diagnosis.** The animation was bound correctly
(`end=46`) but its framerate was zero, so `curr_frame` never left 0,
`AOBJ_NO_ANIM` was never set, and `ftAnim_IsFramesRemaining` — which is just
"does any part still have a live AObj" — stayed true forever. The swing's
only exit is that call.

**Cause.** `Fighter_804D654C` is `pData[2]` of `PlCo.dat`'s
`ftLoadCommonData`, the `float[6][5]` item-swing animation-speed table, and
`conv_ft_common_data` walked `pData[0]`, `[1]`, `[4]`, `[5]`, `[8]`, `[16]`
and `[20]` but **not `[2]`**. On disc the entries are `1.0` and `0.75`; left
big-endian they read **`4.6006e-41`**, the documented byte-reversed `1.0f`.

**Two things to carry forward.**

- **`rate=%.2f` printed that denormal as `0.00`**, which made it look like a
  hard zero rather than a byte-order artifact. `4.6006e-41` and `4.2e-08` are
  now three-for-three the signature of an unconverted float table
  (P-747, P-824, this one) — print enough digits, or the evidence hides.
- **A "walk the interesting members" function is a list, and lists get
  entries missed.** `conv_ft_common_data` had grown one member at a time,
  each added when something broke: `[1]` was P-779, `[8]` was P-689, `[5]`
  was P-754. Prefer auditing such a function against *every* index once over
  waiting for the next symptom. P-780.

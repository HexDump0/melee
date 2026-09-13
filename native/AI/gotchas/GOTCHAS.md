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
passthrough copy of coord 1.  The fragment stage only carries `v_uv0`/`v_uv1`
and treated coord 2 as coord 0, so the add/sub pair sampled different UVs and
the difference term never cancelled.
**Fix:** fold `order_coord` down through identity-matrix `GX_TG_TEXCOORDn`
chains onto the 0/1 varyings it aliases before snapshotting the draw
(`gx_hle.c:resolve_stage_coords`).

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

## G-104: a material can reference three TEV texture maps (base + two shadows)

**Symptom:** with two fighters on the ground only one character shadow shows;
which one depends on the material.
**Cause:** the platform's material compiles as `K0`, base texture (map 0),
fighter A shadow (map 1) and fighter B shadow (map 2).  The GL fragment path
had only `u_tex0`/`u_tex1` and silently dropped the map-2 stage.
**Fix:** added a third texture unit (`u_tex2`, `u_tex_lod_bias.z`, texmap[2]);
the vertex already carries a third texcoord (`v_uv2`).  A 1200-frame match
uses at most map/coord 2, so three is enough for the shipped content.

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

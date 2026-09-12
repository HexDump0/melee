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

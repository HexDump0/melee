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
arrays. `demo_model.c` has both (`rptr`/`rbase`).

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
`demo_texture_decode_ci`. See `learnings/gx_textures.md`.

## G-025: visibility indices are DObj indices, not PObj/batch indices

**Symptom:** wrong parts disappear when applying `FtPartsVis`.
**Cause:** the tables index `fp->dobj_list`; one DObj may own several PObjs.
**Fix:** record `dobj_index` on every `DemoModelBatch` and test visibility via
`demo_model_batch_visible`.

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
`decode_color` in `demo_model.c`).

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

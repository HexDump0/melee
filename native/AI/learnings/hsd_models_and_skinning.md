# HSD models and bind-pose skinning

Sources of truth: `src/sysdolphin/baselib/jobj.h/.c`, `dobj.h`, `pobj.h/.c`,
`displayfunc.c`. Verified numerically against `PlMrNr.dat` on the retail disc.

> Read `hsd_archive_format.md` first. Every offset below is a **file** offset;
> pointer values inside the file are data-relative (add `0x20`).

## HSD_Joint (`HSD_Joint`, 0x40 bytes)

| Offset | Type | Field |
|---|---|---|
| +0x00 | u32 ptr | `class_name` |
| +0x04 | u32 | `flags` |
| +0x08 | u32 ptr | `child` |
| +0x0C | u32 ptr | `next` |
| +0x10 | u32 ptr | union: `dobjdesc` / `spline` / `ptcl` |
| +0x14 | f32[3] | `rotation` (Euler radians, XYZ order) |
| +0x20 | f32[3] | `scale` |
| +0x2C | f32[3] | `position` |
| +0x38 | u32 ptr | `mtx` — **inverse bind world matrix**, 3x4 f32 BE |
| +0x3C | u32 ptr | `robjdesc` |

### Flag bits observed

| Bit | Value | Meaning |
|---|---|---|
| 1 | 0x2 | `JOBJ_SKELETON_ROOT` |
| 2 | 0x4 | `JOBJ_ENVELOPE_MODEL` |
| 3 | 0x8 | scale inherits parent's accumulated scale |
| 5 | 0x20 | `JOBJ_PTCL` — union `u` is particles, not DObj |
| 7 | 0x80 | lighting enabled |
| 12 | 0x1000 | `JOBJ_INSTANCE` |
| 14 | 0x4000 | `JOBJ_SPLINE` — union `u` is a spline |

Mario's root flags: `0x1005018E` (`SKELETON_ROOT | ENVELOPE_MODEL | 0x8 |
LIGHTING | 0x100 | 0x1000000`). Root transform is identity (T=0, R=0, S=1).

### Bind world matrix

`world = parent.world * HSD_MtxSRT(scale, rotation, position, parent.scale_world)`.
The exact local formula (copied from `mtx.c`, do not invent a variant):

```
sinX,cosX = sin/cos(rot.x); sinY,cosY = ...; sinZ,cosZ = ...

m[0][0] = cosZ*(vx2*cosY)
m[1][0] = sinZ*(vx1*cosY)
m[2][0] = -vx*sinY
m[0][1] = vy2*((cosZ*(sinX*sinY)) - (cosX*sinZ))
m[1][1] = vy1*((sinZ*(sinX*sinY)) + (cosX*cosZ))
m[2][1] = cosY*(vy*sinX)
m[0][2] = vz2*((cosZ*(cosX*sinY)) + (sinX*sinZ))
m[1][2] = vz1*((sinZ*(cosX*sinY)) - (sinX*cosZ))
m[2][2] = cosY*(vz*cosX)
m[0][3] = pos.x; m[1][3] = pos.y; m[2][3] = pos.z
```

`vx2,vx1,vx,...` start as `scale.x/y/z` and are adjusted only when the parent
has an accumulated scale (`parent.scale_world`):

```
t1=1/parent.sx; t2=1/parent.sy; t3=1/parent.sz
vy2 *= parent.sy*t1; vz2 *= parent.sz*t1
vx1 *= parent.sx*t2; vz1 *= parent.sz*t2
vx  *= parent.sx*t3; vy  *= parent.sy*t3
```

Accumulated scale: `flags & 8` inherits the parent's accumulated scale;
otherwise it is `scale * parent.scale_world`. The root's accumulated scale is
its own scale. (`HSD_JObjMakeMatrix`, verified.)

Concat for 3x4 affine matrices:

```
out[i][j] = sum_k a[i][k]*b[k][j]        (j = 0..2)
out[i][3] = a[i][3] + sum_k a[i][k]*b[k][3]
```

Point transform: `p' = M * p` with translation in column 3.

### Inverse-bind identity check (how to re-verify)

For every joint with a non-null `mtx` field, `world_bind * mtx == I` to 1e-4.
This is the cheapest sanity check that your matrix convention is right, and it
also proves `mtx` is the inverse bind matrix. On `PlMrNr.dat` it holds for all
53 joints that have one.

## HSD_DObjDesc (0x10 bytes)

| Offset | Field |
|---|---|
| +0 | `class_name` |
| +4 | `next` |
| +8 | `mobjdesc` |
| +0x0C | `pobjdesc` |

## HSD_MObjDesc (0x18 bytes)

| Offset | Field |
|---|---|
| +0 | `class_name` |
| +4 | `rendermode` |
| +8 | `texdesc` (TObjDesc chain) |
| +0x0C | `mat` -> `HSD_Material` |
| +0x10 | `renderdesc` |
| +0x14 | `pedesc` |

`HSD_Material`: `ambient` GXColor +0, `diffuse` +4, `specular` +8,
`alpha` f32 +0x0C, `shininess` f32 +0x10. `GXColor` is RGBA byte order.

## HSD_PObjDesc (0x18 bytes)

| Offset | Field |
|---|---|
| +0 | `class_name` |
| +4 | `next` |
| +8 | `verts` -> `HSD_VtxDescList[]` |
| +0x0C | `flags` u16 |
| +0x0E | `n_display` u16 — display list length in **32-byte blocks** |
| +0x10 | `display` -> GX display list |
| +0x14 | union: `joint` / `shape_set` / `envelope_p` |

PObj flags: `POBJ_ANIM` 0x8, `POBJ_SKIN` 0x0000, `POBJ_SHAPEANIM` 0x1000,
`POBJ_ENVELOPE` 0x2000, `POBJ_CULLFRONT` 0x4000, `POBJ_CULLBACK` 0x8000.
`pobj_type = (flags >> 12) & 3`. Melee fighters use `POBJ_ENVELOPE`.

The display list length is `n_display * 32` bytes, matching
`GXCallDisplayList(pobj->display, pobj->n_display << 5)`.

## HSD_VtxDescList (0x18 bytes each, NULL-terminated)

| Offset | Type | Field |
|---|---|---|
| +0x00 | u32 | `attr` (GXAttr) |
| +0x04 | u32 | `attr_type` (GX_DIRECT=1, GX_INDEX8=2, GX_INDEX16=3) |
| +0x08 | u32 | `comp_cnt` (GXCompCnt) |
| +0x0C | u32 | `comp_type` (GX_U8=0, GX_S8=1, GX_U16=2, GX_S16=3, GX_F32=4) |
| +0x10 | u8 | `frac` (fixed-point fractional bits) |
| +0x11 | u8 | padding |
| +0x12 | u16 | `stride` (bytes between array elements) |
| +0x14 | u32 ptr | `vertex` array (data-relative; 0 = start of data) |

Terminator: `attr == 0xFF`. Relevant GXAttr values: `PNMTXIDX=0`,
`TEX?MTXIDX=1..8`, `POS=9`, `NRM=10`, `CLR0=11`, `CLR1=12`, `TEX0=13`,
`TEX7=20`, `NULL=0xFF`.
Component counts: POS cnt 0=XY/1=XYZ; NRM 0=XYZ/1=NBT; CLR 0=RGB/1=RGBA;
TEX 0=S/1=ST.

### Descriptor sets found in `PlMrNr.dat`

| Set | Entries | Bytes/vertex |
|---|---|---|
| 0xB760 (63 PObjs) | PNMTXIDX direct; POS idx16; NRM idx16; TEX0 idx16 | 7 |
| 0xB6E8 (2 PObjs) | PNMTXIDX direct; TEX0MTXIDX direct; POS idx16; NRM idx16 | 6 |
| 0xB658 (2 PObjs) | PNMTXIDX; POS; NRM; TEX0; TEX1 | 9 |
| 0xB5F8 (1 PObj) | PNMTXIDX; POS; NRM | 5 |

Actual data layout for the 7-byte set:
- POS: `S16 XYZ`, `frac=11`, `stride=6`, base = data+0. So `pos = s16/2048`.
- NRM: `S8 XYZ`, `frac=6`, `stride=3`, base = data+0x3E60. So `nrm = s8/64`.
- TEX0: `S16 ST`, `frac=13`, `stride=4`, base = data+0x5FA0. So `uv = s16/8192`
  (already normalized 0..1 for these models; verified range u[-0.01,1.0]).

## Envelope groups and bind-pose skinning

For `POBJ_ENVELOPE`, `PObjDesc.u.envelope_p` is an array of `u32` pointers,
**NULL-terminated**. Each pointer targets a group, itself an array of
`{ u32 joint; f32 weight }` entries terminated by `joint == 0`. Groups map to
GX position-matrix slots in order, max 10.

`HSD_PObjSetupMtx` / `SetupEnvelopeModelMtx` semantics at rest (`right == NULL`):

- If the group's **first** envelope weight `>= 1 - FLT_EPSILON`:
  matrix = `joint.world` (the joint's bind world matrix).
- Otherwise: matrix = `sum_i weight_i * (joint_i.world * joint_i.inverse_bind)`,
  which is **the identity at bind pose** because `world * inverse_bind == I`.
- If `right != NULL` (envelope-model node, not the skeleton root): additionally
  concat `right`. Implemented in `hsd/model.c` (`joint_right`).
- Blended groups are `sum_i weight_i * (M_i * inverseBind_i)`.  With
  `M_i * inverseBind_i == I` at bind, the matrix is `(sum of weights) * I`.
  Most groups sum to 1, but not all: do not assume identity.  (A group whose
  first weight is 1 is rigid and uses only the first joint, ignoring the rest.)

**Group selection:** the vertex's `PNMTXIDX` attribute is a GX matrix slot:
`GX_PNMTX0..PNMTX9` live at indices `0, 3, 6, ..., 27`. Therefore

```
group_index = pnmtxidx / 3
```

Getting this wrong (using the raw byte) was the cause of the "spiky blob"
render. Also apply the same matrix to the normal (rotation part + normalize).

### The `right` matrix (implemented)

`_HSD_mkEnvelopeModelNodeMtx(m)` for the current joint `m`:

- `m` has `JOBJ_SKELETON_ROOT` -> NULL (no `right`).
- Otherwise find `x` = nearest ancestor of `m` (including `m`) flagged
  `JOBJ_SKELETON` or `JOBJ_SKELETON_ROOT`:
  - `x == m`: `right = inverse(E_x) = bind_world(m)`.
  - `x` is skeleton root: `right = inverse(x.bind) * m.bind`.
  - otherwise: `right = inverse(x.bind * E_x) * m.bind`, which is `m.bind`
    because `x.bind * E_x == I`.

At bind pose the group matrices are identity for blended groups and `M_j * E_j`
for rigid groups, so the final vertex transform reduces to:

```
right != NULL  -> v' = right * v              (any group)
right == NULL  -> v' = M_j * v  (rigid)  or  v' = v  (blended)
```

This is what places Link's sword and shield on his back instead of the floor.
Applying `right` to PObjs on the skeleton root would break Mario; the root
check is essential.

## Joint flags

`HSD_JObjDispDObj` skips a joint's DObjs when `flags & JOBJ_HIDDEN` (1 << 4),
but still recurses into its children.  Model archives use this for duplicate
or helper pieces; the port must honour it or hidden geometry shows through
(Mario's cap decal was being cut by a hidden overlapping piece, and much of
Mr. Game & Watch's extra geometry is hidden this way).

Other flags used by the port: `JOBJ_SKELETON` 1<<0, `JOBJ_SKELETON_ROOT` 1<<1,
`JOBJ_ENVELOPE_MODEL` 1<<2, `JOBJ_CLASSICAL_SCALE` 1<<3, `JOBJ_PTCL` 1<<5,
`JOBJ_INSTANCE` 1<<12, `JOBJ_SPLINE` 1<<14.

## PObj types (`pobj->flags & 0x3000`)

| Type | Value | Bind transform |
|---|---|---|
| `POBJ_SKIN` | 0 | `u.joint == NULL`: current joint's world. Otherwise two slots: PNMTX0 = current joint, PNMTX1 (`PNMTXIDX == 3`) = `u.joint` world |
| `POBJ_SHAPEANIM` | 1 | current joint's world (shape sets select vertex data) |
| `POBJ_ENVELOPE` | 2 | `right` if non-root, else the per-vertex envelope group matrix |

`right` applies **only** to envelope PObjs; skin/shapeanim use the current (or
shared) joint directly.  Mixing this up breaks characters that use skin PObjs
(Kirby, Link, Game & Watch, Captain Falcon).

## Material render modes

`HSD_MObjDesc.rendermode` (at `mobj+4`) carries the `RENDER_*` bits.  The port
currently honours the depth-related ones while drawing a batch:

| Bit | Value | Effect |
|---|---|---|
| `RENDER_ZMODE_ALWAYS` | 1<<27 | `glDepthFunc(GL_ALWAYS)` (decal over depth) |
| `RENDER_NO_ZUPDATE` | 1<<29 | `glDepthMask(GL_FALSE)` |
| `RENDER_XLU` | 1<<30 | transparent material (alpha from the material) |

## Part visibility (faces)

Model archives contain no default visibility.  The fighter's `ftData` has a
`FtPartsDesc`/`FtPartsVis` table that `ftParts_800749CC` uses: every listed
DObj starts hidden and the neutral variant (slot 0, variant 0) is shown.  The
port parses this in `hsd/parts.c`.  See `fighter_data.md` for the layout.

## Verified numbers (`PlMrNr.dat`)

- Joints with geometry: 1 (the root; all 68 PObjs hang off it).
- Triangles: 6328. Textures: 30. Objects: 68.
- Bind-pose bounds after skinning: `[-7.56 -0.28 -2.70] .. [7.57 14.21 3.58]`.
- Raw unskinned bounds (for comparison): `[-3.36 -1.58 -2.97] .. [3.36 9.58 2.92]`.

## Animation (not yet implemented)

`Pl*.dat` archives contain animation joints (e.g.
`PlyMario5K_Share_matanim_joint`, `..._animjoint`). HSD `AObj` curves drive
joint rotation/translation/scale. Implementing this is P-201: evaluate the
curves per frame, recompute world matrices, and stop baking a single display
list. The matrix math above stays the same; only the per-joint values change.

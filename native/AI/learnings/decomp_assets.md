# Host-endian asset conversion rules for the compiled port (P-605, S3 prep)

Written 2026-09-11. This is the format reference for the S3 asset pipeline: the
compiled decompilation under `src/` reads disc data as big-endian (BE) GameCube
structs, but the port runs on little-endian x86. This file records, per
structure, what has to be byte-swapped, what must **not** be touched, and how
pointers/offsets are handled.

## S3 implementation notes (2026-09-12, P-609)

### Stage `map_head` (P-619, converter version 4)

`Gr*.dat` has no `_joint` root; its geometry and scene objects live under the
`map_head` public symbol, which is *not* a nested archive — it is the
`UnkStageDat` struct from `src/melee/gr/types.h`:

```
+0x00 unk0   +0x04 unk4
+0x08 maps   +0x0C map_count      -> HsdStageMap[map_count] (0x34 each)
+0x10 ...    (spline/gobj lists, not converted; stage logic only)
UnkStageDat_x8_t (0x34):
+0x00 HSD_Joint*            +0x04 HSD_AnimJoint**
+0x08 HSD_MatAnimJoint**    +0x0C HSD_ShapeAnimJoint**
+0x10 HSD_CameraDescPerspective*   +0x14
+0x18 LightList** (desc+anims, NUL-terminated)  +0x1C HSD_FogDesc*
+0x20 GrJoint* +0x24 count  +0x28 +0x2C +0x30
```

The converter walk (`conv_stage_maphead`) converts the numeric fields,
`conv_joint` for every map's joint tree, the anim-joint chain, the camera
(`conv_cobjdesc`), the light desc chain and the fog.  Light-list animations
(`LightList.anims[0]` -> `HSD_LightAnim` -> `WObjAnim.aobjdesc`) and
`POBJ_SHAPEANIM` shape sets (`PObjDesc.u.shape_set`) have their own walks
(`conv_aobjdesc_ref`, `conv_shapesetdesc`); see G-074.  AObjDesc.obj_id is a
JObj offset for light/WObj tracks, so `conv_aobjdesc_ref` converts the
referenced joint tree.

Map ids are the stage's Ground GObjs: `Ground_GetStageGObj(map_id)` runs once
per id and each is drawn (foreground platform, background/sky layers).  The
viewer loads them all (`hsd_scene_load_stage_all`) and uses the smallest-bounds
map as the camera/lights/fog source; `--stage-map N` isolates one layer.
Placeholder entries use `0xffffffff` sentinels.

The shipped converter is `native/decomp/assets/hsd_convert.c`; this file stays
the per-structure reference.  Key differences from the §7 recommendation:

- **Relocation-table-first.**  Every relocation entry names a pointer field
  (that is what `archive.c:Locate` assumes), so the converter swaps all of
  them up front.  That makes the whole pointer graph traversable even for
  classes the class walks do not know, and turns "no desync" into a testable
  invariant: `stats.reloc_valid == stats.reloc_total`.
- **Class walks add the numeric fields.**  Each walker converts only
  non-pointer u32/f32/u16 fields of the structures in §2/§3 plus scene data
  (`SceneDesc` models/cameras/lights/fogs, `HSD_CObjDesc`, `HSD_LightDesc`
  and its union, `HSD_FogDesc`, `HSD_WObjDesc`) so compiled loaders see host
  order.  Fields listed in the relocation table are skipped (`conv_u32`
  checks `c->reloc`); byte-defined ranges are simply never written.
- **In-place + cache.**  Conversion runs on the caller's buffer (the
  `HSD_ArchiveParse` shim renames the symbol; see `decomp_shim.h`), so every
  shared list needs an idempotence mark (G-066).  A FNV-1a hash + converter
  version keys a disk cache under `$MELEE_ASSET_CACHE`,
  `$XDG_CACHE_HOME/melee/assets` or `~/.cache/melee/assets`; bump
  `HSD_CONVERTER_VERSION` on any semantic change (G-067).
- **Sweep evidence.**  `ctest decomp_assets` loads 33 `Pl*Nr.dat` + `GrNBa` +
  `MnSlChr` + `IfAll` + `NtMsgWin` with full reloc coverage; the boot loads
  `NtMsgWin.dat`/`SdMsgBox.usd` and stops at the memory-card/pad wait.
- **Name dispatches are exact.**  `convert_roots` keys special walks on the
  public symbol name; a wrong length or literal silently leaves the table
  big-endian (the `tyModelFileTbl`/`tyModelFileUsTbl` branches tested lengths
  15/17 for 14/16-character names, so the results screen resolved every
  character to entry 0; converter v73, G-126).  A table walk that does not
  `mark` the table can also be reached by two symbols — check overlap.
- **Scan the unknown roots.**  Every public that no branch matches increments
  `stats.roots_unknown`.  A scratch pass over all 1,209 disc archives with a
  temporary `[unknown]` print is the cheapest way to find unwalked tables;
  converter v74 added `_modelset` (credits name models) and
  `gmKumiteSystemTable*` (Stadium spawn rows) that way (G-127).  Remaining
  known unknowns: `sqEventInitDataLevelTbl` (event levels + `StartMeleeRules`
  bitfields), `gmIntroEasyTable`, `standScene`/`cut*Scene`, `dbLoadCommonData`,
  and the per-fighter `ftDemo*MotionFile*` strings (no walk needed).
- **Sweep every archive for reloc integrity.**  `test_decomp_assets` converts
  all 861 HSD archives on the disc and checks every relocation field against
  the raw big-endian value plus the data base, and each `Ef*Data.dat`
  descriptor count.  This found the effect-desc overrun and the unaligned
  writes behind G-128; add the archive to the sweep before adding a bespoke
  check.
- **Bounds arithmetic must not wrap.**  `in_data` used `off + need`, which
  wraps on 32-bit; all offsets come from archive data and can be arbitrary.
  Compare `off <= data_size && need <= data_size - off` instead, and refuse
  unaligned offsets in `conv_u32`/`conv_u16` (descriptor fields are aligned,
  and an unaligned write cuts across adjacent pointer fields).
- **Stage `yakumono_param` layouts are per-stage.**  The converter's Zebes
  path only handles `desc == off - 0x24`; GrYt (Yoshi's Story) is
  `YorsterParams` (`{f32 x00..x0C; s32 x10..x1C}`, gryorster.c:61) and is now
  selected by the archive's `GrdYorster*` publics (converter v76, G-130).
  Every other stage that reads these parameters as numbers still needs its
  own struct-to-layout mapping (P-662); do not convert the words generically
  (several stages store sub-structure offsets/packed bytes there).
- **Effect descriptor arrays have no stored count.**  `EF_EffectDesc[]` runs
  from `DataTable + 8` to the first particle bank (`DataTable + 0`/`+4`); when
  both banks are null it ends at the first entry with no relocation-backed
  model pointer.  `stats.effect_descs` reports the walked count so the asset
  sweep can pin it.
- **Pointee walks need their own walker.**  A converted pointer field is not a
  converted payload.  Every `ftData` sub-table was walked except `x58`
  (`ftData_x58_t`: two `u8` leg-part indices plus three f32 IK lengths), so
  the raw BE floats reached `ft_80089B08` and made Link's leg IK NaN (P-627,
  G-096).  When adding or auditing a walk, enumerate every pointer field's
  numeric pointee; converter v58 adds `x58+4/0xC/0x18`.  `ftData->x1C` is the
  same class: its relocation-backed, bounded pointer table leads to descriptors
  whose `u16` first-part/count fields need conversion.  Converter v69 walks the
  leading relocation entries (at most the five `Fighter.x8B0` slots) and stops
  at the first non-relocation field (P-630/G-119).  Converter v71 adds the
  `x40` `itPickup` floats and the `x4C_sfx` `FtSFX` ints/`FtSFXArr` tables
  (G-124).  Converter v72 adds the `x48_items` per-fighter special `Article`
  arrays (G-125): a leading run of relocation-backed entries with legal NULL
  holes, stopped at the first non-NULL non-relocated slot, and only walked
  entry-by-entry when the target's `attr` passes an `ItemAttr` sanity check
  (the tail of the array is unrelated pointer data for several fighters).

### `.ssm` sound banks (S3 scope: make the compiled loader run)

Not an HSD archive.  Probed from `audio/us/main.ssm`:

```
+0x00 u32 header_size        stream table size in bytes
+0x04 u32 sample_data_size   bytes of ADPCM sample data
+0x08 u32 group_count
+0x0C u32 base
+0x10 .. +0x10+header_size   stream table: { u32 n; n * 0x40-byte entries }
+0x10+header_size .. EOF     ADPCM sample data (byte stream, never swapped)
```

`HSD_SynthSFXHeaderLoadCallback` reads the first 0x20 bytes into
`hsd_SynthSFXLoadBuf`, then the metadata from 0x20, then the samples to ARAM
from `header_size + 0x10`.  `platform/ssm.c` converts the four header words and
each record's `n` (the group copy size depends on it); the 0x40-byte entry
fields still hold big-endian sample offsets and are patched with host
arithmetic in the callback.  S5 must convert those entry fields and validate
the sample headers before the mixer trusts them.

`smash2.sem` is still a stub (`AXDriver_8038DA70`, S5).


Sources of truth, in order:

- `src/sysdolphin/baselib/archive.c` / `archive.h` (header + relocation),
- the descriptor structs and their loaders: `jobj.c`, `dobj.c`, `mobj.c`,
  `pobj.c`, `tobj.c`, `aobj.c`, `fobj.c`, `src/melee/lb/lbanim.c`,
- the hand parser `native/hsd/model.c` + `native/hsd/aobj.c` (the oracle for
  every offset below; citations are to these unless stated),
- the S0 probe `native/tests/test_decomp_hsd.c` and `learnings/decomp_port.md`
  §6 (the validated prefix-swap recipe),
- `learnings/gx_textures.md`, `gx_display_lists.md`, `hsd_archive_format.md`,
  `hsd_animation.md`, `hsd_models_and_skinning.md` for the data layouts.

Product constraint: the compiled build is **32-bit** (ADR-0012) because
`archive.c:Locate` writes host pointers into 4-byte slots; see "64-bit" below.

## 0. Ground rules

1. The archive buffer is BE. `HSD_ArchiveParse` (`archive.c:18-66`) `memcpy`s
   the 0x20-byte header over a host `HSD_Archive` and compares
   `header.file_size` to the host file size, so the header u32s must be host
   order before parsing.
2. All pointers inside the data section are 32-bit **data-relative offsets**.
   `Locate` (`archive.c:7-16`) turns each relocation-table entry into a host
   pointer with `*ptr += (u32)archive->data`; `archive->data` is the host
   buffer base (`archive.c:31`). The pointer fields must reach `Locate` as
   host-order u32 offsets, and no such field may already contain a host
   pointer.
3. `HSD_ArchiveGetPublicAddress` (`archive.c:70-83`) `strcmp`s names from the
   symbols blob, which is therefore a byte blob and must not be swapped.
4. Conversion happens on a private copy (the buffer is mutated by `Locate` and
   by any in-place fixups); a cached converted image must be copied back before
   each `HSD_ArchiveParse` because `Locate` is not idempotent.

## 1. Archive header, tables, symbols

Validated counts (S0, `decomp_port.md` §6): `PlMrNr.dat` size 473522,
data 467728, reloc 1423, public 2, extern 0. The P-403 probe re-read the same
values (`0x739B2`, `0x72310`, 1423, 2).

| Region | Layout | Action | Accessor |
|---|---|---|---|
| Header 0x00..0x1F | `u32 file_size, data_size, nb_reloc, nb_public, nb_extern` (`archive.h:13-20`) | swap u32 | `HSD_ArchiveParse` `:26-37` |
| Header +0x14 | `u8 version[4]` (`"/2.0"`) | bytes; reordered cosmetically by a u32 swap, unused | `archive.h:18` |
| Data section | starts at 0x20, `data_size` bytes | mixed; see §2..§6 | — |
| Relocation table | `nb_reloc` × `u32 offset` | swap u32 | `Locate` `:12` |
| Public/extern tables | `nb_*` × `{u32 offset; u32 symbol;}` | swap u32 | `HSD_ArchiveGetPublicAddress` `:74-80` |
| Symbols blob | NUL-terminated names, byte offsets from blob start | **must not be swapped** | `strcmp` `:76` |

S0 recipe, valid for the whole structural prefix:

```c
strings_offset = 0x20 + data_size + nb_reloc*4 + nb_public*8 + nb_extern*8;
swap_u32_words(buffer, strings_offset);       /* header + data + tables only */
```

(`test_decomp_hsd.c:92-103`, `:278-292`). The loop's `i+4 <= bytes` guard
also skips a trailing partial word. All 33 `Pl*Nr.dat` archives have
4-aligned `data_size` and `strings_offset` (probe-verified), so the swap
covers exactly the header + data section + tables with no byte spill. After
this pass all descriptor u32/f32 words and all table entries are host order.

**The prefix swap alone is not sufficient.** The data section also contains
byte runs, u16 fields, and byte streams; all of those are corrupted by a u32
swap and are listed per structure below.

## 2. HSD descriptors

For every field: "u32" means the word swap is correct; "u16"/"bytes" means the
word swap corrupts it and it needs an explicit fix (or must be excluded).

### HSD_Joint (0x40) — `jobj.h:127-140`, loader `jobj.c:610-665`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | ptr | `class_name` | u32 (NULL in retail; see note) |
| +0x04 | u32 | `flags` | u32 |
| +0x08 | ptr | `child` | u32 |
| +0x0C | ptr | `next` | u32 |
| +0x10 | ptr | `u.dobjdesc` / spline / ptcl | u32 |
| +0x14 | f32[3] | `rotation` | u32 |
| +0x20 | f32[3] | `scale` | u32 |
| +0x2C | f32[3] | `position` | u32 |
| +0x38 | ptr | `mtx` (inverse bind, 12 f32) | u32 |
| +0x3C | ptr | `robjdesc` | u32 |

No u16/u8 fields. `JObjLoad` (`jobj.c:629-665`) reads every field above;
`HSD_JObjSetupMatrixSub` consumes the f32s through `HSD_MtxSRT`. `class_name`
is looked up with `hsdSearchClassInfo` at `jobj.c:617`; NULL falls back to the
default class. **Note (verified):** all class-name strings are absent from the
retail archives we checked (`PlMrNr.dat`, `PlMrAJ.dat`, `PlMrGr.dat`,
`GrNBa.dat` contain no `HSD_`, `JObj`, `TObj` bytes), and the `PlMrNr.dat` root
joint's `class_name` is 0. If a non-retail archive ever carries them, they are
byte runs and must survive as bytes.

### HSD_DObjDesc (0x10) — `dobj.h:23-28`

`class_name, next, mobjdesc, pobjdesc` are all pointers → u32. Loaded by
`HSD_DObjLoadDesc` (`dobj.c:202`) and handed to `HSD_DObjResolveRefsAll`.

### HSD_MObjDesc (0x18) — `mobj.h:105-112`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | ptr | `class_name` | u32 |
| +0x04 | u32 | `rendermode` (RENDER_* bits) | u32 |
| +0x08 | ptr | `texdesc` | u32 |
| +0x0C | ptr | `mat` → `HSD_Material` | u32 |
| +0x10 | ptr | `renderdesc` | u32 |
| +0x14 | ptr | `pedesc` → `HSD_PEDesc` | u32 |

Constant path (`HSD_MObjLoadDesc` `mobj.c:167`) plus `MObjMakeTExp`.

### HSD_Material (`mat`) — `mobj.h:82-88`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | GXColor (4 bytes) | `ambient` | **bytes: reverse the word again** |
| +0x04 | GXColor | `diffuse` | bytes |
| +0x08 | GXColor | `specular` | bytes |
| +0x0C | f32 | `alpha` | u32 |
| +0x10 | f32 | `shininess` | u32 |

`GXColor` is `{u8 r; u8 g; u8 b; u8 a;}` in memory; the archive stores it in
that order. A u32 word swap turns `r,g,b,a` into `a,b,g,r`, so the three color
words must be byte-restored. Evidence: the hand parser reads `memcpy(out->
ambient, d+mat, 4)` (`model.c:1062-1066`) and Mario's overalls diffuse, stored
as `0xB3B3B3FF`, renders gray (`gx_textures.md`, "Verified example"); if the
bytes were ARGB the port would render a wrong color.

### HSD_PEDesc (`pedesc`) — 12 bytes, `mobj.h:90-101`

All u8 (`flags, ref0, ref1, dst_alpha, type, src_factor, dst_factor,
logic_op, z_comp, alpha_comp0, alpha_op, alpha_comp1`). These are read as
bytes by `HSD_SetupPEMode` and by the hand parser at `model.c:1070-1085`
(`d[pe+0] … d[pe+11]`). A u32 word swap moves them within each 4-byte word →
must be restored (or excluded).

### HSD_PObjDesc (0x18) — `pobj.h:38-53`, loader `HSD_PObjLoad` `pobj.c:286-287` / `HSD_PObjLoadDesc` `:310`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | ptr | `class_name` | u32 |
| +0x04 | ptr | `next` | u32 |
| +0x08 | ptr | `verts` → `HSD_VtxDescList[]` | u32 |
| +0x0C | u16 | `flags` (POBJ_*) | **u16 swap** |
| +0x0E | u16 | `n_display` (32-byte blocks) | **u16 swap** |
| +0x10 | ptr | `display` → GX FIFO bytes | u32 |
| +0x14 | ptr | union `joint` / `shape_set` / `envelope_p` | u32 |

`pobj.c:286-287` stores `n_display` and `display`; `PObjDispSimplePrimitive`
(`pobj.c:1218-1225`) later issues `GXCallDisplayList` with them.

### HSD_VtxDescList (0x18 each, NULL-terminated) — `pobj.h:57-64`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | u32 | `attr` | u32 |
| +0x04 | u32 | `attr_type` | u32 |
| +0x08 | u32 | `comp_cnt` | u32 |
| +0x0C | u32 | `comp_type` | u32 |
| +0x10 | u8 | `frac` | byte |
| +0x12 | u16 | `stride` | **u16 swap** |
| +0x14 | ptr | `vertex` array | u32 |

Read in `read_descs` (`model.c:575-598`); the compiled side consumes the same
fields through `setupArrayDesc`/`setupVtxDesc` (`displayfunc.c:569-576`,
`GXSetVtxAttrFmt`/`GXSetVtxDesc`) and `GXSetArray`.

### HSD_TObjDesc (0x5C) — `tobj.h:160-180`, loader `HSD_TObjLoadDesc` `tobj.c:280`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | ptr | `class_name` | u32 |
| +0x04 | ptr | `next` | u32 |
| +0x08 | u32 | `id` (GXTexMapID) | u32 |
| +0x0C | u32 | `src` (GXTexGenSrc) | u32 |
| +0x10 | f32[3] | `rotate` | u32 |
| +0x1C | f32[3] | `scale` | u32 |
| +0x28 | f32[3] | `translate` | u32 |
| +0x34 | u32 | `wrap_s` | u32 |
| +0x38 | u32 | `wrap_t` | u32 |
| +0x3C | u8 | `repeat_s` | **byte (word fixup)** |
| +0x3D | u8 | `repeat_t` | **byte (word fixup)** |
| +0x40 | u32 | `blend_flags` | u32 |
| +0x44 | f32 | `blending` | u32 |
| +0x48 | u32 | `magFilt` | u32 |
| +0x4C | ptr | `imagedesc` | u32 |
| +0x50 | ptr | `tlutdesc` | u32 |
| +0x54 | ptr | `lod` | u32 |
| +0x58 | ptr | `tev` | u32 |

`repeat_s`/`repeat_t` scale the UVs (`MakeTextureMtx`, `tobj.c`, hand port
`model.c:959-1038`) and must survive. The port's `make_texture_mtx` reads them
at +0x3C/+0x3D (`model.c:998-999`).

### HSD_TexLODDesc (`lod`) — `tobj.h:195-201`

Declared layout: `minFilt` u32 +0, `LODBias` f32 +4, `bias_clamp` u8 +8,
`edgeLODEnable` u8 +9, `GXAnisotropy max_anisotropy` +0xC (a 4-byte enum), so
the struct is 0x10 bytes under the GC ABI. The two u8s need the word fixup.
Only four unique `Pl*Nr.dat` textures carry a `lod` descriptor and every one
is zero-filled, so nothing here is load-bearing yet. Note: the hand parser
reads `max_anisotropy` as a byte at +0xA (`model.c:1104-1111`); with all-zero
data both readings agree. Confirm the true offset on a non-zero descriptor (or
a GC capture) before the HLE depends on it — open question 9.

### HSD_TObjTevDesc (`tev`, 0x20) — `tobj.h:228-241`

16 u8 fields (+0x00..+0x0F), then `GXColor konst/tev0/tev1` (12 bytes,
+0x10..+0x1B), then `u32 active` at +0x1C. A word swap scrambles all of them;
the hand parser reads them at `model.c:1113-1135` (it bounds the read at 0x28,
a conservative over-estimate). Fix each word.

### HSD_ImageDesc (0x18) — `tobj.h:203-211`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | ptr | `image_ptr` (pixel data, no size field) | u32 |
| +0x04 | u16 | `width` | **u16 swap** |
| +0x06 | u16 | `height` | **u16 swap** |
| +0x08 | u32 | `format` (GXTexFmt) | u32 |
| +0x0C | u32 | `mipmap` | u32 |
| +0x10 | f32 | `minLOD` | u32 |
| +0x14 | f32 | `maxLOD` | u32 |

Read by the hand parser at `model.c:759-762` and by the compiled renderer at
`tobj.c:1212-1242` (`GXInitTexObjCI` / `GXInitTexObj` / `GXInitTexObjLOD`).
Census: all 967 `Pl*Nr.dat` textures have `mipmap == 0` and LOD 0..0, so the
u32 fields happen to be inert; width/height are live and are u16.

### HSD_TlutDesc (0x10) — `tobj.h:188-193`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | ptr | `lut` (n_entries × 2 bytes, BE u16 entries) | u32 |
| +0x04 | u32 | `fmt` (`GXTlutFmt`: 0 IA8, 1 RGB565, 2 RGB5A3) | u32 |
| +0x08 | u32 | `tlut_name` | u32 |
| +0x0C | u16 | `n_entries` | **u16 swap** |

`HSD_TlutLoadDesc` (`tobj.c:299-305`) memcpys the struct and `tobj.c:1205`
calls `GXInitTlutObj` with `tlut->n_entries` (cap 0x4000,
`extern/dolphin/src/dolphin/gx/GXTexture.c:584`). P-403 verified that
`max(index)+1 == n_entries` for all 52 CI textures and that counts include
non-power-of-two values (3, 8, 20, 49, 55, 157, 190, 202, 244, 255, 256), so
the u16 is real data.

### HSD_RObj / HSD_RObjDesc

Not in the minimum set but reachable from `HSD_Joint.robjdesc` and resolved by
`HSD_RObjResolveRefsAll` (`jobj.c:690`). The port's S0 pose check nulls it.
Before S3 ships, walk and convert it with the same rules (u32 pointer fields,
`f32` SRT, type/flags word fields; the Rot/Trans/Ptcl union is a mixed
byte/word structure). Open question below.

## 3. FObj / AObj animation

### HSD_AObjDesc (0x10) — `aobj.h:50-55`, loader `HSD_AObjLoadDesc` `aobj.c:179`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | u32 | `flags` | u32 |
| +0x04 | f32 | `end_frame` | u32 |
| +0x08 | ptr | `fobjdesc` | u32 |
| +0x0C | u32 | `obj_id` | u32 (either an ID-table key or a joint pointer cast to u32; `aobj.c:199-209`) |

All 32-bit — the word swap is correct for this struct.

### HSD_AnimJoint (0x14) — `aobj.h:57-63`

`child, next, aobjdesc, robj_anim` pointers + `flags` u32. All 32-bit.
`HSD_JObjAddAnimAll` walks this chain.

### HSD_FObjDesc (0x14) — `fobj.h:49-57`, loader `HSD_FObjLoadDesc` `fobj.c:468`

| Offset | Type | Field | Action |
|---|---|---|---|
| +0x00 | ptr | `next` | u32 |
| +0x04 | u32 | `length` (byte length of `ad`) | u32 |
| +0x08 | f32 | `startframe` | u32 |
| +0x0C | u8 | `type` (HSD_A_J_*) | byte fixup |
| +0x0D | u8 | `frac_value` | byte fixup |
| +0x0E | u8 | `frac_slope` | byte fixup |
| +0x0F | u8 | `dummy0` | byte |
| +0x10 | ptr | `ad` → FObj byte stream | u32 |

### The `ad` byte stream is little-endian and must NOT be swapped

`fobj.c:114-153` (`parseFloat`) assembles floats/integers byte-by-byte with
`(*pos)[1] << 8 | (*pos)[0]`, i.e. the stream is stored **little-endian by
format definition**, independent of host and console endianness:

- `frac & 0xE0`: `0x00` f32 LE (4 bytes), `0x20` s16 LE, `0x40` u16 LE,
  `0x60` s8, `0x80` u8; value = `numer / (1 << (frac & 0x1F))`.
- `parseOpCode` (`fobj.c:155-158`), `parsePackInfo` (`:160-178`),
  `parseWait` (`:190-203`) read `u8` varints.

`native/hsd/aobj.c:parse_float` (`aobj.c:33-73`) is the hand mirror and was
verified bitwise against a literal transcription of `fobj.c` (`hsd_animation.md`
§6). A u32 word swap destroys variable-length records mid-stream, so `ad`
ranges are **excluded** from the structural pass.

### FigaTree / FigaTrack (`Pl<Char>AJ.dat`) — `lbanim.h:9-23`

| Struct | Offset | Type | Field | Action |
|---|---|---:|---|---|
| FigaTree (0x14) | +0x00 | int | `type` | u32 |
| | +0x04 | u32 | `flags` | u32 |
| | +0x08 | f32 | `frames` | u32 |
| | +0x0C | ptr | `nodes` (`s8` per-joint counts, 0xFF-terminated) | u32 ptr; node bytes untouched |
| | +0x10 | ptr | `tracks` (`FigaTrack[]`) | u32 pointer |
| FigaTrack (0x0C) | +0x00 | u16 | `length` | **u16 swap** |
| | +0x02 | u16 | `startframe` | **u16 swap** |
| | +0x04 | u8 | `obj_type` | byte fixup |
| | +0x05 | u8 | `frac_value` | byte fixup |
| | +0x06 | u8 | `frac_slope` | byte fixup |
| | +0x08 | ptr | `ad_head` → stream | u32 pointer; stream untouched |

`lbAnim_InitFrames` (`lbanim.c:9-36`) copies `length/startframe/obj_type/
frac_value/frac_slope/ad_head` straight onto `HSD_FObj`, so these u16s feed
`parseFloat` directly.

Probe validation (first sub-archive of `PlMrAJ.dat`, `Wait1`): the public
symbol sits at file 0xE88 with `type=1`, `flags=0`, `frames=50.0`,
`nodes=0xE28`, `tracks=0x8F4`; track 0 is
`length=5, startframe=0, obj_type=2, frac_value=0x66, frac_slope=0x88,
ad_head=file 0x20` with stream bytes `11 00 32 00 00`. `ad_head` is data offset
0, i.e. the very first bytes of the data section — exactly the bytes a prefix
swap would destroy.

## 4. GX display lists

A PObj's `display` buffer is a GX FIFO byte stream; `n_display` is in 32-byte
blocks (`GXCallDisplayList(pobj->display, pobj->n_display << 5)`,
`pobj.c:1225`; `PObjDispSimplePrimitive` `pobj.c:1218-1225`). Layout
(`learnings/gx_display_lists.md`, verified in `model.c:read_vertex`):

```
[u8 opcode | u16 BE vertex count] [attributes in HSD_VtxDescList order ...]
```

- opcode low 3 bits = vertex format, primitive = `opcode & 0xF8`; `0x00`
  terminates;
- counts are **u16 big-endian** (`rb16` in `model.c:1237`);
- indexed attributes are `u8`/`u16` BE indices; direct 16-bit/f32 components are
  BE; matrix indices are always one byte; vertex colours use the colour enum;
- `VtxDescList.vertex` points at separate vertex arrays (POS s16, NRM s8/16,
  TEX s16, …) that the compiled `GXSetArray` path consumes.

**Why a whole-word swap is wrong:** the stream is byte/packed-field defined.
Reversing each 4-byte word moves the u8 opcode and count halves across word
boundaries (e.g. `98 00 04 …` becomes `… 04 00 98`), so the consumer desyncs
on the first record; packed 4x4 blocks, index bytes and colour bytes inside a
word are permuted as well. The same applies to the vertex arrays (s16 data
would become byte-swapped pairs at shifted positions depending on stride).

**What the GX backend reads:** the compiled engine sets state from
`HSD_VtxDescList` (attr/type/cnt/comp_type/frac/stride/vertex) via
`setupArrayDesc`/`setupVtxDesc` (`displayfunc.c:569-576`) and then calls
`GXCallDisplayList`; the port's GX HLE must parse the FIFO and arrays as BE,
exactly like `native/hsd/model.c` (`rb16`/`rb32`) and `native/gx/texture.c`
(`be16`). Recommendation: leave display lists and vertex arrays as BE and make
the HLE BE-aware, instead of offline-converting them.

## 5. Textures and TLUTs

`HSD_ImageDesc.image_ptr` points at tiled pixel data with **no byte-size
field**; the size is implied by format+dimensions (and the hand parser passes
`n - image` so the decoder validates). A u32 word swap must not touch it.
Component/tile layouts and their BE reads (all in `native/gx/texture.c`; the
same bytes are what `GXInitTexObj`/GX HLE must consume):

| Format | ID | Storage | BE detail |
|---|---:|---|---|
| I4 | 0 | 8x8 blocks, 4bpp | nibbles, high first (`decode_i4` `:55`) |
| I8 | 1 | 8x4 blocks | 1 byte/texel (`decode_i8` `:74`) |
| IA4 | 2 | 8x4 blocks | 1 byte/texel: low nibble intensity, high nibble alpha (`decode_ia4` `:88`) |
| IA8 | 3 | 4x4 blocks | alpha byte then intensity byte (`decode_ia8` `:101`) |
| RGB565 | 4 | 4x4 blocks | u16 **BE** (`decode_16` `:111` → `rgb565` `:23`) |
| RGB5A3 | 5 | 4x4 blocks | u16 **BE** (`rgb5a3` `:31`) |
| RGBA8 | 6 | 4x4 blocks | 16 AR byte pairs then 32 GB bytes (`decode_rgba8` `:122`) |
| CI4 | 8 | 8x8 blocks, 4bpp | nibble indices; TLUT u16 BE (`gx_texture_decode_ci` `:196`) |
| CI8 | 9 | 8x4 blocks | byte indices; TLUT u16 BE |
| CMPR | 14 | 8x8 (4 sub-blocks) | `c0,c1` u16 BE + 16 index bytes (`decode_cmpr` `:136`) |

TLUT data (`HSD_TlutDesc.lut`): `n_entries` BE u16 entries; expansion is
format-aware (`decode_palette_entry` `model.c:705-731`: `0` IA8, `1` RGB565,
`2` RGB5A3). u16-swap the entries if converting, or read BE in HLE — never
u32-swap (that reverses entry pairs and swaps neighbours).

P-403 census relevance: CMPR + CI8/CI4 (+RGB565/RGB5A3 TLUTs) are 96.7% of the
967 character textures; RGBA8/RGB5A3/I4/I8 are the remainder; IA4/IA8/RGB565
image maps and IA8 palettes are unobserved in `Pl*Nr.dat` (see
`gx_textures.md`). All observed `mipmap` flags are 0.

## 6. REL and other file types

**None found.** The FST holds data-only HSD archives; no REL code module is
loaded at runtime and the platform surface has no REL loader
(`decomp_port.md` §3: "No REL code modules are loaded at runtime; disc assets
are data-only HSD archives"). S0 exercised a plain `PlMrNr.dat` sub-archive and
`Pl<Char>AJ.dat` is a concatenation of HSD sub-archives, each independently
`HSD_ArchiveParse`-able (`hsd_animation.md` §1). If a future asset type
appears, it needs its own rule set; nothing in S3's scope requires REL
conversion today.

## 7. Recommended pipeline

Requirements it must meet: (a) `HSD_ArchiveParse`/`Locate` still run on the
converted buffer, (b) every descriptor field reaching compiled code is host
order, (c) byte-defined data (FObj streams, display lists, vertex arrays,
textures, palettes) is bit-exact, (d) reusable/cacheable per ROADMAP S3.

**Recommended: offline converter + per-load `Locate`, built as a descriptor
walk.** Concretely:

1. **Copy** the archive bytes and parse the header/tables read-only to learn
   `data_size`, `nb_reloc`, `nb_public`, `nb_extern`.
2. **Graph walk** the same paths the loaders walk: public `*_joint` /
   `*_matanim_joint` roots (`*_animjoint` where present; AJ archives use
   `*_figatree`) → `HSD_Joint` → `HSD_DObjDesc` →
   `HSD_MObjDesc`/`HSD_PObjDesc` → `HSD_TObjDesc`/`HSD_VtxDescList`, plus the
   `HSD_AnimJoint`/`HSD_AObjDesc`/`HSD_FObjDesc` animation trees. Record pointer
   fields (for stage 3) and data ranges (for exclusion).
3. **Convert** with a range map instead of a blanket prefix swap:
   - structural descriptor regions: u32 word swap;
   - inside those regions, restore the u16 fields (PObj `flags`/`n_display`,
     VtxDesc `stride`, ImageDesc `width`/`height`, TlutDesc `n_entries`,
     FigaTrack `length`/`startframe`) and the u8 runs (TObj `repeat_s/t`,
     FObjDesc type/frac, material `GXColor`, PEDesc, TObjTevDesc, LOD bytes,
     FigaTrack bytes);
   - exclude all pointed-to data ranges: FObj `ad..ad+length`, PObj
     `display .. +n_display*32`, every `VtxDescList.vertex` array (sized from
     max display-list index × stride), `ImageDesc.image_ptr` (size from format
     and dimensions), `TlutDesc.lut .. +n_entries*2`, FigaTree `nodes/tracks`.
4. **Cache** the converted pre-`Locate` image on disk (hash + converter
   version in the key, e.g. under the user cache dir). On load, copy it into
   the working buffer and call `HSD_ArchiveParse`; `Locate` then resolves
   pointers to the host buffer. Do not cache post-`Locate` bytes.
5. **Verify** every archive by running the hand parser (`native/hsd/model.c` +
   `aobj.c`) and the compiled loader side by side and diffing numbers, exactly
   as S0 did (`tests/test_decomp_hsd.c`): symbol table, joint count, world
   matrices, texture count/formats, clip counts.

Rejected alternatives:

- **Blanket swap-on-load (S0 recipe) only** — enough for the joint bind pose
  but silently corrupts u16/u8 fields, material colors, FObj streams and
  display lists. It must not ship as the S3 pipeline.
- **Global swap + late un-swap** — workable but requires the same graph walk to
  know what to un-swap; the range-map version does it in one pass and is less
  error-prone.
- **Descriptor expansion now** — building an expanded host-struct tree bypasses
  `HSD_ArchiveParse`/`Locate` and is the clean answer for a 64-bit target, but
  it is more invasive than 32-bit S3 needs; keep it as the 64-bit plan.

### S0 validation to reproduce

```
decomp_hsd: PlMrNr.dat size=473522 data=467728 reloc=1423 public=2 extern=0 hand_symbols=2
decomp_hsd: S0a matched 2/2 symbols, 2 offsets correct
decomp_hsd: S0b root=PlyMario5K_Share_joint descriptors=61 objects=61 posed=61 world_worst=0 pose_failures=0
```

`ctest --test-dir build/native -R decomp_hsd` (SKIPs without the disc; the
probe is built 32-bit per ADR-0012).

## 8. Packed dynamics pointees

Relocation only proves that `BoneDynamicsDesc.data` becomes a valid pointer;
it says nothing about the byte order of the pointed-to numeric payload.
`lb_80011710` treats that payload as `count` contiguous 0x3C-byte records and
copies 15 floats from each into its runtime dynamics chain. Converter v59
therefore swaps the header (`bone_id`, count, position) and every word of the
packed records for both fighter and item dynamics. Link's cap is the visible
regression case: finite byte-reversed parameters stretch it into a rogue
polygon without producing the NaNs that matrix diagnostics normally catch.

### Stage map animation arrays

`UnkStageDat_x8_t` (`map_head` entries) holds three parallel pointer arrays
indexed by joint: `AnimJoint**` (+4), `MatAnimJoint**` (+8) and
`ShapeAnimJoint**` (+C).  `grAnime_801C7C1C`/`grAnime_801C6C0C` index them
directly, so a NULL slot is a legal gap; the relocation table is the array
bound (the first non-relocated slot ends it).  All three must be walked, or
stage material/TEV AObjDescs stay big-endian and their animations stop.

### Item state arrays are variable-length

`ItemStateArray` is declared as eight `ItemStateDesc` entries in the decomp,
but `ItCo.dat`/`ItCo.usd` stores the number used by each article; observed
arrays range past eight entries.  The array sits immediately before its
`Article`, with at most 15 bytes of alignment padding, so its count is
`(article_offset - states_offset) / sizeof(ItemStateDesc)`.

Walking a fixed eight entries is unsafe in both directions.  For short arrays
it treats the following `Article` and `ItemModelDesc` pointer fields as
AnimJoint/MatAnimJoint roots.  The converter's shared `seen` map then marks
the real model descriptor as already converted and leaves its counts and joint
tree numeric fields big-endian; Bob-omb later fails in `HSD_PObjResolveRefs`.
For long arrays, a fixed walk leaves later animation descriptors unconverted.
Converter v68 derives the count from the archive layout.  The asset regression
loads all 40 non-null common-item model roots through the compiled
`HSD_JObjLoadJoint` path.

### `Fighter_WaitAnimData` arrays have no stored count

`ftData->xC` and `ftData->x14` are arrays of 0x18-byte `Fighter_WaitAnimData`
records indexed by animation id, and the archive stores no element count.  The
converter bounded each array with the closest `ftData` pointer value after its
start, but that is only an upper bound: for Fox the `x14` array is followed by
the `ftData->x1C` descriptors' part-animation pointer arrays, and the bound
fell past them.  The extra "records" treated those pointers as `x4`/`x8`
numerics and `x10_animCurrFlags`, and `conv_waitanim_flags` rewrote a valid
`HSD_AnimJoint*` with a recomputed word (observed `0x00700313`).  Fox's landing
animation command then called `ftAnim_80070904` with a garbage AnimJoint tree
and crashed or froze the match.

A record is real when its `x0` (animation name) is a relocation target; empty
slots have `x0 == 0` and belong to the array because the runtime indexes by
anim id.  Converter v70 keeps the pointer-value bound but stops at the first
record whose `x0` is neither a relocation target nor zero.  The asset
regression now compares every relocation field against a raw copy of the
archive and walks every part-animation `x8` entry as an `HSD_AnimJoint` tree
for `PlMr`/`PlLk`/`PlFx`/`PlPk`.

## 9. Open questions

1. **HSD_RObj/HSD_RObjDesc conversion.** Required before S3 ships geometry:
   `JObjLoad` resolves `joint->robjdesc` (`jobj.c:650`), and the port's S0
   probe nulls it. Layout (type/flags + union) needs its own field table.
2. **FObj stream reachability.** Is every FObj `ad` stream reachable from the
   public `_animjoint`/`_matanim_joint` roots? If an `HSD_FObjDesc` hangs off a
   path the walk misses, its stream gets swapped. The range map should be
   cross-checked against `nb_reloc` coverage (every relocated pointer belongs
   to a known structure) — a useful invariant test.
3. **Vertex arrays vs HLE.** Leave them BE for the GX HLE to decode (matches
   compiled `GXSetArray`), or convert per `VtxDescList` (ACGC `SWAP_VTX` style)?
   Either needs array lengths, which the format does not store; HLE-side
   decoding avoids having to infer them. Recommend HLE-side.
4. **Material colors.** Should the pipeline normalize `GXColor` to a canonical
   `{r,g,b,a}` layout (as the port assumes), or leave them BE and let the GX
   HLE/vertex-colour paths do the byte work? The compiled `HSD_Material` field
   order is `r,g,b,a` (`mobj.h:82-88`), so normalization is the natural choice.
5. **Texture size bounds.** `HSD_ImageDesc` has no length; the converter needs
   expected sizes from format+dimensions (available in `gx/texture.c`) and must
   reject ranges past the data section. `mipmap != 0` would add unseen data;
   none observed in `Pl*Nr.dat`.
6. **class_name policy.** Retail archives carry NULL class names (probe above);
   decide whether the converter asserts on non-NULL and treats the target as a
   byte string, or rewrites them to a known-class name. Applies to menus/trophy
   `Cp*` archives not yet checked.
7. **Cache invalidation.** Key by archive SHA-256 + converter version; the disc
   region is static, but `Pl<Char>AJ.dat` is a container of sub-archives and
   converters must handle each 0x20-aligned sub-archive independently.
8. **`AObjDesc.obj_id`.** It is a u32 used both as an ID-table key and (when
   lookup fails) cast back to a joint pointer (`aobj.c:199-209`); word swap is
   correct, but the pipeline should not mistake it for a relocation target.
9. **`HSD_TexLODDesc` ABI.** The header declares a 4-byte `GXAnisotropy` at
   +0xC, but `native/hsd/model.c` reads the value at +0xA. Every `lod`
   descriptor seen is zero-filled. Confirm the offset with a positive sample
   (or a GC capture) before the HLE depends on it.

## 10. What changes for a 64-bit build

The 32-bit product build is what makes the current approach possible:
`Locate` adds `(u32)archive->data` into the 4-byte field in place
(`archive.c:13-15`), and the HSD structs contain 4-byte pointers and are
size-asserted for the 32-bit ABI (e.g. `HSD_JObj` at `jobj.h:125`; see also
`test_decomp_hsd.c:10-12`). On a 64-bit host that truncates. A 64-bit asset
path therefore needs:

1. **Descriptor expansion**: the platform loader walks the 32-bit archive and
   allocates native `HSD_*` structs with 8-byte host pointers (same field
   semantics as §2), so compiled code never sees 4-byte pointer slots; or
2. **A parallel 32-bit asset-heap**: load archives into a fixed 32-bit address
   range and patch pointers there — much more invasive and fragile;
3. **ID handling**: `JObjLoad` stores `(u32)joint` keys in the ID table and
   casts them back (`jobj.c:662-663`, `HSD_IDGetData`), so pointer-derived IDs
   also assume 32-bit addresses (`decomp_port.md` §4.2, §6);
4. Everything in §3-§5 that is byte-defined (FObj streams, display lists,
   vertex arrays, textures) is unchanged, since it is not pointer-sized.

Today none of this is on the critical path: ADR-0012 keeps the product 32-bit.
The reason to write this section down now is that the S3 converter's data
model (32-bit offsets + `Locate`) is the thing a future 64-bit port replaces,
and the descriptor walk in §7 is exactly the schema the expansion would use.

## Converter root names are an allow-list — find the gaps (P-696, version 83)

`convert_roots` dispatches on the archive's **public symbol name**.  A name no
rule claims falls into `else { c->st.roots_unknown++; }`, and then that root's
entire sub-graph stays big-endian.  Nothing warns: the archive still parses,
the relocations are still valid, and the damage only shows up when some far
away consumer reads a field.

Three IfAll.dat HUD model sets were in that hole.  All three are
`DynamicModelDesc*` arrays loaded with `lbArchive_LoadSections` and
dereferenced as `(*desc)->joint`, exactly like the `Stc_scemdls` sections the
converter already walked — but their names match no pattern:

| Symbol | Owner | What it is |
|---|---|---|
| `lupe` | `ifmagnify.c:468` | the off-screen player magnifier ("Lupe") |
| `tdsce` | `iftime.c:35` | the countdown timer digits |
| `Stc_rarwmdls` | `if_2FD9.c:202` | the rotating HUD arrows |

`Stc_rarwmdls` is the trap worth remembering: the existing rule is
`name_ends_with(name, length, "scemdls")`, and `rarwmdls` ends with `mdls`,
not `scemdls`.  A name that *looks* like a sibling of a handled root is not
necessarily handled.

The symptom was remote from the cause.  `ifMagnify_802FBBDC` passes the
magnifier's `HSD_ImageDesc` to `HSD_ImageDescCopyFromEFB`, which does

```c
GXSetTexCopySrc(origx, origy, idesc->width, idesc->height);
GXSetTexCopyDst(idesc->width, idesc->height, idesc->format, ...);
```

so the unconverted 64x64 RGB5A3 descriptor asked for a **0x4000 x 0x4000**
copy in format **0x05000000**.  The GL backend's encoder spun
16384 x 16384 = 268M iterations writing nothing (the byte-swapped format
matched no `case`), which is ~400 ms of CPU **per frame**, for as long as a
player stayed off-camera.  See G-146.

### How to look for more of these

```sh
MELEE_ROOT_TRACE=1 MELEE_NO_ASSET_CACHE=1 SDL_VIDEODRIVER=offscreen \
    SDL_AUDIODRIVER=dummy MELEE_NO_CARD=1 \
    ./build/native/melee --match --frames 300 --no-hud --shot /tmp/x.bmp \
    2>&1 | grep 'unhandled root' | sort -u
```

Run it through as many scenes as you can drive; a root is only reported once
its archive is actually loaded, so a 60-frame run sees a fraction of them.
Names still unhandled after P-696, each of which needs its type established
from the `src/` consumer **before** any rule is added (converting the wrong
shape silently corrupts data):

`ALDYakuAll`, `lbBgFlashColAnimData`, `lbRefData`, `lbRumbleData`,
`plLoadCommonData`, `quake_model_set`, `SIS_IntroData`, `SIS_MessageData`,
`TitleMark_sobjdesc`, `ScTitle_cam_int1_camanim`, `MemCardBanner_0*`,
`MemCardIcon*`, `ty*Tbl`, and the `Grd*_image`/`*_tlut*` leaf roots (those last
ones are raw texture payloads with no header to swap — they are correctly
unhandled).

Regression: `check_ifall_hud_modelsets` in `native/tests/test_decomp_assets.c`
reads each root's first joint `flags` word from the converted buffer as
host-endian and from an untouched copy as big-endian; the two must agree.  With
the rule disabled it prints
`IfAll `lupe` joint flags=00000010 want=10000000 (not converted?)` for all
three.

**Bump `HSD_CONVERTER_VERSION` whenever the walk changes** — it is the disk
cache key, and without it a stale `~/.cache/melee/assets` entry hides the fix.


## Shape-set pools are read by the CPU, not the decoder (P-698)

The port's rule is that **vertex arrays stay big-endian**: the GX display-list
decoder reads them that way, so the converter only byte-swaps descriptors
(`conv_vtxdesc`), never the arrays themselves.  `POBJ_SHAPEANIM` is the one
exception to who reads them.

`HSD_PObjDisp` -> `PObjDispShapeAnim` -> `drawShapeAnim` (`pobj.c`) blends the
morph targets **on the CPU** and only then pushes vertices through
`GXPosition3f32`.  The three readers it uses —
`get_shape_vertex_xyz`, `get_shape_normal_xyz`, `get_shape_nbt_xyz` — do a
plain `memcpy` for `GX_F32` and native casts for `GX_U16`/`GX_S16`.  On a
little-endian host every component comes back as a denormal near zero and the
mesh collapses to a point (G-147).

The fix is a `PORT_PC` swap **inside those readers**, not in the converter.
The converter route is tempting and it does fix the mesh, but the same arrays
are shared with sibling PObjs that the GX HLE decodes big-endian, so swapping
the pool in place corrupts them — observed as the clear screen's SPECIAL BONUS
frame and TIME REMAINING/DAMAGE fills disappearing while the banner appeared.

The index lists (`vertex_idx_list[shape]`) need nothing: HSD already assembles
`GX_INDEX16` entries big-endian by hand, which is the tell that these arrays
are disc-order data.

Regression: `ctest decomp_clear_banner` renders the 1P clear screen through
`MELEE_GAMEOVER_TEST` and counts non-black pixels inside the "STAGE CLEAR"
banner (0 before the patch, 30690 after).


## Walk pointer arrays with the relocation table (P-701, version 84)

`conv_scene_desc` walked `SceneDesc.cameras` / `.lights` / `.fogs` as
"read a word, stop at 0 or at an out-of-range offset".  That is not enough.
The word after the last entry is not guaranteed to be a terminator; it can be
unrelated archive data that still looks like a plausible offset, and the walk
then converts whatever it points at.

In GmIntEz.dat the fog walk ran one slot too far and called `conv_fogdesc` on
an `HSD_PEDesc`, byte-swapping its first word: `flags = 0x29` became `0x00`.
`HSD_SetupPEMode` feeds `pe->flags & 1` to `HSD_StateSetColorUpdate`, so every
draw using that material ran with colour writes disabled and rendered nothing
(G-148).

Every real entry in these arrays is a **relocated pointer**, so the fix is to
gate on `c->reloc[p]` before reading the slot.  That is strictly more correct
than a zero test and it also handles G-002 — a genuine pointer to data offset
0 reads back as `0` and a `!= 0` test would wrongly stop (or, worse, a zero
word that is *not* a pointer would wrongly continue).

The same `!= 0` idiom is still used by other walkers in `hsd_convert.c`; they
are candidates for the same treatment whenever something downstream turns out
to be corrupted by a few bytes.

### Debugging recipe that found it

1. `--dump-draws N` showed the marker draws present with correct screen
   extents, so it was not geometry.
2. The dump's `colup=` column (added for this) showed `color_update = 0` on
   the invisible draws and `1` on a visible neighbour.
3. A backtrace on `GXSetColorUpdate` pointed at `HSD_SetupPEMode`, i.e. the
   material's `HSD_PEDesc`.
4. `HSD_PEDesc` is all `u8`, so a byte-swap there is always a converter bug.
   Scanning the converted archive for the descriptor's tail bytes found it at
   one offset with `c->num[]` set — proof that `conv_u32` had written there.
5. A backtrace in `conv_u32` on that offset named `conv_scene_desc`.

## CPU attack database in PlCo.dat (P-705, converter version 86)

`Fighter_LoadCommonData` assigns `ftLoadCommonData` pData[22] to
`Fighter_804D64FC`. Its layout from `fighter.h` is:

| Offset | Data |
|---|---|
| `+0x00` | CPU command-script pointer table; scripts are bytes |
| `+0x04..+0x1C` | seven FighterKind-indexed pointer tables of `ftCo_AttackEntry` lists |
| `+0x20` | 33 per-kind distance-threshold floats |
| `+0x24` | six held-weapon reach-bonus floats |

`ftCo_AttackEntry` is nine 32-bit words (0x24 bytes): command, timing, four
range floats, weight, frequency and minimum CPU level. Lists end with a
zero-command record. All nine words are numeric and require conversion; only
the command-script payloads remain byte-defined.

There are 230 relocation-backed lists and 1,159 non-terminator records in the
US 1.02 `PlCo.dat`. One list demonstrates the archive's zero-pointer rule:
Mario's ground list is at data offset 0, so its slot contains zero but is in
the relocation table. A converter check for `list != 0` silently skips it;
`c->reloc[slot]` is the authoritative pointer test.

Without this walk, a ground command `2` reads as `0x02000000` and `3.0f`
weight reads as a denormal. CPU navigation remains functional because it is
compiled logic, but attack selection cannot return usable commands. The
title attract match is the integration oracle: four level-9 CPUs, no PAD
input, sane table check followed by an attack-state transition and damage.

## Peach's Castle `yakumono_param` in GrCs.dat (P-707, converter version 87)

`grcastle.c` keeps the `GrdCastleCast` public in a file-static
`yakumono_param` and reads it per map: `entries[ground->u.icemt.x2]`, where
`x2 = map_id - 8` covers the nine Castle maps. Each entry's `x0` is an s16
frame countdown for the intro sequence; `x4` and `rot` drive its satellite
motion. The ground's `xCA` copies the countdown and decrements it once per
frame; when it goes negative the intro animation starts, and the animation's
completion calls `Ground_801C5544` to stop the ambient loop.

| Offset | Data |
|---|---|
| `+0x00..0x0E`, `+0x40..0x44`, `+0x54`, `+0x58` | s16 scalars |
| `+0x10..0x18`, `+0x20..0x3C`, `+0x48..0x50` | f32 scalars |
| `+0x5C` | `entries[9]`, 0x14 bytes each: s16 `x0`, four f32 (`x4`, `rot`) |
| `+0x110`, `+0x118..0x124`, `+0x134..0x140` | f32 |
| `+0x114` | pointer (relocation pass only) |
| `+0x12C` | s16[4] |

Unconverted, `entries[0].x0` reads 38145 and `entries[1].x0` 22530, so no
Castle map ever starts its intro and the `0x53025` ambient loops until the
match ends (G-157). `check_castle_param` in `test_decomp_assets.c` asserts
the nine converted countdowns against the raw archive.

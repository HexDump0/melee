# GX textures

Source: `src/sysdolphin/baselib/tobj.h`, `gx/texture.c`. Verified by decoding
Mario's 128x128 CMPR overalls texture to a correct denim image.

## Descriptor chain

`HSD_MObjDesc.texdesc` -> chain of `HSD_TObjDesc` via `next` (+4).

### HSD_TObjDesc

| Offset | Type | Field |
|---|---|---|
| +0x00 | ptr | `class_name` |
| +0x04 | ptr | `next` |
| +0x08 | u32 | `id` (GXTexMapID) |
| +0x0C | u32 | `src` (GXTexGenSrc) |
| +0x10 | f32[3] | `rotate` |
| +0x1C | f32[3] | `scale` |
| +0x28 | f32[3] | `translate` |
| +0x34 | u32 | `wrap_s` |
| +0x38 | u32 | `wrap_t` |
| +0x3C | u8 | `repeat_s` |
| +0x3D | u8 | `repeat_t` |
| +0x40 | u32 | `blend_flags` |
| +0x44 | f32 | `blending` |
| +0x48 | u32 | `magFilt` |
| +0x4C | ptr | `imagedesc` |
| +0x50 | ptr | `tlutdesc` (palette; CI formats) |
| +0x54 | ptr | `lod` |
| +0x58 | ptr | `tev` |

The port currently ignores `rotate/scale/translate` (texture matrices). If face
or eye UVs are wrong, check these first (P-205).

### HSD_ImageDesc (0x18 bytes)

| Offset | Type | Field |
|---|---|---|
| +0x00 | u32 ptr | `image_ptr` (data-relative) |
| +0x04 | u16 | `width` |
| +0x06 | u16 | `height` |
| +0x08 | u32 | `format` (GXTexFmt) |
| +0x0C | u32 | `mipmap` |
| +0x10 | f32 | `minLOD` |
| +0x14 | f32 | `maxLOD` |

### HSD_TlutDesc (palette)

| Offset | Field |
|---|---|
| +0x00 | `void* lut` — data-relative pointer to `n_entries * 2` bytes |
| +0x04 | `GXTlutFmt fmt` (0 = IA8, 1 = RGB565, 2 = RGB5A3) |
| +0x08 | `u32 tlut_name` |
| +0x0C | `u16 n_entries` |

The palette **is** in the model archive for fighter models. Verified with
Mario's eye atlas: 190x190 CI8 image at `0x2C9C0`, palette at `0x359C0`
(256 entries, RGB565), immediately after the image data.

`gx_texture_decode_ci` expands the indices with a palette already converted
to RGBA8; `hsd/model.c` reads the tlutdesc, converts the palette with
`decode_palette_entry`, and calls it for formats 8 (CI4) and 9 (CI8).

## Formats and encoded size

| Format | ID | Block size | Block covers | Decoded |
|---|---|---|---|---|
| I4 | 0 | 8 bytes | 8x8 (32 bytes) | grayscale |
| I8 | 1 | 32 bytes | 8x4 | grayscale |
| IA4 | 2 | 32 bytes | 8x4 | intensity + alpha |
| IA8 | 3 | 32 bytes | 4x4 | intensity + alpha |
| RGB565 | 4 | 32 bytes | 4x4 | color |
| RGB5A3 | 5 | 32 bytes | 4x4 | color or color+alpha |
| RGBA8 | 6 | 64 bytes | 4x4 | color+alpha |
| CI4 | 8 | 32 bytes | 8x8 indices | needs TLUT |
| CI8 | 9 | 32 bytes | 8x4 indices | needs TLUT |
| CMPR | 14 | 32 bytes | 8x8 (4 sub-blocks) | DXT1-like |

Byte count = `blocks_x * blocks_y * block_size`. `gx/texture.c` validates
`pixel_length >= expected` before decoding, which also makes it safe to pass
`file_size - image_offset` as the input length.

## Tiling

GX textures are stored as 8x8 (or 8x4 / 4x4) tiles, in tile raster order:
left to right, then top to bottom. Within a 4x4 sub-block, RGBA8 pixels are
stored in a strided layout: 16 AR bytes, then 32 GB bytes; the decoder in
`gx/texture.c` handles it. CMPR has four 4x4 sub-blocks per 8x8 tile in the
order TL, TR, BL, BR; each sub-block is `c0 u16, c1 u16, 4 index bytes`.
RGB565 colors compare numerically to choose the 3-color vs 4-color mode,
matching GX.

## Verified example

Mario overalls: `image_ptr` data offset `0x1AD40` (file `0x1AD60`),
128x128, format 14 (CMPR). Decodes to a blue denim texture. Material diffuse is
`0xB3B3B3FF` (gray), so the color comes from the texture, not the material.

## CI palette path (implemented)

1. `TObjDesc +0x50` -> `HSD_TlutDesc`.
2. Convert `n_entries` palette words using `GXTlutFmt`.
3. Expand CI4/CI8 indices (CI4: 8x8 blocks, 4 bpp; CI8: 8x4 blocks, 8 bpp)
   via `gx_texture_decode_ci`.
4. Upload as RGBA8; no GX palette emulation needed.

## Texture matrix (`MakeTextureMtx`, implemented)

HSD builds a texture matrix per TObj from `repeat_s`/`repeat_t`, `scale`,
`rotate` and `translate` (`tobj.c:MakeTextureMtx`):

```
scale.x  = repeat_s / tobj.scale.x
scale.y  = repeat_t / tobj.scale.y
trans.x  = -translate.x
trans.y  = -(translate.y + (wrap_t == GX_MIRROR ? 1/(repeat_t/scale.y) : 0))
M        = S * R * T
```

`repeat_s`/`repeat_t` are **not** just metadata: they scale the UVs. This is
how Melee stores half textures that mirror into a whole; Mario's cap "M" is a
64x128 half texture with `repeat_s=2` and `wrap_s=GX_MIRROR`, so the texture
matrix doubles U and the mirror completes the logo. Ignoring the matrix
leaves Mario with half an "M" (and tiled textures wrong elsewhere).

The port computes this matrix per batch and loads it with `glMatrixMode(
GL_TEXTURE)` / `glLoadMatrixf` (GL column-major; HSD's 3x4 transforms like a
column vector).

## Texture wrap modes (applied per batch)

`TObjDesc.wrap_s/wrap_t` (+0x34/+0x38) are GX_CLAMP/GX_REPEAT/GX_MIRROR. The
port always uses `GL_REPEAT`; switch per batch if edge bleeding shows up.

---

# Format census across `Pl*Nr.dat` (P-403, 2026-09-11)

Scope: every `Pl*Nr.dat` on the retail Rev 2 disc (all 33 archives), their
unique textures as the hand parser counts them, TLUTs, second (TEX1) maps and
image dimensions. "Observed" below means measured from disc bytes; "assumed"
means a decoder branch exists but no Nr archive exercises it.

## Enumeration: 33 `Pl*Nr.dat` vs "26 characters"

```sh
cp build/native/melee /tmp/melee-docs          # do not race the S1 build
/tmp/melee-docs --list-models                  # -> 33 Pl*Nr.dat
/tmp/melee-docs --list-models --all-models     # -> 273 Pl*.dat
```

`--all-models` only widens the suffix filter; listing still needs
`--list-models` (`native/main.c:97-109`). The 273 `Pl*.dat` split as 33 `Nr`,
33 `AJ`, 20 `Gr`, 19 `Re` and 168 menu/trophy/copy variants (e.g. `CpMr`).
The 33 model archives are **not** 33 characters: the roster is 26 fighters
(25 selectable slots; Zelda and Sheik are separate `Nr` files, Popo and Nana
share the Ice Climbers slot). The extra seven files are:

| File | Loaded by | Role |
|---|---|---|
| `PlNnNr.dat` | `ftNana/ftnana.c` | second Ice Climber body |
| `PlGkNr.dat` | `ftGigaKoopa/ftgkoopa.c:293` | Giga Bowser |
| `PlMhNr.dat` | `ftMasterHand/ftmasterhand.c:600` | Master Hand |
| `PlChNr.dat` | `ftCrazyHand/ftcrazyhand.c:594` | Crazy Hand |
| `PlSbNr.dat` | `ftSandbag/ftsandbag.c:36` | Sandbag |
| `PlBoNr.dat` | `ftZakoBoy/ftzakoboy.c` | Male Wireframe |
| `PlGlNr.dat` | `ftZakoGirl/ftgirl.c:10` | Female Wireframe |

Evidence: the `"Pl..Nr.dat"` constants in `src/melee/ft/kinds/*/*.c`. S3's
"26 character archives" is the roster; the pipeline still has to load all 33
because `ft` code loads the extras in single-player modes.

## Method

The CLI does not expose per-format counts, so a scratch Python probe walked
the archives with the same traversal as `native/hsd/model.c`:
`HSD_Joint.u.dobjdesc` (+0x10) -> `HSD_DObjDesc` (+0x8 mobj, +0xC pobj) ->
`HSD_MObjDesc.texdesc` (+0x8) -> `HSD_TObjDesc.next` (+0x4), capped at
`HSD_MAX_TOBJS == 2` (`model.c:1087`). It uses the same texture key as
`find_or_add_texture` (`model.c:734`): `(ImageDesc.image_ptr, format,
TlutDesc.lut)`, and reads `HSD_ImageDesc` +0x4 width / +0x6 height / +0x8
format and `HSD_TlutDesc` +0x4 fmt / +0xC n_entries.

```sh
# extract the raw archives (unmodified; only under /tmp, never committed)
while read -r m; do
  case "$m" in Pl*Nr.dat) /tmp/melee-docs --extract "$m" "/tmp/opencode/assets/$m" ;; esac
done < /tmp/opencode/melee-models-33.txt
python3 /tmp/opencode/gx_census.py /tmp/opencode/assets/*.dat /tmp/opencode/gx_census.json

# cross-check every model against the hand parser's own texture list
for m in $(sed '$d' /tmp/opencode/melee-models-33.txt); do
  /tmp/melee-docs --model "$m" --dump-textures /tmp/opencode/texdump
done
```

Cross-check result: for **all 33 models** the probe's texture count and
per-format histogram are identical to `--dump-textures` (which prints
`tex N: WxH fmt F` per decoded texture and is driven by the decoder's own
`find_or_add_texture`). Examples: Mario 32 (CMPR 31, CI8 1), Captain Falcon 52
CMPR, Mr. Game & Watch 0. The probe is therefore measuring exactly what the
port decodes.

## Aggregate (967 unique textures in 33 models)

| Format | ID | Observed | Decoder path (`native/gx/texture.c` unless noted) |
|---|---:|---:|---|
| CMPR | 14 | 883 | `decode_cmpr` (:136), DXT1-like sub-blocks |
| CI8 | 9 | 47 | `gx_texture_decode_ci` (:196); palette via `decode_palette_entry` (`model.c:705`) |
| CI4 | 8 | 5 | same CI path (8x8 blocks) |
| I4 | 0 | 17 | `decode_i4` (:55) |
| I8 | 1 | 1 | `decode_i8` (:74) |
| RGB5A3 | 5 | 2 | `decode_16` (:111) -> `rgb5a3` (:31); also TLUT fmt 2 |
| RGBA8 | 6 | 12 | `decode_rgba8` (:122), AR/GB split layout |
| IA4 | 2 | 0 | assumed: `decode_ia4` (:88), branch at :187 |
| IA8 | 3 | 0 | assumed: `decode_ia8` (:101), branch at :188 |
| RGB565 | 4 | 0 image / 41 TLUT | image: `decode_16` (:111) assumed; TLUT: `decode_palette_entry` fmt 1 (`model.c:710`) |

The dispatch `switch` (`texture.c:166-191`) covers every GX format the disc
uses; nothing observed is missing from the decoder. What is missing for S2 is
the GX object/TMEM layer (`GXInitTexObj`/`GXInitTexObjCI` at `tobj.c:1212`,
`GXLoadTlut` at `tobj.c:1205`), which currently has no port implementation.

### Per-character census

| Character (file) | Tex | CMPR | CI8 | CI4 | I4 | I8 | RGB5A3 | RGBA8 | TEX1 maps | WxH min..max |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Male Wireframe (`PlBoNr.dat`) | 2 | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 128x128..256x256 |
| Captain Falcon (`PlCaNr.dat`) | 52 | 52 | 0 | 0 | 0 | 0 | 0 | 0 | 20 | 64x32..256x256 |
| Crazy Hand (`PlChNr.dat`) | 7 | 1 | 0 | 0 | 0 | 0 | 0 | 6 | 0 | 64x128..128x128 |
| Young Link (`PlClNr.dat`) | 44 | 42 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 16x16..128x256 |
| Donkey Kong (`PlDkNr.dat`) | 29 | 27 | 2 | 0 | 0 | 0 | 0 | 0 | 7 | 64x32..256x256 |
| Dr. Mario (`PlDrNr.dat`) | 38 | 37 | 1 | 0 | 0 | 0 | 0 | 0 | 2 | 16x32..256x190 |
| Falco (`PlFcNr.dat`) | 25 | 25 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 16x16..256x256 |
| Roy (`PlFeNr.dat`) | 60 | 60 | 0 | 0 | 0 | 0 | 0 | 0 | 7 | 8x16..320x282 |
| Fox (`PlFxNr.dat`) | 34 | 34 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8x8..256x256 |
| Giga Bowser (`PlGkNr.dat`) | 38 | 37 | 1 | 0 | 0 | 0 | 0 | 0 | 20 | 8x32..128x128 |
| Female Wireframe (`PlGlNr.dat`) | 2 | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 128x128..256x256 |
| Ganondorf (`PlGnNr.dat`) | 52 | 49 | 0 | 0 | 2 | 0 | 1 | 0 | 4 | 16x16..256x256 |
| Mr. Game & Watch (`PlGwNr.dat`) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | - |
| Kirby (`PlKbNr.dat`) | 17 | 15 | 2 | 0 | 0 | 0 | 0 | 0 | 8 | 8x8..128x128 |
| Bowser (`PlKpNr.dat`) | 50 | 49 | 1 | 0 | 0 | 0 | 0 | 0 | 22 | 32x32..256x256 |
| Luigi (`PlLgNr.dat`) | 37 | 36 | 0 | 0 | 0 | 1 | 0 | 0 | 8 | 8x8..256x256 |
| Link (`PlLkNr.dat`) | 53 | 48 | 4 | 1 | 0 | 0 | 0 | 0 | 2 | 16x16..128x256 |
| Master Hand (`PlMhNr.dat`) | 7 | 1 | 0 | 0 | 0 | 0 | 0 | 6 | 0 | 64x128..128x128 |
| Mario (`PlMrNr.dat`) | 32 | 31 | 1 | 0 | 0 | 0 | 0 | 0 | 2 | 8x8..256x256 |
| Marth (`PlMsNr.dat`) | 59 | 53 | 5 | 0 | 0 | 0 | 1 | 0 | 5 | 32x16..128x256 |
| Mewtwo (`PlMtNr.dat`) | 20 | 18 | 0 | 0 | 2 | 0 | 0 | 0 | 2 | 32x32..128x128 |
| Nana (`PlNnNr.dat`) | 25 | 25 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8x32..256x256 |
| Ness (`PlNsNr.dat`) | 33 | 33 | 0 | 0 | 0 | 0 | 0 | 0 | 11 | 32x64..256x256 |
| Pichu (`PlPcNr.dat`) | 10 | 10 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8x8..128x128 |
| Peach (`PlPeNr.dat`) | 48 | 32 | 14 | 0 | 2 | 0 | 0 | 0 | 2 | 8x8..256x256 |
| Pikachu (`PlPkNr.dat`) | 10 | 10 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8x8..80x80 |
| Popo (`PlPpNr.dat`) | 25 | 25 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 8x32..256x256 |
| Jigglypuff (`PlPrNr.dat`) | 7 | 6 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 4x4..128x128 |
| Sandbag (`PlSbNr.dat`) | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 128x128..128x128 |
| Sheik (`PlSkNr.dat`) | 28 | 25 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 16x16..256x256 |
| Samus (`PlSsNr.dat`) | 51 | 48 | 0 | 0 | 3 | 0 | 0 | 0 | 3 | 24x24..256x208 |
| Yoshi (`PlYsNr.dat`) | 19 | 13 | 6 | 0 | 0 | 0 | 0 | 0 | 8 | 32x21..128x128 |
| Zelda (`PlZdNr.dat`) | 51 | 41 | 2 | 0 | 8 | 0 | 0 | 0 | 10 | 8x8..160x320 |
| **Total** | **967** | **883** | **47** | **5** | **17** | **1** | **2** | **12** | 143 | - |

Notable per-model facts:

- Mr. Game & Watch is fully untextured (vertex colours + the `ftMaterial`
  diffuse override); this is why he rendered correctly without texture work.
- `RGBA8` appears only on the two Hands; `RGB5A3` images only on Ganondorf
  and Marth; the single `I8` is Luigi's. IA4/IA8/RGB565 images never occur.
- Largest texture is Roy's 320x282; smallest is Jigglypuff's 4x4.
- Per-model texture count never exceeds `HSD_MAX_TEXTURES` (128, `model.h:15`);
  the maximum observed is 60 (Roy), so the hand parser never truncates.

### TLUT palettes (CI4/CI8 models)

Every CI texture has a `HSD_TlutDesc`. All 52 TLUTs are `RGB565` (41) or
`RGB5A3` (11); no `IA8` palette occurs. Entry counts are authored, not rounded:
`{3, 8, 16, 20, 49, 55, 128, 157, 173, 190, 202, 244, 255, 256}`. Decoding the
CI index streams and taking `max(index)+1` gives **exactly `n_entries` for all
52 textures, with zero out-of-range indices**, so `n_entries` is trustworthy
and must not be padded to GX's power-of-two TLUT sizes. The compiled path reads
it at `HSD_TlutDesc` +0xC and hands it to `GXInitTlutObj`
(`tobj.c:303`, `extern/dolphin/src/dolphin/gx/GXTexture.c:578-586`, which only
asserts `n_entries <= 0x4000`).

| Character (file) | CI textures | TLUT formats (entries) |
|---|---:|---|
| Male Wireframe (`PlBoNr.dat`) | 2 | RGB5A3/3 x2 |
| Young Link (`PlClNr.dat`) | 2 | RGB565/256 x2 |
| Donkey Kong (`PlDkNr.dat`) | 2 | RGB565/256 x2 |
| Dr. Mario (`PlDrNr.dat`) | 1 | RGB565/256 x1 |
| Giga Bowser (`PlGkNr.dat`) | 1 | RGB565/256 x1 |
| Female Wireframe (`PlGlNr.dat`) | 2 | RGB5A3/3 x2 |
| Kirby (`PlKbNr.dat`) | 2 | RGB565/190 x1, RGB565/256 x1 |
| Bowser (`PlKpNr.dat`) | 1 | RGB565/202 x1 |
| Link (`PlLkNr.dat`) | 5 | RGB565/16 x1, RGB565/256 x4 |
| Mario (`PlMrNr.dat`) | 1 | RGB565/256 x1 |
| Marth (`PlMsNr.dat`) | 5 | RGB565/255 x1, RGB565/256 x2, RGB565/55 x1, RGB5A3/256 x1 |
| Peach (`PlPeNr.dat`) | 14 | RGB565/244 x1, RGB565/256 x13 |
| Jigglypuff (`PlPrNr.dat`) | 1 | RGB565/256 x1 |
| Sandbag (`PlSbNr.dat`) | 2 | RGB565/128 x1, RGB565/256 x1 |
| Sheik (`PlSkNr.dat`) | 3 | RGB565/256 x3 |
| Yoshi (`PlYsNr.dat`) | 6 | RGB5A3/173 x1, RGB5A3/20 x1, RGB5A3/49 x2, RGB5A3/8 x2 |
| Zelda (`PlZdNr.dat`) | 2 | RGB565/157 x1, RGB565/256 x1 |

Known example: Mario's 190x190 CI8 eye atlas is index-complete at 256 entries
(matches the older entry in this file).

### TEX1 second maps

143 of 1,915 TObjs are the second entry of a material's texture chain; every
one has `TObjDesc.id == 1` (GX_TEXMAP1), so "second chain node" and "TEX1" are
the same thing in every Nr archive. Their image formats: CMPR 117, I4 17,
I8 1, CI8 8. The 17 + 17 + 8 non-CMPR second maps line up with lightmap/extra
stages; the port's `MObjMakeTExp`/`TObjMakeTExp` derivation is the reference
for the TEV stages that consume them.

| Character (file) | MObjs with 2 TObjs | Second-map image formats |
|---|---:|---|
| Captain Falcon (`PlCaNr.dat`) | 20 | CMPR x20 |
| Donkey Kong (`PlDkNr.dat`) | 7 | CMPR x7 |
| Dr. Mario (`PlDrNr.dat`) | 2 | CMPR x2 |
| Roy (`PlFeNr.dat`) | 7 | CMPR x7 |
| Giga Bowser (`PlGkNr.dat`) | 20 | CMPR x20 |
| Ganondorf (`PlGnNr.dat`) | 4 | CMPR x2, I4 x2 |
| Kirby (`PlKbNr.dat`) | 8 | CMPR x8 |
| Bowser (`PlKpNr.dat`) | 22 | CMPR x22 |
| Luigi (`PlLgNr.dat`) | 8 | CMPR x7, I8 x1 |
| Link (`PlLkNr.dat`) | 2 | CMPR x2 |
| Mario (`PlMrNr.dat`) | 2 | CMPR x2 |
| Marth (`PlMsNr.dat`) | 5 | CMPR x5 |
| Mewtwo (`PlMtNr.dat`) | 2 | I4 x2 |
| Ness (`PlNsNr.dat`) | 11 | CMPR x11 |
| Peach (`PlPeNr.dat`) | 2 | I4 x2 |
| Samus (`PlSsNr.dat`) | 3 | I4 x3 |
| Yoshi (`PlYsNr.dat`) | 8 | CI8 x8 |
| Zelda (`PlZdNr.dat`) | 10 | CMPR x2, I4 x8 |

### Image dimensions

967 textures, width and height both 4..320, 93 distinct `(w,h)` sizes,
856/967 (88.5%) power-of-two in both dimensions, 595/967 square. Most common:
128x128 (254), 64x64 (204), 64x128 (100), 128x64 (66), 32x32 (57). Non-PoT
sizes are real (190x190 eye atlas, 120x120, 96x64, 176x176, 21/282-high,
etc.), and neither `HSD_ImageDesc.mipmap` nor `minLOD`/`maxLOD` is ever
nonzero in these archives: **no Nr archive carries an authored mip chain**.
The prototype's `glGenerateMipmap` (`STATE.md` "Mipmaps") is a port choice,
not asset data.

## Implications for GX HLE (S2)

1. **Order of work.** CMPR is 91.3% of all textures; adding CI8/CI4+TLUT
   reaches 96.7%. Implement CMPR and CI first, then I4 (17, lightmaps),
   RGBA8 (12, Hands only), RGB5A3 (2), I8 (1). IA4/IA8/RGB565-as-image never
   appear in the 33 character models; their decoder branches already exist
   (`gx/texture.c:187-188`) and can stay untested until stages/menus (S3).
2. **TLUT loading.** The HLE must take `n_entries` from `HSD_TlutDesc` (+0xC)
   and load exactly that many BE u16 entries; the observed counts are not
   powers of two (3..256). Do not emulate GX's TMEM size classes or round the
   palette; `GXInitTlutObj` only rejects >0x4000. Palette formats seen are
   RGB565 and RGB5A3 (`GXTlutFmt` 1 and 2).
3. **Two-map TEV.** 143 second maps across 18 characters mean the HLE needs
   `GX_TEXMAP0/1`, two texcoord generators and the colormap/alphamap/lightmap
   TEV stages the prototype derived (`learnings/hsd_tev_materials.md`), not a
   single-texture pipeline.
4. **No mip chains.** `mipmap` is 0 and LOD range 0..0 for all 967 textures,
   so TMEM/mip allocation can start with one level per texture; generated mips
   are a renderer decision, not an asset requirement.
5. **Non-PoT dimensions must work.** 111/967 textures are non-power-of-two
   (including the 190x190 atlas used by Mario/Dr. Mario), so the HLE cannot
   assume PoT when computing tiled storage sizes or wrapping.
6. **Decoder completeness.** Nothing observed is unsupported by
   `native/gx/texture.c`; the S2 gap is the GX texobj/TMEM path and the
   host-endian conversion of `HSD_ImageDesc`/`HSD_TlutDesc` fields (see
   `decomp_assets.md`, P-605).
7. **Census limits.** This covers only `Pl*Nr.dat`. `Gr*.dat` stages, `Re*`
   results, menu/trophy archives and `AJ` FObj streams are not counted;
   `RGB565`/`IA8` image maps may still occur there. The 273-archive list in
   `/tmp/opencode/melee-all-273.txt` is the enumeration starting point for
   that follow-up.

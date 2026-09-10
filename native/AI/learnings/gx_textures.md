# GX textures

Source: `src/sysdolphin/baselib/tobj.h`, `demo_texture.c`. Verified by decoding
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
| +0x00 | `void* lut` |
| +0x04 | `GXTlutFmt fmt` |
| +0x08 | `u32 tlut_name` |
| +0x0C | `u16 n_entries` |

Not stored in the `.dat`; the runtime loads palettes separately (often from a
sibling archive). This is why CI formats are not yet supported.

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

Byte count = `blocks_x * blocks_y * block_size`. `demo_texture.c` validates
`pixel_length >= expected` before decoding, which also makes it safe to pass
`file_size - image_offset` as the input length.

## Tiling

GX textures are stored as 8x8 (or 8x4 / 4x4) tiles, in tile raster order:
left to right, then top to bottom. Within a 4x4 sub-block, RGBA8 pixels are
stored in a strided layout: 16 AR bytes, then 32 GB bytes; the decoder in
`demo_texture.c` handles it. CMPR has four 4x4 sub-blocks per 8x8 tile in the
order TL, TR, BL, BR; each sub-block is `c0 u16, c1 u16, 4 index bytes`.
RGB565 colors compare numerically to choose the 3-color vs 4-color mode,
matching GX.

## Verified example

Mario overalls: `image_ptr` data offset `0x1AD40` (file `0x1AD60`),
128x128, format 14 (CMPR). Decodes to a blue denim texture. Material diffuse is
`0xB3B3B3FF` (gray), so the color comes from the texture, not the material.

## CI palette path (future work, P-203)

1. Find the `tlutdesc` (or the fighter's shared TLUT asset).
2. Decode the palette entry (`GXTlutFmt`: IA8, RGB565, RGB5A3).
3. Expand CI4/CI8 indices to RGBA using the palette.
4. Upload as RGBA8; no need to emulate GX palettes.

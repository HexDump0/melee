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
| +0x00 | `void* lut` — data-relative pointer to `n_entries * 2` bytes |
| +0x04 | `GXTlutFmt fmt` (0 = IA8, 1 = RGB565, 2 = RGB5A3) |
| +0x08 | `u32 tlut_name` |
| +0x0C | `u16 n_entries` |

The palette **is** in the model archive for fighter models. Verified with
Mario's eye atlas: 190x190 CI8 image at `0x2C9C0`, palette at `0x359C0`
(256 entries, RGB565), immediately after the image data.

`demo_texture_decode_ci` expands the indices with a palette already converted
to RGBA8; `demo_model.c` reads the tlutdesc, converts the palette with
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

## CI palette path (implemented)

1. `TObjDesc +0x50` -> `HSD_TlutDesc`.
2. Convert `n_entries` palette words using `GXTlutFmt`.
3. Expand CI4/CI8 indices (CI4: 8x8 blocks, 4 bpp; CI8: 8x4 blocks, 8 bpp)
   via `demo_texture_decode_ci`.
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

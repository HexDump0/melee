# EFB copy formats and Z-texture edges (P-674)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Ported from:** Aurora `lib/gfx/tex_copy_conv.cpp` (`FragI4/I8/IA4/IA8/
RGB565/R4` and the `intensity()`/`quantize4()` preamble) at pinned
`749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`; block tiling verified against
`native/gx/texture.c`'s decoders (the same bytes the renderer samples).

Evidence: `ctest decomp_efb` pass 7.  Sensitivity flip: reverting the luma
conversion fails `I8 copy 00 (want 91)`, and dropping the RGB5A3 case fails
`RGB5A3 copy 0000 (want 83e0)`.

## What the game requests

| Caller | Format | Notes |
|---|---|---|
| `shadow.c` | `GX_CTF_R4` | dynamic shadow map (P-639, unchanged) |
| `lbrefract.c` | `GX_TF_RGB565` | refraction screen copy |
| `gm_1832.c` / `gm_1798.c` / `gmregclear.c` / `gmregtyfall.c` | `GX_TF_RGB5A3`, `GX_TF_Z24X8` | result/menu portraits and depth snapshots |
| `cmsnap.c` / `grizumi.c` / `grpstadium.c` | `GX_TF_RGB565` | camera/reflection snapshots |
| `ifmagnify.c` | `HSD_ImageDesc` (RGB565) | magnifier scope |

## Implemented (P-674)

`gx_gl.c:copy_tex_encode` now writes every format's GX tiling from the
(bottom-up GL) EFB read:

- **I4/I8/IA4/IA8** use Aurora's `intensity()`: ITU-R BT.601
  `0.257R + 0.504G + 0.098B + 16/255`, and `quantize4()` for the 4-bit
  channels.  The old port copied the red channel only, and only for
  `GX_CTF_R4`.  Byte placement matches `gx/texture.c`:
  - I4: 8x8 tile, high nibble first;
  - I8/IA4: 8x4 tile, 1 byte/px (IA4 high nibble = alpha, low = intensity);
  - IA8: 4x4 tile, bytes `[alpha, intensity]`.
- **RGB5A3**: 0x8000 form for `a >= 0xE0`, else 4/4/4/3.  This is the format
  of the result-screen portraits and menu snapshots (`gm_1798.c`,
  `gmregclear.c`), so those textures are no longer left at their initial
  memory.
- **RGB565/R4/RGBA8** kept byte-identical to the previous code (passes 1-4
  unchanged), now sharing the same tile layout table.
- **Copy clear**: `GXSetCopyClear` is still not latched, but every
  destination texel is written from the scaled source, so the hardware clear
  can never show through; the row is N/A rather than approximated.

## Documented deviation (filed as P-682)

`GX_TF_Z24X8` copies (`gm_1832.c` copies the EFB depth into a Z24X8 image,
then `sobjlib.c` samples it as a `GXSetZTexture` depth source) are not
encoded, and `native/gx/texture.c` has no Z24X8 decode.  Closing this needs
a 24-bit depth read (`glReadPixels(GL_DEPTH_COMPONENT)`) plus the Z24X8 tile
layout, and a fixture that reaches the shadow-object library.  The current
Z8 path (`displayfunc.c` erase rect) is covered by `decomp_efb` pass 2.

`GXSetZTexture` REPLACE is covered by pass 2 and the source `Z8` texture is
decoded as I8.  ADD has no caller in Melee and stays untested until P-682.

## Re-run

```sh
ctest --test-dir build/native -R decomp_efb
./build/native/test_decomp_render --efb
```

## P-682: Z24X8 depth snapshots and `GX_ZT_ADD`/bias

`GXCopyTex` to `GX_TF_Z24X8` (`gm_1832.c`) now captures the EFB depth:

- The EGL pbuffer's depth is not readable through `glReadPixels` (Mesa
  returns `GL_INVALID_OPERATION` for `GL_DEPTH_COMPONENT` there); the port
  blits the depth into a private `DEPTH_COMPONENT24` renderbuffer with
  `glBlitFramebuffer` and reads it as `GL_UNSIGNED_INT` (the only portable
  ES3 depth pair), normalizing by 2^32-1.  The result is written with the
  same 64-byte 4x4 tile shape as RGBA8: `[high, mid]` block then
  `[low, 0]`; `decode_z24x8` (P-682) mirrors it and exposes the top byte as
  red for the `GXSetZTexture` sampler.
- `GX_ZT_ADD` (`gl_FragDepth = incoming + texel + bias`) and the 24-bit
  bias (`bias/0xFFFFFF` added to the normalized depth) are covered by
  passes 11 frame B/C.  `REPLACE` ignores the incoming depth and applies the
  same bias, matching Dolphin's `zCoord = zbias + (Add ? zCoord : 0)`.

`ctest decomp_efb` pass 11 checks the snapshot bytes (0.65/0.85 -> top bytes
166/217), the decoder, an ADD erase (0.5 + 0.5) that lets a 0.9-depth quad
through, and a REPLACE erase (0.2) plus 0.1 bias that lets a 0.25-depth quad
through.  Flips: forcing the readback depth to 1.0 fails the snapshot bytes;
treating ADD as REPLACE fails the ADD pixel; dropping the bias fails the
REPLACE+bias pixel.  Note the renderer caches decoded textures by source
pointer, so tests that rewrite a texture buffer in place must use distinct
buffers (or call `gx_gl_clear_textures`).

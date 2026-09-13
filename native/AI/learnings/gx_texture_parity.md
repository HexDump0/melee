# Texture expansion and per-object texobj state (P-675)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Ported from:** Aurora `lib/gfx/texture_convert.cpp` (`ExpandTo8<3/4/5/6>`,
`TextureDecoderRGB565/RGB5A3`) and `lib/dolphin/gx/GXTexture.cpp`
(per-object state) at pinned `749d6ee7a22bdfab78c8ece9047bca5d79aa72ca`.

Evidence: `ctest decomp_gx_direct` (texobj readback + expansion) and the
`decomp_render` screenshot baseline.  Sensitivity flips: restoring the pending
texobj getter fails `texobj readback a=(4,4,4,0) b=(4,2)`; restoring
`v*255/31` fails `expand r=106 g=69 (want 107/69)`.

## 1. Bit-replication channel expansion

The GX texture unit expands integer channels by bit replication, not by
rescaling to 255: 5-bit `(n<<3)|(n>>2)`, 6-bit `(n<<2)|(n>>4)`, 4-bit
`(n<<4)|n`, 3-bit `(n<<5)|(n<<2)|(n>>1)`.  The old `(n*255)/31` form is off
by one LSB for some values (5-bit 13 → 106 instead of 107), which shifts
every RGB565 image, every TLUT entry and the RGB5A3 alpha by up to 1/255.

Changed in `native/gx/texture.c` (`rgb565`/`rgb5a3` image decode, both used
by the compiled `gx_texture_decode`) and `gx_gl.c:expand_palette` (TLUTs).
Screenshot delta against the P-674 build: RMSE 0.00056 over the model
region, 82 differing pixels — all channel rounding.

## 2. Per-object `GXTexObj` state

The SDK's `GXTexObj` is 32 opaque bytes (`GXStruct.h: u32 dummy[8]`); the
hardware stores texture state in the object and `GXLoadTexObj` reads it.  Our
HLE used a single "pending" object filled by `GXInitTexObj` and returned by
every `GXGetTexObj*`, so any texture initialized in between corrupted the
read-back.  `sobjlib.c:220/287` and `lbspdisplay.c:401/432` read stored
texobjs long after initialization, so the bug was reachable in HUD/effect
paths.

`gx_hle.c` now keeps a 128-entry table keyed by the caller's pointer
(round-robin/LRU on overflow) holding the `GxHleTexture` plus the CI
`tlut_name`.  `GXInitTexObj`/`CI`/`LOD` write the slot; `GXLoadTexObj` and
every `GXGetTexObj*` read it.  The per-name TLUT table still resolves the
palette at load time.  The pending object remains only as a fallback for
never-initialized handles.

## Not changed / documented

- **LOD bias clamp and edge LOD**: `bias_clamp`/`edge_lod` are captured but
  still not distinguished (`GXInitTexObjLOD`); the shader uses
  `texture(..., bias)` plus `GL_TEXTURE_MIN_LOD/MAX_LOD`.  Aurora applies
  them through the sampler mode; our approximation only matters if a texture
  sets bias outside `[min_lod, max_lod]` with clamping, which no observed
  archive does (`minLOD/maxLOD == 0` for all 967 `Pl*Nr` textures).
- **Z24X8 decode**: filed as P-682 (EFB depth snapshots).
- **Mip generation**: assets carry no mip chains; the port's
  `glGenerateMipmap` is a deliberate deviation recorded in the matrix.

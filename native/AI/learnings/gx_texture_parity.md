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
- **Mip generation (corrected by P-763):** stage assets do carry authored mip
  chains. A 128x128 CMPR image followed by its levels through 1x1 occupies
  exactly `0x2b00` bytes, matching the spacing between archive image pointers.
  The GL backend now decodes each registered archive level in order and uses
  `glGenerateMipmap` only for runtime buffers whose readable extent is
  unknown. `decomp_efb` forces LOD 1 on a black base/white authored level, so
  restoring generation makes the probe read black instead of white.

# Display-copy deflicker filtering and screen-door art (P-763)

**Date:** 2026-09-15
**Agent:** codex (gpt-5)

Yoshi's Story (`GrSt.dat`) and Yoshi's Island (`GrYt.dat`) deliberately render
foliage, waves, grass and background sprites with alternating EFB pixels. The
raw EFB therefore looks sparse or transparent. Melee never presents that raw
image: `HSD_VICopyEFB2XFBPtr` calls `GXSetCopyFilter` before `GXCopyDisp`, and
`GXNtsc480IntDf` supplies `vf=true` with coefficients
`{8,8,10,12,10,8,8}`. Both functions were stubs in the HLE, so the GL window
skipped the resolve that the art was authored for.

The hardware groups the seven programmed coefficients into three source-row
weights: `[0]+[1]` for the row above, `[2]+[3]+[4]` for the current row and
`[5]+[6]` for the row below, dividing the result by 64. For Melee this is the
familiar 16/32/16 (quarter/half/quarter) filter. `gx_hle.c` now captures the
filter only when a frame reaches `GXCopyDisp`; `gx_gl.c` copies the completed
EFB to a private texture and performs that three-row RGB resolve immediately
before presentation. Mid-frame `GXCopyTex` reads remain raw, and alpha remains
the current row, matching the GX copy boundary.

`decomp_efb` draws 480 alternating black/white scanlines and requires the
filtered center pixel to be about 128. The two stage captures are
`/tmp/codex-grst-filtered2.bmp` and `/tmp/codex-gryt-filtered2.bmp`; a real
60-frame game render also exercised the captured `GXCopyDisp` path under
ASan/UBSan.

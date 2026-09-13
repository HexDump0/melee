# GX coverage matrix (P-671)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)
**Scope:** every GX surface item Melee (the compiled decomp under `decomp/src`)
actually calls, mapped to our HLE (`native/decomp/gx/gx_hle.c`,
`native/decomp/gx/gx_gl.c`, `native/gx/texture.c`, `native/platform/gx_vi.c`)
and to the Aurora reference at the pinned commit
`749d6ee7a22bdfab78c8ece9047bca5d79aa72ca` (checkout: `aurora-reference/`,
git-ignored; a second copy lives in `/tmp/opencode/aurora-reference`).

This matrix is the worklist and the acceptance metric for the renderer parity
program (ADR-0017, `workflows/renderer_parity.md`). Rows marked **APPROX** or
**STUB** must either close under P-672..P-681 or be documented as a deliberate
deviation in `learnings/`.

## Method

- Surface enumeration: `rg -o 'GX[A-Za-z0-9_]+' decomp/src/` (528 identifiers)
  intersected with the function prototypes in
  `decomp/extern/dolphin/include/dolphin/gx/*.h` → **100 called functions**,
  plus the inline direct-mode writers (`GXVert.h`/`GXWGFifo`, captured by
  `native/decomp/shim/dolphin/gx/GXVert.h`).
- Status is per *behavior Melee uses*, not per API completeness. A function
  whose SDK effect is irrelevant on the host (write-gather pipe, XFB copies,
  field mode) can be **N/A** even though the stub body is empty.
- Exactness claims for command capture were re-checked against the SDK
  decompilation in `decomp/extern/dolphin/src/dolphin/gx/` (the spec the game
  ran on); Aurora is the tie-breaker for evaluation semantics.
- Regressions: `ctest -R 'decomp_render|decomp_gx_direct|decomp_efb|decomp_stage'`,
  `test_decomp_render --direct/--shot/--dump-draws`, plus per-task tests.

Legend: **EXACT** = behavior matches the SDK/Aurora semantics;
**APPROX** = renders but the formula/state is known to differ;
**STUB** = accepted and dropped; **N/A** = correct to drop, with a reason.

## 1. Geometry and vertex decode

| GX item (Melee usage) | Status | Our location | Aurora reference | Action |
|---|---|---|---|---|
| `GXBegin`/`GXEnd`/direct writers (`GXPosition*`/`GXColor*`/`GXNormal*`/`GXTexCoord*`, 82 sites) | EXACT | `GXVert.h` shim → `GXPortWGFifo*`; `GXBegin` snapshot `gx_hle.c:1580` | `lib/dolphin/gx/GXVert.cpp`, `lib/gx/command_processor.cpp` | P-677 broadens coverage |
| `GXCallDisplayList` (PObj streams) | EXACT | `gx_hle.c:1592` | `lib/gx/dl.cpp` (`run_display_list`) | — |
| `GXClearVtxDesc`/`GXSetVtxDesc(v)` | EXACT | `gx_hle.c:1385/1395/1409` | `lib/dolphin/gx/GXAttr.cpp` | — |
| `GXSetVtxAttrFmt(v)` incl. color enum, NBT=9, frac | EXACT | `gx_hle.c:1417`; `read_vertex` `:325` | `lib/gx/attr_fmt.cpp`, `shader.cpp:attr_load` | — |
| `GXSetArray` (indexed attrs) | EXACT | `gx_hle.c:1439` | `lib/dolphin/gx/GXAttr.cpp` | — |
| Attribute byte order | EXACT for HSD streams (differential 17,724 verts, 0 mismatch) | `desc_order` capture | fixed CP order in `attr_fmt.cpp` | note: order is taken from `GXSetVtxDesc` call order; HSD authors the same order. Revisit only if a desync appears (G-062 class) |
| `GX_VA_NBT` binormal/tangent | EXACT | `read_vertex` `:355` (9 comps) | `attr_load_nbt_slice` | Melee never uses `GX_NRM_NBT3` |
| Primitives QUADS/TRIANGLES/STRIP/FAN | EXACT | `exec_primitive` `gx_hle.c:732` | `lib/gx/pipeline.cpp` | — |
| `GX_LINES`/`GX_LINESTRIP`/`GX_POINTS` (12/10/6 sites in `lb_*`, `psdisp`) | **STUB** (silently dropped) | `exec_primitive` default branch `:792` | `lib/gx/pipeline.cpp` primitive map; `GXGeometry.cpp` | **P-680** |
| `GXSetLineWidth`/`GXSetPointSize`/`GXEnableTexOffsets` | **STUB** | `gx_hle.c:2106/2112/2118` | `GXGeometry.cpp`; `shader.cpp` line/point tex offset | **P-680** |
| Cull mode (PObj flags) | EXACT | `gx_hle.c:891`, `gx_gl.c:1182` | `lib/dolphin/gx/GXCull.cpp` | — |

## 2. Transforms, viewport, projection

| GX item | Status | Our location | Aurora reference | Action |
|---|---|---|---|---|
| `GXLoadPosMtxImm`/`GXLoadNrmMtxImm`(+3x3)/`GXLoadTexMtxImm`, `GXSetCurrentMtx` | EXACT | `gx_hle.c:1320-1383` | `GXTransform.cpp` | — |
| Position/normal transform | EXACT | `transform_vertex` `gx_hle.c:621` | `lib/gx/shader.cpp` vtx XFR | — |
| Texcoord generator: MTX2x4 (z=1 before post), MTX3x4, bump, SRTG, mtx/postmtx chain, normalize | EXACT (closed by P-672) | `texgen_coord` `gx_hle.c:496`; SRTG lit source in FS `gx_gl.c:gx_coord_uv` | `shader.cpp:1210-1295` (Aurora sets `z=1` for MTX2x4, normalizes between the matrices) | learning `gx_indirect_toon.md`; `ctest decomp_gx_direct` |
| `GX_TG_MTX3x4` projective divide incl. q==0 quirk | EXACT (closed by P-672) | `texgen_coord` | `shader.cpp` `tex_uvw` + `/w`; Dolphin q==0 clamp | `ctest decomp_gx_direct` MTX3x4 fixture |
| `GXSetProjection` (perspective + ortho) | EXACT | `gx_hle.c:852` | `GXTransform.cpp` | — |
| `GXProject` | EXACT (SDK formula) | `gx_hle.c:817` | `GXTransform.cpp` | — |
| `GXGetViewportv` | EXACT | `gx_hle.c:847` | `GXTransform.cpp` | — |
| `GXSetProjectionv`/`GXGetProjectionv` | EXACT (closed by P-678) | `gx_hle.c` stores the packed `{A..F}`; `GXGetProjectionv` returns `{type,A..F}` | `GXGet.cpp`, SDK `GXTransform.c` | regression: `ctest decomp_gx_direct` (G-131) |
| `GXSetViewport`/`GXSetViewportJitter` | EXACT | `gx_hle.c:859/876`, `gx_gl.c:apply_viewport` | `GXTransform.cpp` | — |
| `GXSetScissor` | EXACT | `gx_hle.c:883`, `gx_gl.c:1718` | `lib/gx/regs.cpp` scissor | — |
| `GXSetCoPlanar` | N/A | `gx_hle.c:897` | `GXGeometry.cpp` | no-op on hardware too |
| `GXSetFieldMode` | N/A | `gx_hle.c:2092` | `GXSet.cpp` | `GXNtsc480IntDf.field_rendering == 0`; progressive only |
| `GXSetMisc(1,8)` (`gmmain.c:155`) | N/A | `gx_hle.c:2100` | `GXMisc.cpp` | write-gather-pipe flush token |

## 3. TEV combiner

| GX item | Status | Our location | Aurora reference | Action |
|---|---|---|---|---|
| `GXSetNumTevStages`/`GXSetTevOrder` (8 stages) | EXACT | `gx_hle.c:1092/1098`; `gx_gl.c:452` | `GXTev.cpp`, `regs.cpp:bp_tev_*`, `shader_info.cpp` | — |
| Color/alpha in/op (ADD/SUB, bias, scale, clamp, out_reg) | EXACT | `gx_gl.c:492-525` | `shader.cpp` TEV expr | — |
| `GX_TEV_COMP_*` compare ops | N/A | treated as ADD | `shader.cpp:375` | Melee never passes one (`rg GX_TEV_COMP decomp/src` empty) |
| TEV color/K registers, S10 | EXACT | `gx_hle.c:1174/1186/1198` | `regs.cpp:bp_tev_reg` (sext11/255) | — |
| KCSEL/KASEL full select table | EXACT | `gx_gl.c:kfrac/konst_color` | `shader_info.cpp` sampled K colors | — |
| Swap tables/modes | EXACT | `gx_gl.c:swap4` | `regs.cpp:bp_tev_ksel` | — |
| `GXSetTevOp` shorthands | EXACT | `gx_hle.c:1254` | `GXTev.cpp:GXSetTevOp` | — |
| `GXSetTevClampMode` | N/A | `gx_hle.c:1289` | `GXTev.cpp` | mode is fixed on hardware; game passes 0 |
| `GXSetTevDirect` (`lbrefract.c:453/464`) | EXACT (closed by P-672) | `gx_hle.c:GXSetTevDirect` calls `GXSetTevIndirect` with the disabled set | `GXBump.cpp` | capture assertion in `ctest decomp_gx_direct` |
| Indirect evaluation (`GXSetNumIndStages`/`SetIndTexOrder`/`SetIndTexCoordScale`/`SetIndTexMtx`/`SetTevIndirect`) | EXACT for Melee's use (closed by P-672): ITF_8/5/4/3, ITB_*, ITM_0..2, ITW_*, add_prev, ITS_1..256 | capture `gx_hle.c:1619-1740`; FS `gx_gl.c` indirect block | `GXBump.cpp`, `shader.cpp:1297-1540` | `ctest decomp_efb` pass 5; dynamic S/T matrices + alpha bump unused (deviation) |
| Toon texture (`GX_TG_SRTG`, `tobj.c:538`; `grpura.c` only) | EXACT (closed by P-672): lit-raster UV substitution | `gx_gl.c:gx_coord_uv`/`u_coord_srtg` | `shader.cpp:1259` | not exercised by a current scene (GrPu-stage only); learning `gx_indirect_toon.md` |

## 4. Channels and lights

| GX item | Status | Our location | Aurora reference | Action |
|---|---|---|---|---|
| `GXSetNumChans` | EXACT | `gx_hle.c:987` | `GXLighting.cpp` | — |
| `GXSetChanCtrl` 4-slot state (incl. combined COLORxA x) | EXACT | `gx_hle.c:1015` | `GXLighting.cpp:GXSetChanCtrl` | — |
| `GXSetChanAmbColor`/`GXSetChanMatColor` | EXACT (combined ids mirror alpha) | `gx_hle.c:1046/1069` | `GXLighting.cpp` | — |
| Channel lighting evaluation (`GX_DF_NONE/SIGN/CLAMP`, amb/mat SRC_REG/VTX) | EXACT | VS `channel_raster` `gx_gl.c:238` | `GXLighting.cpp` + `shader.cpp:lighting_func` | — |
| Light objects: attn a/k, position, color | EXACT | `gx_hle.c:1833-1873` | `GXLighting.cpp` | — |
| `GXInitLightDir` | EXACT capture, documented sign convention | `gx_hle.c:GXInitLightDir` stores the API value; the shader consumes it as H (HSD computes `half`; prototype-validated) | `GXLighting.cpp` stores `-input`; Dolphin's Spec path reads the register | learning `gx_lighting_specular.md`; convention documented, not scheduled |
| `GXInitLightDistAttn` | EXACT (closed by P-673) | `gx_hle.c:GXInitLightDistAttn` — SDK OFF guards + GENTLE/MEDIUM/STEEP | `GXLighting.cpp` (SDK `GXLight.c`) | `ctest decomp_gx_direct` light-math cases |
| `GXInitLightSpot` | EXACT (closed by P-673) | `gx_hle.c:GXInitLightSpot` — out-of-range cutoff → `GX_SP_OFF` | `GXLighting.cpp` (SDK `GXLight.c`) | `ctest decomp_gx_direct` spot cases. (Aurora's RING1 `a0` sign differs from the SDK; our RING1 matches the SDK — keep ours) |
| Channel-1 specular evaluation (`GX_AF_SPEC`) | EXACT for Melee's use (closed by P-673): per-channel tint, `max(0, a(t)/k(t))`, N·L gate, DF-none | VS `channel_raster` spec branch `gx_gl.c` | `shader.cpp:lighting_func`; Dolphin `LightingShaderGen` AttenuationFunc::Spec | `ctest decomp_efb` pass 6. Spot-light cones (`a.y/a.z != 0`) are approximate → P-681 |
| `GXLoadLightObjImm` (8 lights) | EXACT | `gx_hle.c:1962` | `GXLighting.cpp` | — |
| `GXInitSpecularDir`/`HA` | N/A | not called by Melee (HSD builds the half vector itself) | `GXLighting.cpp` | add only if a call site appears |

## 5. Textures, TLUTs, samplers

| GX item | Status | Our location | Aurora reference | Action |
|---|---|---|---|---|
| `GXInitTexObj`/`CI`/`LOD`/`GXLoadTexObj` | APPROX (pending-object model) | `gx_hle.c:1707-1789` | `GXTexture.cpp` (per-object fields) | **P-675**: `GXGetTexObj*`/`GXLoadTexObj` must read the passed object; `sobjlib.c:220/287` and `lbspdisplay.c:401/432` read stored texobjs later (pending fields can be stale) |
| `GXInitTlutObj`/`GXLoadTlut` | EXACT (name → slot; names `% GX_HLE_MAX_TLUTS`) | `gx_hle.c:1748/1758` | `GXTexture.cpp` | — |
| Texture decode: CMPR, CI4/CI8, I4/I8, IA4/IA8, RGB565, RGB5A3, RGBA8 (tiled) | EXACT (verified across 967 textures) | `native/gx/texture.c` | `lib/gfx/texture_convert.cpp` | — |
| 5/6-bit color expansion | **APPROX (1 LSB)** | `expand5/expand6` `native/gx/texture.c:20-21`, `expand_palette` `gx_gl.c:824` use `v*255/31`, `v*255/63`; hardware/Aurora bit-replicate `(v<<3)|(v>>2)` | `ExpandTo8<5/6>` `texture_convert.cpp:113` | **P-675** (changes screenshot baselines by ≤1/255 per channel) |
| TLUT palette formats (RGB565/RGB5A3/IA8), authored entry counts | EXACT | `gx_gl.c:expand_palette`, `model.c` tlut path | `tex_palette_conv.cpp` | entry counts are not PoT and must not be rounded |
| Z8 image decode (z-texture erase, `displayfunc.c:541`) | EXACT (as I8) | `texture.c:228` | `tex_copy_conv.cpp:FragZ8` | — |
| Z24X8 image decode (`sobjlib.c:299`, `gm_1832.c:804`) | documented deviation (P-682) | absent in `texture.c` | depth snapshot path (`snapshot_depth`) | learning `gx_efb_copy.md`; P-682 |
| Wrap modes CLAMP/REPEAT/MIRROR | EXACT | `gx_gl.c:wrap_to_gl` | `GXTexture.cpp` / `regs.cpp` | — |
| Min/mag filters + CI mip downgrade | EXACT | `gx_gl.c:min_filter_to_gl` | `GXTexture.cpp:GXInitTexObjLOD` | — |
| LOD bias (shader `texture(...,bias)`), min/max LOD, edge LOD, bias clamp | APPROX | `gx_gl.c:1077`, FS bias `:465` | `GXTexture.cpp` mode0/mode1 decode | **P-675**: `do_edge_lod` and `bias_clamp` are not distinguished |
| Anisotropy `GX_ANISO_1/2/4` | EXACT | `gx_gl.c:1079` (`1<<v`) | `GXTexture.cpp` | — |
| Mip generation | N/A (asset mipmap==0 for all 967 Nr textures; generated mips are a port choice) | `glGenerateMipmap` when flag set | Aurora generates mips from the GX mip chain | documented deviation |
| `GXInvalidateTexAll`/`GXInvalidateVtxCache`/`GXInvalidateTexRegion` | N/A | no-ops | `resource_cache.cpp` | our cache key is the source pointer; EFB copies invalidate explicitly (`gx_gl.c:899`) |
| `GXGetTexBufferSize` | EXACT (SDK copy) | `native/platform/gx_vi.c:106` | `GXTexture.cpp` | — |

## 6. EFB copies and Z-texture

| GX item | Status | Our location | Aurora reference | Action |
|---|---|---|---|---|
| `GXSetTexCopySrc`/`Dst` | EXACT capture | `gx_hle.c:2070/2078` | `GXCpu2Efb.cpp` | — |
| `GXCopyTex` RGB565 | EXACT (truncating re-encode, tiled) | `gx_gl.c:1512` | `tex_copy_conv.cpp:FragRGB565` | — |
| `GXCopyTex` R4 (shadow map, `shadow.c:108`) | EXACT (red channel, high nibble first) | `gx_gl.c:1529` (GPU blit `:1394`) | `FragR4` (`quantize4 = floor(v*16)/15`) | keep; add quantization regression in P-674 |
| `GXCopyTex` RGBA8 | EXACT | `gx_gl.c:1553` | `FragPassthrough`/blit | — |
| `GXCopyTex` I4/I8/IA4/IA8 | EXACT (closed by P-674): BT.601 `intensity()` + `quantize4()`, tiling matching the decoders | `gx_gl.c:copy_tex_encode` | `tex_copy_conv.cpp:FragI4/I8/IA4/IA8` | `ctest decomp_efb` pass 7 |
| `GXCopyTex` RGB5A3 (`gm_1832.c:802`, `gm_1798.c` portraits) | EXACT (closed by P-674) | `gx_gl.c:copy_tex_encode` | GPU format conversion / `GX_TF_RGB5A3` decoder inverse | `ctest decomp_efb` pass 7 |
| `GXCopyTex` Z24X8 (`gm_1832.c:804`) | documented deviation (P-682) | not encoded; `texture.c` has no Z24X8 decode | depth snapshot (`snapshot_depth`) | learning `gx_efb_copy.md`; P-682 |
| Copy clear (`GXCopyTex(..., clear=GX_TRUE)`) | N/A | every destination texel is written from the scaled source, so the clear cannot show through | `Lib/Clear` + `GXSetCopyClear` | documented (P-674) |
| `GXSetCopyClear` (`video.c` XFB clear) | N/A today (only feeds `GXCopyDisp`) | `gx_hle.c:2024` | `GXFrameBuffer.cpp` | becomes live with P-674 |
| `GXSetZTexture` REPLACE/ADD, Z8/Z16/Z24X8 (4 sites) | APPROX (REPLACE/Z8 verified, pass 2) | dedicated depth program `gx_gl.c:305`, `draw_ztex:1578` | `GXTev.cpp:GXSetZTexture` is a TODO in Aurora; decomp `GXTev.c:333` + `GXPixel` semantics | bias/format/ADD edges need the Z24X8 source → P-682 |
| `GXSetPixelFmt` | N/A | `gx_hle.c:2086` | `regs.cpp:decode_pixel_fmt` | EFB format is virtualized (always RGBA8); `GXNtsc480IntDf.aa==0` so RGB565_Z16 never applies |
| `GXSetCopyFilter`/`GXSetDispCopyGamma`/`GXSetDispCopy*`/`GXCopyDisp` | N/A | stubs | `GXFrameBuffer.cpp`; Aurora `GXSetCopyFilter` is a no-op too | XFB presentation is the host present hook (`boot_platform_set_present_hook`); filter would only affect `GXCopyDisp` |
| `GXPixModeSync` | N/A | empty | `GXPixel` | host ordering automatic |

## 7. Render state

| GX item | Status | Our location | Aurora reference | Action |
|---|---|---|---|---|
| `GXSetBlendMode` (BLEND/SUBTRACT, all factors) | EXACT | `gx_hle.c:932`, `gx_gl.c:1204` | `GXPixel.cpp` | — |
| `GX_BM_LOGIC` + `GX_LO_COPY` (`displayfunc.c:560`) | EXACT (opaque overwrite; LO_COPY ignores blend) | `gx_gl.c:1209` | GLES has no logic ops; Aurora maps to pipeline state | document; other logic ops unused |
| `GXSetZMode`/`GXSetZCompLoc` | EXACT / N/A | `gx_hle.c:918/926` | `GXPixel.cpp` | ZCompLoc only orders early-z; alpha test after TEV is equivalent |
| `GXSetAlphaCompare` (AND/OR/XOR/XNOR + 8-bit refs) | EXACT | `gx_hle.c:942`, FS `gx_gl.c:527` | `GXPixel.cpp` | — |
| `GXSetDstAlpha` | EXACT | `gx_hle.c:911`, FS `:553` | `GXPixel.cpp` | — |
| `GXSetColorUpdate`/`GXSetAlphaUpdate` (via color mask) | EXACT | `gx_gl.c:1227` | `GXPixel.cpp` | — |
| `GXSetDither` | N/A | `gx_hle.c:953` | Aurora shader ignores it too | 8-bit host framebuffer; no visible banding in captures |
| Fog color/type linear | APPROX | FS `gx_gl.c:536` | `GXPixel.cpp:GXSetFog` + `shader.cpp:1537` | **P-679**: hardware fog uses the packed `a/(b−z)−c` form (perspective) / `a·z+c` (ortho), not `(end−eye)/(end−start)` |
| Fog exp/exp2/rev variants | APPROX | FS `:543` uses `exp(−density·d)` | `shader.cpp`: `1−exp2(−8·f)`, `1−exp2(−8·f²)`, `exp2(−8(1−f))`, `exp2(−8(1−f)²)` | **P-679** |
| `GXSetFogRangeAdj`/`GXInitFogAdjTable` (`fog.c:51/80`) | **STUB** | `gx_hle.c:973/980` | `GXPixel.cpp` (SDK table math), `shader.cpp` `fog_range_base` indexed by `in.pos.x` | **P-679**; `HSD_FogDesc.fogadjdesc` is currently nulled by the converter (`hsd_convert.c:161`) — file the converter side with P-662/P-658, renderer side under P-679 |

## 8. Host-equivalent / lifecycle

| GX item | Status | Our location | Note |
|---|---|---|---|
| `GXInit` | N/A (returns a fake Fifo) | `gx_hle.c:800` | defaults come from `gx_hle_reset_state` + `test_decomp_render`; engine re-emits per-material state |
| `GXSetDrawDone`/`GXWaitDrawDone`/`GXDrawDone` | EXACT host semantics (immediate) | `native/platform/gx_vi.c:143-155` | draws complete synchronously; `HSD_VIGXSetDrawDone` gates are real |
| `GXSetDrawSync`/callbacks, `GXPixModeSync` | N/A | `gx_hle.c:2129`, `gx_vi.c` | no token consumer on host |
| `GXSetGPMetric`/`GXClearGPMetric` | N/A | `gx_hle.c:2137` | profiler only |
| `GXSetFifoObj`, `GXFlush`, `GXAbortFrame`, `GXResetWriteGatherPipe` | N/A | not called by the port's game paths | HSD doesn't call them at runtime |

## 9. Prioritized gap list (all from the tables above)

| Task | Rows | Verification plan |
|---|---|---|
| ~~**P-672** indirect + toon + projective texgen~~ **DONE** | §1/§3 | `ctest decomp_gx_direct` (texgen/capture) + `ctest decomp_efb` pass 5 (GPU indirect); learning `gx_indirect_toon.md` |
| ~~**P-673** lighting/specular~~ **DONE** | §4 | `ctest decomp_gx_direct` light-object cases + `ctest decomp_efb` pass 6 (tinted spec); learning `gx_lighting_specular.md`. Follow-up: P-681 spot cones |
| ~~**P-674** EFB copy formats~~ **DONE** (Z24X8 → P-682) | §6 | `ctest decomp_efb` pass 7; learning `gx_efb_copy.md` |
| **P-675** textures/samplers | expand5/expand6 bit replication, per-object texobj state (`GXGetTexObj*`), edge-lod/bias-clamp, TLUT bounds | `ctest decomp_stage`/`decomp_render` baselines + new unit assertions on decode of a synthetic 5/6-bit pattern; `sobjlib`/`lbspdisplay` object read-back |
| **P-676** perf | state-change batching, redundant binds, uniform upload diffing, VBO stream | `[match] frame N ... render=Xms` before/after, frame-718 pixel parity |
| **P-677** harness | cross-character/stage/effect parity artifacts | one command per slice producing a pass/fail artifact |
| ~~**P-678** `GXGetProjectionv` packed layout~~ **DONE** | §2 | `ctest decomp_gx_direct` asserts both layouts + `GXSetProjectionv` round trip; G-131 |
| **P-679** fog math + range adj | §7 | shader vs `shader.cpp` formula comparison on a synthetic depth ramp; range table read |
| **P-680** lines/points primitives | §1 | `test_decomp_render --direct` line/point fixture asserting emitted geometry; `psdisp` HUD capture |
| converter follow-up (P-658/P-662 family) | `HSD_FogDesc.fogadjdesc` nulled | converter field table for `Gr*` fog-adj descriptors; renders P-679 reachable |

## 10. Explicit deviations (documented, not scheduled)

- **EFB format is always RGBA8**: `GXSetPixelFmt` (RGB565_Z16) and 16-bit Z
  precision are not emulated. All visual checks pass with RGB8/A8; `GXCopyTex`
  re-encodes to the requested GX format. Aurora's WebGPU backend keeps a real
  pixel format, but that is an architectural difference, not a portable
  formula (ADR-0017 §"Where Aurora behavior is architectural").
- **XFB copies / VI present**: `GXSetDispCopy*`, `GXCopyDisp`, `GXSetCopyFilter`,
  `GXSetCopyClear`, `GXSetDispCopyGamma` are no-ops; the host present hook
  reads the EFB. Logic-op `GX_LO_COPY` is rendered as an opaque overwrite,
  which is what COPY means.
- **Texture residency / TMEM**: we decode to RGBA8 and cache by source
  pointer; Aurora sample-converts and caches by GX resource. Not a formula.
- **`GXInit` defaults / no write-gather pipe**: the compiled engine emits the
  state it needs per material; `gx_hle_reset_state` covers pre-first-draw.
- **No mip chains in character assets**: `HSD_ImageDesc.mipmap == 0` for all
  967 Nr textures; our `glGenerateMipmap` is a port choice.

## Evidence / reproduce

```sh
# surface census
rg -o 'GX[A-Za-z0-9_]+' decomp/src/ | sed 's/.*://' | sort -u
# our implementation locations
rg -n 'void GX' native/decomp/gx/gx_hle.c native/platform/gx_vi.c
# baseline
ctest --test-dir build/native --output-on-failure        # 17/17
MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets # PASS
```

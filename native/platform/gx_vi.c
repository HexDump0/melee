/*
 * S1 GX/VI backend: the real behaviors that the boot loop depends on, plus
 * log-only stubs for the GX command surface.
 *
 * Real here:
 *   - GXGetTexBufferSize (copied from extern/dolphin/src/dolphin/gx/
 *     GXTexture.c: GXGetTexBufferSize and __GXGetTexTileShift),
 *   - GXSetDrawDone/GXSetDrawDoneCallback (draw completion is immediate on
 *     the host, which is what lets HSD's XFB machinery advance),
 *   - the VI retrace callbacks and count; VIWaitForRetrace drives HSD's XFB
 *     state machine and the S1 frame budget.
 *
 * Everything else writes GX state into nothing; each call is recorded by the
 * triage logger.  This file replaces native/decomp/hsd_port_stubs.c for the
 * product/boot targets; that probe file remains for the S0 test only.  S2
 * replaces these with the FIFO/state backend grown from native/gx/.
 */
#include <dolphin/gx.h>
#include <dolphin/vi.h>

#include "decomp/boot/boot_triage.h"
#include "platform/platform.h"

/* ------------------------------------------------------------ render modes
 * Verbatim from extern/dolphin/src/dolphin/gx/GXFrameBuf.c (GXNtsc480IntDf
 * 0x43, GXNtsc480Int 0x52, GXNtsc480Prog 0x70). */

GXRenderModeObj GXNtsc480IntDf = {
    0,    640,  480,  480,  40,   0,    640, 480, 1, 0, 0,
    { { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 },
      { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
    { 8, 8, 10, 12, 10, 8, 8 }
};

GXRenderModeObj GXNtsc480Int = {
    0,    640,  480,  480,  40,   0,    640, 480, 1, 0, 0,
    { { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 },
      { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
    { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc480Prog = {
    2,    640,  480,  480,  40,   0,    640, 480, 0, 0, 0,
    { { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 },
      { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
    { 0, 0, 21, 22, 21, 0, 0 }
};

/* --------------------------------------------------------------- GX specials */

static u8 gx_fifo_storage[512] __attribute__((aligned(32)));
static GXDrawDoneCallback gx_draw_done_callback;
static VIRetraceCallback vi_pre_callback;
static VIRetraceCallback vi_post_callback;
static u32 vi_retrace_count;

GXFifoObj* GXInit(void* base, u32 size)
{
    (void) base;
    (void) size;
    boot_triage_stub("GXInit", BOOT_CAT_GX);
    return (GXFifoObj*) gx_fifo_storage;
}

/* extern/dolphin/src/dolphin/gx/GXTexture.c: __GXGetTexTileShift (0x2B) */
static void gx_tex_tile_shift(u32 format, u32* row_tile_s, u32* col_tile_s)
{
    switch (format) {
    case GX_TF_C4:
    case GX_TF_C8:
    case GX_TF_C14X2:
    case GX_TF_CMPR:
    case GX_CTF_R4:
    case GX_CTF_Z4:
        *row_tile_s = 3;
        *col_tile_s = 3;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_Z8:
    case GX_CTF_RA4:
    case GX_TF_A8:
    case GX_CTF_R8:
    case GX_CTF_G8:
    case GX_CTF_B8:
    case GX_CTF_Z8M:
    case GX_CTF_Z8L:
        *row_tile_s = 3;
        *col_tile_s = 2;
        break;
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_TF_RGBA8:
    case GX_TF_Z16:
    case GX_TF_Z24X8:
    case GX_CTF_RA8:
    case GX_CTF_RG8:
    case GX_CTF_GB8:
    case GX_CTF_Z16L:
        *row_tile_s = 2;
        *col_tile_s = 2;
        break;
    default:
        *row_tile_s = 0;
        *col_tile_s = 0;
        break;
    }
}

/* extern/dolphin/src/dolphin/gx/GXTexture.c: GXGetTexBufferSize (0x194) */
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap,
                       u8 max_lod)
{
    u32 tile_shift_x;
    u32 tile_shift_y;
    u32 tile_bytes;
    u32 buffer_size;
    u32 nx;
    u32 ny;
    u32 level;

    gx_tex_tile_shift(format, &tile_shift_x, &tile_shift_y);
    if (format == GX_TF_RGBA8 || format == GX_TF_Z24X8) {
        tile_bytes = 64;
    } else {
        tile_bytes = 32;
    }
    if (mipmap == 1) {
        buffer_size = 0;
        for (level = 0; level < max_lod; level++) {
            nx = (width + (1 << tile_shift_x) - 1) >> tile_shift_x;
            ny = (height + (1 << tile_shift_y) - 1) >> tile_shift_y;
            buffer_size += tile_bytes * (nx * ny);
            if (width == 1 && height == 1) {
                break;
            }
            width = (width > 1) ? width >> 1 : 1;
            height = (height > 1) ? height >> 1 : 1;
        }
    } else {
        nx = (width + (1 << tile_shift_x) - 1) >> tile_shift_x;
        ny = (height + (1 << tile_shift_y) - 1) >> tile_shift_y;
        buffer_size = nx * ny * tile_bytes;
    }
    return buffer_size;
}

GXTexFmt GXGetTexObjFmt(const GXTexObj* to)
{
    (void) to;
    boot_triage_stub("GXGetTexObjFmt", BOOT_CAT_GX);
    return GX_TF_RGBA8;
}

u16 GXGetTexObjWidth(const GXTexObj* to)
{
    (void) to;
    boot_triage_stub("GXGetTexObjWidth", BOOT_CAT_GX);
    return 0;
}

u16 GXGetTexObjHeight(const GXTexObj* to)
{
    (void) to;
    boot_triage_stub("GXGetTexObjHeight", BOOT_CAT_GX);
    return 0;
}

u32 GXSetDispCopyYScale(f32 vscale)
{
    (void) vscale;
    boot_triage_stub("GXSetDispCopyYScale", BOOT_CAT_GX);
    return 0;
}

GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb)
{
    GXDrawDoneCallback old = gx_draw_done_callback;

    gx_draw_done_callback = cb;
    return old;
}

/* Host draws complete immediately; HSD_VIGXSetDrawDone depends on the
 * callback firing so its XFB state machine can advance. */
void GXSetDrawDone(void)
{
    boot_triage_stub("GXSetDrawDone", BOOT_CAT_GX);
    if (gx_draw_done_callback != NULL) {
        gx_draw_done_callback();
    }
}

/* --------------------------------------------------------------- VI specials */

u32 VIGetDTVStatus(void)
{
    boot_triage_stub("VIGetDTVStatus", BOOT_CAT_VI);
    return 0;
}

u32 VIGetNextField(void)
{
    boot_triage_stub("VIGetNextField", BOOT_CAT_VI);
    return 0;
}

u32 VIGetRetraceCount(void)
{
    return vi_retrace_count;
}

u32 VIGetTvFormat(void)
{
    return VI_NTSC;
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = vi_pre_callback;

    vi_pre_callback = cb;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = vi_post_callback;

    vi_post_callback = cb;
    return old;
}

void VIInit(void)
{
    boot_triage_real("VIInit", BOOT_CAT_VI);
}

/* One emulated display frame: run the HSD pre/post retrace callbacks, advance
 * the virtual clock, and charge the S1 frame budget. */
void VIWaitForRetrace(void)
{
    vi_retrace_count++;
    if (vi_pre_callback != NULL) {
        vi_pre_callback(vi_retrace_count);
    }
    if (vi_post_callback != NULL) {
        vi_post_callback(vi_retrace_count);
    }
    boot_platform_advance_frame();
    boot_triage_frame();
}

/* ----------------------------------------------------------------- log stubs */
void GXCopyDisp(void *dest, GXBool clear)
{
    (void) dest; (void) clear;
    boot_triage_stub("GXCopyDisp", BOOT_CAT_GX);
}

void GXCopyTex(void *dest, GXBool clear)
{
    (void) dest; (void) clear;
    boot_triage_stub("GXCopyTex", BOOT_CAT_GX);
}

void GXEnableTexOffsets(GXTexCoordID coord, u8 line_enable, u8 point_enable)
{
    (void) coord; (void) line_enable; (void) point_enable;
    boot_triage_stub("GXEnableTexOffsets", BOOT_CAT_GX);
}

void GXGetProjectionv(f32 *ptr)
{
    (void) ptr;
    boot_triage_stub("GXGetProjectionv", BOOT_CAT_GX);
}

void GXGetViewportv(f32 *vp)
{
    (void) vp;
    boot_triage_stub("GXGetViewportv", BOOT_CAT_GX);
}

void GXInitFogAdjTable(GXFogAdjTable *table, u16 width, f32 projmtx[4][4])
{
    (void) table; (void) width; (void) projmtx;
    boot_triage_stub("GXInitFogAdjTable", BOOT_CAT_GX);
}

void GXInitLightAttn(GXLightObj *lt_obj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2)
{
    (void) lt_obj; (void) a0; (void) a1; (void) a2; (void) k0; (void) k1; (void) k2;
    boot_triage_stub("GXInitLightAttn", BOOT_CAT_GX);
}

void GXInitLightColor(GXLightObj *lt_obj, GXColor color)
{
    (void) lt_obj; (void) color;
    boot_triage_stub("GXInitLightColor", BOOT_CAT_GX);
}

void GXInitLightDir(GXLightObj *lt_obj, f32 nx, f32 ny, f32 nz)
{
    (void) lt_obj; (void) nx; (void) ny; (void) nz;
    boot_triage_stub("GXInitLightDir", BOOT_CAT_GX);
}

void GXInitLightDistAttn(GXLightObj *lt_obj, f32 ref_dist, f32 ref_br, GXDistAttnFn dist_func)
{
    (void) lt_obj; (void) ref_dist; (void) ref_br; (void) dist_func;
    boot_triage_stub("GXInitLightDistAttn", BOOT_CAT_GX);
}

void GXInitLightPos(GXLightObj *lt_obj, f32 x, f32 y, f32 z)
{
    (void) lt_obj; (void) x; (void) y; (void) z;
    boot_triage_stub("GXInitLightPos", BOOT_CAT_GX);
}

void GXInitLightSpot(GXLightObj *lt_obj, f32 cutoff, GXSpotFn spot_func)
{
    (void) lt_obj; (void) cutoff; (void) spot_func;
    boot_triage_stub("GXInitLightSpot", BOOT_CAT_GX);
}

void GXInvalidateTexAll(void)
{
    boot_triage_stub("GXInvalidateTexAll", BOOT_CAT_GX);
}

void GXInvalidateVtxCache(void)
{
    boot_triage_stub("GXInvalidateVtxCache", BOOT_CAT_GX);
}

void GXLoadLightObjImm(GXLightObj *lt_obj, GXLightID light)
{
    (void) lt_obj; (void) light;
    boot_triage_stub("GXLoadLightObjImm", BOOT_CAT_GX);
}

void GXPixModeSync(void)
{
    boot_triage_stub("GXPixModeSync", BOOT_CAT_GX);
}

void GXProject(f32 x, f32 y, f32 z, f32 mtx[3][4], f32 *pm, f32 *vp, f32 *sx, f32 *sy, f32 *sz)
{
    (void) x; (void) y; (void) z; (void) mtx; (void) pm; (void) vp; (void) sx; (void) sy; (void) sz;
    boot_triage_stub("GXProject", BOOT_CAT_GX);
}

void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1)
{
    (void) comp0; (void) ref0; (void) op; (void) comp1; (void) ref1;
    boot_triage_stub("GXSetAlphaCompare", BOOT_CAT_GX);
}

void GXSetAlphaUpdate(GXBool update_enable)
{
    (void) update_enable;
    boot_triage_stub("GXSetAlphaUpdate", BOOT_CAT_GX);
}

void GXSetBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op)
{
    (void) type; (void) src_factor; (void) dst_factor; (void) op;
    boot_triage_stub("GXSetBlendMode", BOOT_CAT_GX);
}

void GXSetChanAmbColor(GXChannelID chan, GXColor amb_color)
{
    (void) chan; (void) amb_color;
    boot_triage_stub("GXSetChanAmbColor", BOOT_CAT_GX);
}

void GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src, GXColorSrc mat_src, u32 light_mask, GXDiffuseFn diff_fn, GXAttnFn attn_fn)
{
    (void) chan; (void) enable; (void) amb_src; (void) mat_src; (void) light_mask; (void) diff_fn; (void) attn_fn;
    boot_triage_stub("GXSetChanCtrl", BOOT_CAT_GX);
}

void GXSetChanMatColor(GXChannelID chan, GXColor mat_color)
{
    (void) chan; (void) mat_color;
    boot_triage_stub("GXSetChanMatColor", BOOT_CAT_GX);
}

void GXSetColorUpdate(GXBool update_enable)
{
    (void) update_enable;
    boot_triage_stub("GXSetColorUpdate", BOOT_CAT_GX);
}

void GXSetCopyClamp(GXFBClamp clamp)
{
    (void) clamp;
    boot_triage_stub("GXSetCopyClamp", BOOT_CAT_GX);
}

void GXSetCopyClear(GXColor clear_clr, u32 clear_z)
{
    (void) clear_clr; (void) clear_z;
    boot_triage_stub("GXSetCopyClear", BOOT_CAT_GX);
}

void GXSetCopyFilter(GXBool aa, const u8 sample_pattern[12][2], GXBool vf, const u8 vfilter[7])
{
    (void) aa; (void) sample_pattern; (void) vf; (void) vfilter;
    boot_triage_stub("GXSetCopyFilter", BOOT_CAT_GX);
}

void GXSetCullMode(GXCullMode mode)
{
    (void) mode;
    boot_triage_stub("GXSetCullMode", BOOT_CAT_GX);
}

void GXSetDispCopyDst(u16 wd, u16 ht)
{
    (void) wd; (void) ht;
    boot_triage_stub("GXSetDispCopyDst", BOOT_CAT_GX);
}

void GXSetDispCopyGamma(GXGamma gamma)
{
    (void) gamma;
    boot_triage_stub("GXSetDispCopyGamma", BOOT_CAT_GX);
}

void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    (void) left; (void) top; (void) wd; (void) ht;
    boot_triage_stub("GXSetDispCopySrc", BOOT_CAT_GX);
}

void GXSetDither(GXBool dither)
{
    (void) dither;
    boot_triage_stub("GXSetDither", BOOT_CAT_GX);
}

void GXSetDstAlpha(GXBool enable, u8 alpha)
{
    (void) enable; (void) alpha;
    boot_triage_stub("GXSetDstAlpha", BOOT_CAT_GX);
}

void GXSetFieldMode(GXBool field_mode, GXBool half_aspect_ratio)
{
    (void) field_mode; (void) half_aspect_ratio;
    boot_triage_stub("GXSetFieldMode", BOOT_CAT_GX);
}

void GXSetFog(GXFogType type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color)
{
    (void) type; (void) startz; (void) endz; (void) nearz; (void) farz; (void) color;
    boot_triage_stub("GXSetFog", BOOT_CAT_GX);
}

void GXSetFogRangeAdj(GXBool enable, u16 center, GXFogAdjTable *table)
{
    (void) enable; (void) center; (void) table;
    boot_triage_stub("GXSetFogRangeAdj", BOOT_CAT_GX);
}

void GXSetIndTexCoordScale(GXIndTexStageID ind_state, GXIndTexScale scale_s, GXIndTexScale scale_t)
{
    (void) ind_state; (void) scale_s; (void) scale_t;
    boot_triage_stub("GXSetIndTexCoordScale", BOOT_CAT_GX);
}

void GXSetIndTexMtx(GXIndTexMtxID mtx_id, f32 offset[2][3], s8 scale_exp)
{
    (void) mtx_id; (void) offset; (void) scale_exp;
    boot_triage_stub("GXSetIndTexMtx", BOOT_CAT_GX);
}

void GXSetIndTexOrder(GXIndTexStageID ind_stage, GXTexCoordID tex_coord, GXTexMapID tex_map)
{
    (void) ind_stage; (void) tex_coord; (void) tex_map;
    boot_triage_stub("GXSetIndTexOrder", BOOT_CAT_GX);
}

void GXSetLineWidth(u8 width, GXTexOffset texOffsets)
{
    (void) width; (void) texOffsets;
    boot_triage_stub("GXSetLineWidth", BOOT_CAT_GX);
}

void GXSetMisc(GXMiscToken token, u32 val)
{
    (void) token; (void) val;
    boot_triage_stub("GXSetMisc", BOOT_CAT_GX);
}

void GXSetNumChans(u8 nChans)
{
    (void) nChans;
    boot_triage_stub("GXSetNumChans", BOOT_CAT_GX);
}

void GXSetNumIndStages(u8 nIndStages)
{
    (void) nIndStages;
    boot_triage_stub("GXSetNumIndStages", BOOT_CAT_GX);
}

void GXSetNumTevStages(u8 nStages)
{
    (void) nStages;
    boot_triage_stub("GXSetNumTevStages", BOOT_CAT_GX);
}

void GXSetNumTexGens(u8 nTexGens)
{
    (void) nTexGens;
    boot_triage_stub("GXSetNumTexGens", BOOT_CAT_GX);
}

void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt)
{
    (void) pix_fmt; (void) z_fmt;
    boot_triage_stub("GXSetPixelFmt", BOOT_CAT_GX);
}

void GXSetPointSize(u8 pointSize, GXTexOffset texOffsets)
{
    (void) pointSize; (void) texOffsets;
    boot_triage_stub("GXSetPointSize", BOOT_CAT_GX);
}

void GXSetProjection(f32 mtx[4][4], GXProjectionType type)
{
    (void) mtx; (void) type;
    boot_triage_stub("GXSetProjection", BOOT_CAT_GX);
}

void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht)
{
    (void) left; (void) top; (void) wd; (void) ht;
    boot_triage_stub("GXSetScissor", BOOT_CAT_GX);
}

void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d)
{
    (void) stage; (void) a; (void) b; (void) c; (void) d;
    boot_triage_stub("GXSetTevAlphaIn", BOOT_CAT_GX);
}

void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg)
{
    (void) stage; (void) op; (void) bias; (void) scale; (void) clamp; (void) out_reg;
    boot_triage_stub("GXSetTevAlphaOp", BOOT_CAT_GX);
}

void GXSetTevClampMode(int a0, int a1)
{
    (void) a0; (void) a1;
    boot_triage_stub("GXSetTevClampMode", BOOT_CAT_GX);
}

void GXSetTevColor(GXTevRegID id, GXColor color)
{
    (void) id; (void) color;
    boot_triage_stub("GXSetTevColor", BOOT_CAT_GX);
}

void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d)
{
    (void) stage; (void) a; (void) b; (void) c; (void) d;
    boot_triage_stub("GXSetTevColorIn", BOOT_CAT_GX);
}

void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg)
{
    (void) stage; (void) op; (void) bias; (void) scale; (void) clamp; (void) out_reg;
    boot_triage_stub("GXSetTevColorOp", BOOT_CAT_GX);
}

void GXSetTevColorS10(GXTevRegID id, GXColorS10 color)
{
    (void) id; (void) color;
    boot_triage_stub("GXSetTevColorS10", BOOT_CAT_GX);
}

void GXSetTevDirect(GXTevStageID tev_stage)
{
    (void) tev_stage;
    boot_triage_stub("GXSetTevDirect", BOOT_CAT_GX);
}

void GXSetTevIndirect(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexFormat format, GXIndTexBiasSel bias_sel, GXIndTexMtxID matrix_sel, GXIndTexWrap wrap_s, GXIndTexWrap wrap_t, GXBool add_prev, GXBool utc_lod, GXIndTexAlphaSel alpha_sel)
{
    (void) tev_stage; (void) ind_stage; (void) format; (void) bias_sel; (void) matrix_sel; (void) wrap_s; (void) wrap_t; (void) add_prev; (void) utc_lod; (void) alpha_sel;
    boot_triage_stub("GXSetTevIndirect", BOOT_CAT_GX);
}

void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel)
{
    (void) stage; (void) sel;
    boot_triage_stub("GXSetTevKAlphaSel", BOOT_CAT_GX);
}

void GXSetTevKColor(GXTevKColorID id, GXColor color)
{
    (void) id; (void) color;
    boot_triage_stub("GXSetTevKColor", BOOT_CAT_GX);
}

void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel)
{
    (void) stage; (void) sel;
    boot_triage_stub("GXSetTevKColorSel", BOOT_CAT_GX);
}

void GXSetTevOp(GXTevStageID id, GXTevMode mode)
{
    (void) id; (void) mode;
    boot_triage_stub("GXSetTevOp", BOOT_CAT_GX);
}

void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color)
{
    (void) stage; (void) coord; (void) map; (void) color;
    boot_triage_stub("GXSetTevOrder", BOOT_CAT_GX);
}

void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel ras_sel, GXTevSwapSel tex_sel)
{
    (void) stage; (void) ras_sel; (void) tex_sel;
    boot_triage_stub("GXSetTevSwapMode", BOOT_CAT_GX);
}

void GXSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha)
{
    (void) table; (void) red; (void) green; (void) blue; (void) alpha;
    boot_triage_stub("GXSetTevSwapModeTable", BOOT_CAT_GX);
}

void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap)
{
    (void) wd; (void) ht; (void) fmt; (void) mipmap;
    boot_triage_stub("GXSetTexCopyDst", BOOT_CAT_GX);
}

void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    (void) left; (void) top; (void) wd; (void) ht;
    boot_triage_stub("GXSetTexCopySrc", BOOT_CAT_GX);
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz)
{
    (void) left; (void) top; (void) wd; (void) ht; (void) nearz; (void) farz;
    boot_triage_stub("GXSetViewport", BOOT_CAT_GX);
}

void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz, u32 field)
{
    (void) left; (void) top; (void) wd; (void) ht; (void) nearz; (void) farz; (void) field;
    boot_triage_stub("GXSetViewportJitter", BOOT_CAT_GX);
}

void GXSetZCompLoc(GXBool before_tex)
{
    (void) before_tex;
    boot_triage_stub("GXSetZCompLoc", BOOT_CAT_GX);
}

void GXSetZMode(GXBool compare_enable, GXCompare func, GXBool update_enable)
{
    (void) compare_enable; (void) func; (void) update_enable;
    boot_triage_stub("GXSetZMode", BOOT_CAT_GX);
}

void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias)
{
    (void) op; (void) fmt; (void) bias;
    boot_triage_stub("GXSetZTexture", BOOT_CAT_GX);
}

void GXWaitDrawDone(void)
{
    boot_triage_stub("GXWaitDrawDone", BOOT_CAT_GX);
}

void VIConfigure(GXRenderModeObj *rm)
{
    (void) rm;
    boot_triage_stub("VIConfigure", BOOT_CAT_VI);
}

void VIFlush(void)
{
    boot_triage_stub("VIFlush", BOOT_CAT_VI);
}

void VISetBlack(BOOL black)
{
    (void) black;
    boot_triage_stub("VISetBlack", BOOT_CAT_VI);
}

void VISetNextFrameBuffer(void *fb)
{
    (void) fb;
    boot_triage_stub("VISetNextFrameBuffer", BOOT_CAT_VI);
}

/* Declarations the prototype extractor missed (multi-line or macro-guarded
 * headers); signatures copied from dolphin/gx/GXVert.h, GXAttr.h, GXTexture.h. */

void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts)
{
    (void) type;
    (void) vtxfmt;
    (void) nverts;
    boot_triage_stub("GXBegin", BOOT_CAT_GX);
}

void GXCallDisplayList(void* list, u32 nbytes)
{
    (void) list;
    (void) nbytes;
    boot_triage_stub("GXCallDisplayList", BOOT_CAT_GX);
}

void GXClearVtxDesc(void)
{
    boot_triage_stub("GXClearVtxDesc", BOOT_CAT_GX);
}

void GXInitTexObj(GXTexObj* obj, void* image_ptr, u16 width, u16 height,
                  GXTexFmt format, GXTexWrapMode wrap_s, GXTexWrapMode wrap_t,
                  u8 mipmap)
{
    (void) obj;
    (void) image_ptr;
    (void) width;
    (void) height;
    (void) format;
    (void) wrap_s;
    (void) wrap_t;
    (void) mipmap;
    boot_triage_stub("GXInitTexObj", BOOT_CAT_GX);
}

void GXInitTexObjCI(GXTexObj* obj, void* image_ptr, u16 width, u16 height,
                    GXTexFmt format, GXTexWrapMode wrap_s,
                    GXTexWrapMode wrap_t, u8 mipmap, u32 tlut_name)
{
    (void) obj;
    (void) image_ptr;
    (void) width;
    (void) height;
    (void) format;
    (void) wrap_s;
    (void) wrap_t;
    (void) mipmap;
    (void) tlut_name;
    boot_triage_stub("GXInitTexObjCI", BOOT_CAT_GX);
}

void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter min_filt, GXTexFilter mag_filt,
                     f32 min_lod, f32 max_lod, f32 lod_bias, GXBool bias_clamp,
                     GXBool do_edge_lod, GXAnisotropy max_aniso)
{
    (void) obj;
    (void) min_filt;
    (void) mag_filt;
    (void) min_lod;
    (void) max_lod;
    (void) lod_bias;
    (void) bias_clamp;
    (void) do_edge_lod;
    (void) max_aniso;
    boot_triage_stub("GXInitTexObjLOD", BOOT_CAT_GX);
}

void GXInitTlutObj(GXTlutObj* tlut_obj, void* lut, GXTlutFmt fmt,
                   u16 n_entries)
{
    (void) tlut_obj;
    (void) lut;
    (void) fmt;
    (void) n_entries;
    boot_triage_stub("GXInitTlutObj", BOOT_CAT_GX);
}

void GXLoadNrmMtxImm(f32 mtx[3][4], u32 id)
{
    (void) mtx;
    (void) id;
    boot_triage_stub("GXLoadNrmMtxImm", BOOT_CAT_GX);
}

void GXLoadPosMtxImm(f32 mtx[3][4], u32 id)
{
    (void) mtx;
    (void) id;
    boot_triage_stub("GXLoadPosMtxImm", BOOT_CAT_GX);
}

void GXLoadTexMtxImm(f32 mtx[][4], u32 id, GXTexMtxType type)
{
    (void) mtx;
    (void) id;
    (void) type;
    boot_triage_stub("GXLoadTexMtxImm", BOOT_CAT_GX);
}

void GXLoadTexObj(GXTexObj* obj, GXTexMapID id)
{
    (void) obj;
    (void) id;
    boot_triage_stub("GXLoadTexObj", BOOT_CAT_GX);
}

void GXLoadTlut(GXTlutObj* tlut_obj, u32 tlut_name)
{
    (void) tlut_obj;
    (void) tlut_name;
    boot_triage_stub("GXLoadTlut", BOOT_CAT_GX);
}

void GXSetArray(GXAttr attr, const void* base_ptr, u8 stride)
{
    (void) attr;
    (void) base_ptr;
    (void) stride;
    boot_triage_stub("GXSetArray", BOOT_CAT_GX);
}

void GXSetCurrentMtx(u32 id)
{
    (void) id;
    boot_triage_stub("GXSetCurrentMtx", BOOT_CAT_GX);
}

void GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func,
                       GXTexGenSrc src_param, u32 mtx, GXBool normalize,
                       u32 pt_texmtx)
{
    (void) dst_coord;
    (void) func;
    (void) src_param;
    (void) mtx;
    (void) normalize;
    (void) pt_texmtx;
    boot_triage_stub("GXSetTexCoordGen2", BOOT_CAT_GX);
}

void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt,
                     GXCompType type, u8 frac)
{
    (void) vtxfmt;
    (void) attr;
    (void) cnt;
    (void) type;
    (void) frac;
    boot_triage_stub("GXSetVtxAttrFmt", BOOT_CAT_GX);
}

void GXSetVtxDesc(GXAttr attr, GXAttrType type)
{
    (void) attr;
    (void) type;
    boot_triage_stub("GXSetVtxDesc", BOOT_CAT_GX);
}

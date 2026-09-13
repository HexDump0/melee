/*
 * S2 exit harness: render a real character through the compiled HSD runtime
 * and the GX HLE backend (headless EGL/GLES3).
 *
 * The scene setup (bootstrap, asset bridge, model scaling, ftData
 * visibility, prototype camera/lights) lives in decomp/render/render_scene.c,
 * shared with the interactive SDL3 viewer (decomp/render/viewer_main.c).
 *
 * Exit code 0 = rendered (SKIP without a disc image); non-zero = failure.
 */
#include <GLES3/gl3.h>
#include <math.h>
#include <placeholder.h>
#include <sysdolphin/baselib/quatlib.h>
#include <sysdolphin/baselib/state.h>
#include <dolphin/gx/GXVert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"
#include "gx/texture.h"
#include "decomp/render/render_scene.h"
#include "platform/disc.h"

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--disc PATH] [--model NAME] [--stage NAME]\n"
            "          [--fighter NAME] [--stage-map N] [--stage-cam]\n"
            "          [--no-fighter] [--shot FILE] [--width N] [--height N]\n"
            "          [--scale F] [--angle DEG] [--elevation DEG]\n"
            "          [--no-lights] [--dump] [--no-scale] [--no-gl]\n"
            "          [--direct] [--efb] [--toggle-mode N]\n",
            argv0);
}

/*
 * P-608: exercise the GXVert.h shim end to end.  A quad written through the
 * shim's inline GXPosition/GXColor/GXTexCoord functions must come back as one
 * draw whose two triangles carry the source attributes.  Runs without a disc.
 */
static int direct_test(void)
{
    static const float identity[4][4] = { { 1, 0, 0, 0 },
                                          { 0, 1, 0, 0 },
                                          { 0, 0, 1, 0 },
                                          { 0, 0, 0, 1 } };
    const GxHleVertex* verts = NULL;
    const GxHleDraw* draws = NULL;
    size_t vc = 0;
    size_t dc = 0;
    int fail = 0;

    /* The compiled game refines __frsqrte as a reciprocal-square-root
     * estimate.  The old host placeholder returned sqrt(x), which made the
     * leg IK diverge to NaN and poisoned all skinned matrices using it. */
    if (fabs(__frsqrte(4.0) - 0.5) > 1e-12) {
        printf("direct: FAIL __frsqrte host semantics\n");
        fail = 1;
    }

    /* These adjacent unit quaternions previously exposed x87 excess
     * precision: the dot stayed just below 1.0 in an 80-bit register, then
     * rounded to 1.0 at the acosf call, yielding sin(0) divisions. */
    {
        Quaternion p = { 0.232235119f, 0.0f, 0.0f, 0.972659707f };
        Quaternion q = { 0.232617468f, 0.0f, 0.0f, 0.972568333f };
        Quaternion out;
        HSD_QuatLib_8037EF28(&p, &q, &out, 0.9f);
        if (!isfinite(out.x) || !isfinite(out.y) || !isfinite(out.z) ||
            !isfinite(out.w))
        {
            printf("direct: FAIL quaternion interpolation is not finite\n");
            fail = 1;
        }
    }

    gx_hle_begin_frame();
    gx_hle_reset_state();
    GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(0.0f, 0.0f, 0.0f);
    GXColor4u8(255, 0, 0, 255);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(1.0f, 0.0f, 0.0f);
    GXColor4u8(0, 255, 0, 255);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition3f32(1.0f, 1.0f, 0.0f);
    GXColor4u8(0, 0, 255, 255);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(0.0f, 1.0f, 0.0f);
    GXColor4u8(255, 255, 255, 128);
    GXTexCoord2f32(0.0f, 1.0f);
    gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);

    if (dc != 1 || vc != 6) {
        printf("direct: FAIL draws=%zu verts=%zu (want 1/6)\n", dc, vc);
        return 0;
    }
    /* QUADS (0,1,2),(0,2,3): tri 1 = p0,p1,p2; tri 2 = p0,p2,p3. */
    {
        static const float want_pos[6][3] = {
            { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 },
            { 0, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }
        };
        static const unsigned char want_rgba[6][4] = {
            { 255, 0, 0, 255 },   { 0, 255, 0, 255 },   { 0, 0, 255, 255 },
            { 255, 0, 0, 255 },   { 0, 0, 255, 255 },   { 255, 255, 255, 128 }
        };
        static const float want_uv[6][2] = {
            { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 }
        };
        size_t k;
        int a;
        for (k = 0; k < 6; ++k) {
            const GxHleVertex* v = &verts[draws[0].first_vertex + k];
            for (a = 0; a < 3; ++a) {
                if (fabsf(v->view[a] - want_pos[k][a]) > 1e-6f) {
                    printf("direct: FAIL vert %zu pos[%d]=%.3f want %.3f\n", k,
                           a, (double) v->view[a], (double) want_pos[k][a]);
                    fail = 1;
                }
            }
            for (a = 0; a < 4; ++a) {
                if (v->color[a] != want_rgba[k][a]) {
                    printf("direct: FAIL vert %zu clr[%d]=%u want %u\n", k, a,
                           v->color[a], want_rgba[k][a]);
                    fail = 1;
                }
            }
            for (a = 0; a < 2; ++a) {
                if (fabsf(v->uv[0][a] - want_uv[k][a]) > 1e-6f) {
                    printf("direct: FAIL vert %zu uv[%d]=%.3f want %.3f\n", k,
                           a, (double) v->uv[0][a], (double) want_uv[k][a]);
                    fail = 1;
                }
            }
        }
    }
    /* NBT: shape-anim PObjs use GX_VA_NBT = 9 float components per vertex;
     * only the first three drive lighting, but the cursor must advance nine
     * or the following vertices desync (pobj.c:setupShapeAnimVtxDesc). */
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NBT, GX_NRM_NBT, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NBT, GX_DIRECT);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    {
        int v;
        for (v = 0; v < 3; ++v) {
            GXPosition3f32((float) v + 2.0f, 0.0f, 0.0f);
            GXNormal3f32(1.0f, 0.0f, 0.0f); /* normal */
            GXNormal3f32(0.0f, 1.0f, 0.0f); /* binormal */
            GXNormal3f32(0.0f, 0.0f, 1.0f); /* tangent */
        }
    }
    gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
    if (dc != 2 || vc != 9) {
        printf("direct: FAIL NBT draws=%zu verts=%zu (want 2/9)\n", dc, vc);
        return 0;
    }
    {
        size_t k;
        for (k = 0; k < 3; ++k) {
            const GxHleVertex* v = &verts[draws[1].first_vertex + k];
            if (fabsf(v->view[0] - (float) (k + 2)) > 1e-6f ||
                fabsf(v->view[1]) > 1e-6f || fabsf(v->view[2]) > 1e-6f) {
                printf("direct: FAIL NBT vert %zu pos=%.3f,%.3f,%.3f\n", k,
                       (double) v->view[0], (double) v->view[1],
                       (double) v->view[2]);
                fail = 1;
            }
        }
    }
    /* P-612: GX_TG_BUMP0 emboss texgen on coord 2 adds the light direction
     * projected on the vertex binormal/tangent to the source coordinate. */
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_NONE, GX_PTIDENTITY);
    GXSetTexCoordGen2(GX_TEXCOORD2, GX_TG_BUMP0, GX_TG_TEXCOORD0, GX_IDENTITY,
                      GX_NONE, GX_PTIDENTITY);
    {
        GXLightObj lo;
        GXInitLightPos(&lo, 0.0f, 1.0e6f, 0.0f); /* infinite, dir (0,1,0) */
        GXLoadLightObjImm(&lo, GX_LIGHT0);
    }
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NBT, GX_NRM_NBT, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NBT, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    {
        int v;
        for (v = 0; v < 3; ++v) {
            GXPosition3f32((float) v + 3.0f, 0.0f, 0.0f);
            GXNormal3f32(1.0f, 0.0f, 0.0f); /* normal  */
            GXNormal3f32(0.0f, 1.0f, 0.0f); /* binormal */
            GXNormal3f32(0.0f, 0.0f, 1.0f); /* tangent */
            GXTexCoord2f32(0.25f, 0.5f);
        }
    }
    gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
    if (dc != 3 || vc != 12) {
        printf("direct: FAIL bump draws=%zu verts=%zu (want 3/12)\n", dc, vc);
        return 0;
    }
    {
        const GxHleVertex* v = &verts[draws[2].first_vertex];
        if (fabsf(v->uv[2][0] - 0.25f) > 1e-5f ||
            fabsf(v->uv[2][1] - 1.5f) > 1e-5f) {
            printf("direct: FAIL bump uv=(%.3f,%.3f) want (0.250,1.500)\n",
                   (double) v->uv[2][0], (double) v->uv[2][1]);
            fail = 1;
        }
    }

    /* P-612: indirect-texture state is captured per draw (shader evaluation
     * lands with the S4 effects that use it). */
    GXSetNumIndStages(1);
    GXSetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD0, GX_TEXMAP0);
    GXSetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
    {
        f32 off[2][3] = { { 0.5f, 0.25f, 0.0f }, { 0.0f, 0.5f, 0.25f } };
        GXSetIndTexMtx(GX_ITM_0, off, 1);
    }
    GXSetTevIndirect(GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_ST,
                     GX_ITM_0, GX_ITW_0, GX_ITW_0, GX_DISABLE, GX_DISABLE,
                     GX_ITBA_OFF);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    {
        int v;
        for (v = 0; v < 3; ++v) {
            GXPosition3f32((float) v + 6.0f, 0.0f, 0.0f);
        }
    }
    gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
    if (dc != 4) {
        printf("direct: FAIL indirect draws=%zu (want 4)\n", dc);
        return 0;
    }
    {
        const GxHleDrawState* st = &draws[3].state;
        if (st->num_ind_stages != 1 ||
            st->ind[0].tex_map != GX_TEXMAP0 ||
            st->ind[0].tex_coord != GX_TEXCOORD0 ||
            st->ind[0].mtx[0][0] != 0.5f || st->ind[0].scale != 2.0f ||
            !st->stages[0].ind_enable || st->stages[0].ind_mtx != GX_ITM_0) {
            printf("direct: FAIL indirect state\n");
            fail = 1;
        }
    }

    /* Channel slots: HSD emits COLOR0 (light mask set) then ALPHA0 (disabled,
     * mask 0) for every material.  The alpha update must not clobber the
     * colour channel, or a model switched after the first frame loses its
     * lighting when HSD's own cache skips re-emitting the colour state. */
    GXSetChanCtrl(GX_COLOR0, GX_TRUE, GX_SRC_REG, GX_SRC_REG, 0x1,
                  GX_DF_CLAMP, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0x0,
                  GX_DF_NONE, GX_AF_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    {
        int v;
        for (v = 0; v < 3; ++v) {
            GXPosition3f32((float) v + 8.0f, 0.0f, 0.0f);
        }
    }
    gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
    if (dc != 5) {
        printf("direct: FAIL channels draws=%zu (want 5)\n", dc);
        return 0;
    }
    {
        const GxHleDrawState* st = &draws[4].state;
        if (!st->ch_enable[0] || st->ch_light_mask[0] != 0x1 ||
            st->ch_diff_fn[0] != GX_DF_CLAMP) {
            printf("direct: FAIL channels mask=0x%x enable=%u diff=%u\n",
                   st->ch_light_mask[0], st->ch_enable[0], st->ch_diff_fn[0]);
            fail = 1;
        }
        /* Lit raster alpha is evaluated by the vertex shader now.  The
         * paired ALPHA0 channel is disabled, so its registered material
         * alpha must remain 1 for TEV graphs that multiply by RASA. */
        if (fabsf(st->ch_mat[2][3] - 1.0f) > 1e-6f) {
            printf("direct: FAIL channel alpha=%.3f (want 1.000)\n",
                   (double) st->ch_mat[2][3]);
            fail = 1;
        }
    }

    /* A combined COLOR0A0 control addresses colour *and* alpha at once: stage
     * vertex-colour materials (Battlefield/Final Destination cores) put the
     * gradient in the vertex alpha and rely on mat_src=VTX reaching the alpha
     * slot.  Without the mirror the raster alpha stays the opaque register. */
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, 0x0,
                  GX_DF_NONE, GX_AF_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    {
        int v;
        for (v = 0; v < 3; ++v) {
            GXPosition3f32((float) v + 10.0f, 0.0f, 0.0f);
            GXColor4u8(255, 255, 255, 0);
        }
    }
    gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
    if (dc != 6) {
        printf("direct: FAIL combined channel draws=%zu (want 6)\n", dc);
        return 0;
    }
    {
        const GxHleDrawState* st = &draws[5].state;
        if (st->ch_mat_src[0] != GX_SRC_VTX ||
            st->ch_mat_src[2] != GX_SRC_VTX || st->ch_enable[2] != 0) {
            printf("direct: FAIL combined channel mat_src=%u alphamat=%u "
                   "en=%u (want VTX/VTX/0)\n",
                   st->ch_mat_src[0], st->ch_mat_src[2], st->ch_enable[2]);
            fail = 1;
        }
        if (verts[draws[5].first_vertex].color[3] != 0) {
            printf("direct: FAIL combined channel vertex alpha=%u (want 0)\n",
                   verts[draws[5].first_vertex].color[3]);
            fail = 1;
        }
    }

    printf("direct: %s draws=%zu verts=%zu primitives=%u\n",
           fail ? "FAIL" : "PASS", dc, vc,
           (unsigned) gx_hle_primitive_count());
    return !fail;
}

/*
 * P-615: synthetic EFB test.  Pass 1 captures a known colour through
 * GXCopyTex into RGB565 tiled memory; pass 2 writes a far depth through
 * GXSetZTexture(GX_ZT_REPLACE) and checks that a nearer quad then passes the
 * depth test (the erase-rect path HSD uses for shadows/XLU sorting).
 */
static int efb_test(void)
{
    static const float identity[4][4] = { { 1, 0, 0, 0 },
                                          { 0, 1, 0, 0 },
                                          { 0, 0, 1, 0 },
                                          { 0, 0, 0, 1 } };
    static unsigned char z8_image[32]; /* 4x4 GX_TF_Z8, all far (0xFF) */
    unsigned char copy[8 * 8 * 2];
    unsigned char pixel[4] = { 0, 0, 0, 0 };
    GXTexObj ztex;
    GXColor red = { 0xFF, 0x00, 0x00, 0xFF };
    GXColor blue = { 0x00, 0x00, 0xFF, 0xFF };
    int fail = 0;
    int texel;

    memset(z8_image, 0xFF, sizeof(z8_image));

    /* ---- pass 1: colour EFB capture through GXCopyTex ---- */
    gx_hle_begin_frame();
    gx_hle_reset_state();
    GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX,
                  GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                  GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-1.0f, -1.0f, 0.0f);
    GXColor4u8(64, 128, 192, 255);
    GXPosition3f32(1.0f, -1.0f, 0.0f);
    GXColor4u8(64, 128, 192, 255);
    GXPosition3f32(1.0f, 1.0f, 0.0f);
    GXColor4u8(64, 128, 192, 255);
    GXPosition3f32(-1.0f, 1.0f, 0.0f);
    GXColor4u8(64, 128, 192, 255);
    GXSetTexCopySrc(0, 0, 640, 480);
    GXSetTexCopyDst(8, 8, GX_TF_RGB565, GX_FALSE);
    memset(copy, 0, sizeof(copy));
    GXCopyTex(copy, GX_FALSE);
    if (gx_gl_render_frame() < 0) {
        printf("efb: FAIL render_frame\n");
        return 0;
    }
    for (texel = 0; texel < 8 * 8; ++texel) {
        int x = texel % 8;
        int y = texel / 8;
        size_t off = ((size_t) (y / 4) * 2 + (size_t) (x / 4)) * 32 +
                     (size_t) (y % 4) * 8 + (size_t) (x % 4) * 2;
        unsigned v = ((unsigned) copy[off] << 8) | copy[off + 1];
        int r = (v >> 11) & 31;
        int g = (v >> 5) & 63;
        int b = v & 31;
        if (abs((r << 3) - 64) > 8 || abs((g << 2) - 128) > 8 ||
            abs((b << 3) - 192) > 8) {
            fail = 1;
            break;
        }
    }

    /* ---- pass 2: GXSetZTexture(GX_ZT_REPLACE) erases depth to far ---- */
    gx_hle_begin_frame();
    gx_hle_reset_state();
    GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX,
                  GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                  GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    GXSetColorUpdate(GX_TRUE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-1.0f, -1.0f, 0.2f);
    GXColor4u8(red.r, red.g, red.b, red.a);
    GXPosition3f32(1.0f, -1.0f, 0.2f);
    GXColor4u8(red.r, red.g, red.b, red.a);
    GXPosition3f32(1.0f, 1.0f, 0.2f);
    GXColor4u8(red.r, red.g, red.b, red.a);
    GXPosition3f32(-1.0f, 1.0f, 0.2f);
    GXColor4u8(red.r, red.g, red.b, red.a);

    /* Erase pass: depth replaced by the Z8 dummy (1.0 = far), colour off. */
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                      GX_NONE, GX_PTIDENTITY);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXInitTexObj(&ztex, z8_image, 4, 4, GX_TF_Z8, GX_REPEAT, GX_REPEAT,
                 GX_FALSE);
    GXLoadTexObj(&ztex, GX_TEXMAP0);
    GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z8, 0);
    GXSetColorUpdate(GX_FALSE);
    GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-1.0f, -1.0f, 0.1f);
    GXColor4u8(0, 0, 0, 0);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(1.0f, -1.0f, 0.1f);
    GXColor4u8(0, 0, 0, 0);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition3f32(1.0f, 1.0f, 0.1f);
    GXColor4u8(0, 0, 0, 0);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(-1.0f, 1.0f, 0.1f);
    GXColor4u8(0, 0, 0, 0);
    GXTexCoord2f32(0.0f, 1.0f);
    GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
    GXSetColorUpdate(GX_TRUE);

    /* Nearby quad at z=0.5 with LESS: passes only if the erase set far. */
    GXSetNumTexGens(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                  GX_COLOR0A0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetZMode(GX_TRUE, GX_LESS, GX_TRUE);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-1.0f, -1.0f, 0.5f);
    GXColor4u8(blue.r, blue.g, blue.b, blue.a);
    GXPosition3f32(1.0f, -1.0f, 0.5f);
    GXColor4u8(blue.r, blue.g, blue.b, blue.a);
    GXPosition3f32(1.0f, 1.0f, 0.5f);
    GXColor4u8(blue.r, blue.g, blue.b, blue.a);
    GXPosition3f32(-1.0f, 1.0f, 0.5f);
    GXColor4u8(blue.r, blue.g, blue.b, blue.a);
    if (gx_gl_render_frame() < 0) {
        printf("efb: FAIL render_frame (ZT)\n");
        return 0;
    }
    glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!(pixel[2] > 200 && pixel[0] < 60)) {
        printf("efb: FAIL center=%u,%u,%u (want blue)\n", pixel[0], pixel[1],
               pixel[2]);
        fail = 1;
    }

    /* ---- pass 3: GX_CTF_R4 EFB copy (HSD's shadow map) ---- */
    {
        static unsigned char copy4[32]; /* 8x8 px, 4-bit tiled */
        GXColor shade = { 0xA0, 0x50, 0x10, 0xFF };

        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX,
                      GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                      GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.0f);
        GXColor4u8(shade.r, shade.g, shade.b, shade.a);
        GXPosition3f32(1.0f, -1.0f, 0.0f);
        GXColor4u8(shade.r, shade.g, shade.b, shade.a);
        GXPosition3f32(1.0f, 1.0f, 0.0f);
        GXColor4u8(shade.r, shade.g, shade.b, shade.a);
        GXPosition3f32(-1.0f, 1.0f, 0.0f);
        GXColor4u8(shade.r, shade.g, shade.b, shade.a);
        GXSetTexCopySrc(0, 0, 640, 480);
        GXSetTexCopyDst(8, 8, GX_CTF_R4, GX_FALSE);
        memset(copy4, 0x5A, sizeof(copy4)); /* prove the copy overwrites */
        GXCopyTex(copy4, GX_FALSE);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (R4)\n");
            return 0;
        }
        for (texel = 0; texel < 32; ++texel) {
            int hi = copy4[texel] >> 4;
            int lo = copy4[texel] & 0xF;
            if (abs(hi - 0xA) > 1 || abs(lo - 0xA) > 1) {
                printf("efb: FAIL R4 texel %d = %02x (want aa)\n", texel,
                       copy4[texel]);
                fail = 1;
                break;
            }
        }
    }

    /* ---- pass 4: GX REG0 alpha must address uniform slot 1, not PREV ----
     * GXTevRegID is PREV=0, REG0=1, REG1=2, REG2=3.  Final Destination's
     * animated XLU effects select GX_CA_A0; treating uniform slot 0 as C0
     * makes their zero/partial alpha fully opaque. */
    {
        GXColor reg0 = { 0xFF, 0x00, 0x00, 0x40 };

        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
        GXSetNumChans(0);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevColor(GX_TEVREG0, reg0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                      GX_COLOR_NULL);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                        GX_CC_C0);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                        GX_CA_A0);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                        GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                        GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                       GX_LO_COPY);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.0f);
        GXPosition3f32(1.0f, -1.0f, 0.0f);
        GXPosition3f32(1.0f, 1.0f, 0.0f);
        GXPosition3f32(-1.0f, 1.0f, 0.0f);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (TEV REG0 alpha)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (pixel[0] < 65 || pixel[0] > 85 || pixel[1] > 20 ||
            pixel[2] > 25) {
            printf("efb: FAIL REG0 alpha pixel=%u,%u,%u (want ~73,11,17)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
    }

    printf("efb: %s\n", fail ? "FAIL" : "PASS");
    return !fail;
}

int main(int argc, char** argv)
{
    RenderSceneOptions opt;
    RenderScene scene;
    char error[256];
    const char* shot = NULL;
    int dump = 0;
    int no_gl = 0;
    const char* dump_world = NULL;
    int direct = 0;
    int efb = 0;
    int toggle_mode = 0;
    int draw_first = 0;
    int rendered;
    int loaded;
    size_t i;
    const GxHleVertex* vertices = NULL;
    const GxHleDraw* draws = NULL;
    GxHleTexture* textures = NULL;
    size_t vertex_count = 0;
    size_t draw_count = 0;
    size_t texture_count = 0;

    memset(&opt, 0, sizeof(opt));
    opt.disc = RENDER_SCENE_DISC_DEFAULT;
    opt.model = RENDER_SCENE_MODEL_DEFAULT;
    opt.stage_map = -1; /* all maps */
    opt.width = 640;
    opt.height = 480;
    opt.angle = 25.0f;
    opt.elevation = -12.0f;
    opt.zoom = 1.0f;
    opt.scale_override = -1.0f;

    for (i = 1; (int) i < argc; ++i) {
        if (strcmp(argv[i], "--disc") == 0 && (int) i + 1 < argc) {
            opt.disc = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && (int) i + 1 < argc) {
            opt.model = argv[++i];
        } else if (strcmp(argv[i], "--stage") == 0 && (int) i + 1 < argc) {
            opt.stage = argv[++i];
        } else if (strcmp(argv[i], "--fighter") == 0 && (int) i + 1 < argc) {
            opt.fighter = argv[++i];
        } else if (strcmp(argv[i], "--stage-map") == 0 &&
                   (int) i + 1 < argc) {
            opt.stage_map = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--stage-cam") == 0) {
            opt.stage_camera = 1;
        } else if (strcmp(argv[i], "--no-fighter") == 0) {
            opt.no_fighter = 1;
        } else if (strcmp(argv[i], "--shot") == 0 && (int) i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && (int) i + 1 < argc) {
            opt.width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && (int) i + 1 < argc) {
            opt.height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && (int) i + 1 < argc) {
            opt.scale_override = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--angle") == 0 && (int) i + 1 < argc) {
            opt.angle = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--elevation") == 0 &&
                   (int) i + 1 < argc) {
            opt.elevation = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--no-lights") == 0) {
            opt.no_lights = 1;
        } else if (strcmp(argv[i], "--no-scale") == 0) {
            opt.no_scale = 1;
        } else if (strcmp(argv[i], "--dump") == 0) {
            dump = 1;
        } else if (strcmp(argv[i], "--dump-world") == 0 &&
                   (int) i + 1 < argc) {
            dump_world = argv[++i];
        } else if (strcmp(argv[i], "--no-gl") == 0) {
            no_gl = 1;
        } else if (strcmp(argv[i], "--direct") == 0) {
            direct = 1;
        } else if (strcmp(argv[i], "--efb") == 0) {
            efb = 1;
        } else if (strcmp(argv[i], "--toggle-mode") == 0 &&
                   (int) i + 1 < argc) {
            toggle_mode = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--draw-first") == 0 &&
                   (int) i + 1 < argc) {
            draw_first = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: unknown option %s\n", argv[0], argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (direct) {
        return direct_test() ? 0 : 1;
    }
    if (efb) {
        if (!gx_gl_init(640, 480, error, sizeof(error))) {
            printf("efb: SKIP (no GL: %s)\n", error);
            return 0;
        }
        {
            int ok = efb_test();
            gx_gl_shutdown();
            return ok ? 0 : 1;
        }
    }

    if (!render_scene_boot()) {
        fprintf(stderr, "decomp_render: HSD bootstrap failed\n");
        return 1;
    }
    loaded = render_scene_open(&scene, &opt, error, sizeof(error));
    if (loaded == 0) {
        printf("decomp_render: SKIP (%s: %s)\n", opt.disc, error);
        return 0;
    }
    if (loaded < 0) {
        fprintf(stderr, "decomp_render: load failed: %s\n", error);
        return 1;
    }
    while (draw_first-- > 0) {
        render_scene_draw(&scene);
    }
    while (toggle_mode-- > 0) {
        if (!render_scene_toggle_mode(&scene, error, sizeof(error))) {
            fprintf(stderr, "decomp_render: mode toggle failed: %s\n",
                    error);
            return 1;
        }
    }
    printf("decomp_render: %s root=%p scale=%.4f hidden_dobjs=%d\n",
           scene.stage_mode ? scene.stage : scene.model,
           (void*) scene.hsd.root, (double) scene.model_scale,
           scene.hidden_dobjs);
    if (scene.stage_mode) {
        printf("stage: %s map=%d root=%p fighter=%s%s scale=%.4f "
               "camera=%s lights=%s fog=%s\n",
               scene.stage, scene.stage_map, (void*) scene.hsd.root,
               scene.have_fighter ? scene.fighter_name : "(none)",
               scene.have_fighter && scene.show_fighter ? "" : " (hidden)",
               (double) scene.fighter_scale,
               scene.hsd.stage_cobj != NULL ? "yes" : "no",
               scene.hsd.stage_lobj != NULL ? "yes" : "no",
               scene.hsd.stage_fog != NULL ? "yes" : "no");
    }
    printf("decomp_render: bounds [%.2f %.2f %.2f] to [%.2f %.2f %.2f]\n",
           scene.bounds_min[0], scene.bounds_min[1], scene.bounds_min[2],
           scene.bounds_max[0], scene.bounds_max[1], scene.bounds_max[2]);
    if (scene.have_lights) {
        printf("decomp_render: lights=%zu\n", scene.lights.count);
    }
    if (dump_world != NULL) {
        /* Dev diagnostic: world-space vertices per draw from an identity pass
         * (compare with the prototype's --dump-verts to catch GX decode or
         * primitive-assembly bugs; see learnings/decomp_viewer.md). */
        static const float identity[4][4] = { { 1, 0, 0, 0 },
                                              { 0, 1, 0, 0 },
                                              { 0, 0, 1, 0 },
                                              { 0, 0, 0, 1 } };
        FILE* f;
        gx_hle_begin_frame();
        gx_hle_reset_state();
        HSD_StateInvalidate(-1);
        GXSetProjection((f32 (*)[4]) identity, GX_PERSPECTIVE);
        HSD_JObjDispAll(scene.hsd.root, (f32 (*)[4]) identity, HSD_TRSP_ALL,
                        0);
        gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count, NULL,
                         NULL);
        f = fopen(dump_world, "wb");
        for (i = 0; i < draw_count && f != NULL; ++i) {
            unsigned int count = (unsigned int) draws[i].vertex_count;
            size_t k;
            fwrite(&count, 4, 1, f);
            for (k = 0; k < draws[i].vertex_count; ++k) {
                const GxHleVertex* vv = &vertices[draws[i].first_vertex + k];
                fwrite(vv->view, 4, 3, f);
                fwrite(vv->uv[0], 4, 2, f);
                fwrite(vv->uv[1], 4, 2, f);
            }
        }
        if (f != NULL) {
            fclose(f);
            printf("decomp_render: dumped %zu draws to %s\n", draw_count,
                   dump_world);
        } else {
            fprintf(stderr, "decomp_render: cannot write %s\n", dump_world);
            return 1;
        }
    }

    render_scene_draw(&scene);

    if (no_gl) {
        rendered = 0;
    } else {
        if (!gx_gl_init(opt.width, opt.height, error, sizeof(error))) {
            fprintf(stderr, "decomp_render: GL init failed: %s\n", error);
            return 1;
        }
        gx_gl_set_clear(0.05f, 0.06f, 0.09f, 1.0f);
        rendered = gx_gl_render_frame();
    }
    gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count,
                     &textures, &texture_count);
    printf("decomp_render: draws=%d vertices=%u textures=%u display_lists=%u "
           "primitives=%u skipped=%u degenerate=%u\n",
           rendered, (unsigned) vertex_count, (unsigned) texture_count,
           (unsigned) gx_hle_display_list_count(),
           (unsigned) gx_hle_primitive_count(),
           (unsigned) gx_hle_skipped_count(),
           (unsigned) gx_hle_degenerate_count());
    if (dump) {
        size_t d;
        for (d = 0; d < draw_count; ++d) {
            const GxHleDraw* dr = &draws[d];
            float vmn[3] = { 1e30f, 1e30f, 1e30f };
            float vmx[3] = { -1e30f, -1e30f, -1e30f };
            size_t k;
            int a;
            long off0 = -1;
            long off1 = -1;
            for (k = 0; k < dr->vertex_count; ++k) {
                const GxHleVertex* vv = &vertices[dr->first_vertex + k];
                for (a = 0; a < 3; ++a) {
                    if (vv->view[a] < vmn[a]) {
                        vmn[a] = vv->view[a];
                    }
                    if (vv->view[a] > vmx[a]) {
                        vmx[a] = vv->view[a];
                    }
                }
            }
            if (dr->state.texmap[0] >= 0 &&
                (size_t) dr->state.texmap[0] < texture_count) {
                off0 = (long) ((const unsigned char*)
                                   textures[dr->state.texmap[0]].image -
                               scene.hsd.work);
            }
            if (dr->state.texmap[1] >= 0 &&
                (size_t) dr->state.texmap[1] < texture_count) {
                off1 = (long) ((const unsigned char*)
                                   textures[dr->state.texmap[1]].image -
                               scene.hsd.work);
            }
            printf("  draw %u: verts=%u cull=%u blend=%u z=%u zf=%u stages=%u "
                   "texoff=%ld/%ld view=[%.2f %.2f %.2f]..[%.2f %.2f %.2f]\n",
                   (unsigned) d, (unsigned) dr->vertex_count,
                   dr->state.cull_mode, dr->state.blend_type,
                   dr->state.z_enable, dr->state.z_func,
                   dr->state.num_stages, off0, off1, vmn[0], vmn[1], vmn[2],
                   vmx[0], vmx[1], vmx[2]);
        }
    }
    if (shot != NULL && !no_gl) {
        if (!gx_gl_save_bmp(shot)) {
            fprintf(stderr, "decomp_render: screenshot failed\n");
            return 1;
        }
        printf("decomp_render: wrote %s\n", shot);
    }
    if (!no_gl && (rendered <= 0 || vertex_count == 0)) {
        fprintf(stderr, "decomp_render: nothing rendered\n");
        return 1;
    }
    if (!no_gl) {
        gx_gl_shutdown();
    }
    render_scene_close(&scene);
    printf("decomp_render: PASS\n");
    return 0;
}

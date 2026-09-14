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
    size_t light_vc = 0;
    size_t light_dc = 0;
    size_t light_prims = 0;
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

    /* P-678: GXGetProjectionv must expose the SDK packed form
     * {projType, A, B, C, D, E, F} (GXTransform.c).  psdisp.c:1936 rebuilds
     * particle billboard axes from it, so a diagonal-only return silently
     * breaks match particles. */
    {
        f32 pm[4][4] = {
            { 2.0f, 0.0f, 0.25f, 0.125f },
            { 0.0f, 3.0f, 0.5f, 0.75f },
            { 0.0f, 0.0f, 4.0f, 5.0f },
            { 0.0f, 0.0f, -1.0f, 0.0f },
        };
        f32 got[7];
        static const f32 want_persp[6] = { 2.0f, 0.25f, 3.0f, 0.5f, 4.0f, 5.0f };
        static const f32 want_ortho[6] = { 2.0f, 0.125f, 3.0f, 0.75f, 4.0f, 5.0f };
        int k;
        GXSetProjection(pm, GX_PERSPECTIVE);
        GXGetProjectionv(got);
        if (got[0] != (f32) GX_PERSPECTIVE) {
            printf("direct: FAIL GXGetProjectionv type=%.0f want %d\n",
                   (double) got[0], (int) GX_PERSPECTIVE);
            fail = 1;
        }
        for (k = 0; k < 6; ++k) {
            if (got[k + 1] != want_persp[k]) {
                printf("direct: FAIL GXGetProjectionv persp[%d]=%.3f want "
                       "%.3f\n", k, (double) got[k + 1],
                       (double) want_persp[k]);
                fail = 1;
            }
        }
        GXSetProjection(pm, GX_ORTHOGRAPHIC);
        GXGetProjectionv(got);
        for (k = 0; k < 6; ++k) {
            if (got[k + 1] != want_ortho[k]) {
                printf("direct: FAIL GXGetProjectionv ortho[%d]=%.3f want "
                       "%.3f\n", k, (double) got[k + 1],
                       (double) want_ortho[k]);
                fail = 1;
            }
        }
        /* GXSetProjectionv is the pointer-fed equivalent; round-trip it. */
        {
            f32 pv[7] = { (f32) GX_PERSPECTIVE, 7.0f, 0.1f, 8.0f, 0.2f,
                          9.0f, 0.3f };
            GXSetProjectionv(pv);
            GXGetProjectionv(got);
            for (k = 0; k < 6; ++k) {
                if (got[k + 1] != pv[k + 1]) {
                    printf("direct: FAIL GXSetProjectionv round-trip[%d]\n", k);
                    fail = 1;
                }
            }
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

    /* P-672: GX_TG_MTX3x4 texgen produces STQ and the hardware divides by q
     * (lbrefract's reflection coordinate); normalize runs after the first
     * matrix and before the post matrix. */
    gx_hle_begin_frame();
    gx_hle_reset_state();
    GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(1);
    {
        /* P-693: the q row carries a z coefficient (2) plus translation (1)
         * so the source's third component matters: a TEX source must be
         * (u, v, 1) (Aurora shader.cpp `vec4f(uv, 1.0, 1.0)`), or q comes
         * out 1.5 instead of 3.5. */
        f32 post[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 2, 0, 2, 1 } };
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3x4, GX_TG_TEX0, GX_IDENTITY,
                          GX_FALSE, GX_PTTEXMTX0);
        GXLoadTexMtxImm(post, GX_PTTEXMTX0, GX_MTX3x4);
    }
    GXClearVtxDesc();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    {
        int v;
        for (v = 0; v < 3; ++v) {
            GXPosition3f32((float) v, 0.0f, 0.0f);
            GXTexCoord2f32(0.25f, 0.625f);
        }
    }
    /* Second draw: MTX2x4 with normalize (length 5 -> unit vector). */
    {
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY,
                          GX_TRUE, GX_PTIDENTITY);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        {
            int v;
            for (v = 0; v < 3; ++v) {
                GXPosition3f32((float) v, 0.0f, 0.0f);
                GXTexCoord2f32(3.0f, 4.0f);
            }
        }
    }
    gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
    if (dc != 2) {
        printf("direct: FAIL texgen draws=%zu (want 2)\n", dc);
        return 0;
    }
    {
        /* q-row {2,0,2,1}: x'=0.25, y'=0.625, q=2*0.25+2*1+1=3.5 ->
         * (0.07143, 0.17857).  q=1.5 (0.16667, 0.41667) means the source z
         * was fed as 0 instead of 1. */
        const GxHleVertex* v = &verts[draws[0].first_vertex];
        if (fabsf(v->uv[0][0] - 0.0714286f) > 1e-5f ||
            fabsf(v->uv[0][1] - 0.1785714f) > 1e-5f) {
            printf("direct: FAIL MTX3x4 uv=(%.4f,%.4f) want (0.0714,0.1786)\n",
                   (double) v->uv[0][0], (double) v->uv[0][1]);
            fail = 1;
        }
    }
    {
        /* MTX2x4 forces z=1, then normalize(3,4,1) = (0.58835, 0.78446). */
        const GxHleVertex* v = &verts[draws[1].first_vertex];
        if (fabsf(v->uv[0][0] - 0.58835f) > 1e-4f ||
            fabsf(v->uv[0][1] - 0.78446f) > 1e-4f) {
            printf("direct: FAIL normalize uv=(%.4f,%.4f) want "
                   "(0.5884,0.7845)\n", (double) v->uv[0][0],
                   (double) v->uv[0][1]);
            fail = 1;
        }
    }

    /* P-672: GXSetTevDirect must clear the indirect fields (SDK GXBump.c);
     * a stale matrix would offset every later draw that reuses the stage. */
    gx_hle_begin_frame();
    {
        f32 offs[2][3] = { { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.5f, 0.0f } };
        GXSetNumIndStages(1);
        GXSetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD0, GX_TEXMAP0);
        GXSetIndTexMtx(GX_ITM_0, offs, 1);
        GXSetTevIndirect(GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_ITF_8,
                         GX_ITB_ST, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF,
                         GX_FALSE, GX_FALSE, GX_ITBA_OFF);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        {
            int v;
            for (v = 0; v < 3; ++v) {
                GXPosition3f32((float) v, 0.0f, 0.0f);
                GXTexCoord2f32(0.5f, 0.5f);
            }
        }
        GXSetTevDirect(GX_TEVSTAGE0);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        {
            int v;
            for (v = 0; v < 3; ++v) {
                GXPosition3f32((float) v + 10.0f, 0.0f, 0.0f);
                GXTexCoord2f32(0.5f, 0.5f);
            }
        }
        gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
        if (dc != 2) {
            printf("direct: FAIL indirect draws=%zu (want 2)\n", dc);
            return 0;
        }
        if (draws[0].state.stages[0].ind_enable != 1 ||
            draws[0].state.stages[0].ind_mtx != GX_ITM_0 ||
            draws[0].state.ind[0].mtx[0][0] != 0.5f ||
            draws[0].state.ind[0].scale != 2.0f) {
            printf("direct: FAIL indirect capture mtx=%u en=%u\n",
                   draws[0].state.stages[0].ind_mtx,
                   draws[0].state.stages[0].ind_enable);
            fail = 1;
        }
        if (draws[1].state.stages[0].ind_enable != 0 ||
            draws[1].state.stages[0].ind_mtx != GX_ITM_OFF ||
            draws[1].state.stages[0].ind_wrap_s != GX_ITW_OFF) {
            printf("direct: FAIL GXSetTevDirect did not clear (en=%u mtx=%u "
                   "wrap=%u)\n", draws[1].state.stages[0].ind_enable,
                   draws[1].state.stages[0].ind_mtx,
                   draws[1].state.stages[0].ind_wrap_s);
            fail = 1;
        }
    }

    /* P-673: light-object math must match the SDK GXLight.c transcription.
     * MEDIUM/STEEP previously used k1 for k2; out-of-range reference
     * brightness/distance and cutoff angles must fall back to OFF. */
    {
        struct {
            f32 ref_dist, ref_br;
            GXDistAttnFn fn;
            f32 k0, k1, k2;
        } dist_cases[] = {
            { 200.0f, 0.5f, GX_DA_GENTLE, 1.0f, 0.005f, 0.0f },
            { 200.0f, 0.5f, GX_DA_MEDIUM, 1.0f, 0.0025f, 0.0000125f },
            { 200.0f, 0.5f, GX_DA_STEEP, 1.0f, 0.0f, 0.000025f },
            { 200.0f, 1.5f, GX_DA_GENTLE, 1.0f, 0.0f, 0.0f },
            { -1.0f, 0.5f, GX_DA_MEDIUM, 1.0f, 0.0f, 0.0f },
        };
        struct {
            f32 cutoff;
            GXSpotFn fn;
            f32 a0, a1, a2;
        } spot_cases[] = {
            { 60.0f, GX_SP_COS, -1.0f, 2.0f, 0.0f },
            { 120.0f, GX_SP_COS, 1.0f, 0.0f, 0.0f },
            { 0.0f, GX_SP_SHARP, 1.0f, 0.0f, 0.0f },
        };
        GXLightObj lt;
        const GxHleDraw* d;
        int c;
        int expect_draws = 5 + 3 + 1;

        gx_hle_begin_frame();
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        for (c = 0; c < 5; ++c) {
            GXInitLightDistAttn(&lt, dist_cases[c].ref_dist,
                                dist_cases[c].ref_br, dist_cases[c].fn);
            GXLoadLightObjImm(&lt, GX_LIGHT0);
            GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
            GXPosition3f32(0.0f, 0.0f, 0.0f);
            GXPosition3f32(1.0f, 0.0f, 0.0f);
            GXPosition3f32(0.0f, 1.0f, 0.0f);
        }
        for (c = 0; c < 3; ++c) {
            GXInitLightSpot(&lt, spot_cases[c].cutoff, spot_cases[c].fn);
            GXLoadLightObjImm(&lt, GX_LIGHT0);
            GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
            GXPosition3f32(0.0f, 0.0f, 0.0f);
            GXPosition3f32(1.0f, 0.0f, 0.0f);
            GXPosition3f32(0.0f, 1.0f, 0.0f);
        }
        GXInitLightDir(&lt, 0.25f, 0.5f, 0.75f);
        GXLoadLightObjImm(&lt, GX_LIGHT0);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition3f32(0.0f, 0.0f, 0.0f);
        GXPosition3f32(1.0f, 0.0f, 0.0f);
        GXPosition3f32(0.0f, 1.0f, 0.0f);
        gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
        if (dc != (size_t) expect_draws) {
            printf("direct: FAIL light draws=%zu (want %d)\n", dc,
                   expect_draws);
            return 0;
        }
        for (c = 0; c < 5; ++c) {
            const GxHleLight* l = &draws[c].state.lights[0];
            if (fabsf(l->k[0] - dist_cases[c].k0) > 1e-6f ||
                fabsf(l->k[1] - dist_cases[c].k1) > 1e-6f ||
                fabsf(l->k[2] - dist_cases[c].k2) > 1e-6f) {
                printf("direct: FAIL dist attn case %d k=(%.6f,%.6f,%.6f) want "
                       "(%.6f,%.6f,%.6f)\n", c, (double) l->k[0],
                       (double) l->k[1], (double) l->k[2],
                       (double) dist_cases[c].k0, (double) dist_cases[c].k1,
                       (double) dist_cases[c].k2);
                fail = 1;
            }
        }
        for (c = 0; c < 3; ++c) {
            const GxHleLight* l = &draws[5 + c].state.lights[0];
            if (fabsf(l->a[0] - spot_cases[c].a0) > 1e-5f ||
                fabsf(l->a[1] - spot_cases[c].a1) > 1e-5f ||
                fabsf(l->a[2] - spot_cases[c].a2) > 1e-5f) {
                printf("direct: FAIL spot case %d a=(%.5f,%.5f,%.5f) want "
                       "(%.5f,%.5f,%.5f)\n", c, (double) l->a[0],
                       (double) l->a[1], (double) l->a[2],
                       (double) spot_cases[c].a0, (double) spot_cases[c].a1,
                       (double) spot_cases[c].a2);
                fail = 1;
            }
        }
        d = &draws[8];
        if (d->state.lights[0].dir[0] != 0.25f ||
            d->state.lights[0].dir[1] != 0.5f ||
            d->state.lights[0].dir[2] != 0.75f) {
            printf("direct: FAIL light dir=(%.3f,%.3f,%.3f) want raw input\n",
                   (double) d->state.lights[0].dir[0],
                   (double) d->state.lights[0].dir[1],
                   (double) d->state.lights[0].dir[2]);
            fail = 1;
        }
        light_dc = dc;
        light_vc = vc;
        light_prims = gx_hle_primitive_count();
    }

    /* P-680: GX_LINES/GX_LINESTRIP/GX_POINTS were dropped before; each
     * primitive group must land as a run with its own topology. */
    {
        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.0f);
        GXColor4u8(255, 255, 255, 255);
        GXPosition3f32(1.0f, -1.0f, 0.0f);
        GXColor4u8(255, 255, 255, 255);
        GXPosition3f32(1.0f, 1.0f, 0.0f);
        GXColor4u8(255, 255, 255, 255);
        GXPosition3f32(-1.0f, 1.0f, 0.0f);
        GXColor4u8(255, 255, 255, 255);
        GXBegin(GX_LINES, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, 0.0f, 0.0f);
        GXColor4u8(255, 0, 0, 255);
        GXPosition3f32(1.0f, 0.0f, 0.0f);
        GXColor4u8(255, 0, 0, 255);
        GXPosition3f32(-1.0f, 0.5f, 0.0f);
        GXColor4u8(255, 0, 0, 255);
        GXPosition3f32(1.0f, 0.5f, 0.0f);
        GXColor4u8(255, 0, 0, 255);
        GXBegin(GX_POINTS, GX_VTXFMT0, 3);
        GXPosition3f32(0.0f, 0.0f, 0.0f);
        GXColor4u8(0, 255, 0, 255);
        GXPosition3f32(0.5f, 0.5f, 0.0f);
        GXColor4u8(0, 255, 0, 255);
        GXPosition3f32(-0.5f, 0.5f, 0.0f);
        GXColor4u8(0, 255, 0, 255);
        gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
        if (dc != 3) {
            printf("direct: FAIL line/point draws=%zu (want 3)\n", dc);
            return 0;
        }
        if (draws[0].run_count != 1 ||
            draws[0].runs[0].mode != GX_HLE_MODE_TRIANGLES ||
            draws[0].runs[0].vertex_count != 6 ||
            draws[1].run_count != 1 ||
            draws[1].runs[0].mode != GX_HLE_MODE_LINES ||
            draws[1].runs[0].vertex_count != 4 ||
            draws[2].run_count != 1 ||
            draws[2].runs[0].mode != GX_HLE_MODE_POINTS ||
            draws[2].runs[0].vertex_count != 3) {
            printf("direct: FAIL runs tri=%d/%u lines=%d/%u points=%d/%u\n",
                   draws[0].run_count, draws[0].runs[0].vertex_count,
                   draws[1].run_count, draws[1].runs[0].vertex_count,
                   draws[2].run_count, draws[2].runs[0].vertex_count);
            fail = 1;
        }
    }

    /* P-679: GXSetFog packs the SDK's perspective coefficients
     * (A = f*n/((f-n)*(e-s)), B = f/(f-n), C = s/(e-s)) and the
     * degenerate-input fallback (A=0, B=0.5, C=0).  GXInitFogAdjTable's
     * 10-entry sqrt table and GXSetFogRangeAdj capture are checked too. */
    {
        GXColor fogcolor = { 0x00, 0x00, 0xFF, 0xFF };
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        gx_hle_begin_frame();
        GXSetFog(GX_FOG_LIN, 500.0f, 1000.0f, 100.0f, 5000.0f, fogcolor);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition3f32(0.0f, 0.0f, 0.0f);
        GXPosition3f32(1.0f, 0.0f, 0.0f);
        GXPosition3f32(0.0f, 1.0f, 0.0f);
        GXSetFog(GX_FOG_LIN, 0.0f, 0.0f, 100.0f, 5000.0f, fogcolor);
        {
            f32 proj[4][4] = { { 2.0f, 0.0f, 0.0f, 0.0f },
                               { 0.0f, 2.0f, 0.0f, 0.0f },
                               { 0.0f, 0.0f, -1.0f, -0.1f },
                               { 0.0f, 0.0f, -1.0f, 0.0f } };
            GXFogAdjTable* tbl =
                (GXFogAdjTable*) malloc(sizeof(GXFogAdjTable));
            GXInitFogAdjTable(tbl, 640, proj);
            if (tbl->r[0] != 256 || tbl->r[9] != 286) {
                printf("direct: FAIL fog adj r0=%u r9=%u (want 256/286)\n",
                       tbl->r[0], tbl->r[9]);
                fail = 1;
            }
            GXSetFogRangeAdj(GX_TRUE, 320, tbl);
            free(tbl);
        }
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GXPosition3f32(0.0f, 0.0f, 0.0f);
        GXPosition3f32(1.0f, 0.0f, 0.0f);
        GXPosition3f32(0.0f, 1.0f, 0.0f);
        gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
        if (dc != 2) {
            printf("direct: FAIL fog draws=%zu (want 2)\n", dc);
            return 0;
        }
        if (fabsf(draws[0].state.fog_a - 0.20408163f) > 1e-5f ||
            fabsf(draws[0].state.fog_b - 1.02040816f) > 1e-5f ||
            fabsf(draws[0].state.fog_c - 1.0f) > 1e-6f) {
            printf("direct: FAIL fog abc=(%.6f,%.6f,%.6f) want "
                   "(0.204082,1.020408,1.0)\n", (double) draws[0].state.fog_a,
                   (double) draws[0].state.fog_b,
                   (double) draws[0].state.fog_c);
            fail = 1;
        }
        if (draws[1].state.fog_a != 0.0f ||
            draws[1].state.fog_b != 0.5f ||
            draws[1].state.fog_c != 0.0f) {
            printf("direct: FAIL fog degenerate abc=(%.3f,%.3f,%.3f)\n",
                   (double) draws[1].state.fog_a,
                   (double) draws[1].state.fog_b,
                   (double) draws[1].state.fog_c);
            fail = 1;
        }
        if (draws[1].state.fog_adj_enable != 1 ||
            draws[1].state.fog_adj_center != 320 ||
            fabsf(draws[1].state.fog_adj_k[0] - 1.0f) > 1e-6f ||
            fabsf(draws[1].state.fog_adj_k[9] - 286.0f / 256.0f) > 1e-4f) {
            printf("direct: FAIL fog adj capture en=%u center=%u k0=%.5f\n",
                   draws[1].state.fog_adj_enable,
                   draws[1].state.fog_adj_center,
                   (double) draws[1].state.fog_adj_k[0]);
            fail = 1;
        }
    }

    /* P-675: the GXGetTexObj accessors and GXLoadTexObj must read the
     * caller's object, not the most recently initialized one (sobjlib and
     * lbspdisplay read stored texobjs long after other textures were
     * initialized). */
    {
        GXTexObj a, b;
        static unsigned char img_a[32];
        static unsigned char img_b[64];
        GxHleTexture* textures = NULL;
        size_t tcount = 0;

        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXInitTexObj(&a, img_a, 4, 4, GX_TF_I8, GX_CLAMP, GX_CLAMP,
                     GX_FALSE);
        GXInitTexObjLOD(&a, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE,
                        GX_FALSE, GX_ANISO_1);
        GXInitTexObj(&b, img_b, 8, 8, GX_TF_RGB565, GX_REPEAT, GX_MIRROR,
                     GX_FALSE);
        if (GXGetTexObjFmt(&a) != GX_TF_I8 || GXGetTexObjWidth(&a) != 4 ||
            GXGetTexObjHeight(&a) != 4 || GXGetTexObjWrapS(&a) != GX_CLAMP ||
            GXGetTexObjFmt(&b) != GX_TF_RGB565 ||
            GXGetTexObjWrapT(&b) != GX_MIRROR) {
            printf("direct: FAIL texobj readback a=(%d,%u,%u,%u) "
                   "b=(%d,%u)\n", (int) GXGetTexObjFmt(&a),
                   GXGetTexObjWidth(&a), GXGetTexObjHeight(&a),
                   (unsigned) GXGetTexObjWrapS(&a), (int) GXGetTexObjFmt(&b),
                   (unsigned) GXGetTexObjWrapT(&b));
            fail = 1;
        }
        GXLoadTexObj(&a, GX_TEXMAP0);
        gx_hle_get_frame(&verts, &vc, &draws, &dc, &textures, &tcount);
        if (tcount != 1 || textures[0].format != GX_TF_I8 ||
            textures[0].width != 4 || textures[0].height != 4) {
            printf("direct: FAIL texobj load n=%zu fmt=%u %ux%u (want 1/I8/"
                   "4x4)\n", tcount, tcount ? textures[0].format : 0,
                   tcount ? textures[0].width : 0,
                   tcount ? textures[0].height : 0);
            fail = 1;
        }
    }

    /* P-675: 5/6-bit channels expand by bit replication
     * (Aurora ExpandTo8): 5-bit 13 -> (13<<3)|(13>>2) = 107 (the old
     * v*255/31 formula gave 106); 6-bit 17 -> (17<<2)|(17>>4) = 69. */
    {
        static unsigned char tex565[32];
        uint8_t* rgba = NULL;
        char err[64];
        unsigned v = (13u << 11) | (17u << 5) | 7u;
        tex565[0] = (unsigned char) (v >> 8);
        tex565[1] = (unsigned char) (v & 0xFF);
        if (gx_texture_decode(tex565, sizeof(tex565), 4, 4, TEX_FMT_RGB565,
                              &rgba, err, sizeof(err)) != 0) {
            printf("direct: FAIL RGB565 decode: %s\n", err);
            fail = 1;
        } else {
            if (rgba[0] != 107 || rgba[1] != 69) {
                printf("direct: FAIL expand r=%u g=%u (want 107/69)\n",
                       rgba[0], rgba[1]);
                fail = 1;
            }
            free(rgba);
        }
    }

    /* P-692: vertex colours use the same bit-replication expansions as the
     * texture decoder (Aurora ExpandTo8), and RGBX8's X byte is ignored.
     * RGB565 (13,17,7): 5-bit 13 -> 107 (old v*255/31 gave 106). */
    {
        static const unsigned char want565[4] = { 107, 69, 57, 255 };
        static const unsigned char want_rgbx[4] = { 10, 20, 30, 255 };
        unsigned v = (13u << 11) | (17u << 5) | 7u;

        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGB, GX_RGB565, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        {
            int i;
            for (i = 0; i < 3; ++i) {
                GXPosition3f32((float) i, 0.0f, 0.0f);
                GXColor1u16((u16) v);
            }
        }
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGB, GX_RGBX8, 0);
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
        {
            int i;
            for (i = 0; i < 3; ++i) {
                GXPosition3f32((float) i + 4.0f, 0.0f, 0.0f);
                GXColor4u8(10, 20, 30, 0);
            }
        }
        gx_hle_get_frame(&verts, &vc, &draws, &dc, NULL, NULL);
        if (dc != 2 || vc != 6) {
            printf("direct: FAIL colour-fmt draws=%zu verts=%zu (want 2/6)\n",
                   dc, vc);
            fail = 1;
        } else {
            int a;
            for (a = 0; a < 4; ++a) {
                if (verts[draws[0].first_vertex].color[a] != want565[a]) {
                    printf("direct: FAIL RGB565 clr[%d]=%u want %u\n", a,
                           verts[draws[0].first_vertex].color[a],
                           want565[a]);
                    fail = 1;
                }
                if (verts[draws[1].first_vertex].color[a] != want_rgbx[a]) {
                    printf("direct: FAIL RGBX8 clr[%d]=%u want %u\n", a,
                           verts[draws[1].first_vertex].color[a],
                           want_rgbx[a]);
                    fail = 1;
                }
            }
        }
    }

    printf("direct: %s draws=%zu verts=%zu primitives=%u\n",
           fail ? "FAIL" : "PASS", light_dc, light_vc,
           (unsigned) light_prims);
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

    /* ---- pass 5: P-672 indirect texturing ----
     * A 4x4 RGBA8 texture with texel (1,1) green and (2,1) magenta is
     * sampled at the centre of (1,1) while an IA8 indirect map (A=128)
     * offsets by one texel in +S through a static GX_ITM_0 matrix.  The
     * readback must be magenta with the matrix and green after
     * GXSetTevDirect disabled the stage. */
    {
        static unsigned char dest_rgba8[64];
        static unsigned char ind_ia8[32];
        GXTexObj dest_obj, ind_obj;
        f32 ind_mtx[2][3] = { { 1.0f / 128.0f, 0.0f, 0.0f },
                              { 0.0f, 1.0f / 128.0f, 0.0f } };
        int x, y;

        memset(dest_rgba8, 0, sizeof(dest_rgba8));
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                unsigned char rgba[4] = { 0, 0, 0, 255 };
                size_t ar = (size_t) y * 4 + (size_t) x;
                size_t gb = 32 + (size_t) y * 8 + (size_t) x * 2;
                if (x == 1 && y == 1) {
                    rgba[0] = 0;
                    rgba[1] = 255;
                    rgba[2] = 0;
                } else if (x == 2 && y == 1) {
                    rgba[0] = 255;
                    rgba[1] = 0;
                    rgba[2] = 255;
                }
                dest_rgba8[ar * 2 + 0] = rgba[3];
                dest_rgba8[ar * 2 + 1] = rgba[0];
                dest_rgba8[gb] = rgba[1];
                dest_rgba8[gb + 1] = rgba[2];
            }
        }
        /* GX IA8 stores the alpha byte first, then intensity (texture.c:
         * decode_ia8).  A=0x80 -> indirect S offset of +128/128 = 1 texel. */
        for (x = 0; x < 16; ++x) {
            ind_ia8[x * 2] = 0x80;
            ind_ia8[x * 2 + 1] = 0x00;
        }

        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX,
                      GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetNumTexGens(2);
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0,
                          GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1,
                          GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0,
                      GX_COLOR_NULL);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                        GX_CC_TEXC);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                        GX_CA_TEXA);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                        GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                        GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXInitTexObj(&dest_obj, dest_rgba8, 4, 4, GX_TF_RGBA8, GX_CLAMP,
                     GX_CLAMP, GX_FALSE);
        GXLoadTexObj(&dest_obj, GX_TEXMAP0);
        GXInitTexObj(&ind_obj, ind_ia8, 4, 4, GX_TF_IA8, GX_REPEAT,
                     GX_REPEAT, GX_FALSE);
        GXLoadTexObj(&ind_obj, GX_TEXMAP1);
        GXSetNumIndStages(1);
        GXSetIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD1, GX_TEXMAP1);
        GXSetIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
        GXSetIndTexMtx(GX_ITM_0, ind_mtx, 0);
        GXSetTevIndirect(GX_TEVSTAGE0, GX_INDTEXSTAGE0, GX_ITF_8,
                         GX_ITB_NONE, GX_ITM_0, GX_ITW_OFF, GX_ITW_OFF,
                         GX_FALSE, GX_FALSE, GX_ITBA_OFF);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX1, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3f32(1.0f, -1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3f32(1.0f, 1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3f32(-1.0f, 1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        GXTexCoord2f32(0.0f, 0.0f);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (indirect)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[0] > 200 && pixel[1] < 60 && pixel[2] > 200)) {
            printf("efb: FAIL indirect offset pixel=%u,%u,%u (want magenta "
                   "texel 2,1)\n", pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }

        /* Disable the stage; the base coordinate must select texel (1,1). */
        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX,
                      GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetNumTexGens(2);
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0,
                          GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        GXSetTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1,
                          GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0,
                      GX_COLOR_NULL);
        GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXInitTexObj(&dest_obj, dest_rgba8, 4, 4, GX_TF_RGBA8, GX_CLAMP,
                     GX_CLAMP, GX_FALSE);
        GXLoadTexObj(&dest_obj, GX_TEXMAP0);
        GXSetTevDirect(GX_TEVSTAGE0);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        GXPosition3f32(1.0f, -1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        GXPosition3f32(1.0f, 1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        GXPosition3f32(-1.0f, 1.0f, 0.0f);
        GXTexCoord2f32(0.375f, 0.375f);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (direct)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[1] > 200 && pixel[0] < 60 && pixel[2] < 60)) {
            printf("efb: FAIL direct pixel=%u,%u,%u (want green texel 1,1)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
    }

    /* ---- pass 6: P-673 specular channel is light-tinted ----
     * Channel 1 with GX_AF_SPEC (the SDK's 0, HSD's default) accumulates
     * attn * light.color per channel.  With N = H the a/k polynomial gives
     * attn = 1, so the readback must be the light colour (orange), not the
     * old grey average. */
    {
        GXLightObj lt;
        GXColor white = { 0xFF, 0xFF, 0xFF, 0xFF };
        GXColor black = { 0x00, 0x00, 0x00, 0xFF };
        GXColor orange = { 0xFF, 0x80, 0x00, 0xFF };

        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
        GXSetNumChans(2);
        GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG,
                      GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        GXSetChanCtrl(GX_COLOR1, GX_TRUE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT0,
                      GX_DF_CLAMP, GX_AF_SPEC);
        GXSetChanAmbColor(GX_COLOR1, black);
        GXSetChanMatColor(GX_COLOR1, white);
        GXInitLightColor(&lt, orange);
        GXInitLightPos(&lt, 0.0f, 0.0f, 1048576.0f);
        GXInitLightAttn(&lt, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.5f);
        GXInitLightDir(&lt, 0.0f, 0.0f, 1.0f);
        GXLoadLightObjImm(&lt, GX_LIGHT0);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                      GX_COLOR1);
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                        GX_CC_RASC);
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                        GX_CA_RASA);
        GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                        GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                        GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.0f);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        GXPosition3f32(1.0f, -1.0f, 0.0f);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        GXPosition3f32(1.0f, 1.0f, 0.0f);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        GXPosition3f32(-1.0f, 1.0f, 0.0f);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (spec)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[0] > 240 && pixel[1] > 110 && pixel[1] < 150 &&
              pixel[2] < 20)) {
            printf("efb: FAIL spec pixel=%u,%u,%u (want tinted 255,128,0)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
    }

    /* ---- pass 7: P-674 EFB copy formats ----
     * A green screen copied to I8/IA8/IA4/I4/RGB5A3 must use the BT.601
     * luma path (Aurora tex_copy_conv.cpp) and the tiling the decoders read:
     * I8 byte 0x91, IA8 [0xFF,0x91], IA4 0xF9, I4 0x99, RGB5A3 0x83E0. */
    {
        unsigned char dst_i8[64];
        unsigned char dst_ia8[64];
        unsigned char dst_ia4[64];
        unsigned char dst_i4[64];
        unsigned char dst_5a3[64];
        GXColor green = { 0x00, 0xFF, 0x00, 0xFF };

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
        GXColor4u8(green.r, green.g, green.b, green.a);
        GXPosition3f32(1.0f, -1.0f, 0.0f);
        GXColor4u8(green.r, green.g, green.b, green.a);
        GXPosition3f32(1.0f, 1.0f, 0.0f);
        GXColor4u8(green.r, green.g, green.b, green.a);
        GXPosition3f32(-1.0f, 1.0f, 0.0f);
        GXColor4u8(green.r, green.g, green.b, green.a);

        memset(dst_i8, 0x11, sizeof(dst_i8));
        memset(dst_ia8, 0x11, sizeof(dst_ia8));
        memset(dst_ia4, 0x11, sizeof(dst_ia4));
        memset(dst_i4, 0x11, sizeof(dst_i4));
        memset(dst_5a3, 0x11, sizeof(dst_5a3));
        GXSetTexCopySrc(0, 0, 640, 480);
        GXSetTexCopyDst(8, 4, GX_TF_I8, GX_FALSE);
        GXCopyTex(dst_i8, GX_FALSE);
        GXSetTexCopyDst(8, 4, GX_TF_IA8, GX_FALSE);
        GXCopyTex(dst_ia8, GX_FALSE);
        GXSetTexCopyDst(8, 4, GX_TF_IA4, GX_FALSE);
        GXCopyTex(dst_ia4, GX_FALSE);
        GXSetTexCopyDst(8, 4, GX_TF_I4, GX_FALSE);
        GXCopyTex(dst_i4, GX_FALSE);
        GXSetTexCopyDst(8, 4, GX_TF_RGB5A3, GX_FALSE);
        GXCopyTex(dst_5a3, GX_FALSE);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (copy formats)\n");
            return 0;
        }
        if (dst_i8[0] != 0x91) {
            printf("efb: FAIL I8 copy %02x (want 91)\n", dst_i8[0]);
            fail = 1;
        }
        if (dst_ia8[0] != 0xFF || dst_ia8[1] != 0x91) {
            printf("efb: FAIL IA8 copy %02x%02x (want ff91)\n", dst_ia8[0],
                   dst_ia8[1]);
            fail = 1;
        }
        if (dst_ia4[0] != 0xF9) {
            printf("efb: FAIL IA4 copy %02x (want f9)\n", dst_ia4[0]);
            fail = 1;
        }
        if (dst_i4[0] != 0x99) {
            printf("efb: FAIL I4 copy %02x (want 99)\n", dst_i4[0]);
            fail = 1;
        }
        if (dst_5a3[0] != 0x83 || dst_5a3[1] != 0xE0) {
            printf("efb: FAIL RGB5A3 copy %02x%02x (want 83e0)\n", dst_5a3[0],
                   dst_5a3[1]);
            fail = 1;
        }
    }

    /* ---- pass 8: P-680 lines and points rasterize ----
     * A horizontal red line at window row 240 and a 5px green point at
     * window (480,360) must produce those pixels (before P-680 the
     * primitives were dropped entirely). */
    {
        GXColor red = { 0xFF, 0x00, 0x00, 0xFF };
        GXColor green = { 0x00, 0xFF, 0x00, 0xFF };
        const float y_center = 1.0f / 480.0f; /* NDC -> window y 240.5 */

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
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXSetPointSize(5, GX_TO_ONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXBegin(GX_LINES, GX_VTXFMT0, 2);
        GXPosition3f32(-1.0f, y_center, 0.0f);
        GXColor4u8(red.r, red.g, red.b, red.a);
        GXPosition3f32(1.0f, y_center, 0.0f);
        GXColor4u8(red.r, red.g, red.b, red.a);
        GXBegin(GX_POINTS, GX_VTXFMT0, 1);
        GXPosition3f32(0.5f, 0.5f, 0.0f);
        GXColor4u8(green.r, green.g, green.b, green.a);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (lines/points)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[0] > 200 && pixel[1] < 60 && pixel[2] < 60)) {
            printf("efb: FAIL line pixel=%u,%u,%u (want red row 240)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
        glReadPixels(480, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[1] > 200 && pixel[0] < 60 && pixel[2] < 60)) {
            printf("efb: FAIL point pixel=%u,%u,%u (want green 5px point)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
        /* One pixel off center: only a point larger than 1px covers it. */
        glReadPixels(481, 360, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[1] > 200 && pixel[0] < 60 && pixel[2] < 60)) {
            printf("efb: FAIL point size pixel=%u,%u,%u (want 5px coverage)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
    }

    /* ---- pass 9: P-679/P-690 hardware fog coordinates ----
     * A green quad spans GX screen depth 0.3 (left) .. 0.8 (right).  GX clip
     * z/w is [-1,0] and the hardware evaluates fog against the viewport
     * screen depth `far + z/w * (far - near)` (SDK GXProject), which is
     * `z/w + 1` for the usual [0,1] depth range; GL instead reports
     * `(z/w + 1) / 2`.  The shader must undo GL's mapping or every fog is
     * half strength.  LIN and EXP2 must match the SDK coefficient math, and
     * range adjustment must darken the far pixel by the SDK table. */
    {
        GXColor fog_red = { 0xFF, 0x00, 0x00, 0xFF };
        GXColor green = { 0x00, 0xFF, 0x00, 0xFF };
        const float nearz = 0.1f;
        const float farz = 1.0f;
        const float startz = 0.1f;
        const float endz = 0.5f;
        const float a =
            (farz * nearz) / ((farz - nearz) * (endz - startz));
        const float b = farz / (farz - nearz);
        const float c = startz / (endz - startz);
        GXFogAdjTable tbl;
        int frame;

        for (frame = 0; frame < 3; ++frame) {
            /* Identity leaves clip z/w at -0.7..-0.2, which the GX viewport
             * maps to screen depth 0.3..0.8.  GL reports 0.15..0.4 instead. */
            const float far_x_ndc = 2.0f * 608.0f / 640.0f - 1.0f;
            const float near_x_ndc = 2.0f * 64.0f / 640.0f - 1.0f;
            const float d_far = 0.3f + 0.5f * (far_x_ndc + 1.0f) * 0.5f;
            const float d_near = 0.3f + 0.5f * (near_x_ndc + 1.0f) * 0.5f;
            const float base = a / (b - d_far);
            float fog_far = base - c;
            float fog_near = (a / (b - d_near)) - c;
            GXColor expect;
            GXColor expect_near;

            if (fog_far < 0.0f) fog_far = 0.0f;
            if (fog_far > 1.0f) fog_far = 1.0f;
            if (fog_near < 0.0f) fog_near = 0.0f;
            if (fog_near > 1.0f) fog_near = 1.0f;
            if (frame == 1) {
                fog_far = 1.0f - exp2f(-8.0f * fog_far * fog_far);
            }
            expect.r = (unsigned char) (fog_far * 255.0f + 0.5f);
            expect.g = (unsigned char) ((1.0f - fog_far) * 255.0f + 0.5f);
            expect_near.r = (unsigned char) (fog_near * 255.0f + 0.5f);
            expect_near.g =
                (unsigned char) ((1.0f - fog_near) * 255.0f + 0.5f);

            gx_hle_begin_frame();
            gx_hle_reset_state();
            GXSetProjection((f32(*)[4]) identity, GX_ORTHOGRAPHIC);
            GXSetNumChans(1);
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX,
                          GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
            GXSetNumTexGens(0);
            GXSetNumTevStages(1);
            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                          GX_COLOR0A0);
            GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
            GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
            GXSetCullMode(GX_CULL_NONE);
            GXSetFog(frame == 1 ? GX_FOG_EXP2 : GX_FOG_LIN, startz, endz,
                     nearz, farz, fog_red);
            if (frame == 2) {
                f32 adj_proj[4][4] = { { 2.0f, 0.0f, 0.0f, 0.0f },
                                       { 0.0f, 2.0f, 0.0f, 0.0f },
                                       { 0.0f, 0.0f, -1.0f, -0.1f },
                                       { 0.0f, 0.0f, -1.0f, 0.0f } };
                const float offset = (608.0f - 320.0f) * 2.0f / 640.0f;
                const float fi = 9.0f - fabsf(offset) * 9.0f;
                const int ilo = (int) fi;
                const int ihi = ilo < 9 ? ilo + 1 : 9;
                float k;

                GXInitFogAdjTable(&tbl, 640, adj_proj);
                GXSetFogRangeAdj(GX_TRUE, 320, &tbl);
                /* Same adjustment the shader applies: k interpolated from the
                 * SDK table for x=608 (0.9 of the way to the right edge from
                 * the 320-pixel center), then base *= sqrt(offset^2+k^2)/k. */
                k = ((float) (tbl.r[ilo] & 0xFFFu) / 256.0f) +
                    (((float) (tbl.r[ihi] & 0xFFFu) / 256.0f) -
                     ((float) (tbl.r[ilo] & 0xFFFu) / 256.0f)) *
                        (fi - (float) ilo);
                fog_far = base * sqrtf(offset * offset + k * k) / k - c;
                if (fog_far < 0.0f) fog_far = 0.0f;
                if (fog_far > 1.0f) fog_far = 1.0f;
                expect.r = (unsigned char) (fog_far * 255.0f + 0.5f);
                expect.g = (unsigned char) ((1.0f - fog_far) * 255.0f + 0.5f);
            }
            GXClearVtxDesc();
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8,
                            0);
            GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
            GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
            GXPosition3f32(-1.0f, -1.0f, -0.7f);
            GXColor4u8(green.r, green.g, green.b, green.a);
            GXPosition3f32(1.0f, -1.0f, -0.2f);
            GXColor4u8(green.r, green.g, green.b, green.a);
            GXPosition3f32(1.0f, 1.0f, -0.2f);
            GXColor4u8(green.r, green.g, green.b, green.a);
            GXPosition3f32(-1.0f, 1.0f, -0.7f);
            GXColor4u8(green.r, green.g, green.b, green.a);
            if (gx_gl_render_frame() < 0) {
                printf("efb: FAIL render_frame (fog)\n");
                return 0;
            }
            glReadPixels(608, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (frame == 0) {
                if (abs((int) pixel[0] - expect.r) > 6 ||
                    abs((int) pixel[1] - expect.g) > 6) {
                    printf("efb: FAIL fog LIN pixel=%u,%u,%u want ~%u,%u\n",
                           pixel[0], pixel[1], pixel[2], expect.r, expect.g);
                    fail = 1;
                }
                glReadPixels(64, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                             pixel);
                if (abs((int) pixel[0] - expect_near.r) > 6 ||
                    abs((int) pixel[1] - expect_near.g) > 6) {
                    printf("efb: FAIL fog near pixel=%u,%u,%u want ~%u,%u\n",
                           pixel[0], pixel[1], pixel[2], expect_near.r,
                           expect_near.g);
                    fail = 1;
                }
            } else if (frame == 1) {
                if (abs((int) pixel[0] - expect.r) > 8 ||
                    abs((int) pixel[1] - expect.g) > 8) {
                    printf("efb: FAIL fog EXP2 pixel=%u,%u,%u want ~%u,%u\n",
                           pixel[0], pixel[1], pixel[2], expect.r, expect.g);
                    fail = 1;
                }
            } else {
                if (abs((int) pixel[0] - expect.r) > 6 ||
                    abs((int) pixel[1] - expect.g) > 6) {
                    printf("efb: FAIL fog range adj pixel=%u,%u,%u want "
                           "~%u,%u\n", pixel[0], pixel[1], pixel[2], expect.r,
                           expect.g);
                    fail = 1;
                }
            }
        }
    }

    /* ---- pass 10: P-681 spot cone cosine attenuation ----
     * GX_AF_SPOT with a = (0,0,1) and k = (1,0,0) makes the channel raster
     * attn * lightColor with attn = cos^2(axis).  Two size-1 points: one on
     * the axis (cos=1 -> green), one at cos=0.5 (-> 0.25 green). */
    {
        static const float spot_identity[4][4] = {
            { 0.2f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.5f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        GXLightObj lt;
        GXColor white = { 0xFF, 0xFF, 0xFF, 0xFF };
        GXColor black = { 0x00, 0x00, 0x00, 0xFF };
        GXColor green = { 0x00, 0xFF, 0x00, 0xFF };

        gx_hle_begin_frame();
        gx_hle_reset_state();
        GXSetProjection((f32(*)[4]) spot_identity, GX_PERSPECTIVE);
        GXSetNumChans(1);
        GXSetChanCtrl(GX_COLOR0A0, GX_TRUE, GX_SRC_REG, GX_SRC_REG,
                      GX_LIGHT0, GX_DF_NONE, GX_AF_SPOT);
        GXSetChanAmbColor(GX_COLOR0A0, black);
        GXSetChanMatColor(GX_COLOR0A0, white);
        GXInitLightColor(&lt, green);
        GXInitLightPos(&lt, 0.0f, 0.0f, 2.0f);
        GXInitLightDir(&lt, 0.0f, 0.0f, -1.0f); /* travel direction */
        GXInitLightAttn(&lt, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f);
        GXLoadLightObjImm(&lt, GX_LIGHT0);
        GXSetNumTexGens(0);
        GXSetNumTevStages(1);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                      GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
        GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        GXSetCullMode(GX_CULL_NONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        /* Clip coordinates chosen so the 1px point centres land on pixel
         * centres (320.5, 240.5) and (541.5, 240.5). */
        GXBegin(GX_POINTS, GX_VTXFMT0, 2);
        GXPosition3f32(1.0f / 640.0f, 1.0f / 480.0f, 0.0f);
        GXPosition3f32(3.4570313f, 1.0f / 480.0f, 0.0f);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (spot)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[1] > 240 && pixel[0] < 20 && pixel[2] < 20)) {
            printf("efb: FAIL spot axis pixel=%u,%u,%u (want green)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
        glReadPixels(541, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[1] > 50 && pixel[1] < 80 && pixel[0] < 20)) {
            printf("efb: FAIL spot cone pixel=%u,%u,%u (want ~0,64,0)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
    }

    /* ---- pass 11: P-682 Z24X8 depth snapshots and Z-texture ADD/bias ----
     * Frame A encodes a two-depth scene through GXCopyTex(Z24X8) and decodes
     * it back; frame B proves ZT_ADD (erase depth 0.5 + texel 0.5); frame C
     * proves the 24-bit bias is added under ZT_REPLACE. */
    {
        static unsigned char z24[64];
        static unsigned char z24_b[64];
        static unsigned char z24_c[64];
        int x, y;

        /* ---- frame A: snapshot encode + decode ---- */
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
        GXSetCullMode(GX_CULL_NONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.3f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(0.0f, -1.0f, 0.3f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(0.0f, 1.0f, 0.3f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(-1.0f, 1.0f, 0.3f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(0.0f, -1.0f, 0.7f);
        GXColor4u8(0, 0xFF, 0, 0xFF);
        GXPosition3f32(1.0f, -1.0f, 0.7f);
        GXColor4u8(0, 0xFF, 0, 0xFF);
        GXPosition3f32(1.0f, 1.0f, 0.7f);
        GXColor4u8(0, 0xFF, 0, 0xFF);
        GXPosition3f32(0.0f, 1.0f, 0.7f);
        GXColor4u8(0, 0xFF, 0, 0xFF);
        GXSetTexCopySrc(0, 0, 640, 480);
        GXSetTexCopyDst(4, 4, GX_TF_Z24X8, GX_FALSE);
        memset(z24, 0x55, sizeof(z24));
        GXCopyTex(z24, GX_FALSE);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (z24 snapshot)\n");
            return 0;
        }
        /* gl depth (z+1)/2: 0.65 left, 0.85 right -> top bytes 166/217. */
        for (x = 0; x < 4; ++x) {
            int expected = x < 2 ? 166 : 217;
            int got = z24[(size_t) x * 2];
            if (abs(got - expected) > 2) {
                printf("efb: FAIL z24 snapshot x=%d high=%d want ~%d\n", x,
                       got, expected);
                fail = 1;
            }
        }
        {
            uint8_t* dec = NULL;
            char err[64];
            if (gx_texture_decode(z24, sizeof(z24), 4, 4, TEX_FMT_Z24X8,
                                  &dec, err, sizeof(err)) != 0) {
                printf("efb: FAIL z24 decode: %s\n", err);
                fail = 1;
            } else {
                if (abs((int) dec[0] - 166) > 2 ||
                    abs((int) dec[8] - 217) > 2) {
                    printf("efb: FAIL z24 decoded %u/%u want 166/217\n",
                           dec[0], dec[8]);
                    fail = 1;
                }
                free(dec);
            }
        }

        /* ---- frame B: ZT_ADD ---- */
        for (x = 0; x < 64; ++x) {
            z24_b[x] = 0;
        }
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                size_t hi = (size_t) y * 4 + (size_t) x;
                z24_b[hi * 2] = 0x80; /* top byte 128 = depth 0.5 */
            }
        }
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
        GXSetCullMode(GX_CULL_NONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(1.0f, -1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(1.0f, 1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(-1.0f, 1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        {
            GXTexObj ztex;
            GXSetNumTexGens(1);
            GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0,
                              GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0,
                          GX_COLOR0A0);
            GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            GXInitTexObj(&ztex, z24_b, 4, 4, GX_TF_Z24X8, GX_CLAMP,
                         GX_CLAMP, GX_FALSE);
            GXLoadTexObj(&ztex, GX_TEXMAP0);
            GXSetZTexture(GX_ZT_ADD, GX_TF_Z24X8, 0);
            GXSetColorUpdate(GX_FALSE);
            GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
            GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
            GXPosition3f32(-1.0f, -1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(0.0f, 0.0f);
            GXPosition3f32(1.0f, -1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(1.0f, 0.0f);
            GXPosition3f32(1.0f, 1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(1.0f, 1.0f);
            GXPosition3f32(-1.0f, 1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(0.0f, 1.0f);
            GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
            GXSetColorUpdate(GX_TRUE);
        }
        GXSetNumTexGens(0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                      GX_COLOR0A0);
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetZMode(GX_TRUE, GX_LESS, GX_TRUE);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.8f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        GXPosition3f32(1.0f, -1.0f, 0.8f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        GXPosition3f32(1.0f, 1.0f, 0.8f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        GXPosition3f32(-1.0f, 1.0f, 0.8f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (ZT_ADD)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[2] > 200 && pixel[0] < 60)) {
            printf("efb: FAIL ZT_ADD pixel=%u,%u,%u (want blue)\n", pixel[0],
                   pixel[1], pixel[2]);
            fail = 1;
        }

        /* ---- frame C: ZT_REPLACE + 24-bit bias ---- */
        for (x = 0; x < 64; ++x) {
            z24_c[x] = 0;
        }
        for (y = 0; y < 4; ++y) {
            for (x = 0; x < 4; ++x) {
                size_t hi = (size_t) y * 4 + (size_t) x;
                z24_c[hi * 2] = 0x33; /* top byte 51 = depth 0.2 */
            }
        }
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
        GXSetCullMode(GX_CULL_NONE);
        GXClearVtxDesc();
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(1.0f, -1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(1.0f, 1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        GXPosition3f32(-1.0f, 1.0f, 0.2f);
        GXColor4u8(0xFF, 0, 0, 0xFF);
        {
            GXTexObj ztex;
            GXSetNumTexGens(1);
            GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0,
                              GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0,
                          GX_COLOR0A0);
            GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
            GXInitTexObj(&ztex, z24_c, 4, 4, GX_TF_Z24X8, GX_CLAMP,
                         GX_CLAMP, GX_FALSE);
            GXLoadTexObj(&ztex, GX_TEXMAP0);
            /* bias 0x199999 = 0.1: REPLACE stores 0.2 + 0.1 = 0.3. */
            GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0x199999);
            GXSetColorUpdate(GX_FALSE);
            GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
            GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
            GXPosition3f32(-1.0f, -1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(0.0f, 0.0f);
            GXPosition3f32(1.0f, -1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(1.0f, 0.0f);
            GXPosition3f32(1.0f, 1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(1.0f, 1.0f);
            GXPosition3f32(-1.0f, 1.0f, 0.0f);
            GXColor4u8(0, 0, 0, 0);
            GXTexCoord2f32(0.0f, 1.0f);
            GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
            GXSetColorUpdate(GX_TRUE);
        }
        GXSetNumTexGens(0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                      GX_COLOR0A0);
        GXClearVtxDesc();
        GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
        GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        GXSetZMode(GX_TRUE, GX_LESS, GX_TRUE);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(-1.0f, -1.0f, -0.5f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        GXPosition3f32(1.0f, -1.0f, -0.5f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        GXPosition3f32(1.0f, 1.0f, -0.5f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        GXPosition3f32(-1.0f, 1.0f, -0.5f);
        GXColor4u8(0, 0, 0xFF, 0xFF);
        if (gx_gl_render_frame() < 0) {
            printf("efb: FAIL render_frame (ZT_REPLACE bias)\n");
            return 0;
        }
        glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        if (!(pixel[2] > 200 && pixel[0] < 60)) {
            printf("efb: FAIL ZT_REPLACE bias pixel=%u,%u,%u (want blue)\n",
                   pixel[0], pixel[1], pixel[2]);
            fail = 1;
        }
    }

    /* P-694: the TEV raster channel selects rast1 for COLOR1/ALPHA1/COLOR1A1
     * and black for GX_COLOR_NULL/GX_COLOR_ZERO (Aurora attr_fmt.cpp
     * color_channel + shader.cpp color_arg_reg).  A stage ordering ALPHA1
     * must see the COLOR1A1 material, not the COLOR0A0 one. */
    {
        GXColor red = { 0xFF, 0x00, 0x00, 0xFF };
        GXColor green = { 0x00, 0xFF, 0x00, 0xFF };
        int pass;

        for (pass = 0; pass < 2; ++pass) {
            gx_hle_begin_frame();
            gx_hle_reset_state();
            GXSetProjection((f32(*)[4]) identity, GX_PERSPECTIVE);
            GXSetNumChans(2);
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG,
                          GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
            GXSetChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG,
                          GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
            GXSetChanMatColor(GX_COLOR0A0, red);
            GXSetChanMatColor(GX_COLOR1A1, green);
            GXSetNumTexGens(0);
            GXSetNumTevStages(1);
            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL,
                          pass == 0 ? GX_ALPHA1 : GX_COLOR_NULL);
            GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                            GX_CC_RASC);
            GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                            GX_CA_RASA);
            GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                            GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO,
                            GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
            GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
            GXSetCullMode(GX_CULL_NONE);
            GXClearVtxDesc();
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
            GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
            GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
            GXPosition3f32(-1.0f, -1.0f, 0.0f);
            GXColor4u8(255, 255, 255, 255);
            GXPosition3f32(1.0f, -1.0f, 0.0f);
            GXColor4u8(255, 255, 255, 255);
            GXPosition3f32(1.0f, 1.0f, 0.0f);
            GXColor4u8(255, 255, 255, 255);
            GXPosition3f32(-1.0f, 1.0f, 0.0f);
            GXColor4u8(255, 255, 255, 255);
            if (gx_gl_render_frame() < 0) {
                printf("efb: FAIL render_frame (raster channel)\n");
                return 0;
            }
            glReadPixels(320, 240, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (pass == 0) {
                if (!(pixel[1] > 200 && pixel[0] < 60 && pixel[2] < 60)) {
                    printf("efb: FAIL ALPHA1 channel pixel=%u,%u,%u (want "
                           "green rast1)\n", pixel[0], pixel[1], pixel[2]);
                    fail = 1;
                }
            } else if (pixel[0] > 20 || pixel[1] > 20 || pixel[2] > 20) {
                printf("efb: FAIL NULL channel pixel=%u,%u,%u (want black)\n",
                       pixel[0], pixel[1], pixel[2]);
                fail = 1;
            }
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

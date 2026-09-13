/*
 * S2 GX high-level emulation: see gx_hle.h.
 *
 * Command semantics come from extern/dolphin/src/dolphin/gx/{GXAttr,GXTev,
 * GXLight,GXTexture,GXPixel,GXGeometry,GXDisplayList}.c; the vertex-array and
 * display-list decoding mirrors native/hsd/model.c, the verified prototype
 * parser.  HSD's usage is in src/sysdolphin/baselib/{pobj,tobj,mobj,tev,
 * state,displayfunc}.c.
 */
#include "decomp/gx/gx_hle.h"

#include <dolphin/gx.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ state */

typedef struct {
    u8 type; /* GXAttrType, GX_NONE = absent */
} GxHleAttrDesc;

typedef struct {
    u8 cnt, type, frac;
} GxHleAttrFmt;

typedef struct {
    void* base;
    u8 stride;
} GxHleArray;

typedef struct {
    const void* lut;
    u32 fmt;
    u16 n_entries;
    int used;
} GxHleTlut;

typedef struct {
    GxHleArray arrays[32];
    GxHleAttrDesc desc[32];
    u8 desc_order[32];
    int desc_count;
    GxHleAttrFmt fmt[8][32];

    float pos_mtx[30][3][4];
    float nrm_mtx[30][3][4];
    float tex_mtx[20][3][4]; /* 0..9 GX_TEXMTX0..9, 10..17 PTTEXMTX0..7, 18 identity */

    u32 current_mtx;
    float projection[4][4];
    /* Packed XF projection form {A,B,C,D,E,F} retained for GXGetProjectionv
     * (SDK GXTransform.c: GXSetProjection/GXGetProjectionv). */
    float proj_mtx[6];
    float viewport[6];
    int projection_type;

    GxHleDrawState cur;

    GxHleTlut tluts[GX_HLE_MAX_TLUTS];
    GxHleTlut pending_tlut;
    GxHleTexture pending_tex;
    int pending_tlut_name;

    int begun;
} GxHleState;

static GxHleState gx;

static GxHleVertex* frame_verts;
static size_t frame_vcount;
static size_t frame_vcap;
static void flush_direct(void);

static GxHleDraw frame_draws[GX_HLE_MAX_DRAWS];
static unsigned short copy_src_left, copy_src_top, copy_src_w = 640,
                      copy_src_h = 480;
static unsigned short copy_dst_w = 640, copy_dst_h = 480;
static unsigned int copy_dst_fmt = GX_TF_RGBA8;
static size_t frame_dcount;
static GxHleTexture frame_textures[GX_HLE_MAX_TEXTURES];
static size_t frame_tcount;

static int draw_active;
static size_t draw_vertex_start;

/*
 * Direct mode (GXBegin + the GXVert.h inline writers) is captured here in
 * hardware byte order and decoded when the next command or the frame boundary
 * arrives: GXEnd is an empty inline (GXGeometry.h), so there is no call to
 * flush from.
 */
#define GX_HLE_BEGIN_BYTES (64 * 1024)
static u8 begin_buf[GX_HLE_BEGIN_BYTES];
static size_t begin_len;
static u16 begin_nverts;
static u8 begin_op;
static int begin_pending;
static int begin_overflow;

static size_t stat_display_lists;
static size_t stat_primitives;
static size_t stat_skipped;

/* ------------------------------------------------------------ byte order */

static u16 be16(const void* p)
{
    const u8* b = (const u8*) p;
    return (u16) ((b[0] << 8) | b[1]);
}

static u32 be32(const void* p)
{
    const u8* b = (const u8*) p;
    return ((u32) b[0] << 24) | ((u32) b[1] << 16) | ((u32) b[2] << 8) |
           b[3];
}

static float bef32(const void* p)
{
    u32 bits = be32(p);
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

/* --------------------------------------------------------------- decoding */

typedef struct {
    float pos[3];
    float nrm[3];
    float binormal[3];
    float tangent[3];
    float uv[8][2];
    u8 color[4];
    u8 matrix;
    int has_pos, has_nrm, has_nbt, has_color, has_uv[8];
} GxRawVertex;

typedef struct {
    u8 attr;
    u8 desc_type;
    u8 comp_type;
    u8 frac;
    u8 count;
    u8 component_size;
    u8 element_size;
    u8 stride;
    const u8* base;
} GxDecodeAttr;

typedef struct {
    GxDecodeAttr attrs[32];
    int count;
} GxVertexDecoder;

static size_t scalar_size(u32 type)
{
    switch (type) {
    case GX_U8:
    case GX_S8:
        return 1;
    case GX_U16:
    case GX_S16:
        return 2;
    default: /* GX_F32 */
        return 4;
    }
}

/* GX_VA_CLR0/CLR1 store the color enum in the comp_type slot. */
static size_t color_size(u32 type)
{
    switch (type) {
    case GX_RGB565:
    case GX_RGBA4:
        return 2;
    case GX_RGBX8:
    case GX_RGBA8:
        return 4;
    default:
        return 3;
    }
}

static size_t comp_count(u32 attr, u32 cnt)
{
    if (attr == GX_VA_POS) {
        return cnt == GX_POS_XY ? 2 : 3;
    }
    if (attr == GX_VA_NRM) {
        return cnt == GX_NRM_XYZ ? 3 : 9;
    }
    if (attr == GX_VA_NBT) {
        /* GX_VA_NBT is normal/binormal/tangent: always 9 components; the
         * first three are the lighting normal (pobj.c:setupShapeAnimVtxDesc). */
        return 9;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        return cnt == GX_CLR_RGB ? 3 : 4;
    }
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        return cnt == GX_TEX_S ? 1 : 2;
    }
    return 1;
}

static int is_matrix_attr(u32 attr)
{
    return attr <= GX_VA_TEX7MTXIDX;
}

static void init_vertex_decoder(GXVtxFmt vtxfmt, GxVertexDecoder* decoder)
{
    int i;

    decoder->count = 0;
    for (i = 0; i < gx.desc_count; ++i) {
        u32 attr = gx.desc_order[i];
        const GxHleAttrDesc* d = &gx.desc[attr];
        const GxHleAttrFmt* f = &gx.fmt[vtxfmt][attr];
        GxDecodeAttr* a;
        size_t count;
        size_t component_size;

        /* Preserve the command stream's established matrix-index handling:
         * descriptor order determines their byte, while GX_NONE suppresses
         * ordinary attributes. */
        if (!is_matrix_attr(attr) && d->type == GX_NONE) {
            continue;
        }
        a = &decoder->attrs[decoder->count++];
        memset(a, 0, sizeof(*a));
        a->attr = (u8) attr;
        a->desc_type = d->type;
        if (is_matrix_attr(attr)) {
            a->element_size = 1;
            continue;
        }
        count = comp_count(attr, f->cnt);
        component_size = scalar_size(f->type);
        a->comp_type = f->type;
        a->frac = f->frac;
        a->count = (u8) count;
        a->component_size = (u8) component_size;
        a->element_size = (u8)
            ((attr == GX_VA_CLR0 || attr == GX_VA_CLR1)
                 ? color_size(f->type)
                 : count * component_size);
        a->base = (const u8*) gx.arrays[attr].base;
        a->stride = gx.arrays[attr].stride;
    }
}

/* One component at p; integer values are fixed point (value / 2^frac). */
static float read_comp(u32 type, u8 frac, const u8* p)
{
    switch (type) {
    case GX_U8:
        return (float) p[0] / (float) (1u << frac);
    case GX_S8:
        return (float) (s8) p[0] / (float) (1u << frac);
    case GX_U16:
        return (float) be16(p) / (float) (1u << frac);
    case GX_S16:
        return (float) (s16) be16(p) / (float) (1u << frac);
    case GX_F32:
    default:
        return bef32(p);
    }
}

static u8 expand4(unsigned v) { return (u8) ((v << 4) | v); }
static u8 expand5(unsigned v) { return (u8) ((v * 255u) / 31u); }
static u8 expand6(unsigned v) { return (u8) ((v * 255u) / 63u); }

/* Mirrors native/hsd/model.c:decode_color (verified on the assets). */
static void decode_color(u32 type, const u8* p, u8 out[4])
{
    switch (type) {
    case GX_RGB565: {
        u16 v = be16(p);
        out[0] = expand5((v >> 11) & 31);
        out[1] = expand6((v >> 5) & 63);
        out[2] = expand5(v & 31);
        out[3] = 255;
        break;
    }
    case GX_RGB8:
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        out[3] = 255;
        break;
    case GX_RGBX8:
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        out[3] = p[3];
        break;
    case GX_RGBA4: {
        u16 v = be16(p);
        out[0] = expand4((v >> 12) & 15);
        out[1] = expand4((v >> 8) & 15);
        out[2] = expand4((v >> 4) & 15);
        out[3] = expand4(v & 15);
        break;
    }
    case GX_RGBA6:
        out[0] = (u8) ((p[0] >> 2) * 255 / 63);
        out[1] = (u8) ((((p[0] & 3) << 4) | (p[1] >> 4)) * 255 / 63);
        out[2] = (u8) ((((p[1] & 15) << 2) | (p[2] >> 6)) * 255 / 63);
        out[3] = (u8) ((p[2] & 63) * 255 / 63);
        break;
    case GX_RGBA8:
    default:
        memcpy(out, p, 4);
        break;
    }
}

/*
 * Reads one vertex from the display list.  Attributes are consumed in
 * GXSetVtxDesc order; matrix indices are always a single byte, colors use the
 * color enum for their inline size (both verified in native/hsd/model.c).
 */
static int read_vertex(const u8* list, size_t length, size_t* cursor,
                       const GxVertexDecoder* decoder, GxRawVertex* out)
{
    int i;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < decoder->count; ++i) {
        const GxDecodeAttr* plan = &decoder->attrs[i];
        u32 attr = plan->attr;
        const u8* src = NULL;
        size_t a;

        if (is_matrix_attr(attr)) {
            if (*cursor + 1 > length) {
                return 0;
            }
            out->matrix = list[(*cursor)++];
            continue;
        }
        if (plan->desc_type == GX_DIRECT) {
            if (*cursor + plan->element_size > length) {
                return 0;
            }
            src = list + *cursor;
            *cursor += plan->element_size;
            if (attr == GX_VA_POS) {
                for (a = 0; a < plan->count && a < 3; ++a) {
                    out->pos[a] = read_comp(plan->comp_type, plan->frac,
                                            src + a * plan->component_size);
                }
                out->has_pos = 1;
            } else if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
                for (a = 0; a < 3; ++a) {
                    out->nrm[a] = read_comp(plan->comp_type, plan->frac,
                                            src + a * plan->component_size);
                }
                out->has_nrm = 1;
                if (attr == GX_VA_NBT) {
                    for (a = 0; a < 3; ++a) {
                        out->binormal[a] = read_comp(
                            plan->comp_type, plan->frac,
                            src + (3 + a) * plan->component_size);
                        out->tangent[a] = read_comp(
                            plan->comp_type, plan->frac,
                            src + (6 + a) * plan->component_size);
                    }
                    out->has_nbt = 1;
                }
            } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
                int t = attr - GX_VA_TEX0;
                for (a = 0; a < plan->count && a < 2; ++a) {
                    out->uv[t][a] = read_comp(
                        plan->comp_type, plan->frac,
                        src + a * plan->component_size);
                }
                out->has_uv[t] = 1;
            } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
                u8 c[4];
                decode_color(plan->comp_type, src, c);
                if (attr == GX_VA_CLR0) {
                    memcpy(out->color, c, 4);
                    out->has_color = 1;
                }
            }
        } else if (plan->desc_type == GX_INDEX8 ||
                   plan->desc_type == GX_INDEX16) {
            size_t index;
            const u8* elem;
            if (plan->desc_type == GX_INDEX8) {
                if (*cursor + 1 > length) {
                    return 0;
                }
                index = list[(*cursor)++];
            } else {
                if (*cursor + 2 > length) {
                    return 0;
                }
                index = be16(list + *cursor);
                *cursor += 2;
            }
            if (plan->base == NULL || plan->stride == 0) {
                continue;
            }
            elem = plan->base + index * plan->stride;
            if (attr == GX_VA_POS) {
                for (a = 0; a < plan->count && a < 3; ++a) {
                    out->pos[a] = read_comp(plan->comp_type, plan->frac,
                                            elem + a * plan->component_size);
                }
                out->has_pos = 1;
            } else if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
                for (a = 0; a < 3; ++a) {
                    out->nrm[a] = read_comp(plan->comp_type, plan->frac,
                                            elem + a * plan->component_size);
                }
                out->has_nrm = 1;
                if (attr == GX_VA_NBT) {
                    for (a = 0; a < 3; ++a) {
                        out->binormal[a] = read_comp(
                            plan->comp_type, plan->frac,
                            elem + (3 + a) * plan->component_size);
                        out->tangent[a] = read_comp(
                            plan->comp_type, plan->frac,
                            elem + (6 + a) * plan->component_size);
                    }
                    out->has_nbt = 1;
                }
            } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
                int t = attr - GX_VA_TEX0;
                for (a = 0; a < plan->count && a < 2; ++a) {
                    out->uv[t][a] = read_comp(
                        plan->comp_type, plan->frac,
                        elem + a * plan->component_size);
                }
                out->has_uv[t] = 1;
            } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
                u8 c[4];
                decode_color(plan->comp_type, elem, c);
                if (attr == GX_VA_CLR0) {
                    memcpy(out->color, c, 4);
                    out->has_color = 1;
                }
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------ transforms */

static void mtx3x4_mul_vec(const float* m, const float v[3], float out[3])
{
    out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
    out[1] = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7];
    out[2] = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11];
}

static void mtx3x3_mul_vec(const float* m, const float v[3], float out[3])
{
    out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    out[1] = m[4] * v[0] + m[5] * v[1] + m[6] * v[2];
    out[2] = m[8] * v[0] + m[9] * v[1] + m[10] * v[2];
}

static void proj_mul_vec4(const float* m, const float v[4], float out[4])
{
    int i;
    for (i = 0; i < 4; ++i) {
        out[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] +
                 m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3];
    }
}

/* Infinite light positions are 1048576 * direction (lobj.c). */
/* Infinite light positions are 1048576 * direction (lobj.c). */
static int light_is_infinite(const float p[3])
{
    return p[0] * p[0] + p[1] * p[1] + p[2] * p[2] > 1.0e10f;
}

/* Maps a GX texture-matrix id to the local bank; -1 = identity/no matrix. */
static int tex_mtx_slot(u32 id)
{
    if (id >= GX_TEXMTX0 && id <= GX_TEXMTX9) {
        return ((int) id - (int) GX_TEXMTX0) / 3;
    }
    if (id >= GX_PTTEXMTX0 && id <= GX_PTTEXMTX7) {
        return 10 + ((int) id - (int) GX_PTTEXMTX0) / 3;
    }
    return -1;
}

static void texgen_coord(int coord, const GxRawVertex* raw, const float pos[3],
                         const float nrm[3], const float binormal[3],
                         const float tangent[3], float out[2])
{
    const GxHleTexGen* tg = &gx.cur.texgen[coord & 7];
    float in[3];
    const float* m;
    int id;

    if (tg->type >= GX_TG_BUMP0 && tg->type <= GX_TG_BUMP7) {
        /* GX emboss/bump: the source coordinate plus the light direction
         * projected on the vertex's binormal/tangent (Dolphin
         * VertexShaderGen.cpp, TexGenType::EmbossMap). */
        int light = (int) tg->type - (int) GX_TG_BUMP0;
        int src_coord = (int) tg->src - (int) GX_TG_TEXCOORD0;
        const GxHleLight* l = &gx.cur.lights[light & 7];
        float ldir[3];
        float base[2];
        float len;
        if (src_coord < 0 || src_coord > 7) {
            src_coord = 0;
        }
        texgen_coord(src_coord, raw, pos, nrm, binormal, tangent, base);
        if (light_is_infinite(l->pos)) {
            len = sqrtf(l->pos[0] * l->pos[0] + l->pos[1] * l->pos[1] +
                        l->pos[2] * l->pos[2]);
            if (len > 0.0f) {
                ldir[0] = l->pos[0] / len;
                ldir[1] = l->pos[1] / len;
                ldir[2] = l->pos[2] / len;
            } else {
                ldir[0] = ldir[1] = 0.0f;
                ldir[2] = 1.0f;
            }
        } else {
            float d[3];
            d[0] = l->pos[0] - pos[0];
            d[1] = l->pos[1] - pos[1];
            d[2] = l->pos[2] - pos[2];
            len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 0.0f) {
                ldir[0] = d[0] / len;
                ldir[1] = d[1] / len;
                ldir[2] = d[2] / len;
            } else {
                ldir[0] = ldir[1] = 0.0f;
                ldir[2] = 1.0f;
            }
        }
        out[0] = base[0] + ldir[0] * tangent[0] + ldir[1] * tangent[1] +
                 ldir[2] * tangent[2];
        out[1] = base[1] + ldir[0] * binormal[0] + ldir[1] * binormal[1] +
                 ldir[2] * binormal[2];
        return;
    }

    switch (tg->src) {
    case GX_TG_POS:
        in[0] = pos[0];
        in[1] = pos[1];
        in[2] = pos[2];
        break;
    case GX_TG_NRM:
        in[0] = nrm[0];
        in[1] = nrm[1];
        in[2] = nrm[2];
        break;
    case GX_TG_COLOR0:
        in[0] = raw->color[0] / 255.0f;
        in[1] = raw->color[1] / 255.0f;
        in[2] = raw->color[2] / 255.0f;
        break;
    case GX_TG_TEX0:
    case GX_TG_TEX1:
    case GX_TG_TEX2:
    case GX_TG_TEX3:
    case GX_TG_TEX4:
    case GX_TG_TEX5:
    case GX_TG_TEX6:
    case GX_TG_TEX7: {
        int t = tg->src - GX_TG_TEX0;
        in[0] = raw->uv[t][0];
        in[1] = raw->uv[t][1];
        in[2] = 0.0f;
        break;
    }
    default:
        in[0] = raw->uv[0][0];
        in[1] = raw->uv[0][1];
        in[2] = 0.0f;
        break;
    }

    if (tg->type == GX_TG_SRTG) {
        /* Toon ramp: the S/T coordinate is the rasterized (lit) colour.
         * The GL fragment shader substitutes v_ras0 for this coordinate;
         * keep the per-vertex value here as the fallback. */
        out[0] = in[0];
        out[1] = in[1];
        return;
    }
    /* GX texcoord generation = postmtx * mtx * source.  HSD's default path
     * passes GX_IDENTITY for `mtx` and puts the TObj matrix in `pt_texmtx`
     * (tobj.c:setupTextureCoordGen).  GX_TG_MTX3x4 keeps the third row: the
     * hardware produces STQ and divides by q per pixel (Aurora
     * shader.cpp: tex_uvw, `uv = uvw.xy / uvw.z`), with the q==0 special
     * case (Dolphin VertexShaderGen: clamp(xy/2, -1, 1)). */
    {
        float v[3];
        v[0] = in[0];
        v[1] = in[1];
        v[2] = in[2];
        id = tex_mtx_slot(tg->mtx_id);
        if (id >= 0) {
            m = &gx.tex_mtx[id][0][0];
            {
                float t0 = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
                float t1 = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7];
                float t2 = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11];
                v[0] = t0;
                v[1] = t1;
                v[2] = t2;
            }
        }
        /* GX_TG_MTX2x4 forces z=1 before the post matrix (Aurora
         * shader.cpp: `if (tcg.type == GX_TG_MTX2x4) tc_tmp.z = 1.0f;`). */
        if (tg->type == GX_TG_MTX2x4) {
            v[2] = 1.0f;
        }
        if (tg->normalize) {
            float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (len > 1e-8f) {
                v[0] /= len;
                v[1] /= len;
                v[2] /= len;
            }
        }
        id = tex_mtx_slot(tg->postmtx);
        if (id >= 0) {
            m = &gx.tex_mtx[id][0][0];
            {
                float t0 = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
                float t1 = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7];
                float t2 = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11];
                v[0] = t0;
                v[1] = t1;
                v[2] = t2;
            }
        }
        if (tg->type == GX_TG_MTX3x4) {
            if (v[2] == 0.0f) {
                v[0] = v[0] / 2.0f;
                v[1] = v[1] / 2.0f;
                if (v[0] < -1.0f) v[0] = -1.0f;
                if (v[0] > 1.0f) v[0] = 1.0f;
                if (v[1] < -1.0f) v[1] = -1.0f;
                if (v[1] > 1.0f) v[1] = 1.0f;
            } else {
                v[0] /= v[2];
                v[1] /= v[2];
            }
        }
        out[0] = v[0];
        out[1] = v[1];
    }
}

static void transform_vertex(const GxRawVertex* raw, GxHleVertex* v)
{
    float pos[3];
    float nrm[3];
    float binormal[3] = { 0.0f, 0.0f, 0.0f };
    float tangent[3] = { 0.0f, 0.0f, 0.0f };
    float v4[4];
    float clip[4];
    int idx = raw->matrix + (int) gx.current_mtx;
    int texgen_count = gx.cur.num_texgens;
    int need_nbt = 0;
    int i;

    if (idx < 0 || idx > 29) {
        idx = 0;
    }
    /* Direct-mode callers may configure generators without emitting
     * GXSetNumTexGens (displayfunc.c's small utility draws); retain the
     * established three-coordinate fallback for that command stream. */
    if (texgen_count <= 0 || texgen_count > 3) {
        texgen_count = 3;
    }
    for (i = 0; i < texgen_count; i++) {
        int type = gx.cur.texgen[i].type;
        if (type >= GX_TG_BUMP0 && type <= GX_TG_BUMP7) {
            need_nbt = raw->has_nbt;
            break;
        }
    }
    mtx3x4_mul_vec(&gx.pos_mtx[idx][0][0], raw->pos, pos);
    mtx3x3_mul_vec(&gx.nrm_mtx[idx][0][0], raw->nrm, nrm);
    if (need_nbt) {
        mtx3x3_mul_vec(&gx.nrm_mtx[idx][0][0], raw->binormal, binormal);
        mtx3x3_mul_vec(&gx.nrm_mtx[idx][0][0], raw->tangent, tangent);
    }
    {
        float len = sqrtf(nrm[0] * nrm[0] + nrm[1] * nrm[1] +
                          nrm[2] * nrm[2]);
        if (len > 1e-8f) {
            nrm[0] /= len;
            nrm[1] /= len;
            nrm[2] /= len;
        }
        if (need_nbt) {
            len = sqrtf(binormal[0] * binormal[0] +
                        binormal[1] * binormal[1] +
                        binormal[2] * binormal[2]);
            if (len > 1e-8f) {
                binormal[0] /= len;
                binormal[1] /= len;
                binormal[2] /= len;
            }
            len = sqrtf(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                        tangent[2] * tangent[2]);
            if (len > 1e-8f) {
                tangent[0] /= len;
                tangent[1] /= len;
                tangent[2] /= len;
            }
        }
    }
    v4[0] = pos[0];
    v4[1] = pos[1];
    v4[2] = pos[2];
    v4[3] = 1.0f;
    proj_mul_vec4(&gx.projection[0][0], v4, clip);

    v->clip[0] = clip[0];
    v->clip[1] = clip[1]; /* SDK projection and GL NDC share +y up */
    v->clip[2] = clip[2];
    v->clip[3] = clip[3];
    v->view[0] = pos[0];
    v->view[1] = pos[1];
    v->view[2] = pos[2];
    memcpy(v->color, raw->color, 4);
    memset(v->uv, 0, sizeof(v->uv));
    for (i = 0; i < texgen_count; i++) {
        texgen_coord(i, raw, pos, nrm, binormal, tangent, v->uv[i]);
    }
    v->nrm[0] = nrm[0];
    v->nrm[1] = nrm[1];
    v->nrm[2] = nrm[2];
    v->has_color = raw->has_color ? 1.0f : 0.0f;
}

static size_t stat_degenerate;

static void submit_triangle(const GxHleVertex* a, const GxHleVertex* b,
                            const GxHleVertex* c)
{
    if (!draw_active || frame_vcount + 3 > frame_vcap ||
        frame_dcount >= GX_HLE_MAX_DRAWS) {
        stat_skipped++;
        return;
    }
    {
        float ux = b->clip[0] * a->clip[3] - a->clip[0] * b->clip[3];
        float uy = b->clip[1] * a->clip[3] - a->clip[1] * b->clip[3];
        float vx = c->clip[0] * a->clip[3] - a->clip[0] * c->clip[3];
        float vy = c->clip[1] * a->clip[3] - a->clip[1] * c->clip[3];
        float area = ux * vy - uy * vx;
        if (area < 1e-9f && area > -1e-9f) {
            stat_degenerate++;
        }
    }
    frame_verts[frame_vcount++] = *a;
    frame_verts[frame_vcount++] = *b;
    frame_verts[frame_vcount++] = *c;
    frame_draws[frame_dcount].vertex_count += 3;
}

static void exec_primitive(u8 op, const u8* list, size_t length,
                           size_t* cursor, u16 nverts)
{
    GXVtxFmt vtxfmt = (GXVtxFmt) (op & 7);
    GxVertexDecoder decoder;
    u8 prim = op & 0xF8;
    GxHleVertex win[4];
    GxHleVertex fan_first;
    unsigned i;

    init_vertex_decoder(vtxfmt, &decoder);
    for (i = 0; i < nverts; ++i) {
        GxRawVertex raw;
        GxHleVertex* v;
        if (!read_vertex(list, length, cursor, &decoder, &raw)) {
            stat_skipped++;
            return;
        }
        v = &win[i & 3];
        transform_vertex(&raw, v);
        if (i == 0) {
            fan_first = *v;
        }
        switch (prim) {
        case 0x80: /* GX_QUADS: (0,1,2),(0,2,3) per group of four */
            if ((i % 4) == 3) {
                GxHleVertex q0 = win[(i - 3) & 3];
                GxHleVertex q1 = win[(i - 2) & 3];
                GxHleVertex q2 = win[(i - 1) & 3];
                GxHleVertex q3 = win[i & 3];
                submit_triangle(&q0, &q1, &q2);
                submit_triangle(&q0, &q2, &q3);
            }
            break;
        case 0x90: /* GX_TRIANGLES: sequential triples */
            if ((i % 3) == 2) {
                GxHleVertex q0 = win[(i - 2) & 3];
                GxHleVertex q1 = win[(i - 1) & 3];
                GxHleVertex q2 = win[i & 3];
                submit_triangle(&q0, &q1, &q2);
            }
            break;
        case 0x98: /* GX_TRIANGLESTRIP: alternating winding */
            if (i >= 2) {
                unsigned ai = (i & 1) ? (i - 1) & 3 : (i - 2) & 3;
                unsigned bi = (i & 1) ? (i - 2) & 3 : (i - 1) & 3;
                GxHleVertex a = win[ai];
                GxHleVertex b = win[bi];
                GxHleVertex c = win[i & 3];
                submit_triangle(&a, &b, &c);
            }
            break;
        case 0xA0: /* GX_TRIANGLEFAN: (first, i-1, i) */
            if (i >= 2) {
                GxHleVertex a = fan_first;
                GxHleVertex b = win[(i - 1) & 3];
                GxHleVertex c = win[i & 3];
                submit_triangle(&a, &b, &c);
            }
            break;
        default:
            break;
        }
    }
}

/* --------------------------------------------------------------- GX API */

GXFifoObj* GXInit(void* base, u32 size)
{
    static u32 fifo[64];
    (void) base;
    (void) size;
    return (GXFifoObj*) fifo;
}

/* extern/dolphin/src/dolphin/gx/GXTransform.c: GXGetProjectionv packs the XF
 * projection registers as {projType, A, B, C, D, E, F}.  Since the game's
 * GXProject/HUD code reads the six coefficients (psdisp.c:1936 particle
 * billboards, fog.c:56 fog-range adjustment), the diagonal-only return was
 * silently corrupting billboard axes in matches. */
void GXGetProjectionv(f32* ptr)
{
    int i;
    ptr[0] = (f32) gx.projection_type;
    for (i = 0; i < 6; ++i) {
        ptr[i + 1] = gx.proj_mtx[i];
    }
}

/* extern/dolphin/src/dolphin/gx/GXTransform.c: GXProject. */
void GXProject(f32 x, f32 y, f32 z, f32 mtx[3][4], f32* pm, f32* vp, f32* sx,
               f32* sy, f32* sz)
{
    f32 peye_x;
    f32 peye_y;
    f32 peye_z;
    f32 xc;
    f32 yc;
    f32 zc;
    f32 wc;

    peye_x = mtx[0][3] + ((mtx[0][2] * z) + ((mtx[0][0] * x) + (mtx[0][1] * y)));
    peye_y = mtx[1][3] + ((mtx[1][2] * z) + ((mtx[1][0] * x) + (mtx[1][1] * y)));
    peye_z = mtx[2][3] + ((mtx[2][2] * z) + ((mtx[2][0] * x) + (mtx[2][1] * y)));
    if (pm[0] == 0.0f) {
        xc = (peye_x * pm[1]) + (peye_z * pm[2]);
        yc = (peye_y * pm[3]) + (peye_z * pm[4]);
        zc = pm[6] + (peye_z * pm[5]);
        wc = 1.0f / -peye_z;
    } else {
        xc = pm[2] + (peye_x * pm[1]);
        yc = pm[4] + (peye_y * pm[3]);
        zc = pm[6] + (peye_z * pm[5]);
        wc = 1.0f;
    }
    *sx = (vp[2] / 2.0f) + (vp[0] + (wc * (xc * vp[2] / 2.0f)));
    *sy = (vp[3] / 2.0f) + (vp[1] + (wc * (-yc * vp[3] / 2.0f)));
    *sz = vp[5] + (wc * (zc * (vp[5] - vp[4])));
}

void GXGetViewportv(f32* vp)
{
    memcpy(vp, gx.viewport, sizeof(gx.viewport));
}

void GXSetProjection(f32 mtx[4][4], GXProjectionType type)
{
    flush_direct();
    memcpy(gx.projection, mtx, sizeof(gx.projection));
    gx.projection_type = (int) type;
    /* extern/dolphin/src/dolphin/gx/GXTransform.c: GXSetProjection packs the
     * XF registers; ortho keeps the translation in column 3, perspective in
     * column 2 (both plus [2][3] = F). */
    gx.proj_mtx[0] = mtx[0][0];
    gx.proj_mtx[2] = mtx[1][1];
    gx.proj_mtx[4] = mtx[2][2];
    gx.proj_mtx[5] = mtx[2][3];
    if (type == GX_ORTHOGRAPHIC) {
        gx.proj_mtx[1] = mtx[0][3];
        gx.proj_mtx[3] = mtx[1][3];
    } else {
        gx.proj_mtx[1] = mtx[0][2];
        gx.proj_mtx[3] = mtx[1][2];
    }
}

/* Companion to GXSetProjection above; HSD's fog range adjustment builds a
 * 4x4 back from the packed form exactly this way (fog.c:56-83). */
void GXSetProjectionv(f32* ptr)
{
    flush_direct();
    gx.projection_type = (int) ptr[0];
    memcpy(gx.proj_mtx, ptr + 1, sizeof(gx.proj_mtx));
    memset(gx.projection, 0, sizeof(gx.projection));
    gx.projection[0][0] = ptr[1];
    gx.projection[1][1] = ptr[3];
    gx.projection[2][2] = ptr[5];
    gx.projection[2][3] = ptr[6];
    if (gx.projection_type == GX_ORTHOGRAPHIC) {
        gx.projection[0][3] = ptr[2];
        gx.projection[1][3] = ptr[4];
        gx.projection[3][3] = 1.0f;
    } else {
        gx.projection[0][2] = ptr[2];
        gx.projection[1][2] = ptr[4];
        gx.projection[3][2] = -1.0f;
    }
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz)
{
    flush_direct();
    gx.viewport[0] = left;
    gx.viewport[1] = top;
    gx.viewport[2] = wd;
    gx.viewport[3] = ht;
    gx.viewport[4] = nearz;
    gx.viewport[5] = farz;
    gx.cur.viewport[0] = left;
    gx.cur.viewport[1] = top;
    gx.cur.viewport[2] = wd;
    gx.cur.viewport[3] = ht;
    gx.cur.depth_range[0] = nearz;
    gx.cur.depth_range[1] = farz;
}

void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz,
                         f32 farz, u32 field)
{
    (void) field;
    GXSetViewport(left, top, wd, ht, nearz, farz);
}

void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht)
{
    gx.cur.scissor_x = (unsigned short) left;
    gx.cur.scissor_y = (unsigned short) top;
    gx.cur.scissor_w = (unsigned short) wd;
    gx.cur.scissor_h = (unsigned short) ht;
}

void GXSetCullMode(GXCullMode mode)
{
    flush_direct();
    gx.cur.cull_mode = (u8) mode;
}

void GXSetCoPlanar(GXBool enable) { (void) enable; }

void GXSetColorUpdate(GXBool enable)
{
    flush_direct();
    gx.cur.color_update = (u8) enable;
}

void GXSetAlphaUpdate(GXBool enable)
{
    flush_direct();
    gx.cur.alpha_update = (u8) enable;
}

void GXSetDstAlpha(GXBool enable, u8 alpha)
{
    flush_direct();
    gx.cur.dst_alpha_enable = (u8) enable;
    gx.cur.dst_alpha = alpha;
}

void GXSetZMode(GXBool compare_enable, GXCompare func, GXBool update_enable)
{
    flush_direct();
    gx.cur.z_enable = (u8) compare_enable;
    gx.cur.z_func = (u8) func;
    gx.cur.z_update = (u8) update_enable;
}

void GXSetZCompLoc(GXBool before_tex)
{
    flush_direct();
    gx.cur.z_comp_loc = (u8) before_tex;
}

void GXSetBlendMode(GXBlendMode type, GXBlendFactor src_factor,
                    GXBlendFactor dst_factor, GXLogicOp op)
{
    flush_direct();
    gx.cur.blend_type = (u8) type;
    gx.cur.blend_src = (u8) src_factor;
    gx.cur.blend_dst = (u8) dst_factor;
    gx.cur.logic_op = (u8) op;
}

void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op,
                       GXCompare comp1, u8 ref1)
{
    flush_direct();
    gx.cur.alpha_comp0 = (u8) comp0;
    gx.cur.alpha_ref0 = ref0;
    gx.cur.alpha_op = (u8) op;
    gx.cur.alpha_comp1 = (u8) comp1;
    gx.cur.alpha_ref1 = ref1;
}

void GXSetDither(GXBool dither)
{
    gx.cur.dither = (u8) dither;
}

void GXSetFog(GXFogType type, f32 startz, f32 endz, f32 nearz, f32 farz,
              GXColor color)
{
    flush_direct();
    (void) nearz;
    (void) farz;
    gx.cur.fog_enable = type != GX_FOG_NONE;
    gx.cur.fog_type = (u8) type;
    gx.cur.fog_start = startz;
    gx.cur.fog_end = endz;
    gx.cur.fog_color[0] = color.r / 255.0f;
    gx.cur.fog_color[1] = color.g / 255.0f;
    gx.cur.fog_color[2] = color.b / 255.0f;
}

void GXSetFogRangeAdj(GXBool enable, u16 center, GXFogAdjTable* table)
{
    (void) enable;
    (void) center;
    (void) table;
}

void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, f32 projmtx[4][4])
{
    (void) table;
    (void) width;
    (void) projmtx;
}

void GXSetNumChans(u8 nChans)
{
    flush_direct();
    gx.cur.num_chans = nChans;
}

/* GX channel id -> state slot: COLOR0/COLOR1 are the TEV raster sources,
 * ALPHA0/ALPHA1 carry only the alpha component.  The combined A0/A1 ids
 * address the colour slot; the matching alpha slot is updated from the same
 * colour value because GX_COLOR0A0/GX_COLOR1A1 write both registers. */
static int channel_slot(GXChannelID chan)
{
    switch (chan) {
    case GX_COLOR0:
    case GX_COLOR0A0:
        return 0;
    case GX_COLOR1:
    case GX_COLOR1A1:
        return 1;
    case GX_ALPHA0:
        return 2;
    case GX_ALPHA1:
        return 3;
    default:
        return -1;
    }
}

void GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src,
                   GXColorSrc mat_src, u32 light_mask, GXDiffuseFn diff_fn,
                   GXAttnFn attn_fn)
{
    int ch;
    flush_direct();
    ch = channel_slot(chan);
    if (ch < 0) {
        return;
    }
    gx.cur.ch_enable[ch] = (u8) enable;
    gx.cur.ch_amb_src[ch] = (u8) amb_src;
    gx.cur.ch_mat_src[ch] = (u8) mat_src;
    gx.cur.ch_light_mask[ch] = light_mask;
    gx.cur.ch_diff_fn[ch] = (u8) diff_fn;
    gx.cur.ch_attn_fn[ch] = (u8) attn_fn;
    /* The combined COLOR0A0/COLOR1A1 control addresses the colour channel and
     * its paired alpha channel at once (GXSetChanMatColor already mirrors the
     * alpha component the same way).  A separate ALPHA0/ALPHA1 write, when the
     * material uses one, overwrites the alpha slot afterwards. */
    if (chan == GX_COLOR0A0 || chan == GX_COLOR1A1) {
        int a = ch + 2;
        gx.cur.ch_enable[a] = (u8) enable;
        gx.cur.ch_amb_src[a] = (u8) amb_src;
        gx.cur.ch_mat_src[a] = (u8) mat_src;
        gx.cur.ch_light_mask[a] = light_mask;
        gx.cur.ch_diff_fn[a] = (u8) diff_fn;
        gx.cur.ch_attn_fn[a] = (u8) attn_fn;
    }
}

void GXSetChanAmbColor(GXChannelID chan, GXColor amb_color)
{
    int ch;
    flush_direct();
    ch = channel_slot(chan);
    if (ch < 0) {
        return;
    }
    if (ch >= 2) {
        gx.cur.ch_amb[ch][3] = amb_color.a / 255.0f;
        return;
    }
    gx.cur.ch_amb[ch][0] = amb_color.r / 255.0f;
    gx.cur.ch_amb[ch][1] = amb_color.g / 255.0f;
    gx.cur.ch_amb[ch][2] = amb_color.b / 255.0f;
    gx.cur.ch_amb[ch][3] = amb_color.a / 255.0f;
    if (chan == GX_COLOR0A0) {
        gx.cur.ch_amb[2][3] = amb_color.a / 255.0f;
    } else if (chan == GX_COLOR1A1) {
        gx.cur.ch_amb[3][3] = amb_color.a / 255.0f;
    }
}

void GXSetChanMatColor(GXChannelID chan, GXColor mat_color)
{
    int ch;
    flush_direct();
    ch = channel_slot(chan);
    if (ch < 0) {
        return;
    }
    if (ch >= 2) {
        gx.cur.ch_mat[ch][3] = mat_color.a / 255.0f;
        return;
    }
    gx.cur.ch_mat[ch][0] = mat_color.r / 255.0f;
    gx.cur.ch_mat[ch][1] = mat_color.g / 255.0f;
    gx.cur.ch_mat[ch][2] = mat_color.b / 255.0f;
    gx.cur.ch_mat[ch][3] = mat_color.a / 255.0f;
    if (chan == GX_COLOR0A0) {
        gx.cur.ch_mat[2][3] = mat_color.a / 255.0f;
    } else if (chan == GX_COLOR1A1) {
        gx.cur.ch_mat[3][3] = mat_color.a / 255.0f;
    }
}

void GXSetNumTevStages(u8 nStages)
{
    flush_direct();
    gx.cur.num_stages = nStages;
}

void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map,
                   GXChannelID color)
{
    flush_direct();
    GxHleTevStage* s;
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    s = &gx.cur.stages[stage];
    s->order_coord = (u8) coord;
    s->order_map = (u8) map;
    s->order_chan = (u8) color;
}

void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b,
                     GXTevColorArg c, GXTevColorArg d)
{
    flush_direct();
    GxHleTevStage* s;
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    s = &gx.cur.stages[stage];
    s->color_a = (u8) a;
    s->color_b = (u8) b;
    s->color_c = (u8) c;
    s->color_d = (u8) d;
}

void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b,
                     GXTevAlphaArg c, GXTevAlphaArg d)
{
    flush_direct();
    GxHleTevStage* s;
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    s = &gx.cur.stages[stage];
    s->alpha_a = (u8) a;
    s->alpha_b = (u8) b;
    s->alpha_c = (u8) c;
    s->alpha_d = (u8) d;
}

void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                     GXTevScale scale, GXBool clamp, GXTevRegID out_reg)
{
    flush_direct();
    GxHleTevStage* s;
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    s = &gx.cur.stages[stage];
    s->color_op = (u8) op;
    s->color_bias = (u8) bias;
    s->color_scale = (u8) scale;
    s->color_clamp = (u8) clamp;
    s->color_reg = (u8) out_reg;
}

void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                     GXTevScale scale, GXBool clamp, GXTevRegID out_reg)
{
    flush_direct();
    GxHleTevStage* s;
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    s = &gx.cur.stages[stage];
    s->alpha_op = (u8) op;
    s->alpha_bias = (u8) bias;
    s->alpha_scale = (u8) scale;
    s->alpha_clamp = (u8) clamp;
    s->alpha_reg = (u8) out_reg;
}

void GXSetTevColor(GXTevRegID id, GXColor color)
{
    int idx = (int) id;
    if (idx < 0 || idx >= 4) {
        return;
    }
    gx.cur.tev_color[idx][0] = color.r / 255.0f;
    gx.cur.tev_color[idx][1] = color.g / 255.0f;
    gx.cur.tev_color[idx][2] = color.b / 255.0f;
    gx.cur.tev_color[idx][3] = color.a / 255.0f;
}

void GXSetTevColorS10(GXTevRegID id, GXColorS10 color)
{
    int idx = (int) id;
    if (idx < 0 || idx >= 4) {
        return;
    }
    gx.cur.tev_color[idx][0] = color.r / 255.0f;
    gx.cur.tev_color[idx][1] = color.g / 255.0f;
    gx.cur.tev_color[idx][2] = color.b / 255.0f;
    gx.cur.tev_color[idx][3] = color.a / 255.0f;
}

void GXSetTevKColor(GXTevKColorID id, GXColor color)
{
    flush_direct();
    int idx = (int) id;
    if (idx < 0 || idx >= 4) {
        return;
    }
    gx.cur.tev_kcolor[idx][0] = color.r / 255.0f;
    gx.cur.tev_kcolor[idx][1] = color.g / 255.0f;
    gx.cur.tev_kcolor[idx][2] = color.b / 255.0f;
    gx.cur.tev_kcolor[idx][3] = color.a / 255.0f;
}

void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel)
{
    flush_direct();
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    gx.cur.stages[stage].kc_sel = (u8) sel;
}

void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel)
{
    flush_direct();
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    gx.cur.stages[stage].ka_sel = (u8) sel;
}

void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel ras_sel,
                      GXTevSwapSel tex_sel)
{
    flush_direct();
    if ((int) stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    gx.cur.stages[stage].ras_sel = (u8) ras_sel;
    gx.cur.stages[stage].tex_sel = (u8) tex_sel;
}

void GXSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red,
                           GXTevColorChan green, GXTevColorChan blue,
                           GXTevColorChan alpha)
{
    flush_direct();
    if ((int) table < 0 || (int) table >= 4) {
        return;
    }
    gx.cur.swap_table[table][0] = (u8) red;
    gx.cur.swap_table[table][1] = (u8) green;
    gx.cur.swap_table[table][2] = (u8) blue;
    gx.cur.swap_table[table][3] = (u8) alpha;
}

void GXSetTevOp(GXTevStageID id, GXTevMode mode)
{
    flush_direct();
    GXTevColorArg carg = id == GX_TEVSTAGE0 ? GX_CC_RASC : GX_CC_CPREV;
    GXTevAlphaArg aarg = id == GX_TEVSTAGE0 ? GX_CA_RASA : GX_CA_APREV;

    switch (mode) {
    case GX_MODULATE:
        GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_TEXC, carg, GX_CC_ZERO);
        GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_TEXA, aarg, GX_CA_ZERO);
        break;
    case GX_DECAL:
        GXSetTevColorIn(id, carg, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
        GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, aarg);
        break;
    case GX_BLEND:
        GXSetTevColorIn(id, carg, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO);
        GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_TEXA, aarg, GX_CA_ZERO);
        break;
    case GX_REPLACE:
        GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
        GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
        break;
    case GX_PASSCLR:
    default:
        GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, carg);
        GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, aarg);
        break;
    }
    GXSetTevColorOp(id, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                    GX_TEVPREV);
    GXSetTevAlphaOp(id, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                    GX_TEVPREV);
}

void GXSetTevClampMode(int stage, int mode)
{
    (void) stage;
    (void) mode;
}

/* SDK GXBump.c: GXSetTevDirect is GXSetTevIndirect with every offset source
 * disabled.  lbrefract.c calls it to tear down its refraction stage, so the
 * fields must be reset, not ignored: a stale matrix/wrap/add_prev would keep
 * offsetting later draws that reuse the stage. */
void GXSetTevDirect(GXTevStageID tev_stage)
{
    GXSetTevIndirect(tev_stage, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_NONE,
                     GX_ITM_OFF, GX_ITW_OFF, GX_ITW_OFF, GX_FALSE, GX_FALSE,
                     GX_ITBA_OFF);
}

void GXSetNumTexGens(u8 nTexGens)
{
    flush_direct();
    gx.cur.num_texgens = nTexGens;
}

void GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func,
                       GXTexGenSrc src_param, u32 mtx, GXBool normalize,
                       u32 pt_texmtx)
{
    flush_direct();
    GxHleTexGen* tg;
    if ((int) dst_coord < 0 || (int) dst_coord > 7) {
        return;
    }
    tg = &gx.cur.texgen[dst_coord];
    tg->type = (u8) func;
    tg->src = (u8) src_param;
    tg->mtx_id = (u8) mtx;
    tg->normalize = (u8) normalize;
    tg->postmtx = (u8) (pt_texmtx == 0 ? GX_PTIDENTITY : pt_texmtx);
}

void GXLoadPosMtxImm(f32 mtx[3][4], u32 id)
{
    flush_direct();
    int idx = (int) id;
    if (idx < 0 || idx + 2 >= 30) {
        idx = 0;
    }
    memcpy(gx.pos_mtx[idx], mtx, sizeof(float) * 12);
}

void GXLoadPosMtxIndx(u16 index, u32 id)
{
    (void) index;
    (void) id;
}

void GXLoadNrmMtxImm(f32 mtx[3][4], u32 id)
{
    flush_direct();
    int idx = (int) id;
    if (idx < 0 || idx + 2 >= 30) {
        idx = 0;
    }
    memcpy(gx.nrm_mtx[idx], mtx, sizeof(float) * 12);
}

void GXLoadNrmMtxImm3x3(f32 mtx[3][3], u32 id)
{
    float m[3][4] = { { 0 } };
    m[0][0] = mtx[0][0];
    m[0][1] = mtx[0][1];
    m[0][2] = mtx[0][2];
    m[1][0] = mtx[1][0];
    m[1][1] = mtx[1][1];
    m[1][2] = mtx[1][2];
    m[2][0] = mtx[2][0];
    m[2][1] = mtx[2][1];
    m[2][2] = mtx[2][2];
    GXLoadNrmMtxImm(m, id);
}

void GXLoadTexMtxImm(f32 mtx[][4], u32 id, GXTexMtxType type)
{
    flush_direct();
    int idx = tex_mtx_slot(id);
    (void) type;
    if (idx < 0) {
        return;
    }
    memcpy(gx.tex_mtx[idx], mtx, sizeof(float) * 12);
}

void GXLoadTexMtxIndx(u16 index, u32 id, GXTexMtxType type)
{
    (void) index;
    (void) id;
    (void) type;
}

void GXSetCurrentMtx(u32 id)
{
    flush_direct();
    gx.current_mtx = id;
}

void GXClearVtxDesc(void)
{
    flush_direct();
    int i;
    gx.desc_count = 0;
    for (i = 0; i < 32; ++i) {
        gx.desc[i].type = GX_NONE;
    }
}

void GXSetVtxDesc(GXAttr attr, GXAttrType type)
{
    flush_direct();
    if ((int) attr >= 32) {
        return;
    }
    if (gx.desc[attr].type == GX_NONE && type != GX_NONE) {
        if (gx.desc_count < 32) {
            gx.desc_order[gx.desc_count++] = (u8) attr;
        }
    }
    gx.desc[attr].type = (u8) type;
}

void GXSetVtxDescv(const GXVtxDescList* list)
{
    while (list != NULL && list->attr != GX_VA_NULL) {
        GXSetVtxDesc(list->attr, list->type);
        list++;
    }
}

void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt,
                     GXCompType type, u8 frac)
{
    flush_direct();
    if ((int) vtxfmt < 0 || (int) vtxfmt >= 8 || (int) attr < 0 ||
        (int) attr >= 32) {
        return;
    }
    gx.fmt[vtxfmt][attr].cnt = (u8) cnt;
    gx.fmt[vtxfmt][attr].type = (u8) type;
    gx.fmt[vtxfmt][attr].frac = frac;
}

void GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list)
{
    while (list != NULL && list->attr != GX_VA_NULL) {
        GXSetVtxAttrFmt(vtxfmt, list->attr, list->cnt, list->type,
                        list->frac);
        list++;
    }
}

void GXSetArray(GXAttr attr, const void* base_ptr, u8 stride)
{
    flush_direct();
    if ((int) attr < 0 || (int) attr >= 32) {
        return;
    }
    gx.arrays[attr].base = (void*) base_ptr;
    gx.arrays[attr].stride = stride;
}

/*
 * A stage may name a texcoord that was generated from an earlier texcoord
 * through identity matrices (GX_TG_TEXCOORDn, e.g. Giga Koopa's add/sub
 * bump pairs).  The fragment stage only carries two UV varyings, so fold
 * such chains back onto the coord they alias.
 */
static void resolve_stage_coords(GxHleDrawState* s)
{
    int i;
    for (i = 0; i < s->num_stages && i < GX_HLE_MAX_STAGES; ++i) {
        int coord = s->stages[i].order_coord;
        int guard = 0;
        while (coord >= 2 && guard++ < 8) {
            const GxHleTexGen* tg = &s->texgen[coord & 7];
            if (tg->type >= GX_TG_BUMP0 ||
                tg->src < GX_TG_TEXCOORD0 || tg->src >= GX_TG_COLOR0 ||
                tg->mtx_id != GX_IDENTITY || tg->postmtx != GX_PTIDENTITY) {
                break;
            }
            coord = (int) tg->src - (int) GX_TG_TEXCOORD0;
        }
        s->stages[i].order_coord = (u8) coord;
    }
}

/* Start a draw snapshot unless one is already open. */
static void begin_draw_snapshot(void)
{
    if (!draw_active && frame_dcount < GX_HLE_MAX_DRAWS) {
        draw_active = 1;
        draw_vertex_start = frame_vcount;
        /* A recycled slot must not keep the previous frame's copy command
         * (kind/copy_*), or a primitive lands in the EFB-copy path. */
        memset(&frame_draws[frame_dcount], 0,
               sizeof(frame_draws[frame_dcount]));
        frame_draws[frame_dcount].first_vertex = frame_vcount;
        frame_draws[frame_dcount].vertex_count = 0;
        frame_draws[frame_dcount].state = gx.cur;
        resolve_stage_coords(&frame_draws[frame_dcount].state);
    }
}

static void end_draw_snapshot(void)
{
    draw_active = 0;
    if (frame_dcount < GX_HLE_MAX_DRAWS &&
        frame_draws[frame_dcount].vertex_count > 0) {
        frame_dcount++;
    }
}

static void direct_append(const u8* bytes, size_t n)
{
    if (!begin_pending) {
        return;
    }
    if (begin_len + n > sizeof(begin_buf)) {
        begin_overflow = 1;
        return;
    }
    memcpy(begin_buf + begin_len, bytes, n);
    begin_len += n;
}

/* GXVert.h shim entry points; values must land in FIFO (big-endian) order. */
void GXPortWGFifoU8(u8 x) { direct_append(&x, 1); }

void GXPortWGFifoU16(u16 x)
{
    u8 b[2];
    b[0] = (u8) (x >> 8);
    b[1] = (u8) x;
    direct_append(b, 2);
}

void GXPortWGFifoU32(u32 x)
{
    u8 b[4];
    b[0] = (u8) (x >> 24);
    b[1] = (u8) (x >> 16);
    b[2] = (u8) (x >> 8);
    b[3] = (u8) x;
    direct_append(b, 4);
}

void GXPortWGFifoS8(s8 x) { direct_append((const u8*) &x, 1); }

void GXPortWGFifoS16(s16 x)
{
    u16 v = (u16) x;
    GXPortWGFifoU16(v);
}

void GXPortWGFifoS32(s32 x)
{
    u32 v = (u32) x;
    GXPortWGFifoU32(v);
}

void GXPortWGFifoF32(f32 x)
{
    u32 v;
    memcpy(&v, &x, sizeof(v));
    GXPortWGFifoU32(v);
}

static void flush_direct(void)
{
    if (!begin_pending) {
        return;
    }
    begin_pending = 0;
    if (begin_overflow) {
        stat_primitives++;
        stat_skipped++;
    } else if (begin_len > 0) {
        size_t cursor = 0;
        stat_primitives++;
        exec_primitive(begin_op, begin_buf, begin_len, &cursor, begin_nverts);
    }
    begin_len = 0;
    begin_overflow = 0;
    end_draw_snapshot();
}

/*
 * Direct mode (GXBegin + the GXVert.h inline writers) starts a draw snapshot
 * here and captures the attribute writes; the primitive is decoded when the
 * next command or the frame boundary arrives because GXEnd is an empty inline
 * (GXGeometry.h).  The draw's state is the state at GXBegin, as on hardware.
 */
void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts)
{
    flush_direct();
    gx.begun = 1;
    begin_op = (u8) ((u8) type | (u8) vtxfmt);
    begin_nverts = nverts;
    begin_len = 0;
    begin_overflow = 0;
    begin_pending = 1;
    begin_draw_snapshot();
}

void GXCallDisplayList(void* list, u32 nbytes)
{
    const u8* p = (const u8*) list;
    size_t length = nbytes;
    size_t cursor = 0;

    flush_direct();
    stat_display_lists++;
    begin_draw_snapshot();
    while (cursor + 3 <= length) {
        u8 op = p[cursor];
        u16 n;
        if (op == 0) {
            break;
        }
        n = be16(p + cursor + 1);
        cursor += 3;
        stat_primitives++;
        exec_primitive(op, p, length, &cursor, n);
    }
    (void) draw_vertex_start;
    end_draw_snapshot();
}

/* P-612: indirect texturing state.  The commands are captured per draw; the
 * GLES evaluation lands with the S4 effects that use it (lb/lbrefract.c). */

void GXSetNumIndStages(u8 nIndStages)
{
    gx.cur.num_ind_stages = nIndStages > 4 ? 4 : nIndStages;
}

void GXSetIndTexOrder(GXIndTexStageID ind_stage, GXTexCoordID tex_coord,
                      GXTexMapID tex_map)
{
    GxHleIndStage* ind;
    if ((int) ind_stage >= 4) {
        return;
    }
    ind = &gx.cur.ind[ind_stage];
    ind->tex_coord = (unsigned char) tex_coord;
    ind->tex_map = (unsigned char) tex_map;
}

void GXSetIndTexCoordScale(GXIndTexStageID ind_stage, GXIndTexScale scale_s,
                           GXIndTexScale scale_t)
{
    GxHleIndStage* ind;
    if ((int) ind_stage >= 4) {
        return;
    }
    ind = &gx.cur.ind[ind_stage];
    ind->scale_s = (unsigned char) scale_s;
    ind->scale_t = (unsigned char) scale_t;
}

void GXSetIndTexMtx(GXIndTexMtxID mtx_id, f32 offset[2][3], s8 scale_exp)
{
    /* GX_ITM_0..2 map to the even indirect matrices, _S0..S2 to the odd
     * ones; HSD only uses GX_ITM_0 (lbrefract.c:644). */
    int slot = (int) mtx_id - (int) GX_ITM_0;
    int i, j;
    if (slot < 0 || slot >= 4) {
        return;
    }
    /* The XF stores each coefficient as 11-bit signed fixed point with 10
     * fractional bits (SDK GXBump.c: `(int)(1024.0f * offset) & 0x7FF`),
     * so the hardware result is the API value quantized to 1/1024. */
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 3; ++j) {
            s32 q = (s32) (1024.0f * offset[i][j]) & 0x7FF;
            if (q & 0x400) {
                q |= ~0x7FF;
            }
            gx.cur.ind[slot].mtx[i][j] = (float) q / 1024.0f;
        }
    }
    /* Shader factor: hardware applies 2^(adjScale-17) = 2^scale_exp
     * (Aurora regs.cpp:bp_ind_mtx, shader.cpp indirect matrix). */
    gx.cur.ind[slot].scale = ldexpf(1.0f, scale_exp);
}

void GXSetTevIndirect(GXTevStageID tev_stage, GXIndTexStageID ind_stage,
                      GXIndTexFormat format, GXIndTexBiasSel bias_sel,
                      GXIndTexMtxID matrix_sel, GXIndTexWrap wrap_s,
                      GXIndTexWrap wrap_t, GXBool add_prev, GXBool utc_lod,
                      GXIndTexAlphaSel alpha_sel)
{
    GxHleTevStage* st;
    if ((int) tev_stage >= GX_HLE_MAX_STAGES) {
        return;
    }
    st = &gx.cur.stages[tev_stage];
    /* Only the fields that actually transform the coordinate matter; the
     * fully-disabled set is what GXSetTevDirect installs. */
    st->ind_enable =
        (matrix_sel != GX_ITM_OFF || wrap_s != GX_ITW_OFF ||
         wrap_t != GX_ITW_OFF || add_prev != GX_FALSE)
            ? 1
            : 0;
    st->ind_stage = (unsigned char) ind_stage;
    st->ind_format = (unsigned char) format;
    st->ind_bias = (unsigned char) bias_sel;
    st->ind_mtx = (unsigned char) matrix_sel;
    st->ind_wrap_s = (unsigned char) wrap_s;
    st->ind_wrap_t = (unsigned char) wrap_t;
    st->ind_add_prev = (unsigned char) add_prev;
    (void) utc_lod;
    (void) alpha_sel;
}

void GXSetTevIndWarp(GXTevStageID tev_stage, GXIndTexStageID ind_stage,
                     GXBool signed_offset, GXBool replace_mode,
                     GXIndTexMtxID matrix_sel)
{
    GXSetTevIndirect(tev_stage, ind_stage,
                     signed_offset ? GX_ITF_8 : GX_ITF_8,
                     signed_offset ? GX_ITB_ST : GX_ITB_NONE, matrix_sel,
                     replace_mode ? GX_ITW_OFF : GX_ITW_0, GX_ITW_0,
                     GX_DISABLE, GX_DISABLE, GX_ITBA_OFF);
}

/* -------------------------------------------------------------- textures */

static void init_tex_defaults(GxHleTexture* t)
{
    memset(t, 0, sizeof(*t));
    t->wrap_s = GX_CLAMP;
    t->wrap_t = GX_CLAMP;
    t->min_filt = GX_LIN_MIP_LIN;
    t->mag_filt = GX_LINEAR;
    t->mipmap = GX_DISABLE;
    t->gl_texture = 0;
}

void GXInitTexObj(GXTexObj* obj, void* image_ptr, u16 width, u16 height,
                  GXTexFmt format, GXTexWrapMode wrap_s,
                  GXTexWrapMode wrap_t, u8 mipmap)
{
    (void) obj;
    init_tex_defaults(&gx.pending_tex);
    gx.pending_tex.image = image_ptr;
    gx.pending_tex.width = width;
    gx.pending_tex.height = height;
    gx.pending_tex.format = (u32) format;
    gx.pending_tex.wrap_s = (u8) wrap_s;
    gx.pending_tex.wrap_t = (u8) wrap_t;
    gx.pending_tex.mipmap = mipmap;
    gx.pending_tlut_name = -1;
}

void GXInitTexObjCI(GXTexObj* obj, void* image_ptr, u16 width, u16 height,
                    GXTexFmt format, GXTexWrapMode wrap_s,
                    GXTexWrapMode wrap_t, u8 mipmap, u32 tlut_name)
{
    GXInitTexObj(obj, image_ptr, width, height, format, wrap_s, wrap_t,
                 mipmap);
    gx.pending_tlut_name = (int) tlut_name;
}

void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter min_filt,
                     GXTexFilter mag_filt, f32 min_lod, f32 max_lod,
                     f32 lod_bias, GXBool bias_clamp, GXBool do_edge_lod,
                     GXAnisotropy max_aniso)
{
    (void) obj;
    gx.pending_tex.min_filt = (u8) min_filt;
    gx.pending_tex.mag_filt = (u8) mag_filt;
    gx.pending_tex.min_lod = min_lod;
    gx.pending_tex.max_lod = max_lod;
    gx.pending_tex.lod_bias = lod_bias;
    gx.pending_tex.bias_clamp = (u8) bias_clamp;
    gx.pending_tex.edge_lod = (u8) do_edge_lod;
    gx.pending_tex.anisotropy = (u8) max_aniso;
}

void GXInitTlutObj(GXTlutObj* tlut_obj, void* lut, GXTlutFmt fmt,
                   u16 n_entries)
{
    (void) tlut_obj;
    gx.pending_tlut.lut = lut;
    gx.pending_tlut.fmt = (u32) fmt;
    gx.pending_tlut.n_entries = n_entries;
    gx.pending_tlut.used = 1;
}

void GXLoadTlut(GXTlutObj* tlut_obj, u32 tlut_name)
{
    int name = (int) (tlut_name % GX_HLE_MAX_TLUTS);
    (void) tlut_obj;
    gx.tluts[name] = gx.pending_tlut;
}

void GXLoadTexObj(GXTexObj* obj, GXTexMapID id)
{
    GxHleTexture t = gx.pending_tex;
    (void) obj;
    if (gx.pending_tlut_name >= 0 && gx.pending_tlut_name < GX_HLE_MAX_TLUTS) {
        GxHleTlut* tl = &gx.tluts[gx.pending_tlut_name];
        t.palette = tl->lut;
        t.palette_format = tl->fmt;
        t.palette_entries = tl->n_entries;
    }
    if (frame_tcount < GX_HLE_MAX_TEXTURES) {
        frame_textures[frame_tcount] = t;
        if ((int) id >= 0 && (int) id < 8) {
            gx.cur.texmap[id] = (int) frame_tcount;
        }
        frame_tcount++;
    }
}

void GXLoadTexObjPreLoaded(GXTexObj* obj, GXTexRegion* region,
                           GXTexMapID id)
{
    (void) region;
    GXLoadTexObj(obj, id);
}

GXTexFmt GXGetTexObjFmt(const GXTexObj* to)
{
    (void) to;
    return (GXTexFmt) gx.pending_tex.format;
}

u16 GXGetTexObjWidth(const GXTexObj* to)
{
    (void) to;
    return gx.pending_tex.width;
}

u16 GXGetTexObjHeight(const GXTexObj* to)
{
    (void) to;
    return gx.pending_tex.height;
}

GXTexWrapMode GXGetTexObjWrapS(const GXTexObj* to)
{
    (void) to;
    return (GXTexWrapMode) gx.pending_tex.wrap_s;
}

GXTexWrapMode GXGetTexObjWrapT(const GXTexObj* to)
{
    (void) to;
    return (GXTexWrapMode) gx.pending_tex.wrap_t;
}

u8 GXGetTexObjMipMap(const GXTexObj* to)
{
    (void) to;
    return gx.pending_tex.mipmap;
}

void GXInvalidateTexAll(void) {}
void GXInvalidateTexRegion(GXTexRegion* region) { (void) region; }
void GXInvalidateVtxCache(void) {}

/* ---------------------------------------------------------------- lights */

void GXInitLightAttn(GXLightObj* lt_obj, f32 a0, f32 a1, f32 a2, f32 k0,
                     f32 k1, f32 k2)
{
    GxHleLight* l = (GxHleLight*) lt_obj;
    l->a[0] = a0;
    l->a[1] = a1;
    l->a[2] = a2;
    l->k[0] = k0;
    l->k[1] = k1;
    l->k[2] = k2;
}

void GXInitLightAttnA(GXLightObj* lt_obj, f32 a0, f32 a1, f32 a2)
{
    GxHleLight* l = (GxHleLight*) lt_obj;
    l->a[0] = a0;
    l->a[1] = a1;
    l->a[2] = a2;
}

void GXInitLightAttnK(GXLightObj* lt_obj, f32 k0, f32 k1, f32 k2)
{
    GxHleLight* l = (GxHleLight*) lt_obj;
    l->k[0] = k0;
    l->k[1] = k1;
    l->k[2] = k2;
}

void GXInitLightColor(GXLightObj* lt_obj, GXColor color)
{
    GxHleLight* l = (GxHleLight*) lt_obj;
    l->color = color;
}

void GXInitLightPos(GXLightObj* lt_obj, f32 x, f32 y, f32 z)
{
    GxHleLight* l = (GxHleLight*) lt_obj;
    l->pos[0] = x;
    l->pos[1] = y;
    l->pos[2] = z;
}

void GXInitLightDir(GXLightObj* lt_obj, f32 nx, f32 ny, f32 nz)
{
    GxHleLight* l = (GxHleLight*) lt_obj;
    l->dir[0] = nx;
    l->dir[1] = ny;
    l->dir[2] = nz;
}

void GXInitLightDistAttn(GXLightObj* lt_obj, f32 ref_dist, f32 ref_br,
                         GXDistAttnFn dist_func)
{
    f32 k0;
    if (ref_br < 0.0f) {
        ref_br = 0.0f;
    } else if (ref_br >= 1.0f) {
        ref_br = 1.0f;
    }
    switch (dist_func) {
    case GX_DA_OFF:
        GXInitLightAttnK(lt_obj, 1.0f, 0.0f, 0.0f);
        break;
    case GX_DA_GENTLE:
        k0 = (1.0f - ref_br) / (ref_br * ref_dist);
        GXInitLightAttnK(lt_obj, 1.0f, k0, 0.0f);
        break;
    case GX_DA_MEDIUM:
        k0 = (1.0f - ref_br) / (ref_br * ref_dist);
        GXInitLightAttnK(lt_obj, 1.0f, k0, k0 * k0);
        break;
    case GX_DA_STEEP:
    default:
        k0 = (1.0f - ref_br) / (ref_br * ref_dist);
        k0 = k0 * k0;
        GXInitLightAttnK(lt_obj, 1.0f, 0.0f, k0);
        break;
    }
}

void GXInitLightSpot(GXLightObj* lt_obj, f32 cutoff, GXSpotFn spot_func)
{
    f32 a0, a1, a2;
    f32 d;
    if (cutoff <= 0.0f || cutoff > 90.0f) {
        cutoff = 90.0f;
    }
    d = cosf(cutoff * 3.14159265f / 180.0f);
    switch (spot_func) {
    case GX_SP_FLAT:
        a0 = -1000.0f * d;
        a1 = 1000.0f;
        a2 = 0.0f;
        break;
    case GX_SP_COS:
        a0 = -d / (1.0f - d);
        a1 = 1.0f / (1.0f - d);
        a2 = 0.0f;
        break;
    case GX_SP_COS2:
        a0 = 0.0f;
        a1 = -d / (1.0f - d);
        a2 = 1.0f / (1.0f - d);
        break;
    case GX_SP_SHARP:
        a0 = (d * (d - 2.0f)) / ((1.0f - d) * (1.0f - d));
        a1 = (2.0f * d) / ((1.0f - d) * (1.0f - d));
        a2 = -1.0f / ((1.0f - d) * (1.0f - d));
        break;
    case GX_SP_RING1:
        a0 = (-4.0f * d) / ((1.0f - d) * (1.0f - d));
        a1 = (4.0f * (1.0f + d)) / ((1.0f - d) * (1.0f - d));
        a2 = -4.0f / ((1.0f - d) * (1.0f - d));
        break;
    case GX_SP_RING2:
        a0 = (1.0f - 2.0f * d) / ((1.0f - d) * (1.0f - d));
        a1 = (4.0f * d) / ((1.0f - d) * (1.0f - d));
        a2 = -2.0f / ((1.0f - d) * (1.0f - d));
        break;
    case GX_SP_OFF:
    default:
        a0 = 1.0f;
        a1 = 0.0f;
        a2 = 0.0f;
        break;
    }
    GXInitLightAttnA(lt_obj, a0, a1, a2);
}

void GXLoadLightObjImm(GXLightObj* lt_obj, GXLightID light)
{
    int idx = -1;
    int i;
    for (i = 0; i < 8; ++i) {
        if (light == (GXLightID) (1u << i)) {
            idx = i;
        }
    }
    if (idx < 0) {
        return;
    }
    gx.cur.lights[idx] = *(const GxHleLight*) lt_obj;
}

void GXLoadLightObjIndx(u32 obj_index, GXLightID light)
{
    (void) obj_index;
    (void) light;
}

void GXSetLightObjMtx(GXLightObj* lt_obj, f32 mtx[3][4])
{
    (void) lt_obj;
    (void) mtx;
}

/* ------------------------------------------------- P-615 EFB / Z texture */

void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias)
{
    flush_direct();
    gx.cur.ztex_op = (unsigned char) op;
    gx.cur.ztex_fmt = (unsigned char) fmt;
    /* bias is a 24-bit fraction of the Z range. */
    gx.cur.ztex_bias = (float) (bias & 0xFFFFFFu) / 16777215.0f;
}

u32 GXSetDispCopyYScale(f32 vscale)
{
    (void) vscale;
    return 0;
}

void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    (void) left;
    (void) top;
    (void) wd;
    (void) ht;
}

void GXSetDispCopyDst(u16 wd, u16 ht)
{
    (void) wd;
    (void) ht;
}

void GXSetDispCopyGamma(GXGamma gamma) { (void) gamma; }

void GXSetCopyClamp(GXFBClamp clamp) { (void) clamp; }

void GXSetCopyClear(GXColor clear_clr, u32 clear_z)
{
    flush_direct();
    (void) clear_clr;
    (void) clear_z;
}

void GXSetCopyFilter(GXBool aa, const u8 sample_pattern[12][2], GXBool vf,
                     const u8 vfilter[7])
{
    (void) aa;
    (void) sample_pattern;
    (void) vf;
    (void) vfilter;
}

void GXCopyDisp(void* dest, GXBool clear)
{
    (void) dest;
    (void) clear;
}

void GXCopyTex(void* dest, GXBool clear)
{
    GxHleDraw* d;

    flush_direct();
    if (frame_dcount >= GX_HLE_MAX_DRAWS || dest == NULL) {
        return;
    }
    d = &frame_draws[frame_dcount];
    memset(d, 0, sizeof(*d));
    d->kind = GX_HLE_DRAW_COPY_TEX;
    d->state = gx.cur;
    d->copy_dest = dest;
    d->copy_left = copy_src_left;
    d->copy_top = copy_src_top;
    d->copy_w = copy_src_w;
    d->copy_h = copy_src_h;
    d->copy_dst_w = copy_dst_w;
    d->copy_dst_h = copy_dst_h;
    d->copy_fmt = copy_dst_fmt;
    d->copy_clear = (unsigned char) clear;
    frame_dcount++;
}

void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht)
{
    copy_src_left = left;
    copy_src_top = top;
    copy_src_w = wd;
    copy_src_h = ht;
}

void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap)
{
    (void) mipmap;
    copy_dst_w = wd;
    copy_dst_h = ht;
    copy_dst_fmt = (unsigned int) fmt;
}

void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt)
{
    (void) pix_fmt;
    (void) z_fmt;
}

void GXSetFieldMode(GXBool field_mode, GXBool half_aspect_ratio)
{
    (void) field_mode;
    (void) half_aspect_ratio;
}

void GXPixModeSync(void) {}

void GXSetMisc(GXMiscToken token, u32 val)
{
    (void) token;
    (void) val;
}

void GXSetLineWidth(u8 width, GXTexOffset texOffsets)
{
    (void) width;
    (void) texOffsets;
}

void GXSetPointSize(u8 pointSize, GXTexOffset texOffsets)
{
    (void) pointSize;
    (void) texOffsets;
}

void GXEnableTexOffsets(GXTexCoordID coord, u8 line_enable,
                        u8 point_enable)
{
    (void) coord;
    (void) line_enable;
    (void) point_enable;
}

void GXWaitDrawDone(void) {}
void GXDrawDone(void) {}

void GXSetDrawSync(u16 token) { (void) token; }

GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb)
{
    (void) cb;
    return NULL;
}

void GXSetGPMetric(GXPerf0 perf0, GXPerf1 perf1)
{
    (void) perf0;
    (void) perf1;
}

void GXClearGPMetric(void) {}

/* ------------------------------------------------------------- lifecycle */

static void ensure_vertex_buffer(void)
{
    if (frame_verts == NULL) {
        frame_verts = (GxHleVertex*) calloc(GX_HLE_MAX_VERTS,
                                            sizeof(GxHleVertex));
        frame_vcap = frame_verts != NULL ? GX_HLE_MAX_VERTS : 0;
    }
}

static void reset_state(void)
{
    int i;
    static const float ident[3][4] = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }
    };

    memset(&gx.cur, 0, sizeof(gx.cur));
    memset(gx.desc, 0, sizeof(gx.desc));
    memset(gx.fmt, 0, sizeof(gx.fmt));
    memset(gx.arrays, 0, sizeof(gx.arrays));
    gx.desc_count = 0;
    gx.current_mtx = 0;
    gx.cur.num_stages = 1;
    gx.cur.num_chans = 1;
    gx.cur.z_enable = 1;
    gx.cur.z_func = GX_LEQUAL;
    gx.cur.z_update = 1;
    gx.cur.color_update = 1;
    gx.cur.blend_type = GX_BM_NONE;
    gx.cur.cull_mode = GX_CULL_NONE;
    gx.cur.alpha_comp0 = GX_ALWAYS;
    gx.cur.alpha_comp1 = GX_ALWAYS;
    gx.cur.alpha_op = GX_AOP_AND;
    /* GX scissor defaults to the full EFB; HSD overwrites it in
     * HSD_CObjSetup, but leave a sane value for draws before that. */
    gx.cur.scissor_w = 640;
    gx.cur.scissor_h = 480;
    gx.cur.viewport[2] = 640.0f;
    gx.cur.viewport[3] = 480.0f;
    gx.cur.depth_range[1] = 1.0f;
    gx.cur.ch_mat[0][3] = 1.0f;
    gx.cur.ch_mat[1][3] = 1.0f;
    gx.cur.ch_mat[2][3] = 1.0f;
    gx.cur.ch_mat[3][3] = 1.0f;
    for (i = 0; i < 8; ++i) {
        gx.cur.texmap[i] = -1;
        gx.cur.texgen[i].type = GX_TG_MTX2x4;
        gx.cur.texgen[i].src = GX_TG_TEX0;
        gx.cur.texgen[i].mtx_id = GX_IDENTITY;
    }
    for (i = 0; i < 4; ++i) {
        gx.cur.tev_color[i][3] = 1.0f;
        gx.cur.tev_kcolor[i][3] = 1.0f;
        gx.cur.swap_table[i][0] = GX_CH_RED;
        gx.cur.swap_table[i][1] = GX_CH_GREEN;
        gx.cur.swap_table[i][2] = GX_CH_BLUE;
        gx.cur.swap_table[i][3] = GX_CH_ALPHA;
    }
    for (i = 0; i < 30; ++i) {
        memcpy(gx.pos_mtx[i], ident, sizeof(ident));
        memcpy(gx.nrm_mtx[i], ident, sizeof(ident));
    }
    for (i = 0; i < 20; ++i) {
        memcpy(gx.tex_mtx[i], ident, sizeof(ident));
    }
    memcpy(gx.projection, ident, sizeof(ident));
    gx.projection[3][3] = 1.0f;
    /* Packed identity: A=1, B=0, C=1, D=0, E=1, F=0. */
    gx.proj_mtx[0] = 1.0f;
    gx.proj_mtx[1] = 0.0f;
    gx.proj_mtx[2] = 1.0f;
    gx.proj_mtx[3] = 0.0f;
    gx.proj_mtx[4] = 1.0f;
    gx.proj_mtx[5] = 0.0f;
}

void gx_hle_begin_frame(void)
{
    ensure_vertex_buffer();
    begin_pending = 0;
    begin_len = 0;
    begin_overflow = 0;
    draw_active = 0;
    frame_vcount = 0;
    frame_dcount = 0;
    frame_tcount = 0;
    stat_display_lists = 0;
    stat_primitives = 0;
    stat_skipped = 0;
    stat_degenerate = 0;
    memset(gx.tluts, 0, sizeof(gx.tluts));
    reset_state();
}

/* Wipe the captured GX register state.  Frame boundaries must NOT do this:
 * hardware GX state persists across frames and the compiled engine caches it
 * (HSD_StateInvalidate is the game-side equivalent).  Use this only when a
 * target knowingly reinitializes the state (S2 scene boot, tests) and pair it
 * with HSD_StateInvalidate(-1) when the compiled engine is running, or the
 * engine will skip re-emitting state it thinks is still valid (G-054). */
void gx_hle_reset_state(void)
{
    reset_state();
}

void gx_hle_discard_geometry(void)
{
    begin_pending = 0;
    begin_len = 0;
    begin_overflow = 0;
    draw_active = 0;
    frame_vcount = 0;
    frame_dcount = 0;
    /* Texture descriptors are per captured frame; keeping them accumulated
     * overflows GX_HLE_MAX_TEXTURES after a few frames and leaves every draw
     * pointing at stale entries. */
    frame_tcount = 0;
    stat_display_lists = 0;
    stat_primitives = 0;
    stat_skipped = 0;
}

int gx_hle_get_frame(const GxHleVertex** vertices, size_t* vertex_count,
                     const GxHleDraw** draws, size_t* draw_count,
                     GxHleTexture** textures, size_t* texture_count)
{
    flush_direct();
    if (vertices) {
        *vertices = frame_verts;
    }
    if (vertex_count) {
        *vertex_count = frame_vcount;
    }
    if (draws) {
        *draws = frame_draws;
    }
    if (draw_count) {
        *draw_count = frame_dcount;
    }
    if (textures) {
        *textures = frame_textures;
    }
    if (texture_count) {
        *texture_count = frame_tcount;
    }
    return 1;
}

size_t gx_hle_display_list_count(void)
{
    return stat_display_lists;
}

size_t gx_hle_primitive_count(void)
{
    return stat_primitives;
}

size_t gx_hle_skipped_count(void)
{
    return stat_skipped;
}

size_t gx_hle_degenerate_count(void)
{
    return stat_degenerate;
}

/* Every archive the game parses is registered here so the HLE can bound
 * display lists/textures that point into them.  The table grows on demand:
 * a match loads far more than a fixed small cap (the 8-entry static list
 * silently dropped later archives, so their CI textures hit the fallback
 * bound and failed to decode). */
typedef struct GxAsset {
    const unsigned char* base;
    size_t size;
} GxAsset;

static GxAsset* gx_assets;
static size_t gx_asset_count;
static size_t gx_asset_capacity;

void gx_hle_register_asset(const void* base, size_t size)
{
    if (base == NULL || size == 0) {
        return;
    }
    if (gx_asset_count == gx_asset_capacity) {
        size_t capacity = gx_asset_capacity != 0 ? gx_asset_capacity * 2 : 16;
        GxAsset* grown = (GxAsset*) realloc(gx_assets,
                                            capacity * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        gx_assets = grown;
        gx_asset_capacity = capacity;
    }
    gx_assets[gx_asset_count].base = (const unsigned char*) base;
    gx_assets[gx_asset_count].size = size;
    gx_asset_count++;
}

void gx_hle_reset_assets(void)
{
    gx_asset_count = 0;
}

size_t gx_hle_asset_remaining(const void* ptr)
{
    size_t i;
    const unsigned char* p = (const unsigned char*) ptr;
    for (i = 0; i < gx_asset_count; ++i) {
        if (p >= gx_assets[i].base &&
            p < gx_assets[i].base + gx_assets[i].size) {
            return (size_t) (gx_assets[i].base + gx_assets[i].size - p);
        }
    }
    return (size_t) -1;
}

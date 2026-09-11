#include "hsd/model.h"

#include "hsd/aobj.h"
#include "gx/texture.h"
#include "decomp/decomp_math.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Raw HSD model reader for the native port.
 *
 * HSD archives store pointers as 32-bit offsets relative to the start of the
 * data section (immediately after the 0x20-byte archive header).  All accessors
 * here translate those offsets to host file offsets and validate every range
 * before reading.  The reader supports the Joint -> DObjDesc -> PObjDesc ->
 * VtxDescList -> display-list path used by Melee character models, including
 * bind-pose envelope skinning, material colors and embedded textures.
 *
 * The bind pose is reconstructed exactly as HSD_PObjSetupMtx does at rest:
 *   - Groups whose first envelope weight is 1 transform by the joint's bind
 *     world matrix (vertices are stored joint-local).
 *   - Groups with blended weights use sum(weight * M_joint * inverseBind),
 *     which is the identity at bind pose.
 * Runtime animation would replace the joint world matrices, not this math.
 */

#define HSD_DATA_BASE 0x20u
#define HSD_MAX_JOINT_DEPTH 128
#define HSD_MAX_JOINTS 512
#define HSD_MAX_ENV_GROUPS 10
#define HSD_MAX_DISPLAY_BYTES (16u * 1024u * 1024u)
#define HSD_MAX_PRIM_VERTS 4096

#define GX_VA_PNMTXIDX 0
#define GX_VA_POS 9
#define GX_VA_NRM 10
#define GX_VA_CLR0 11
#define GX_VA_CLR1 12
#define GX_VA_TEX0 13
#define GX_VA_TEX1 14
#define GX_VA_TEX7 20
#define GX_VA_NULL 0xff

#define GX_DIRECT 1
#define GX_INDEX8 2
#define GX_INDEX16 3

#define GX_QUADS 0x80
#define GX_TRIANGLES 0x90
#define GX_TRIANGLESTRIP 0x98
#define GX_TRIANGLEFAN 0xa0

#define HSD_FLAG_SCL_INHERIT 0x8
#define HSD_FLAG_SKELETON 0x1
#define HSD_FLAG_HIDDEN 0x10
#define HSD_FLAG_INSTANCE 0x1000
#define HSD_FLAG_SKELETON_ROOT 0x2

typedef struct RawDesc {
    uint32_t attr;
    uint32_t type;
    uint32_t cnt;
    uint32_t ctype;
    uint8_t frac;
    uint16_t stride;
    size_t base; /* host offset of the vertex array, SIZE_MAX when absent */
} RawDesc;

typedef struct RawVertex {
    float pos[3];
    float nrm[3];
    float uv[2];
    float uv2[2];
    uint8_t color[4];
    int matrix;
    int skin_sel;
    int has_pos;
    int has_nrm;
    int has_uv;
    int has_uv2;
    int has_color;
} RawVertex;

static uint32_t rb32(const uint8_t *d, size_t n, size_t o)
{
    if (o > n - 4) {
        return 0;
    }
    return ((uint32_t) d[o] << 24) | ((uint32_t) d[o + 1] << 16) |
           ((uint32_t) d[o + 2] << 8) | d[o + 3];
}

static uint16_t rb16(const uint8_t *d, size_t n, size_t o)
{
    if (o > n - 2) {
        return 0;
    }
    return (uint16_t) (((uint16_t) d[o] << 8) | d[o + 1]);
}

static float rf32(const uint8_t *d, size_t n, size_t o)
{
    uint32_t bits = rb32(d, n, o);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range_ok(size_t o, size_t bytes, size_t n)
{
    return o <= n && bytes <= n - o;
}

/* NULL-aware pointer field: 0 is treated as NULL. */
static size_t rptr(const uint8_t *d, size_t n, size_t field)
{
    uint32_t value = rb32(d, n, field);
    if (value == 0 || value > n - HSD_DATA_BASE) {
        return SIZE_MAX;
    }
    return (size_t) value + HSD_DATA_BASE;
}

/* Array-base pointer field where 0 is a valid offset (start of data). */
static size_t rbase(const uint8_t *d, size_t n, size_t field)
{
    uint32_t value = rb32(d, n, field);
    if (value > n - HSD_DATA_BASE) {
        return SIZE_MAX;
    }
    return (size_t) value + HSD_DATA_BASE;
}

static void seterr(char *dst, size_t n, const char *msg)
{
    if (dst != NULL && n != 0) {
        size_t length = strlen(msg);
        if (length >= n) {
            length = n - 1;
        }
        memcpy(dst, msg, length);
        dst[length] = 0;
    }
}

static void mtx_identity(float m[3][4])
{
    memset(m, 0, sizeof(float) * 12);
    m[0][0] = 1.0f;
    m[1][1] = 1.0f;
    m[2][2] = 1.0f;
}

/* out = a * b for 3x4 affine matrices (implicit last row 0 0 0 1).  The
 * pointers are flat 12-float arrays in row-major order. */
static void mtx_concat(const float *a, const float *b, float *out)
{
    float r[12];
    int i;
    int j;
    int k;
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            float sum = 0.0f;
            for (k = 0; k < 3; ++k) {
                sum += a[i * 4 + k] * b[k * 4 + j];
            }
            r[i * 4 + j] = sum;
        }
        r[i * 4 + 3] = a[i * 4 + 3];
        for (k = 0; k < 3; ++k) {
            r[i * 4 + 3] += a[i * 4 + k] * b[k * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static void mtx_transform_point(const float *m, const float in[3], float out[3])
{
    int i;
    for (i = 0; i < 3; ++i) {
        out[i] = m[i * 4 + 0] * in[0] + m[i * 4 + 1] * in[1] +
                 m[i * 4 + 2] * in[2] + m[i * 4 + 3];
    }
}

static void mtx_transform_normal(const float *m, const float in[3],
                                 float out[3])
{
    float x = m[0] * in[0] + m[1] * in[1] + m[2] * in[2];
    float y = m[4] * in[0] + m[5] * in[1] + m[6] * in[2];
    float z = m[8] * in[0] + m[9] * in[1] + m[10] * in[2];
    float length = sqrtf(x * x + y * y + z * z);
    if (length > 1e-8f) {
        x /= length;
        y /= length;
        z /= length;
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* Inverse of an affine 3x4 matrix (rotation/scale block plus translation). */
static int mtx_invert(const float *m, float *out)
{
    float a = m[0], b = m[1], c = m[2];
    float d = m[4], e = m[5], f = m[6];
    float g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    float inv[12];
    float tx;
    float ty;
    float tz;
    if (fabsf(det) < 1e-20f) {
        return 0;
    }
    det = 1.0f / det;
    inv[0] = (e * i - f * h) * det;
    inv[1] = (c * h - b * i) * det;
    inv[2] = (b * f - c * e) * det;
    inv[4] = (f * g - d * i) * det;
    inv[5] = (a * i - c * g) * det;
    inv[6] = (c * d - a * f) * det;
    inv[8] = (d * h - e * g) * det;
    inv[9] = (b * g - a * h) * det;
    inv[10] = (a * e - b * d) * det;
    tx = m[3];
    ty = m[7];
    tz = m[11];
    inv[3] = -(inv[0] * tx + inv[1] * ty + inv[2] * tz);
    inv[7] = -(inv[4] * tx + inv[5] * ty + inv[6] * tz);
    inv[11] = -(inv[8] * tx + inv[9] * ty + inv[10] * tz);
    memcpy(out, inv, sizeof(inv));
    return 1;
}

/* Index of a joint by host offset, or -1. */
static int joint_index_of(const HsdModel *m, size_t offset)
{
    size_t i;
    for (i = 0; i < m->joint_count; ++i) {
        if (m->joints[i].offset == offset) {
            return (int) i;
        }
    }
    return -1;
}

/* Nearest ancestor (including self) flagged JOBJ_SKELETON or JOBJ_SKELETON_ROOT. */
static int joint_find_skeleton(const HsdModel *m, int index)
{
    while (index >= 0) {
        uint32_t flags = m->joints[index].flags;
        if (flags & (HSD_FLAG_SKELETON | HSD_FLAG_SKELETON_ROOT)) {
            return index;
        }
        index = m->joints[index].parent;
    }
    return -1;
}

/* E_j: the joint's inverse bind matrix, from the archive or derived. */
static int joint_env_mtx(const HsdModel *m, int index, float out[3][4])
{
    if (index < 0 || (size_t) index >= m->joint_count) {
        return 0;
    }
    if (m->joints[index].has_inv_bind) {
        memcpy(out, m->joints[index].inv_bind, sizeof(m->joints[index].inv_bind));
        return 1;
    }
    return mtx_invert(&m->joints[index].world_bind[0][0], &out[0][0]);
}

/*
 * HSD's _HSD_mkEnvelopeModelNodeMtx.  Returns 0 when HSD would return NULL
 * (the joint is the skeleton root), otherwise writes the 3x4 `right` matrix.
 * Unlike the bind-pose shortcut, the skeleton-root and deep-skeleton cases
 * depend on the current world matrices, so this runs every pose.
 */
static int batch_right(const HsdModel *m, int m_index, float right[3][4])
{
    int x;
    if (m_index < 0 || (size_t) m_index >= m->joint_count) {
        return 0;
    }
    if (m->joints[m_index].flags & HSD_FLAG_SKELETON_ROOT) {
        return 0;
    }
    x = joint_find_skeleton(m, m_index);
    if (x < 0) {
        return 0;
    }
    if (x == m_index) {
        /* inverse(E_x): the joint's bind world matrix. */
        float env[3][4];
        if (!joint_env_mtx(m, x, env)) {
            return 0;
        }
        if (!mtx_invert(&env[0][0], &right[0][0])) {
            return 0;
        }
    } else if (m->joints[x].flags & HSD_FLAG_SKELETON_ROOT) {
        float inverse[3][4];
        if (!mtx_invert(&m->joints[x].world[0][0], &inverse[0][0])) {
            return 0;
        }
        mtx_concat(&inverse[0][0], &m->joints[m_index].world[0][0],
                   &right[0][0]);
    } else {
        float env[3][4];
        float n[3][4];
        float inverse[3][4];
        if (!joint_env_mtx(m, x, env)) {
            return 0;
        }
        mtx_concat(&m->joints[x].world[0][0], &env[0][0], &n[0][0]);
        if (!mtx_invert(&n[0][0], &inverse[0][0])) {
            return 0;
        }
        mtx_concat(&inverse[0][0], &m->joints[m_index].world[0][0],
                   &right[0][0]);
    }
    return 1;
}

/* Thin type adapter for the compiled decomp HSD_MtxSRT (P-301,
 * src/sysdolphin/baselib/mtx.c, built by the melee_decomp_math target).  The
 * formula is no longer hand-copied; only the float[3] -> Vec3 marshalling is
 * local.  Bind-pose output was verified bitwise against the deleted hand copy
 * in tests/test_decomp_mtx.c. */
static void make_local_mtx(float m[3][4], const float scale[3],
                           const float rot[3], const float pos[3],
                           const float *parent_scale)
{
    DecompVec3 scale_v = { scale[0], scale[1], scale[2] };
    DecompVec3 rot_v = { rot[0], rot[1], rot[2] };
    DecompVec3 pos_v = { pos[0], pos[1], pos[2] };
    DecompVec3 parent_v;
    DecompVec3 *parent_p = NULL;
    if (parent_scale != NULL) {
        parent_v.x = parent_scale[0];
        parent_v.y = parent_scale[1];
        parent_v.z = parent_scale[2];
        parent_p = &parent_v;
    }
    HSD_MtxSRT(m, &scale_v, &rot_v, &pos_v, parent_p);
}

static void joint_table_add(const uint8_t *d, size_t n, size_t jo, int parent,
                            HsdModel *m, int depth)
{
    float rot[3];
    float scale[3];
    float pos[3];
    float local[3][4];
    uint32_t flags;
    size_t mtx_field;
    HsdJoint *info;
    size_t child;
    size_t next;
    size_t i;
    if (jo == SIZE_MAX || depth > HSD_MAX_JOINT_DEPTH ||
        m->joint_count >= HSD_MAX_JOINTS || !range_ok(jo, 0x40, n)) {
        return;
    }
    for (i = 0; i < m->joint_count; ++i) {
        if (m->joints[i].offset == jo) {
            return;
        }
    }
    flags = rb32(d, n, jo + 4);
    rot[0] = rf32(d, n, jo + 0x14);
    rot[1] = rf32(d, n, jo + 0x18);
    rot[2] = rf32(d, n, jo + 0x1c);
    scale[0] = rf32(d, n, jo + 0x20);
    scale[1] = rf32(d, n, jo + 0x24);
    scale[2] = rf32(d, n, jo + 0x28);
    pos[0] = rf32(d, n, jo + 0x2c);
    pos[1] = rf32(d, n, jo + 0x30);
    pos[2] = rf32(d, n, jo + 0x34);
    info = &m->joints[m->joint_count];
    memset(info, 0, sizeof(*info));
    info->offset = jo;
    info->flags = flags;
    info->parent = parent;
    memcpy(info->rotation_bind, rot, sizeof(rot));
    memcpy(info->scale_bind, scale, sizeof(scale));
    memcpy(info->position_bind, pos, sizeof(pos));
    {
        const float *parent_scale = NULL;
        if (parent >= 0) {
            parent_scale = m->joints[parent].scale_world;
        }
        make_local_mtx(local, scale, rot, pos, parent_scale);
    }
    if (parent >= 0) {
        mtx_concat(&m->joints[parent].world_bind[0][0], &local[0][0],
                   &info->world_bind[0][0]);
    } else {
        memcpy(info->world_bind, local, sizeof(local));
    }
    if (parent >= 0 && (flags & HSD_FLAG_SCL_INHERIT)) {
        memcpy(info->scale_world, m->joints[parent].scale_world,
               sizeof(info->scale_world));
    } else if (parent >= 0) {
        info->scale_world[0] = scale[0] * m->joints[parent].scale_world[0];
        info->scale_world[1] = scale[1] * m->joints[parent].scale_world[1];
        info->scale_world[2] = scale[2] * m->joints[parent].scale_world[2];
    } else {
        memcpy(info->scale_world, scale, sizeof(info->scale_world));
    }
    /* HSD_Joint.mtx is the inverse bind world matrix (verified per joint). */
    mtx_field = rptr(d, n, jo + 0x38);
    if (mtx_field != SIZE_MAX && range_ok(mtx_field, 48, n)) {
        int r;
        int c;
        for (r = 0; r < 3; ++r) {
            for (c = 0; c < 4; ++c) {
                info->inv_bind[r][c] = rf32(d, n, mtx_field + (r * 4 + c) * 4);
            }
        }
        info->has_inv_bind = 1;
    }
    {
        int index = (int) m->joint_count;
        m->joint_count++;
        child = rptr(d, n, jo + 8);
        while (child != SIZE_MAX) {
            joint_table_add(d, n, child, index, m, depth + 1);
            child = rptr(d, n, child + 12);
        }
        next = rptr(d, n, jo + 12);
        if (next != SIZE_MAX) {
            joint_table_add(d, n, next, parent, m, depth);
        }
    }
}



static size_t attr_comp_count(uint32_t attr, uint32_t cnt)
{
    if (attr == GX_VA_POS) {
        return cnt == 0 ? 2 : 3;
    }
    if (attr == GX_VA_NRM) {
        return cnt == 0 ? 3 : 9;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        return cnt == 0 ? 3 : 4;
    }
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        return cnt == 0 ? 1 : 2;
    }
    return 1;
}

static size_t ctype_size(uint32_t ctype)
{
    if (ctype <= 1) {
        return 1;
    }
    if (ctype <= 3) {
        return 2;
    }
    return 4;
}

static uint8_t model_expand4(unsigned int v) { return (uint8_t) ((v << 4) | v); }
static uint8_t model_expand5(unsigned int v) { return (uint8_t) ((v * 255u) / 31u); }
static uint8_t model_expand6(unsigned int v) { return (uint8_t) ((v * 255u) / 63u); }

/*
 * For GX_VA_CLR0/CLR1 the comp_type field uses the color encoding enum
 * (GX_RGB565=0, GX_RGB8=1, GX_RGBX8=2, GX_RGBA4=3, GX_RGBA6=4, GX_RGBA8=5),
 * not the scalar component enum.  This was the cause of the vertex stream
 * desync on coloured meshes.
 */
static size_t color_attribute_size(uint32_t ctype)
{
    switch (ctype) {
    case 0: /* GX_RGB565 */
    case 3: /* GX_RGBA4 */
        return 2;
    case 2: /* GX_RGBX8 */
    case 5: /* GX_RGBA8 */
        return 4;
    case 1: /* GX_RGB8 */
    case 4: /* GX_RGBA6 */
    default:
        return 3;
    }
}

static void decode_color(uint32_t ctype, const uint8_t *p, size_t n, size_t o,
                         uint8_t out[4])
{
    switch (ctype) {
    case 0: { /* RGB565 */
        uint16_t v = rb16(p, n, o);
        out[0] = model_expand5((v >> 11) & 31);
        out[1] = model_expand6((v >> 5) & 63);
        out[2] = model_expand5(v & 31);
        out[3] = 255;
        break;
    }
    case 1: /* RGB8 */
        out[0] = p[o];
        out[1] = p[o + 1];
        out[2] = p[o + 2];
        out[3] = 255;
        break;
    case 2: /* RGBX8 */
        out[0] = p[o];
        out[1] = p[o + 1];
        out[2] = p[o + 2];
        out[3] = p[o + 3];
        break;
    case 3: { /* RGBA4 */
        uint16_t v = rb16(p, n, o);
        out[0] = model_expand4((v >> 12) & 15);
        out[1] = model_expand4((v >> 8) & 15);
        out[2] = model_expand4((v >> 4) & 15);
        out[3] = model_expand4(v & 15);
        break;
    }
    case 4: /* RGBA6, 18 bits packed big-endian */
        out[0] = (uint8_t) ((p[o] >> 2) * 255 / 63);
        out[1] = (uint8_t) ((((p[o] & 3) << 4) | (p[o + 1] >> 4)) * 255 / 63);
        out[2] = (uint8_t) ((((p[o + 1] & 15) << 2) | (p[o + 2] >> 6)) * 255 / 63);
        out[3] = (uint8_t) ((p[o + 2] & 63) * 255 / 63);
        break;
    case 5: /* RGBA8 */
    default:
        out[0] = p[o];
        out[1] = p[o + 1];
        out[2] = p[o + 2];
        out[3] = p[o + 3];
        break;
    }
}

static float decode_comp(uint32_t ctype, uint8_t frac, const uint8_t *p, size_t n,
                         size_t o)
{
    float scale;
    switch (ctype) {
    case 0: /* GX_U8 */
        scale = (float) (1u << frac);
        return (float) p[o] / scale;
    case 1: /* GX_S8 */
        scale = (float) (1u << frac);
        return (float) (int8_t) p[o] / scale;
    case 2: /* GX_U16 */
        scale = (float) (1u << frac);
        return (float) rb16(p, n, o) / scale;
    case 3: /* GX_S16 */
        scale = (float) (1u << frac);
        return (float) (int16_t) rb16(p, n, o) / scale;
    case 4: { /* GX_F32 stored big-endian */
        uint32_t bits = rb32(p, n, o);
        float value;
        memcpy(&value, &bits, sizeof(value));
        return value;
    }
    default:
        return 0.0f;
    }
}

static void read_descs(const uint8_t *d, size_t n, size_t vo, RawDesc *descs,
                       size_t max_descs, size_t *out_count)
{
    size_t count = 0;
    size_t i;
    for (i = 0; i < 32 && count < max_descs; ++i) {
        size_t o = vo + i * 0x18;
        if (!range_ok(o, 0x18, n)) {
            break;
        }
        descs[count].attr = rb32(d, n, o);
        descs[count].type = rb32(d, n, o + 4);
        descs[count].cnt = rb32(d, n, o + 8);
        descs[count].ctype = rb32(d, n, o + 0xc);
        descs[count].frac = d[o + 0x10];
        descs[count].stride = rb16(d, n, o + 0x12);
        descs[count].base = rbase(d, n, o + 0x14);
        if (descs[count].attr == GX_VA_NULL) {
            break;
        }
        count++;
    }
    *out_count = count;
}

/*
 * Reads one vertex from the display list stream at *cursor.  Every descriptor
 * is consumed in order so the cursor lands exactly on the next vertex.
 */
static int read_vertex(const uint8_t *d, size_t n, const RawDesc *descs,
                       size_t desc_count, size_t *cursor, size_t end,
                       RawVertex *out)
{
    size_t a;
    size_t cur = *cursor;
    memset(out, 0, sizeof(*out));
    for (a = 0; a < desc_count; ++a) {
        const RawDesc *desc = &descs[a];
        const uint8_t *source = NULL;
        size_t offset = 0;
        size_t comps;
        size_t i;
        int is_color = (desc->attr == GX_VA_CLR0 || desc->attr == GX_VA_CLR1);
        size_t element_size = is_color
                                  ? color_attribute_size(desc->ctype)
                                  : attr_comp_count(desc->attr, desc->cnt) *
                                        ctype_size(desc->ctype);
        if (desc->type == GX_DIRECT) {
            /* Matrix indices are always a single byte in GX, even though the
             * descriptor records F32 as the component type. */
            size_t bytes = (desc->attr <= 8) ? 1 : element_size;
            if (cur + bytes > end) {
                return 0;
            }
            source = d + cur;
            offset = 0;
            cur += bytes;
            comps = desc->attr <= 8 ? 1 : attr_comp_count(desc->attr, desc->cnt);
        } else {
            size_t index;
            size_t bytes = ctype_size(desc->ctype);
            if (desc->type == GX_INDEX8) {
                if (cur + 1 > end) {
                    return 0;
                }
                index = d[cur++];
            } else if (desc->type == GX_INDEX16) {
                if (cur + 2 > end) {
                    return 0;
                }
                index = rb16(d, n, cur);
                cur += 2;
            } else {
                continue;
            }
            comps = attr_comp_count(desc->attr, desc->cnt);
            if (desc->base != SIZE_MAX && desc->stride != 0) {
                size_t element = desc->base + index * desc->stride;
                if (element + (is_color ? element_size : comps * bytes) <= n) {
                    source = d;
                    offset = element;
                }
            }
        }
        if (source == NULL) {
            continue;
        }
        if (desc->attr == GX_VA_PNMTXIDX) {
            out->matrix = source[offset];
        } else if (desc->attr == GX_VA_POS) {
            for (i = 0; i < comps && i < 3; ++i) {
                out->pos[i] = decode_comp(desc->ctype, desc->frac, source, n,
                                          offset + i * ctype_size(desc->ctype));
            }
            out->has_pos = 1;
        } else if (desc->attr == GX_VA_NRM) {
            for (i = 0; i < 3; ++i) {
                out->nrm[i] = decode_comp(desc->ctype, desc->frac, source, n,
                                          offset + i * ctype_size(desc->ctype));
            }
            out->has_nrm = 1;
        } else if (desc->attr == GX_VA_TEX0) {
            size_t step = ctype_size(desc->ctype);
            for (i = 0; i < comps && i < 2; ++i) {
                out->uv[i] = decode_comp(desc->ctype, desc->frac, source, n,
                                         offset + i * step);
            }
            out->has_uv = 1;
        } else if (desc->attr == GX_VA_TEX1) {
            size_t step = ctype_size(desc->ctype);
            for (i = 0; i < comps && i < 2; ++i) {
                out->uv2[i] = decode_comp(desc->ctype, desc->frac, source, n,
                                          offset + i * step);
            }
            out->has_uv2 = 1;
        } else if (is_color) {
            if (!out->has_color) {
                decode_color(desc->ctype, source, n, offset, out->color);
                out->has_color = 1;
            }
        }
    }
    *cursor = cur;
    return out->has_pos;
}

static uint8_t expand4(unsigned int v) { return (uint8_t) ((v << 4) | v); }
static uint8_t expand5(unsigned int v) { return (uint8_t) ((v * 255u) / 31u); }
static uint8_t expand6(unsigned int v) { return (uint8_t) ((v * 255u) / 63u); }

static void decode_palette_entry(uint16_t raw, uint32_t format, uint8_t out[4])
{
    if (format == 0) { /* IA8 */
        out[0] = out[1] = out[2] = (uint8_t) (raw >> 8);
        out[3] = (uint8_t) (raw & 0xFF);
    } else if (format == 1) { /* RGB565 */
        out[0] = expand5((raw >> 11) & 31);
        out[1] = expand6((raw >> 5) & 63);
        out[2] = expand5(raw & 31);
        out[3] = 255;
    } else if (format == 2) { /* RGB5A3 */
        if (raw & 0x8000) {
            out[0] = expand5((raw >> 10) & 31);
            out[1] = expand5((raw >> 5) & 31);
            out[2] = expand5(raw & 31);
            out[3] = 255;
        } else {
            out[0] = expand4((raw >> 8) & 15);
            out[1] = expand4((raw >> 4) & 15);
            out[2] = expand4(raw & 15);
            out[3] = (uint8_t) ((((raw >> 12) & 7) * 255u) / 7u);
        }
    } else {
        out[0] = out[1] = out[2] = 255;
        out[3] = (raw & 0xFF) ? 255 : 0;
    }
}

/* Decodes an embedded GX texture once and returns its index, or -1. */
static int find_or_add_texture(HsdModel *m, const uint8_t *d, size_t n,
                               size_t texdesc)
{
    size_t imagedesc;
    size_t tlutdesc;
    uint32_t palette_value = 0;
    if (texdesc == SIZE_MAX || !range_ok(texdesc, 0x60, n)) {
        return -1;
    }
    imagedesc = rptr(d, n, texdesc + 0x4c);
    tlutdesc = rptr(d, n, texdesc + 0x50);
    if (tlutdesc != SIZE_MAX && range_ok(tlutdesc, 0x10, n)) {
        palette_value = rb32(d, n, tlutdesc);
    }
    uint32_t image_value;
    uint32_t format;
    size_t image;
    uint16_t width;
    uint16_t height;
    size_t i;
    uint8_t *rgba = NULL;
    char error[64];
    if (imagedesc == SIZE_MAX || !range_ok(imagedesc, 0x18, n)) {
        return -1;
    }
    width = rb16(d, n, imagedesc + 4);
    height = rb16(d, n, imagedesc + 6);
    format = rb32(d, n, imagedesc + 8);
    image_value = rb32(d, n, imagedesc + 0);
    if (image_value == 0 || image_value > n - HSD_DATA_BASE || width == 0 ||
        height == 0 || width > 1024 || height > 1024) {
        return -1;
    }
    image = (size_t) image_value + HSD_DATA_BASE;
    for (i = 0; i < m->texture_count; ++i) {
        HsdTexture *tex = &m->textures[i];
        if (tex->source_offset == image_value && tex->format == format &&
            tex->palette_offset == palette_value) {
            return (int) i;
        }
    }
    if (m->texture_count >= HSD_MAX_TEXTURES) {
        return -1;
    }
    {
        int decoded;
        if ((format == 8 || format == 9) &&
            palette_value != 0 && palette_value <= n - HSD_DATA_BASE &&
            tlutdesc != SIZE_MAX && range_ok(tlutdesc, 0x10, n)) {
            uint32_t palette_format = rb32(d, n, tlutdesc + 4);
            uint16_t palette_entries = rb16(d, n, tlutdesc + 0xc);
            size_t palette = (size_t) palette_value + HSD_DATA_BASE;
            if (palette_entries > 0 && palette_entries <= 512 &&
                range_ok(palette, (size_t) palette_entries * 2, n)) {
                uint8_t *palette_rgba =
                    malloc((size_t) palette_entries * 4);
                if (palette_rgba != NULL) {
                    size_t pi;
                    for (pi = 0; pi < palette_entries; ++pi) {
                        decode_palette_entry(rb16(d, n, palette + pi * 2),
                                             palette_format,
                                             &palette_rgba[pi * 4]);
                    }
                    decoded = gx_texture_decode_ci(
                        d + image, n - image, width, height, (int) format,
                        palette_rgba, palette_entries, &rgba, error,
                        sizeof(error));
                    free(palette_rgba);
                } else {
                    decoded = -1;
                }
            } else {
                decoded = -1;
            }
        } else {
            decoded = gx_texture_decode(d + image, n - image, width, height,
                                          (int) format, &rgba, error,
                                          sizeof(error));
        }
        if (decoded != 0) {
            /* Unsupported formats are recorded so the viewer can report them;
             * rgba stays NULL and they render untextured. */
            rgba = NULL;
        }
    }
    m->textures[m->texture_count].rgba = rgba;
    m->textures[m->texture_count].width = width;
    m->textures[m->texture_count].height = height;
    m->textures[m->texture_count].source_offset = image_value;
    m->textures[m->texture_count].palette_offset = palette_value;
    m->textures[m->texture_count].format = format;
    m->textures[m->texture_count].mipmap = (uint8_t) rb32(d, n, imagedesc + 0xc);
    m->texture_count++;
    return (int) (m->texture_count - 1);
}

static void emit(HsdModel *m, const RawVertex *v, int texture)
{
    HsdVertex *out;
    size_t index;
    if (m->vertex_count >= m->vertex_capacity || !v->has_pos) {
        return;
    }
    index = m->vertex_count++;
    out = &m->vertices[index];
    out->position[0] = v->pos[0];
    out->position[1] = v->pos[1];
    out->position[2] = v->pos[2];
    if (v->has_nrm) {
        memcpy(out->normal, v->nrm, sizeof(out->normal));
    } else {
        out->normal[0] = 0.0f;
        out->normal[1] = 1.0f;
        out->normal[2] = 0.0f;
    }
    if (v->has_uv) {
        out->uv[0] = v->uv[0];
        out->uv[1] = v->uv[1];
    } else {
        out->uv[0] = 0.0f;
        out->uv[1] = 0.0f;
    }
    if (v->has_uv2) {
        out->uv2[0] = v->uv2[0];
        out->uv2[1] = v->uv2[1];
    } else {
        out->uv2[0] = out->uv[0];
        out->uv2[1] = out->uv[1];
    }
    if (v->has_color) {
        memcpy(out->color, v->color, sizeof(out->color));
    } else {
        out->color[0] = 205;
        out->color[1] = 145;
        out->color[2] = 70;
        out->color[3] = 255;
    }
    out->texture = (int16_t) texture;
    if (m->raw != NULL) {
        float *raw = &m->raw[index * 6];
        raw[0] = v->pos[0];
        raw[1] = v->pos[1];
        raw[2] = v->pos[2];
        if (v->has_nrm) {
            raw[3] = v->nrm[0];
            raw[4] = v->nrm[1];
            raw[5] = v->nrm[2];
        } else {
            raw[3] = 0.0f;
            raw[4] = 1.0f;
            raw[5] = 0.0f;
        }
    }
    if (m->skin != NULL) {
        m->skin[index] = (uint8_t) (v->skin_sel < 0 ? 255 : v->skin_sel);
    }
}

/*
 * HSD_PObjDesc.u.envelope_p is an array of group pointers (NULL terminated),
 * each group an array of {joint, weight} pairs terminated by joint == 0.
 * A group whose first weight >= 1 is rigid; the rest are blended influences.
 */
static void load_env_groups(const uint8_t *d, size_t n, size_t po,
                            const HsdModel *m,
                            HsdEnvGroup groups[HSD_MAX_ENV_GROUPS],
                            size_t *out_group_count)
{
    uint32_t env_value = rb32(d, n, po + 0x14);
    size_t env;
    size_t i;
    *out_group_count = 0;
    if (env_value == 0 || env_value > n - HSD_DATA_BASE) {
        return;
    }
    env = (size_t) env_value + HSD_DATA_BASE;
    for (i = 0; i < HSD_MAX_ENV_GROUPS; ++i) {
        uint32_t group_value;
        size_t group;
        HsdEnvGroup *g = &groups[i];
        size_t e;
        if (!range_ok(env + i * 4, 4, n)) {
            break;
        }
        group_value = rb32(d, n, env + i * 4);
        if (group_value == 0 || group_value > n - HSD_DATA_BASE) {
            break;
        }
        group = (size_t) group_value + HSD_DATA_BASE;
        memset(g, 0, sizeof(*g));
        for (e = 0; e < HSD_MAX_ENV_INFLUENCES; ++e) {
            uint32_t joint_value;
            float weight;
            int index;
            if (!range_ok(group + e * 8, 8, n)) {
                break;
            }
            joint_value = rb32(d, n, group + e * 8);
            if (joint_value == 0) {
                break;
            }
            weight = rf32(d, n, group + e * 8 + 4);
            index = joint_index_of(m, (size_t) joint_value + HSD_DATA_BASE);
            if (index < 0) {
                continue;
            }
            if (e == 0) {
                g->rigid = weight >= 1.0f;
            }
            g->joints[g->count] = (int16_t) index;
            g->weights[g->count] = weight;
            g->count++;
        }
        *out_group_count = i + 1;
    }
}

/*
 * HSD's MakeTextureMtx (tobj.c): texture matrix = S * R * T where
 *   scale.x = repeat_s / tobj.scale.x, scale.y = repeat_t / tobj.scale.y,
 *   trans.x = -translate.x,
 *   trans.y = -(translate.y + (wrap_t == GX_MIRROR ? 1/(repeat_t/scale.y) : 0)),
 * and R comes from the Euler rotate (with rot.z negated).  This is what makes
 * half textures with GX_MIRROR (Mario's cap "M") mirror into a whole logo.
 */
static void make_texture_mtx(const uint8_t *d, size_t n, size_t td,
                             float out[16])
{
    float sx;
    float sy;
    float sz;
    float tx;
    float ty;
    float rot[3];
    unsigned int repeat_s;
    unsigned int repeat_t;
    uint32_t wrap_t;
    float scale[3];
    float trans[3];
    float sin_x;
    float cos_x;
    float sin_y;
    float cos_y;
    float sin_z;
    float cos_z;
    float m[4][4];
    int r;
    int c;
    for (r = 0; r < 4; ++r) {
        for (c = 0; c < 4; ++c) {
            out[c * 4 + r] = (r == c) ? 1.0f : 0.0f;
        }
    }
    if (td == SIZE_MAX || !range_ok(td, 0x40, n)) {
        return;
    }
    sx = rf32(d, n, td + 0x1c);
    sy = rf32(d, n, td + 0x20);
    sz = rf32(d, n, td + 0x24);
    tx = rf32(d, n, td + 0x28);
    ty = rf32(d, n, td + 0x2c);
    rot[0] = rf32(d, n, td + 0x10);
    rot[1] = rf32(d, n, td + 0x14);
    rot[2] = -rf32(d, n, td + 0x18);
    repeat_s = d[td + 0x3c];
    repeat_t = d[td + 0x3d];
    wrap_t = rb32(d, n, td + 0x38);
    scale[0] = fabsf(sx) < 1e-6f ? 0.0f : (float) repeat_s / sx;
    scale[1] = fabsf(sy) < 1e-6f ? 0.0f : (float) repeat_t / sy;
    scale[2] = sz;
    trans[0] = -tx;
    trans[1] = -(ty + (wrap_t == 2 && fabsf(sy) > 1e-6f
                           ? 1.0f / ((float) repeat_t / sy)
                           : 0.0f));
    trans[2] = rf32(d, n, td + 0x30);
    /* R * T, then scale by S. */
    sin_x = sinf(rot[0]);
    cos_x = cosf(rot[0]);
    sin_y = sinf(rot[1]);
    cos_y = cosf(rot[1]);
    sin_z = sinf(rot[2]);
    cos_z = cosf(rot[2]);
    m[0][0] = cos_y * cos_z;
    m[1][0] = cos_y * sin_z;
    m[2][0] = -sin_y;
    m[0][1] = (cos_z * (sin_x * sin_y)) - (cos_x * sin_z);
    m[1][1] = (sin_z * (sin_x * sin_y)) + (cos_x * cos_z);
    m[2][1] = sin_x * cos_y;
    m[0][2] = (cos_z * (cos_x * sin_y)) + (sin_x * sin_z);
    m[1][2] = (sin_z * (cos_x * sin_y)) - (sin_x * cos_z);
    m[2][2] = cos_x * cos_y;
    for (r = 0; r < 3; ++r) {
        float t = m[r][0] * trans[0] + m[r][1] * trans[1] + m[r][2] * trans[2];
        m[r][0] *= scale[0];
        m[r][1] *= scale[1];
        m[r][2] *= scale[2];
        m[r][3] = t * scale[r];
    }
    /* GL consumes column-major; HSD matrices transform like column vectors. */
    for (r = 0; r < 4; ++r) {
        for (c = 0; c < 4; ++c) {
            out[c * 4 + r] = (r < 3 && c < 4) ? m[r][c] : (r == 3 && c == 3 ? 1.0f : 0.0f);
        }
    }
}

/*
 * HSD_MObjDesc + HSD_Material + HSD_MObjDesc.pedesc + the TObjDesc chain.
 * Offsets mirror mobj.h/tobj.h: rendermode +4, texdesc +8, mat +0xC,
 * pedesc +0x14; TObjDesc id +8, src +0xC, wrap +0x34/+0x38, flags +0x40,
 * blending +0x44, imagedesc +0x4C, tlutdesc +0x50, tev +0x58.
 * MObjLoad forces RENDER_TOON on every material (mobj.c:158).
 */
static void parse_material(HsdModel *m, const uint8_t *d, size_t n,
                           size_t mobj, HsdMaterial *out)
{
    size_t mat;
    size_t texdesc;
    size_t td;
    int i;
    memset(out, 0, sizeof(*out));
    out->alpha = 1.0f;
    if (mobj == SIZE_MAX || !range_ok(mobj, 0x18, n)) {
        return;
    }
    out->rendermode = rb32(d, n, mobj + 4) | 0x1000u; /* RENDER_TOON */
    texdesc = rptr(d, n, mobj + 8);
    mat = rptr(d, n, mobj + 0xc);
    if (mat != SIZE_MAX && range_ok(mat, 0x14, n)) {
        memcpy(out->ambient, d + mat, 4);
        memcpy(out->diffuse, d + mat + 4, 4);
        memcpy(out->specular, d + mat + 8, 4);
        out->alpha = rf32(d, n, mat + 0xc);
        out->shininess = rf32(d, n, mat + 0x10);
    }
    {
        size_t pe = rptr(d, n, mobj + 0x14);
        if (pe != SIZE_MAX && range_ok(pe, 12, n)) {
            out->pe_present = 1;
            out->pe_flags = d[pe + 0];
            out->pe_ref0 = d[pe + 1];
            out->pe_ref1 = d[pe + 2];
            out->pe_dst_alpha = d[pe + 3];
            out->pe_type = d[pe + 4];
            out->pe_src_factor = d[pe + 5];
            out->pe_dst_factor = d[pe + 6];
            out->pe_logic_op = d[pe + 7];
            out->pe_z_comp = d[pe + 8];
            out->pe_alpha_comp0 = d[pe + 9];
            out->pe_alpha_op = d[pe + 10];
            out->pe_alpha_comp1 = d[pe + 11];
        }
    }
    for (i = 0, td = texdesc;
         i < HSD_MAX_TOBJS && td != SIZE_MAX && range_ok(td, 0x5c, n);
         ++i)
    {
        HsdTobj *t = &out->tobjs[out->tobj_count];
        size_t tev;
        t->texture = (int16_t) find_or_add_texture(m, d, n, td);
        t->id = (uint8_t) rb32(d, n, td + 8);
        t->src = (uint8_t) rb32(d, n, td + 0xc);
        t->wrap_s = (uint8_t) rb32(d, n, td + 0x34);
        t->wrap_t = (uint8_t) rb32(d, n, td + 0x38);
        t->magfilt = (uint8_t) rb32(d, n, td + 0x48);
        t->flags = rb32(d, n, td + 0x40);
        t->blending = rf32(d, n, td + 0x44);
        /* TObjSetup's default when HSD_TObjDesc.lod (+0x54) is NULL. */
        t->minfilt = 5; /* GX_LIN_MIP_LIN */
        {
            size_t lod = rptr(d, n, td + 0x54);
            if (lod != SIZE_MAX && range_ok(lod, 12, n)) {
                t->minfilt = (uint8_t) rb32(d, n, lod + 0);
                t->lod_bias = rf32(d, n, lod + 4);
                t->bias_clamp = d[lod + 8];
                t->edge_lod = d[lod + 9];
                t->anisotropy = d[lod + 10];
            }
        }
        tev = rptr(d, n, td + 0x58);
        if (tev != SIZE_MAX && range_ok(tev, 0x28, n)) {
            t->has_tev = 1;
            t->tev_color_op = d[tev + 0];
            t->tev_alpha_op = d[tev + 1];
            t->tev_color_bias = d[tev + 2];
            t->tev_alpha_bias = d[tev + 3];
            t->tev_color_scale = d[tev + 4];
            t->tev_alpha_scale = d[tev + 5];
            t->tev_color_clamp = d[tev + 6];
            t->tev_alpha_clamp = d[tev + 7];
            t->tev_color_a = d[tev + 8];
            t->tev_color_b = d[tev + 9];
            t->tev_color_c = d[tev + 10];
            t->tev_color_d = d[tev + 11];
            t->tev_alpha_a = d[tev + 12];
            t->tev_alpha_b = d[tev + 13];
            t->tev_alpha_c = d[tev + 14];
            t->tev_alpha_d = d[tev + 15];
            memcpy(t->tev_konst, d + tev + 16, 4);
            memcpy(t->tev_tev0, d + tev + 20, 4);
            memcpy(t->tev_tev1, d + tev + 24, 4);
            t->tev_active = rb32(d, n, tev + 28);
        }
        out->tobj_count++;
        td = rptr(d, n, td + 4);
    }

    /* HSD_SetupChannelMode(arg0 & 7): case 4 lights the raster through the
     * diffuse channel; cases 2 and default leave the channel disabled. */
    out->channel_lit = (out->rendermode & 7u) == 4u;
    out->initial_ras = (out->rendermode & 0x2u) != 0;  /* RENDER_VERTEX */
    out->diffuse_mul = (out->rendermode & 0x4u) != 0;  /* RENDER_DIFFUSE */
    out->specular_tev = (out->rendermode & 0x8u) != 0; /* RENDER_SPECULAR */
    out->z_enable = 1;
    out->z_func = (out->rendermode & 0x08000000u) ? 7 : 3; /* ALWAYS / LEQUAL */
    out->z_update = (out->rendermode & 0x20000000u) == 0;
    out->blend = (out->rendermode & 0x40000000u) ? 1 : 0; /* XLU: GX_BM_BLEND */
    out->blend_src = 4; /* GX_BL_SRCALPHA */
    out->blend_dst = 5; /* GX_BL_INVSRCALPHA */
    out->blend_op = 0;
    if (out->blend && out->z_update) {
        /* HSD_SetupPEMode default: discard alpha == 0 (GX_GREATER, ref 0). */
        out->alpha_test = 1;
        out->alpha_comp0 = 4;
        out->alpha_ref0 = 0;
        out->alpha_op = 0;
        out->alpha_comp1 = 4;
        out->alpha_ref1 = 0;
    }
    if (out->pe_present) {
        out->blend = out->pe_type;
        out->blend_src = out->pe_src_factor;
        out->blend_dst = out->pe_dst_factor;
        out->blend_op = out->pe_logic_op;
        out->z_enable = (out->pe_flags & 0x10) != 0;
        out->z_func = out->pe_z_comp;
        out->z_update = (out->pe_flags & 0x20) != 0;
        out->alpha_test = 1;
        out->alpha_comp0 = out->pe_alpha_comp0;
        out->alpha_ref0 = out->pe_ref0;
        out->alpha_op = out->pe_alpha_op;
        out->alpha_comp1 = out->pe_alpha_comp1;
        out->alpha_ref1 = out->pe_ref1;
    }
}

static void parse_pobj(HsdModel *m, const uint8_t *d, size_t n, size_t po,
                       int current_joint, size_t dobj_index, uint8_t color[4],
                       int texture, uint8_t wrap_s, uint8_t wrap_t,
                       uint32_t rendermode, const float texmtx[16],
                       const float texmtx2[16],
                       const HsdMaterial *material)
{
    RawDesc descs[32];
    HsdEnvGroup groups[HSD_MAX_ENV_GROUPS];
    size_t group_count = 0;
    size_t desc_count = 0;
    size_t first_vertex = m->vertex_count;
    size_t vo;
    size_t dl;
    size_t limit;
    size_t cur;
    uint16_t display_blocks;
    uint16_t pobj_flags;
    unsigned int pobj_type;
    unsigned int cull_mode;
    int shared_joint = -1;
    if (!range_ok(po, 0x18, n)) {
        return;
    }
    vo = rptr(d, n, po + 8);
    display_blocks = rb16(d, n, po + 0xe);
    dl = rptr(d, n, po + 0x10);
    if (vo == SIZE_MAX || dl == SIZE_MAX || display_blocks == 0) {
        return;
    }
    limit = (size_t) display_blocks * 32u;
    if (limit > HSD_MAX_DISPLAY_BYTES || !range_ok(dl, limit, n)) {
        return;
    }
    read_descs(d, n, vo, descs, 32, &desc_count);
    if (desc_count == 0) {
        return;
    }
    pobj_flags = rb16(d, n, po + 0xc);
    pobj_type = (pobj_flags >> 12) & 3;
    cull_mode = (pobj_flags & 0xc000u) >> 14;
    if (pobj_type < 3) {
        m->pobj_type_count[pobj_type]++;
    }
    if (pobj_type == 2) {
        load_env_groups(d, n, po, m, groups, &group_count);
    } else if (pobj_type == 0) {
        /* POBJ_SKIN: a non-null joint means two matrix slots (GX_PNMTX0 =
         * current joint, GX_PNMTX1 = this joint), selected per vertex. */
        size_t shared = rptr(d, n, po + 0x14);
        if (shared != SIZE_MAX) {
            shared_joint = joint_index_of(m, shared);
        }
    }
    cur = dl;
    while (cur + 3 <= dl + limit) {
        uint8_t op = d[cur] & 0xf8;
        uint16_t count = rb16(d, n, cur + 1);
        RawVertex verts[HSD_MAX_PRIM_VERTS];
        uint16_t got;
        uint16_t i;
        int complete = 1;
        cur += 3;
        if (op == 0) {
            break;
        }
        if (count > HSD_MAX_PRIM_VERTS) {
            count = HSD_MAX_PRIM_VERTS;
        }
        for (i = 0; i < count; ++i) {
            if (!read_vertex(d, n, descs, desc_count, &cur, dl + limit,
                             &verts[i])) {
                complete = 0;
                break;
            }
            if (!verts[i].has_color) {
                memcpy(verts[i].color, color, 4);
            }
            /* Record which matrix slot skins this vertex; the transform is
             * applied by hsd_model_pose_apply so it can follow animation. */
            if (pobj_type == 2) {
                size_t group = (size_t) (verts[i].matrix / 3);
                verts[i].skin_sel = group < group_count ? (int) group : -1;
            } else if (pobj_type == 0) {
                verts[i].skin_sel = verts[i].matrix; /* 0 slot 0, 3 slot 1 */
            } else {
                verts[i].skin_sel = 0;
            }
        }
        if (!complete) {
            m->skipped_primitives++;
            break;
        }
        got = i;
        if (op == GX_TRIANGLES || op == GX_QUADS) {
            uint16_t stride = (op == GX_TRIANGLES) ? 3 : 4;
            if (got >= stride) {
                uint16_t base;
                for (base = 0; base + stride <= got; base += stride) {
                    if (op == GX_TRIANGLES) {
                        emit(m, &verts[base], texture);
                        emit(m, &verts[base + 1], texture);
                        emit(m, &verts[base + 2], texture);
                        m->triangle_count++;
                    } else {
                        emit(m, &verts[base], texture);
                        emit(m, &verts[base + 1], texture);
                        emit(m, &verts[base + 2], texture);
                        emit(m, &verts[base], texture);
                        emit(m, &verts[base + 2], texture);
                        emit(m, &verts[base + 3], texture);
                        m->triangle_count += 2;
                    }
                }
            }
        } else if (op == GX_TRIANGLESTRIP || op == GX_TRIANGLEFAN) {
            if (got >= 3) {
                uint16_t k;
                for (k = 2; k < got; ++k) {
                    uint16_t a;
                    uint16_t b;
                    if (op == GX_TRIANGLESTRIP) {
                        a = (k & 1) ? k - 1 : k - 2;
                        b = (k & 1) ? k - 2 : k - 1;
                    } else {
                        a = 0;
                        b = k - 1;
                    }
                    emit(m, &verts[a], texture);
                    emit(m, &verts[b], texture);
                    emit(m, &verts[k], texture);
                    m->triangle_count++;
                }
            }
        } else {
            m->skipped_primitives++;
        }
        if (cur <= dl) {
            break;
        }
    }
    if (m->vertex_count > first_vertex && m->batch_count < HSD_MAX_BATCHES) {
        HsdBatch *batch = &m->batches[m->batch_count];
        HsdBatchSkin *skin = &m->batch_skin[m->batch_count];
        batch->first_vertex = first_vertex;
        batch->vertex_count = m->vertex_count - first_vertex;
        batch->object_index = m->object_count ? m->object_count - 1 : 0;
        batch->dobj_index = dobj_index;
        batch->texture = (int16_t) texture;
        batch->cull_mode = (uint8_t) cull_mode;
        batch->rendermode = rendermode;
        memcpy(batch->texmtx, texmtx, sizeof(batch->texmtx));
        memcpy(batch->texmtx2, texmtx2, sizeof(batch->texmtx2));
        batch->wrap_s = wrap_s;
        batch->wrap_t = wrap_t;
        batch->translucent =
            (color[3] != 255) ? 1 : 0;
        if (material != NULL) {
            batch->material = *material;
        }
        memset(skin, 0, sizeof(*skin));
        skin->pobj_type = (uint8_t) pobj_type;
        skin->current_joint = (int16_t) current_joint;
        skin->shared_joint = (int16_t) shared_joint;
        skin->group_count = (uint8_t) group_count;
        memcpy(skin->groups, groups,
               group_count * sizeof(HsdEnvGroup));
        m->batch_count++;
    }
}

static void walk_joint(HsdModel *m, const uint8_t *d, size_t n, size_t jo,
                       int depth)
{
    size_t dobj;
    size_t child;
    size_t next;
    if (jo == SIZE_MAX || depth > HSD_MAX_JOINT_DEPTH || !range_ok(jo, 0x40, n)) {
        return;
    }
    child = rptr(d, n, jo + 8);
    next = rptr(d, n, jo + 12);
    dobj = rptr(d, n, jo + 16);
    if (rb32(d, n, jo + 4) & HSD_FLAG_HIDDEN) {
        dobj = SIZE_MAX; /* HSD_JObjDispDObj skips hidden joints */
    }
    {
        int m_index = joint_index_of(m, jo);
        while (dobj != SIZE_MAX && range_ok(dobj, 0x10, n)) {
            size_t dobj_index = m->dobj_count < HSD_MAX_DOBJS ? m->dobj_count : HSD_MAX_DOBJS;
            size_t pobj = rptr(d, n, dobj + 12);
            size_t mobj = rptr(d, n, dobj + 8);
            uint8_t color[4] = { 205, 145, 70, 255 };
            uint8_t wrap_s = 0;
            uint8_t wrap_t = 0;
            uint32_t mobj_rendermode = 0;
            int texture = -1;
            float texmtx[16];
            float texmtx2[16];
            HsdMaterial material;
            memset(&material, 0, sizeof(material));
            make_texture_mtx(d, n, SIZE_MAX, texmtx);
            make_texture_mtx(d, n, SIZE_MAX, texmtx2);
            if (mobj != SIZE_MAX && range_ok(mobj, 0x18, n)) {
                size_t texdesc = rptr(d, n, mobj + 8);
                mobj_rendermode = rb32(d, n, mobj + 4);
                parse_material(m, d, n, mobj, &material);
                make_texture_mtx(d, n, texdesc, texmtx);
                if (texdesc != SIZE_MAX && range_ok(texdesc, 0x5c, n)) {
                    size_t texdesc2 = rptr(d, n, texdesc + 4);
                    wrap_s = (uint8_t) rb32(d, n, texdesc + 0x34);
                    wrap_t = (uint8_t) rb32(d, n, texdesc + 0x38);
                    texture = find_or_add_texture(m, d, n, texdesc);
                    make_texture_mtx(d, n, texdesc2, texmtx2);
                }
            }
            memcpy(color, material.diffuse, 4);
            if (color[0] == 0 && color[1] == 0 && color[2] == 0 &&
                color[3] == 0)
            {
                color[0] = 205;
                color[1] = 145;
                color[2] = 70;
                color[3] = 255;
            }
            /* RENDER_XLU (1<<30) marks a blended material; opaque parts ignore
             * the material alpha so they never render see-through. */
            if ((mobj_rendermode & 0x40000000u) == 0) {
                color[3] = 255;
            }
            while (pobj != SIZE_MAX && range_ok(pobj, 0x18, n)) {
                m->object_count++;
                parse_pobj(m, d, n, pobj, m_index, dobj_index, color, texture,
                           wrap_s, wrap_t, mobj_rendermode, texmtx, texmtx2,
                           &material);
                pobj = rptr(d, n, pobj + 4);
            }
            if (m->dobj_count < HSD_MAX_DOBJS) {
                m->dobj_count++;
            }
            dobj = rptr(d, n, dobj + 4);
        }
    }
    walk_joint(m, d, n, child, depth + 1);
    walk_joint(m, d, n, next, depth);
}


/*
 * Picks a root joint from the archive's public symbol table.  Model archives
 * expose "<name>_joint" as the first symbol; prefer that over material or
 * animation joints.
 */
static size_t find_root_symbol(const uint8_t *d, size_t n)
{
    uint32_t data_size = rb32(d, n, 4);
    uint32_t nb_reloc = rb32(d, n, 8);
    uint32_t nb_public = rb32(d, n, 0xc);
    uint32_t nb_extern = rb32(d, n, 0x10);
    size_t public_table = HSD_DATA_BASE + (size_t) data_size + (size_t) nb_reloc * 4;
    size_t strings;
    size_t first = SIZE_MAX;
    size_t i;
    if (nb_public == 0 || nb_public > 4096 ||
        !range_ok(public_table, (size_t) nb_public * 8, n)) {
        return SIZE_MAX;
    }
    strings = public_table + (size_t) nb_public * 8 + (size_t) nb_extern * 8;
    if (strings >= n) {
        return SIZE_MAX;
    }
    for (i = 0; i < nb_public; ++i) {
        uint32_t data_offset = rb32(d, n, public_table + i * 8);
        uint32_t symbol_offset = rb32(d, n, public_table + i * 8 + 4);
        size_t name = strings + symbol_offset;
        size_t length = 0;
        int has_matanim = 0;
        int ends_joint = 0;
        if (name >= n) {
            continue;
        }
        while (name + length < n && d[name + length] != 0) {
            length++;
        }
        if (length >= 6 && memcmp(d + name + length - 6, "_joint", 6) == 0) {
            ends_joint = 1;
        }
        if (length >= 7) {
            size_t k;
            for (k = 0; k + 7 <= length; ++k) {
                if (memcmp(d + name + k, "matanim", 7) == 0) {
                    has_matanim = 1;
                    break;
                }
            }
        }
        if (data_offset > n - HSD_DATA_BASE) {
            continue;
        }
        if (first == SIZE_MAX) {
            first = (size_t) data_offset + HSD_DATA_BASE;
        }
        if (ends_joint && !has_matanim) {
            return (size_t) data_offset + HSD_DATA_BASE;
        }
    }
    return first;
}

void hsd_model_init(HsdModel *m, HsdVertex *storage, size_t capacity)
{
    size_t i;
    memset(m, 0, sizeof(*m));
    m->vertices = storage;
    m->vertex_capacity = capacity;
    m->model_scale = 1.0f;
    m->model_scale_x = 0.0f;
    for (i = 0; i < 3; ++i) {
        m->bounds_min[i] = 1e30f;
        m->bounds_max[i] = -1e30f;
    }
}

void hsd_model_free(HsdModel *m)
{
    size_t i;
    if (m == NULL) {
        return;
    }
    for (i = 0; i < m->texture_count; ++i) {
        free(m->textures[i].rgba);
        m->textures[i].rgba = NULL;
    }
    m->texture_count = 0;
    free(m->raw);
    m->raw = NULL;
    free(m->skin);
    m->skin = NULL;
}

int hsd_model_batch_visible(const HsdModel *model, size_t batch_index)
{
    size_t dobj;
    if (batch_index >= model->batch_count) {
        return 0;
    }
    dobj = model->batches[batch_index].dobj_index;
    if (dobj >= HSD_MAX_DOBJS) {
        return 1;
    }
    return model->dobj_hidden[dobj] == 0;
}

int hsd_model_batch_pose_visible(const HsdModel *model, size_t batch_index)
{
    int joint;
    if (!hsd_model_batch_visible(model, batch_index)) {
        return 0;
    }
    joint = model->batch_skin[batch_index].current_joint;
    if (joint < 0 || (size_t) joint >= model->joint_count) {
        return 1;
    }
    return model->joints[joint].hidden_dyn == 0;
}

void hsd_model_pose_reset(HsdModel *m)
{
    size_t j;
    for (j = 0; j < m->joint_count; ++j) {
        HsdJoint *joint = &m->joints[j];
        memcpy(joint->rotation, joint->rotation_bind, sizeof(joint->rotation));
        memcpy(joint->scale, joint->scale_bind, sizeof(joint->scale));
        memcpy(joint->position, joint->position_bind,
               sizeof(joint->position));
        joint->hidden_dyn = 0;
    }
}

/* 1 when `joint` is `ancestor` or one of its descendants. */
static int joint_in_subtree(const HsdModel *m, size_t joint, size_t ancestor)
{
    while (joint < m->joint_count) {
        if (joint == ancestor) {
            return 1;
        }
        if (m->joints[joint].parent < 0) {
            return 0;
        }
        joint = (size_t) m->joints[joint].parent;
    }
    return 0;
}

void hsd_model_pose_channel(HsdModel *m, size_t joint, int channel,
                             float value)
{
    HsdJoint *jt;
    if (joint >= m->joint_count) {
        return;
    }
    jt = &m->joints[joint];
    switch (channel) {
    case HSD_A_J_ROTX:
    case HSD_A_J_ROTY:
    case HSD_A_J_ROTZ:
        jt->rotation[channel - HSD_A_J_ROTX] = value;
        break;
    case HSD_A_J_TRAX:
    case HSD_A_J_TRAY:
    case HSD_A_J_TRAZ:
        jt->position[channel - HSD_A_J_TRAX] = value;
        break;
    case HSD_A_J_SCAX:
    case HSD_A_J_SCAY:
    case HSD_A_J_SCAZ:
        /* JObjUpdateFunc clamps near-zero scales to 1e-3. */
        if (fabsf(value) < 1e-3f) {
            value = 1e-3f;
        }
        jt->scale[channel - HSD_A_J_SCAX] = value;
        break;
    case HSD_A_J_NODE:
        jt->hidden_dyn = value > 0.5f ? 0 : 1;
        break;
    case HSD_A_J_BRANCH: {
        size_t j;
        for (j = 0; j < m->joint_count; ++j) {
            if (joint_in_subtree(m, j, joint)) {
                m->joints[j].hidden_dyn = value > 0.5f ? 0 : 1;
            }
        }
        break;
    }
    default:
        /* HSD_A_J_PATH and the SETBYTE/SETFLOAT event channels are not
         * consumed by the port yet. */
        break;
    }
}

/* Normal transform: inverse-transpose of the 3x3 block, then normalize. */
static void mtx_transform_normal_it(const float *m, const float in[3],
                                    float out[3])
{
    float inv[3][4];
    float x;
    float y;
    float z;
    float length;
    if (!mtx_invert(m, &inv[0][0])) {
        mtx_transform_normal(m, in, out);
        return;
    }
    x = inv[0][0] * in[0] + inv[1][0] * in[1] + inv[2][0] * in[2];
    y = inv[0][1] * in[0] + inv[1][1] * in[1] + inv[2][1] * in[2];
    z = inv[0][2] * in[0] + inv[1][2] * in[1] + inv[2][2] * in[2];
    length = sqrtf(x * x + y * y + z * z);
    if (length > 1e-8f) {
        x /= length;
        y /= length;
        z /= length;
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

void hsd_model_pose_apply(HsdModel *m)
{
    size_t j;
    size_t b;
    size_t i;
    m->bounds_min[0] = m->bounds_min[1] = m->bounds_min[2] = 1e30f;
    m->bounds_max[0] = m->bounds_max[1] = m->bounds_max[2] = -1e30f;
    if (m->raw == NULL) {
        return;
    }
    /* HSD_JObjMakeMatrix traversal: parents always come first (the joint table
     * is built parent-before-child). */
    for (j = 0; j < m->joint_count; ++j) {
        HsdJoint *jt = &m->joints[j];
        float local[3][4];
        /* x34_scale.z (Mr. Game & Watch's width) overrides X when set. */
        float root_scale[3] = {
            m->model_scale_x > 0.0f ? m->model_scale_x : m->model_scale,
            m->model_scale, m->model_scale
        };
        const float *parent_scale = NULL;
        if (jt->parent >= 0) {
            parent_scale = m->joints[jt->parent].scale_world;
            make_local_mtx(local, jt->scale, jt->rotation, jt->position,
                           parent_scale);
            mtx_concat(&m->joints[jt->parent].world[0][0], &local[0][0],
                       &jt->world[0][0]);
        } else {
            /* Fighter_UpdateModelScale overrides the root joint scale with
             * the per-character model scaling each frame. */
            make_local_mtx(local, root_scale, jt->rotation, jt->position, NULL);
            memcpy(jt->world, local, sizeof(local));
        }
        if (jt->parent >= 0 && (jt->flags & HSD_FLAG_SCL_INHERIT)) {
            memcpy(jt->scale_world, m->joints[jt->parent].scale_world,
                   sizeof(jt->scale_world));
        } else if (jt->parent >= 0) {
            jt->scale_world[0] =
                jt->scale[0] * m->joints[jt->parent].scale_world[0];
            jt->scale_world[1] =
                jt->scale[1] * m->joints[jt->parent].scale_world[1];
            jt->scale_world[2] =
                jt->scale[2] * m->joints[jt->parent].scale_world[2];
        } else {
            memcpy(jt->scale_world, root_scale, sizeof(jt->scale_world));
        }
    }
    for (b = 0; b < m->batch_count; ++b) {
        const HsdBatchSkin *bs = &m->batch_skin[b];
        const HsdBatch *batch = &m->batches[b];
        float right[3][4];
        float group_mtx[HSD_MAX_ENV_GROUPS][3][4];
        float identity[3][4];
        int has_right;
        size_t g;
        if (bs->pobj_type == 2) {
            has_right = batch_right(m, bs->current_joint, right);
        } else {
            has_right = 0;
        }
        mtx_identity(identity);
        for (g = 0; g < HSD_MAX_ENV_GROUPS; ++g) {
            const HsdEnvGroup *eg = &bs->groups[g];
            float base[3][4];
            if (bs->pobj_type != 2 || g >= bs->group_count || eg->count == 0) {
                memcpy(group_mtx[g], identity, sizeof(identity));
                continue;
            }
            if (eg->rigid) {
                memcpy(base, m->joints[eg->joints[0]].world, sizeof(base));
                if (has_right) {
                    float env[3][4];
                    float tmp[3][4];
                    if (!joint_env_mtx(m, eg->joints[0], env)) {
                        mtx_identity(env);
                    }
                    mtx_concat(&base[0][0], &env[0][0], &tmp[0][0]);
                    mtx_concat(&tmp[0][0], &right[0][0], &group_mtx[g][0][0]);
                } else {
                    memcpy(group_mtx[g], base, sizeof(base));
                }
            } else {
                size_t e;
                memset(base, 0, sizeof(base));
                for (e = 0; e < eg->count; ++e) {
                    float env[3][4];
                    float tmp[3][4];
                    float weight = eg->weights[e];
                    if (!joint_env_mtx(m, eg->joints[e], env)) {
                        continue;
                    }
                    mtx_concat(&m->joints[eg->joints[e]].world[0][0],
                               &env[0][0], &tmp[0][0]);
                    base[0][0] += weight * tmp[0][0];
                    base[0][1] += weight * tmp[0][1];
                    base[0][2] += weight * tmp[0][2];
                    base[0][3] += weight * tmp[0][3];
                    base[1][0] += weight * tmp[1][0];
                    base[1][1] += weight * tmp[1][1];
                    base[1][2] += weight * tmp[1][2];
                    base[1][3] += weight * tmp[1][3];
                    base[2][0] += weight * tmp[2][0];
                    base[2][1] += weight * tmp[2][1];
                    base[2][2] += weight * tmp[2][2];
                    base[2][3] += weight * tmp[2][3];
                }
                if (has_right) {
                    mtx_concat(&base[0][0], &right[0][0],
                               &group_mtx[g][0][0]);
                } else {
                    memcpy(group_mtx[g], base, sizeof(base));
                }
            }
        }
        for (i = 0; i < batch->vertex_count; ++i) {
            size_t index = batch->first_vertex + i;
            const float *raw = &m->raw[index * 6];
            const float *matrix;
            float moved[3];
            float normal[3];
            HsdVertex *out = &m->vertices[index];
            uint8_t selector = m->skin[index];
            if (selector == 255) {
                matrix = identity[0];
            } else if (bs->pobj_type == 2) {
                matrix = selector < bs->group_count
                             ? group_mtx[selector < HSD_MAX_ENV_GROUPS
                                             ? selector
                                             : 0][0]
                             : identity[0];
            } else if (bs->pobj_type == 0 && selector == 3 &&
                       bs->shared_joint >= 0) {
                matrix = m->joints[bs->shared_joint].world[0];
            } else if (bs->current_joint >= 0 &&
                       (size_t) bs->current_joint < m->joint_count) {
                matrix = m->joints[bs->current_joint].world[0];
            } else {
                matrix = identity[0];
            }
            mtx_transform_point(matrix, raw, moved);
            out->position[0] = moved[0];
            out->position[1] = moved[1];
            out->position[2] = moved[2];
            mtx_transform_normal_it(matrix, &raw[3], normal);
            out->normal[0] = normal[0];
            out->normal[1] = normal[1];
            out->normal[2] = normal[2];
            for (j = 0; j < 3; ++j) {
                if (out->position[j] < m->bounds_min[j]) {
                    m->bounds_min[j] = out->position[j];
                }
                if (out->position[j] > m->bounds_max[j]) {
                    m->bounds_max[j] = out->position[j];
                }
            }
        }
    }
}

int hsd_model_load(HsdModel *m, const uint8_t *d, size_t n, size_t root_offset,
                    char *err, size_t errn)
{
    size_t root;
    if (err != NULL && errn != 0) {
        err[0] = 0;
    }
    if (m == NULL || d == NULL || n < HSD_DATA_BASE + 0x40) {
        seterr(err, errn, "invalid model buffer");
        return 0;
    }
    m->model_scale = 1.0f;
    m->model_scale_x = 0.0f;
    root = SIZE_MAX;
    if (root_offset != 0 && root_offset <= n - HSD_DATA_BASE) {
        root = root_offset + HSD_DATA_BASE;
    }
    if (root == SIZE_MAX) {
        root = find_root_symbol(d, n);
    }
    if (root == SIZE_MAX || !range_ok(root, 0x40, n)) {
        seterr(err, errn, "no HSD joint root found");
        return 0;
    }
    joint_table_add(d, n, root, -1, m, 0);
    {
        /* Raw skin inputs: 6 floats (position + normal) and one selector byte
         * per vertex, filled while the display lists are parsed. */
        m->raw = calloc(m->vertex_capacity * 6, sizeof(float));
        m->skin = calloc(m->vertex_capacity, sizeof(uint8_t));
        if (m->raw == NULL || m->skin == NULL) {
            seterr(err, errn, "out of memory for skin data");
            return 0;
        }
    }
    {
        size_t ji;
        for (ji = 0; ji < m->joint_count; ++ji) {
            if (m->joints[ji].flags & HSD_FLAG_INSTANCE) {
                m->instance_count++;
            }
        }
    }
    walk_joint(m, d, n, root, 0);
    if (m->triangle_count == 0) {
        seterr(err, errn, "joint graph contained no supported triangles");
        return 0;
    }
    hsd_model_pose_reset(m);
    hsd_model_pose_apply(m);
    return 1;
}

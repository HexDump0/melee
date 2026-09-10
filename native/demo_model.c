#include "demo_model.h"

#include "demo_texture.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Raw HSD model reader for the native demo.
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
    uint8_t color[4];
    int matrix;
    int has_pos;
    int has_nrm;
    int has_uv;
    int has_color;
} RawVertex;

typedef struct JointInfo {
    size_t offset; /* host offset of the HSD_Joint */
    float world[3][4];
    float scale_world[3];
    uint32_t flags;
    int parent; /* index into JointTable, -1 for the root */
} JointInfo;

typedef struct JointTable {
    JointInfo joints[HSD_MAX_JOINTS];
    size_t count;
} JointTable;

typedef struct EnvGroup {
    int rigid;
    float matrix[3][4];
} EnvGroup;

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
static int joint_index_of(const JointTable *table, size_t offset)
{
    size_t i;
    for (i = 0; i < table->count; ++i) {
        if (table->joints[i].offset == offset) {
            return (int) i;
        }
    }
    return -1;
}

/* Nearest ancestor (including self) flagged JOBJ_SKELETON or JOBJ_SKELETON_ROOT. */
static int joint_find_skeleton(const JointTable *table, int index)
{
    while (index >= 0) {
        uint32_t flags = table->joints[index].flags;
        if (flags & (HSD_FLAG_SKELETON | HSD_FLAG_SKELETON_ROOT)) {
            return index;
        }
        index = table->joints[index].parent;
    }
    return -1;
}

/*
 * HSD's _HSD_mkEnvelopeModelNodeMtx, evaluated at bind pose.  Returns 0 when
 * HSD would return NULL (the joint is the skeleton root), otherwise writes the
 * 3x4 `right` matrix.  With this, a PObj hung off any joint is placed by
 * `right` at bind pose, because every envelope group matrix is the identity.
 */
static int joint_right(const JointTable *table, int m_index, float right[3][4])
{
    const JointInfo *m;
    int x;
    if (m_index < 0 || (size_t) m_index >= table->count) {
        return 0;
    }
    m = &table->joints[m_index];
    if (m->flags & HSD_FLAG_SKELETON_ROOT) {
        return 0;
    }
    x = joint_find_skeleton(table, m_index);
    if (x < 0) {
        return 0;
    }
    if (x == m_index) {
        memcpy(right, m->world, sizeof(float) * 12);
    } else if (table->joints[x].flags & HSD_FLAG_SKELETON_ROOT) {
        float inverse[3][4];
        if (!mtx_invert(&table->joints[x].world[0][0], &inverse[0][0])) {
            return 0;
        }
        mtx_concat(&inverse[0][0], &m->world[0][0], &right[0][0]);
    } else {
        /* x.world * inverseBind(x) == identity at bind pose. */
        memcpy(right, m->world, sizeof(float) * 12);
    }
    return 1;
}

/* HSD_MtxSRT: scale, Euler rotation, translation, parent scale correction. */
static void make_local_mtx(float m[3][4], const float scale[3],
                           const float rot[3], const float pos[3],
                           const float *parent_scale)
{
    float sx = scale[0];
    float sy = scale[1];
    float sz = scale[2];
    float vx2 = sx;
    float vx1 = sx;
    float vx = sx;
    float vy2 = sy;
    float vy1 = sy;
    float vy = sy;
    float vz2 = sz;
    float vz1 = sz;
    float vz = sz;
    float sin_x;
    float cos_x;
    float sin_y;
    float cos_y;
    float sin_z;
    float cos_z;
    if (parent_scale != NULL) {
        float t1 = 1.0f / parent_scale[0];
        float t2 = 1.0f / parent_scale[1];
        float t3 = 1.0f / parent_scale[2];
        vy2 *= parent_scale[1] * t1;
        vz2 *= parent_scale[2] * t1;
        vx1 *= parent_scale[0] * t2;
        vz1 *= parent_scale[2] * t2;
        vx *= parent_scale[0] * t3;
        vy *= parent_scale[1] * t3;
    }
    sin_x = sinf(rot[0]);
    cos_x = cosf(rot[0]);
    sin_y = sinf(rot[1]);
    cos_y = cosf(rot[1]);
    sin_z = sinf(rot[2]);
    cos_z = cosf(rot[2]);
    m[0][0] = cos_z * (vx2 * cos_y);
    m[1][0] = sin_z * (vx1 * cos_y);
    m[2][0] = -vx * sin_y;
    m[0][1] = vy2 * ((cos_z * (sin_x * sin_y)) - (cos_x * sin_z));
    m[1][1] = vy1 * ((sin_z * (sin_x * sin_y)) + (cos_x * cos_z));
    m[2][1] = cos_y * (vy * sin_x);
    m[0][2] = vz2 * ((cos_z * (cos_x * sin_y)) + (sin_x * sin_z));
    m[1][2] = vz1 * ((sin_z * (cos_x * sin_y)) - (sin_x * cos_z));
    m[2][2] = cos_y * (vz * cos_x);
    m[0][3] = pos[0];
    m[1][3] = pos[1];
    m[2][3] = pos[2];
}

static void joint_table_add(const uint8_t *d, size_t n, size_t jo, int parent,
                            JointTable *table, int depth)
{
    float rot[3];
    float scale[3];
    float pos[3];
    float local[3][4];
    uint32_t flags;
    JointInfo *info;
    size_t child;
    size_t next;
    size_t i;
    if (jo == SIZE_MAX || depth > HSD_MAX_JOINT_DEPTH ||
        table->count >= HSD_MAX_JOINTS || !range_ok(jo, 0x40, n)) {
        return;
    }
    for (i = 0; i < table->count; ++i) {
        if (table->joints[i].offset == jo) {
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
    info = &table->joints[table->count];
    info->offset = jo;
    info->flags = flags;
    info->parent = parent;
    {
        const float *parent_scale = NULL;
        if (parent >= 0) {
            parent_scale = table->joints[parent].scale_world;
        }
        make_local_mtx(local, scale, rot, pos, parent_scale);
    }
    if (parent >= 0) {
        mtx_concat(&table->joints[parent].world[0][0], &local[0][0],
                   &info->world[0][0]);
    } else {
        memcpy(info->world, local, sizeof(local));
    }
    if (parent >= 0 && (flags & HSD_FLAG_SCL_INHERIT)) {
        memcpy(info->scale_world, table->joints[parent].scale_world,
               sizeof(info->scale_world));
    } else if (parent >= 0) {
        info->scale_world[0] = scale[0] * table->joints[parent].scale_world[0];
        info->scale_world[1] = scale[1] * table->joints[parent].scale_world[1];
        info->scale_world[2] = scale[2] * table->joints[parent].scale_world[2];
    } else {
        memcpy(info->scale_world, scale, sizeof(info->scale_world));
    }
    {
        int index = (int) table->count;
        table->count++;
        child = rptr(d, n, jo + 8);
        while (child != SIZE_MAX) {
            joint_table_add(d, n, child, index, table, depth + 1);
            child = rptr(d, n, child + 12);
        }
        next = rptr(d, n, jo + 12);
        if (next != SIZE_MAX) {
            joint_table_add(d, n, next, parent, table, depth);
        }
    }
}

static const JointInfo *joint_lookup(const JointTable *table, size_t offset)
{
    size_t i;
    for (i = 0; i < table->count; ++i) {
        if (table->joints[i].offset == offset) {
            return &table->joints[i];
        }
    }
    return NULL;
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
        if (desc->type == GX_DIRECT) {
            /* Matrix indices are always a single byte in GX, even though the
             * descriptor records F32 as the component type. */
            size_t bytes = (desc->attr <= 8) ? 1
                                             : attr_comp_count(desc->attr, desc->cnt) *
                                                   ctype_size(desc->ctype);
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
                if (element + comps * bytes <= n) {
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
        } else if (desc->attr == GX_VA_CLR0 || desc->attr == GX_VA_CLR1) {
            if (!out->has_color) {
                size_t step = ctype_size(desc->ctype);
                for (i = 0; i < comps && i < 4; ++i) {
                    float value = decode_comp(desc->ctype, desc->frac, source, n,
                                              offset + i * step);
                    if (value > 1.0f) {
                        value = 1.0f;
                    }
                    if (value < 0.0f) {
                        value = 0.0f;
                    }
                    out->color[i] = (uint8_t) (value * 255.0f + 0.5f);
                }
                if (comps == 3) {
                    out->color[3] = 255;
                }
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
static int find_or_add_texture(DemoModel *m, const uint8_t *d, size_t n,
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
        DemoModelTexture *tex = &m->textures[i];
        if (tex->source_offset == image_value && tex->format == format &&
            tex->palette_offset == palette_value) {
            return (int) i;
        }
    }
    if (m->texture_count >= DEMO_MAX_TEXTURES) {
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
                    decoded = demo_texture_decode_ci(
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
            decoded = demo_texture_decode(d + image, n - image, width, height,
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
    m->texture_count++;
    return (int) (m->texture_count - 1);
}

static void emit(DemoModel *m, const RawVertex *v, int texture)
{
    DemoModelVertex *out;
    size_t k;
    if (m->vertex_count >= m->vertex_capacity || !v->has_pos) {
        return;
    }
    out = &m->vertices[m->vertex_count++];
    memcpy(out->position, v->pos, sizeof(out->position));
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
    if (v->has_color) {
        memcpy(out->color, v->color, sizeof(out->color));
    } else {
        out->color[0] = 205;
        out->color[1] = 145;
        out->color[2] = 70;
        out->color[3] = 255;
    }
    out->texture = (int16_t) texture;
    for (k = 0; k < 3; ++k) {
        if (v->pos[k] < m->bounds_min[k]) {
            m->bounds_min[k] = v->pos[k];
        }
        if (v->pos[k] > m->bounds_max[k]) {
            m->bounds_max[k] = v->pos[k];
        }
    }
}

static void load_env_groups(const uint8_t *d, size_t n, size_t po,
                            const JointTable *table,
                            EnvGroup groups[HSD_MAX_ENV_GROUPS],
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
        uint32_t joint_value;
        float weight;
        if (!range_ok(env + i * 4, 4, n)) {
            break;
        }
        group_value = rb32(d, n, env + i * 4);
        if (group_value == 0 || group_value > n - HSD_DATA_BASE) {
            break;
        }
        group = (size_t) group_value + HSD_DATA_BASE;
        if (!range_ok(group, 8, n)) {
            break;
        }
        joint_value = rb32(d, n, group);
        weight = rf32(d, n, group + 4);
        if (joint_value == 0) {
            break;
        }
        groups[i].rigid = weight >= (1.0f - 1e-6f);
        if (groups[i].rigid) {
            const JointInfo *info =
                joint_lookup(table, joint_value + HSD_DATA_BASE);
            if (info != NULL) {
                memcpy(groups[i].matrix, info->world, sizeof(float) * 12);
            } else {
                mtx_identity(groups[i].matrix);
                groups[i].rigid = 0;
            }
        } else {
            /* Blended groups sum to the identity at bind pose. */
            mtx_identity(groups[i].matrix);
        }
        *out_group_count = i + 1;
    }
}

static void parse_pobj(DemoModel *m, const uint8_t *d, size_t n, size_t po,
                       const JointTable *table, const float *right,
                       size_t dobj_index, uint8_t color[4], int texture)
{
    RawDesc descs[32];
    EnvGroup groups[HSD_MAX_ENV_GROUPS];
    size_t group_count = 0;
    size_t desc_count = 0;
    size_t first_vertex = m->vertex_count;
    size_t vo;
    size_t dl;
    size_t limit;
    size_t cur;
    uint16_t display_blocks;
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
    load_env_groups(d, n, po, table, groups, &group_count);
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
            if (right != NULL) {
                /* PObj on a non-skeleton-root joint: at bind pose the group
                 * matrix is identity and `right` places the vertices. */
                if (verts[i].has_pos) {
                    float moved[3];
                    mtx_transform_point(right, verts[i].pos, moved);
                    memcpy(verts[i].pos, moved, sizeof(moved));
                    if (verts[i].has_nrm) {
                        float normal[3];
                        mtx_transform_normal(right, verts[i].nrm, normal);
                        memcpy(verts[i].nrm, normal, sizeof(normal));
                    }
                }
            } else {
                /* Envelope matrix slots are GX_PNMTX0..9 at 3-index strides. */
                size_t group = (size_t) (verts[i].matrix / 3);
                if (group < group_count && groups[group].rigid &&
                    verts[i].has_pos) {
                    float moved[3];
                    mtx_transform_point(&groups[group].matrix[0][0],
                                        verts[i].pos, moved);
                    memcpy(verts[i].pos, moved, sizeof(moved));
                    if (verts[i].has_nrm) {
                        float normal[3];
                        mtx_transform_normal(&groups[group].matrix[0][0],
                                             verts[i].nrm, normal);
                        memcpy(verts[i].nrm, normal, sizeof(normal));
                    }
                }
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
    if (m->vertex_count > first_vertex && m->batch_count < DEMO_MAX_BATCHES) {
        DemoModelBatch *batch = &m->batches[m->batch_count++];
        batch->first_vertex = first_vertex;
        batch->vertex_count = m->vertex_count - first_vertex;
        batch->object_index = m->object_count ? m->object_count - 1 : 0;
        batch->dobj_index = dobj_index;
        batch->texture = (int16_t) texture;
    }
}

static void walk_joint(DemoModel *m, const uint8_t *d, size_t n, size_t jo,
                       const JointTable *table, int depth)
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
    {
        float right[3][4];
        int m_index = joint_index_of(table, jo);
        int has_right = joint_right(table, m_index, right);
        while (dobj != SIZE_MAX && range_ok(dobj, 0x10, n)) {
            size_t dobj_index = m->dobj_count < DEMO_MAX_DOBJS ? m->dobj_count : DEMO_MAX_DOBJS;
            size_t pobj = rptr(d, n, dobj + 12);
            size_t mobj = rptr(d, n, dobj + 8);
            uint8_t color[4] = { 205, 145, 70, 255 };
            int texture = -1;
            if (mobj != SIZE_MAX && range_ok(mobj, 0x18, n)) {
                size_t mat = rptr(d, n, mobj + 0xc);
                size_t texdesc = rptr(d, n, mobj + 8);
                if (mat != SIZE_MAX && range_ok(mat, 8, n)) {
                    color[0] = d[mat + 4];
                    color[1] = d[mat + 5];
                    color[2] = d[mat + 6];
                    color[3] = d[mat + 7];
                }
                if (texdesc != SIZE_MAX && range_ok(texdesc, 0x5c, n)) {
                    texture = find_or_add_texture(m, d, n, texdesc);
                }
            }
            while (pobj != SIZE_MAX && range_ok(pobj, 0x18, n)) {
                m->object_count++;
                parse_pobj(m, d, n, pobj, table,
                           has_right ? &right[0][0] : NULL, dobj_index, color,
                           texture);
                pobj = rptr(d, n, pobj + 4);
            }
            if (m->dobj_count < DEMO_MAX_DOBJS) {
                m->dobj_count++;
            }
            dobj = rptr(d, n, dobj + 4);
        }
    }
    walk_joint(m, d, n, child, table, depth + 1);
    walk_joint(m, d, n, next, table, depth);
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

void demo_model_init(DemoModel *m, DemoModelVertex *storage, size_t capacity)
{
    size_t i;
    memset(m, 0, sizeof(*m));
    m->vertices = storage;
    m->vertex_capacity = capacity;
    for (i = 0; i < 3; ++i) {
        m->bounds_min[i] = 1e30f;
        m->bounds_max[i] = -1e30f;
    }
}

void demo_model_free(DemoModel *m)
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
}

int demo_model_batch_visible(const DemoModel *model, size_t batch_index)
{
    size_t dobj;
    if (batch_index >= model->batch_count) {
        return 0;
    }
    dobj = model->batches[batch_index].dobj_index;
    if (dobj >= DEMO_MAX_DOBJS) {
        return 1;
    }
    return model->dobj_hidden[dobj] == 0;
}

int demo_model_load(DemoModel *m, const uint8_t *d, size_t n, size_t root_offset,
                    char *err, size_t errn)
{
    size_t root;
    JointTable table;
    if (err != NULL && errn != 0) {
        err[0] = 0;
    }
    if (m == NULL || d == NULL || n < HSD_DATA_BASE + 0x40) {
        seterr(err, errn, "invalid model buffer");
        return 0;
    }
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
    memset(&table, 0, sizeof(table));
    joint_table_add(d, n, root, -1, &table, 0);
    walk_joint(m, d, n, root, &table, 0);
    if (m->triangle_count == 0) {
        seterr(err, errn, "joint graph contained no supported triangles");
        return 0;
    }
    return 1;
}

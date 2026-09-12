/*
 * S3 host-endian HSD archive converter.  See hsd_convert.h.
 *
 * Conversion strategy
 * -------------------
 * 1. The archive header and the relocation/public/extern tables are u32 words
 *    and are swapped first.
 * 2. Every relocation-table entry names a pointer field; `Locate` adds the
 *    host base to it, so its u32 offset must be host order.  Swapping all of
 *    them first makes the whole pointer graph traversable regardless of which
 *    descriptor classes the walk below knows about.
 * 3. Class walks convert the numeric fields (f32/u32/u16) of the descriptors
 *    the loaders read, following the same chains the loaders follow.  A field
 *    that is a relocation target is skipped (already host order).
 * 4. Byte-defined ranges are simply never written: FObj `ad` streams, GX
 *    display lists, vertex arrays, pixel/TLUT data, FigaTree node bytes and
 *    the symbol strings stay big-endian.
 *
 * `reloc_valid == reloc_total` is the desync check: if some loader sees a
 * pointer field the conversion did not know about, the loader would follow a
 * big-endian offset.  The stats make that visible to tests.
 */
#include "decomp/assets/hsd_convert.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <sysdolphin/baselib/archive.h>

#define HSD_CONVERTER_VERSION 9u
#define HSD_CACHE_MAGIC 0x31444353u /* "SCD1" little-endian */
#define HSD_PREFIX_SIZE 0x20u
#define HSD_MAX_DEPTH 256
#define HSD_MAX_LIST 4096
#define HSD_MAX_GROUPS 4096
#define HSD_MAX_TRACKS 4096

#define HSD_JOINT_SIZE 0x40
#define HSD_DOBJDESC_SIZE 0x10
#define HSD_MOBJDESC_SIZE 0x18
#define HSD_TOBJDESC_SIZE 0x5C
#define HSD_POBJDESC_SIZE 0x18
#define HSD_VTXDESCLIST_SIZE 0x18
#define HSD_IMAGEDESC_SIZE 0x18
#define HSD_TLUTDESC_SIZE 0x10
#define HSD_TEXLODDESC_SIZE 0x10
#define HSD_TOBJTEVDESC_SIZE 0x20
#define HSD_MATERIAL_SIZE 0x14
#define HSD_ROBJDESC_SIZE 0x0C
#define HSD_SHAPESETDESC_SIZE 0x1C
#define HSD_ANIMJOINT_SIZE 0x14
#define HSD_AOBJDESC_SIZE 0x10
#define HSD_FOBJDESC_SIZE 0x14
#define HSD_FIGATREE_SIZE 0x14
#define HSD_FIGATRACK_SIZE 0x0C

#define HSD_JOBJ_PTCL (1u << 5)
#define HSD_JOBJ_INSTANCE (1u << 12)
#define HSD_JOBJ_SPLINE (1u << 14)

#define HSD_POBJ_TYPE_MASK 0x3000u
#define HSD_POBJ_ENVELOPE (2u << 12)
#define HSD_POBJ_SHAPEANIM (1u << 12)

#define HSD_ROBJ_TYPE_MASK 0x70000000u
#define HSD_ROBJ_EXP 0x00000000u
#define HSD_ROBJ_JOBJ 0x10000000u
#define HSD_ROBJ_LIMIT 0x20000000u
#define HSD_ROBJ_BYTECODE 0x30000000u
#define HSD_ROBJ_IKHINT 0x40000000u

#define HSD_GX_VA_NULL 0xFFu

typedef struct Conv {
    unsigned char* d;      /* archive base */
    unsigned char* data;   /* data section (archive base + 0x20) */
    size_t size;
    size_t data_size;
    unsigned char* reloc; /* one byte per data offset: relocation target */
    unsigned char* seen;  /* one byte per data offset: walked */
    HsdConvertStats st;
    int depth;
} Conv;

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

static uint32_t le32(const unsigned char* p)
{
    return ((uint32_t) p[3] << 24) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[1] << 8) | p[0];
}

static uint16_t be16(const unsigned char* p)
{
    return (uint16_t) (((uint32_t) p[0] << 8) | p[1]);
}

static void wr32(unsigned char* p, uint32_t v)
{
    memcpy(p, &v, 4);
}

static void wr16(unsigned char* p, uint16_t v)
{
    memcpy(p, &v, 2);
}

/* Reads a u32 at an archive-absolute offset (header/tables). */
static uint16_t rd16(const Conv* c, uint32_t off)
{
    uint16_t v;
    memcpy(&v, c->data + off, 2);
    return v;
}

static uint32_t rd32_abs(const Conv* c, uint32_t off)
{
    uint32_t v;
    memcpy(&v, c->d + off, 4);
    return v;
}

/* Reads a u32 at a data-section-relative offset (descriptors). */
static uint32_t rd32(const Conv* c, uint32_t off)
{
    uint32_t v;
    memcpy(&v, c->data + off, 4);
    return v;
}

static int in_data(const Conv* c, uint32_t off, size_t need)
{
    return (size_t) off + need <= c->data_size;
}

static int mark(Conv* c, uint32_t off)
{
    if ((size_t) off >= c->data_size || c->seen[off]) {
        return 0;
    }
    c->seen[off] = 1;
    return 1;
}

/* Converts a non-pointer u32/f32 field; relocation targets are already host
 * order and are left alone. */
static void conv_u32(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 4) || c->reloc[off]) {
        return;
    }
    wr32(c->data + off, be32(c->data + off));
}

static void conv_u16(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 2)) {
        return;
    }
    wr16(c->data + off, be16(c->data + off));
}

static void conv_vtxdesc(Conv* c, uint32_t off);
static void conv_pobj(Conv* c, uint32_t off);
static void conv_tobj(Conv* c, uint32_t off);
static void conv_mobj(Conv* c, uint32_t off);
static void conv_dobj(Conv* c, uint32_t off);
static void conv_joint(Conv* c, uint32_t off);
static void conv_robjdesc(Conv* c, uint32_t off);
static void conv_anim_joint(Conv* c, uint32_t off);
static void conv_robj_anim(Conv* c, uint32_t off);
static void conv_figatree(Conv* c, uint32_t off);
static void conv_wobjdesc(Conv* c, uint32_t off);
static void conv_cobjdesc(Conv* c, uint32_t off);
static void conv_lightdesc(Conv* c, uint32_t off);
static void conv_wobjanim(Conv* c, uint32_t off);
static void conv_lightanim(Conv* c, uint32_t off);
static void conv_fogdesc(Conv* c, uint32_t off);
static void conv_scene_desc(Conv* c, uint32_t off);
static void conv_static_model(Conv* c, uint32_t off);
static void conv_stage_maphead(Conv* c, uint32_t off);

static void conv_imagedesc(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_IMAGEDESC_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x04);
    conv_u16(c, off + 0x06);
    conv_u32(c, off + 0x08);
    conv_u32(c, off + 0x0C);
    conv_u32(c, off + 0x10);
    conv_u32(c, off + 0x14);
}

static void conv_tlutdesc(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_TLUTDESC_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x08);
    conv_u16(c, off + 0x0C);
}

static void conv_texloddesc(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_TEXLODDESC_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x00);
    conv_u32(c, off + 0x04);
    /* bias_clamp/edgeLODEnable are bytes and stay as-is. */
    conv_u32(c, off + 0x0C);
}

static void conv_tobjtevdesc(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_TOBJTEVDESC_SIZE) || !mark(c, off)) {
        return;
    }
    /* 16 u8 fields + three GXColor runs are bytes; only `active`. */
    conv_u32(c, off + 0x1C);
}

static void conv_tobj(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t image;
    uint32_t tlut;
    uint32_t lod;
    uint32_t tev;
    int i;

    if (!in_data(c, off, HSD_TOBJDESC_SIZE) || !mark(c, off)) {
        return;
    }
    c->st.tobjs++;
    conv_u32(c, off + 0x08);
    conv_u32(c, off + 0x0C);
    for (i = 0; i < 9; i++) {
        conv_u32(c, off + 0x10 + (uint32_t) i * 4);
    }
    conv_u32(c, off + 0x34);
    conv_u32(c, off + 0x38);
    /* repeat_s/repeat_t bytes at 0x3C/0x3D are not touched. */
    conv_u32(c, off + 0x40);
    conv_u32(c, off + 0x44);
    conv_u32(c, off + 0x48);

    next = rd32(c, off + 0x04);
    image = rd32(c, off + 0x4C);
    tlut = rd32(c, off + 0x50);
    lod = rd32(c, off + 0x54);
    tev = rd32(c, off + 0x58);
    if (next != 0) {
        conv_tobj(c, next);
    }
    if (image != 0) {
        conv_imagedesc(c, image);
    }
    if (tlut != 0) {
        conv_tlutdesc(c, tlut);
    }
    if (lod != 0) {
        conv_texloddesc(c, lod);
    }
    if (tev != 0) {
        conv_tobjtevdesc(c, tev);
    }
}

static void conv_mobj(Conv* c, uint32_t off)
{
    uint32_t texdesc;
    uint32_t mat;

    if (!in_data(c, off, HSD_MOBJDESC_SIZE) || !mark(c, off)) {
        return;
    }
    c->st.mobjs++;
    conv_u32(c, off + 0x04); /* rendermode */
    texdesc = rd32(c, off + 0x08);
    mat = rd32(c, off + 0x0C);
    if (texdesc != 0) {
        conv_tobj(c, texdesc);
    }
    if (mat != 0 && in_data(c, mat, HSD_MATERIAL_SIZE)) {
        /* ambient/diffuse/specular are GXColor byte runs. */
        conv_u32(c, mat + 0x0C);
        conv_u32(c, mat + 0x10);
    }
}

static void conv_vtxdesc(Conv* c, uint32_t off)
{
    int i;
    for (i = 0; i < 32; i++) {
        uint32_t attr;
        if (!in_data(c, off, HSD_VTXDESCLIST_SIZE) || !mark(c, off)) {
            break;
        }
        /* Convert attr first so the NULL terminator reads as 0xFF. */
        conv_u32(c, off + 0x00);
        attr = rd32(c, off + 0x00);
        if (attr == HSD_GX_VA_NULL) {
            break;
        }
        conv_u32(c, off + 0x04);
        conv_u32(c, off + 0x08);
        conv_u32(c, off + 0x0C);
        /* frac u8 stays. */
        conv_u16(c, off + 0x12);
        off += HSD_VTXDESCLIST_SIZE;
    }
}

static void conv_envelopes(Conv* c, uint32_t off)
{
    int i;
    for (i = 0; i < 10; i++) {
        uint32_t list_off;
        uint32_t desc_off;
        int count = 0;
        if (!in_data(c, off + (uint32_t) i * 4, 4)) {
            break;
        }
        list_off = rd32(c, off + (uint32_t) i * 4);
        if (list_off == 0) {
            break;
        }
        desc_off = list_off;
        while (count++ < HSD_MAX_LIST && in_data(c, desc_off, 8) &&
               mark(c, desc_off)) {
            uint32_t joint = rd32(c, desc_off);
            if (joint == 0) {
                break;
            }
            /* joint pointer is a relocation target; the weight is an f32. */
            conv_u32(c, desc_off + 0x04);
            desc_off += 8;
        }
    }
}

/* HSD_ShapeSetDesc (pobj.h:89): flags/nb_shape/vertex count + VtxDesc lists.
 * The vertex/normal index lists are byte arrays and stay big-endian. */
static void conv_shapesetdesc(Conv* c, uint32_t off)
{
    uint32_t vtxdesc;
    uint32_t nrmdesc;

    if (!in_data(c, off, HSD_SHAPESETDESC_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00);
    conv_u16(c, off + 0x02);
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x10);
    vtxdesc = rd32(c, off + 0x08);
    nrmdesc = rd32(c, off + 0x14);
    if (vtxdesc != 0) {
        conv_vtxdesc(c, vtxdesc);
    }
    if (nrmdesc != 0) {
        conv_vtxdesc(c, nrmdesc);
    }
}

static void conv_pobj(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t verts;
    uint32_t upt;
    uint16_t flags;

    if (!in_data(c, off, HSD_POBJDESC_SIZE) || !mark(c, off)) {
        return;
    }
    c->st.pobjs++;
    conv_u16(c, off + 0x0C);
    conv_u16(c, off + 0x0E);
    memcpy(&flags, c->data + off + 0x0C, 2);
    next = rd32(c, off + 0x04);
    verts = rd32(c, off + 0x08);
    upt = rd32(c, off + 0x14);
    if (next != 0) {
        conv_pobj(c, next);
    }
    if (verts != 0) {
        conv_vtxdesc(c, verts);
    }
    if ((flags & HSD_POBJ_TYPE_MASK) == HSD_POBJ_ENVELOPE && upt != 0) {
        conv_envelopes(c, upt);
    } else if ((flags & HSD_POBJ_TYPE_MASK) == HSD_POBJ_SHAPEANIM &&
               upt != 0) {
        conv_shapesetdesc(c, upt);
    }
}

static void conv_dobj(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t mobj;
    uint32_t pobj;

    if (!in_data(c, off, HSD_DOBJDESC_SIZE) || !mark(c, off)) {
        return;
    }
    c->st.dobjs++;
    next = rd32(c, off + 0x04);
    mobj = rd32(c, off + 0x08);
    pobj = rd32(c, off + 0x0C);
    if (next != 0) {
        conv_dobj(c, next);
    }
    if (mobj != 0) {
        conv_mobj(c, mobj);
    }
    if (pobj != 0) {
        conv_pobj(c, pobj);
    }
}

static void conv_rvalue_list(Conv* c, uint32_t off)
{
    int i;
    for (i = 0; i < 64; i++) {
        uint32_t joint;
        if (!in_data(c, off, 8) || !mark(c, off)) {
            return;
        }
        joint = rd32(c, off + 0x04);
        if (joint == 0) {
            return;
        }
        conv_u32(c, off + 0x00);
        off += 8;
    }
}

static void conv_robjdesc(Conv* c, uint32_t off)
{
    uint32_t flags;
    uint32_t u;

    if (!in_data(c, off, HSD_ROBJDESC_SIZE) || !mark(c, off) ||
        c->depth > HSD_MAX_DEPTH) {
        return;
    }
    c->depth++;
    c->st.robjdescs++;
    conv_u32(c, off + 0x04);
    flags = rd32(c, off + 0x04);
    u = rd32(c, off + 0x08);
    switch (flags & HSD_ROBJ_TYPE_MASK) {
    case HSD_ROBJ_EXP:
        if (u != 0 && in_data(c, u, 8)) {
            conv_rvalue_list(c, rd32(c, u + 0x04));
        }
        break;
    case HSD_ROBJ_LIMIT:
        conv_u32(c, off + 0x08);
        break;
    case HSD_ROBJ_IKHINT:
        if (u != 0 && in_data(c, u, 8)) {
            conv_u32(c, u + 0x00);
            conv_u32(c, u + 0x04);
        }
        break;
    case HSD_ROBJ_BYTECODE:
        if (u != 0 && in_data(c, u, 8)) {
            /* bytecode bytes stay; rvalue list is numeric */
            conv_rvalue_list(c, rd32(c, u + 0x04));
        }
        break;
    default: /* REFTYPE_JOBJ: pointer only */
        break;
    }
    if (rd32(c, off + 0x00) != 0) {
        conv_robjdesc(c, rd32(c, off + 0x00));
    }
    c->depth--;
}

static void conv_joint(Conv* c, uint32_t off)
{
    uint32_t flags;
    uint32_t child;
    uint32_t next;
    uint32_t u;
    uint32_t mtx;
    uint32_t robj;
    int i;

    if (!in_data(c, off, HSD_JOINT_SIZE) || !mark(c, off) ||
        c->depth > HSD_MAX_DEPTH) {
        return;
    }
    c->depth++;
    c->st.joints++;
    conv_u32(c, off + 0x04);
    for (i = 0; i < 9; i++) {
        conv_u32(c, off + 0x14 + (uint32_t) i * 4);
    }
    flags = rd32(c, off + 0x04);
    child = rd32(c, off + 0x08);
    next = rd32(c, off + 0x0C);
    u = rd32(c, off + 0x10);
    mtx = rd32(c, off + 0x38);
    robj = rd32(c, off + 0x3C);
    if (mtx != 0 && in_data(c, mtx, 0x30)) {
        for (i = 0; i < 12; i++) {
            conv_u32(c, mtx + (uint32_t) i * 4);
        }
    }
    if (!(flags & (HSD_JOBJ_PTCL | HSD_JOBJ_SPLINE)) && u != 0) {
        conv_dobj(c, u);
    }
    if (!(flags & HSD_JOBJ_INSTANCE) && child != 0) {
        conv_joint(c, child);
    }
    if (next != 0) {
        conv_joint(c, next);
    }
    if (robj != 0) {
        conv_robjdesc(c, robj);
    }
    c->depth--;
}

static void conv_aobjdesc(Conv* c, uint32_t off)
{
    uint32_t fobj;

    if (!in_data(c, off, HSD_AOBJDESC_SIZE) || !mark(c, off)) {
        return;
    }
    c->st.aobjs++;
    conv_u32(c, off + 0x00);
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x0C);
    fobj = rd32(c, off + 0x08);
    if (fobj != 0) {
        /* FObj desc chain */
        int i;
        for (i = 0; i < HSD_MAX_LIST; i++) {
            if (!in_data(c, fobj, HSD_FOBJDESC_SIZE) || !mark(c, fobj)) {
                break;
            }
            c->st.fobjs++;
            conv_u32(c, fobj + 0x04);
            conv_u32(c, fobj + 0x08);
            /* type/frac bytes and the `ad` stream stay big-endian. */
            fobj = rd32(c, fobj + 0x00);
            if (fobj == 0) {
                break;
            }
        }
    }
}

/* AObjDesc.obj_id is an ID for AnimJoint tracks, but a *JObj offset* for the
 * WObj/Light animation tracks (`HSD_AObjLoadDesc` falls back to
 * HSD_JObjLoadJoint when the ID lookup misses).  Convert the referenced joint
 * tree as well; its MObjs are otherwise left big-endian (GrNLa light anims
 * crashed there).  Relocation targets are still data-relative offsets at
 * conversion time (the loader adds the base in `Locate`). */
static void conv_aobjdesc_ref(Conv* c, uint32_t off)
{
    uint32_t obj;

    if (!in_data(c, off, HSD_AOBJDESC_SIZE)) {
        return;
    }
    conv_aobjdesc(c, off);
    obj = rd32(c, off + 0x0C);
    if (obj != 0 && in_data(c, obj, HSD_JOINT_SIZE)) {
        conv_joint(c, obj);
    }
}

static void conv_anim_joint(Conv* c, uint32_t off)
{
    uint32_t child;
    uint32_t next;
    uint32_t aobj;
    uint32_t robj_anim;

    if (!in_data(c, off, HSD_ANIMJOINT_SIZE) || !mark(c, off) ||
        c->depth > HSD_MAX_DEPTH) {
        return;
    }
    c->depth++;
    c->st.anim_joints++;
    conv_u32(c, off + 0x10);
    child = rd32(c, off + 0x00);
    next = rd32(c, off + 0x04);
    aobj = rd32(c, off + 0x08);
    robj_anim = rd32(c, off + 0x0C);
    if (child != 0) {
        conv_anim_joint(c, child);
    }
    if (next != 0) {
        conv_anim_joint(c, next);
    }
    if (aobj != 0) {
        conv_aobjdesc(c, aobj);
    }
    if (robj_anim != 0) {
        conv_robj_anim(c, robj_anim);
    }
    c->depth--;
}

static void conv_robj_anim(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t aobj;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    next = rd32(c, off + 0x00);
    aobj = rd32(c, off + 0x04);
    if (aobj != 0) {
        conv_aobjdesc(c, aobj);
    }
    if (next != 0) {
        conv_robj_anim(c, next);
    }
}

static void conv_figatrack(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_FIGATRACK_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00);
    conv_u16(c, off + 0x02);
    /* obj_type/frac_value/frac_slope bytes and the ad stream stay. */
}

static void conv_figatree(Conv* c, uint32_t off)
{
    uint32_t nodes;
    uint32_t tracks;
    uint32_t track_count = 0;
    uint32_t p;
    uint32_t i;
    int guard = 0;

    if (!in_data(c, off, HSD_FIGATREE_SIZE) || !mark(c, off)) {
        return;
    }
    c->st.figatrees++;
    conv_u32(c, off + 0x00);
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x08);
    nodes = rd32(c, off + 0x0C);
    tracks = rd32(c, off + 0x10);

    /* The node list holds a signed per-joint track count, 0xFF-terminated. */
    p = nodes;
    while (nodes != 0 && guard++ < HSD_MAX_LIST && in_data(c, p, 1)) {
        signed char value = (signed char) c->data[p];
        if (value < 0) {
            break;
        }
        track_count += (uint32_t) value;
        p++;
    }
    if (track_count > HSD_MAX_TRACKS) {
        track_count = HSD_MAX_TRACKS;
    }
    if (tracks != 0) {
        for (i = 0; i < track_count; i++) {
            conv_figatrack(c, tracks + i * HSD_FIGATRACK_SIZE);
        }
    }
}


/* --------------------------------------------------------------- scene data */

static void conv_wobjdesc(Conv* c, uint32_t off)
{
    uint32_t robj;

    if (!in_data(c, off, 0x14) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x08);
    conv_u32(c, off + 0x0C);
    robj = rd32(c, off + 0x10);
    if (robj != 0) {
        conv_robjdesc(c, robj);
    }
}

static void conv_cobjdesc(Conv* c, uint32_t off)
{
    uint32_t w;
    uint16_t projection;
    int i;
    if (!in_data(c, off, 0x30) || !mark(c, off)) {
        return;
    }
    c->st.scene_descs++;
    conv_u16(c, off + 0x04);
    conv_u16(c, off + 0x06);
    for (i = 0; i < 4; i++) {
        conv_u16(c, off + 0x08 + (uint32_t) i * 2);
        conv_u16(c, off + 0x10 + (uint32_t) i * 2);
    }
    conv_u32(c, off + 0x20);
    conv_u32(c, off + 0x28);
    conv_u32(c, off + 0x2C);
    w = rd32(c, off + 0x18);
    if (w != 0) {
        conv_wobjdesc(c, w);
    }
    w = rd32(c, off + 0x1C);
    if (w != 0) {
        conv_wobjdesc(c, w);
    }
    w = rd32(c, off + 0x24);
    if (w != 0 && in_data(c, w, 12)) {
        conv_u32(c, w + 0x00);
        conv_u32(c, w + 0x04);
        conv_u32(c, w + 0x08);
    }
    projection = rd16(c, off + 0x06);
    if (projection == 1) { /* PROJ_PERSPECTIVE: fov, aspect */
        conv_u32(c, off + 0x30);
        conv_u32(c, off + 0x34);
    } else if (projection == 2 || projection == 3) {
        for (i = 0; i < 4; i++) {
            conv_u32(c, off + 0x30 + (uint32_t) i * 4);
        }
    }
}

static void conv_lightdesc(Conv* c, uint32_t off)
{
    uint32_t u;
    uint16_t flags;
    uint16_t attnflags;
    int i;

    if (!in_data(c, off, 0x1C) || !mark(c, off)) {
        return;
    }
    c->st.scene_descs++;
    conv_u16(c, off + 0x08);
    conv_u16(c, off + 0x0A);
    flags = rd16(c, off + 0x08);
    attnflags = rd16(c, off + 0x0A);
    u = rd32(c, off + 0x18);
    { uint32_t w = rd32(c, off + 0x10); if (w != 0) conv_wobjdesc(c, w); }
    { uint32_t w = rd32(c, off + 0x14); if (w != 0) conv_wobjdesc(c, w); }
    switch (flags & 0x3) {
    case 2: /* LOBJ_POINT */
        if (u != 0 && (attnflags & 1) != 0 && in_data(c, u, 0x18)) {
            for (i = 0; i < 6; i++) {
                conv_u32(c, u + (uint32_t) i * 4);
            }
        } else if (u != 0 && in_data(c, u, 0x0C)) {
            conv_u32(c, u + 0x00);
            conv_u32(c, u + 0x04);
            conv_u32(c, u + 0x08);
        }
        break;
    case 3: /* LOBJ_SPOT */
        if (u != 0 && attnflags != 0 && in_data(c, u, 0x18)) {
            for (i = 0; i < 6; i++) {
                conv_u32(c, u + (uint32_t) i * 4);
            }
        } else if (u != 0 && in_data(c, u, 0x14)) {
            for (i = 0; i < 5; i++) {
                conv_u32(c, u + (uint32_t) i * 4);
            }
        }
        break;
    default: /* ambient / infinite */
        break;
    }
    { uint32_t next = rd32(c, off + 0x04); if (next != 0) conv_lightdesc(c, next); }
}

/* HSD_WObjAnim { HSD_AObjDesc* aobjdesc; HSD_RObjAnimJoint* robjanim; } */
static void conv_wobjanim(Conv* c, uint32_t off)
{
    uint32_t aobj;
    uint32_t robjanim;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    aobj = rd32(c, off + 0x00);
    robjanim = rd32(c, off + 0x04);
    if (aobj != 0) {
        conv_aobjdesc_ref(c, aobj);
    }
    if (robjanim != 0) {
        conv_robj_anim(c, robjanim);
    }
}

/* HSD_LightAnim { next; aobjdesc; position_anim; interest_anim; } — the chain
 * `lb_80011AC4` feeds to HSD_LObjAddAnimAll for stage/scene light lists. */
static void conv_lightanim(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t aobj;
    uint32_t position;
    uint32_t interest;

    if (!in_data(c, off, 0x10) || !mark(c, off)) {
        return;
    }
    next = rd32(c, off + 0x00);
    aobj = rd32(c, off + 0x04);
    position = rd32(c, off + 0x08);
    interest = rd32(c, off + 0x0C);
    if (aobj != 0) {
        conv_aobjdesc_ref(c, aobj);
    }
    if (position != 0) {
        conv_wobjanim(c, position);
    }
    if (interest != 0) {
        conv_wobjanim(c, interest);
    }
    if (next != 0) {
        conv_lightanim(c, next);
    }
}

/* LightList { HSD_LightDesc* desc; HSD_LightAnim** anims; }; both
 * lb_80011AC4 (stages/scenes) and the game's light setup read anims[0]. */
static void conv_lightlist(Conv* c, uint32_t off)
{
    uint32_t desc;
    uint32_t anims;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    desc = rd32(c, off + 0x00);
    anims = rd32(c, off + 0x04);
    if (desc != 0) {
        conv_lightdesc(c, desc);
    }
    if (anims != 0 && in_data(c, anims, 4)) {
        uint32_t anim = rd32(c, anims);
        if (anim != 0) {
            conv_lightanim(c, anim);
        }
    }
}

static void conv_fogdesc(Conv* c, uint32_t off)
{
    uint32_t adj;
    int i;

    if (!in_data(c, off, 0x14) || !mark(c, off)) {
        return;
    }
    c->st.scene_descs++;
    conv_u32(c, off + 0x00);
    conv_u32(c, off + 0x08);
    conv_u32(c, off + 0x0C);
    adj = rd32(c, off + 0x04);
    if (adj != 0 && in_data(c, adj, 0x44)) {
        conv_u16(c, adj + 0x00);
        conv_u16(c, adj + 0x02);
        for (i = 0; i < 16; i++) {
            conv_u32(c, adj + 0x04 + (uint32_t) i * 4);
        }
    }
}

static void conv_scene_desc(Conv* c, uint32_t off)
{
    uint32_t models;
    uint32_t cameras;
    uint32_t lights;
    uint32_t fogs;
    int guard;

    if (!in_data(c, off, 0x10) || !mark(c, off)) {
        return;
    }
    c->st.scene_descs++;
    models = rd32(c, off + 0x00);
    cameras = rd32(c, off + 0x04);
    lights = rd32(c, off + 0x08);
    fogs = rd32(c, off + 0x0C);

    if (models != 0) {
        uint32_t p = models;
        for (guard = 0; guard < 64; guard++) {
            uint32_t desc = rd32(c, p);
            if (desc == 0 || !in_data(c, desc, 0x10)) {
                break;
            }
            { uint32_t j = rd32(c, desc + 0x00); if (j != 0) conv_joint(c, j); }
            { /* anims: array of AnimJoint* */
                uint32_t arr = rd32(c, desc + 0x04);
                int k;
                for (k = 0; k < 64 && arr != 0; k++) {
                    uint32_t a = rd32(c, arr + (uint32_t) k * 4);
                    if (a == 0 || !in_data(c, a, HSD_ANIMJOINT_SIZE)) {
                        break;
                    }
                    conv_anim_joint(c, a);
                }
            }
            p += 4;
        }
    }
    if (cameras != 0) {
        uint32_t p = cameras;
        for (guard = 0; guard < 64; guard++) {
            uint32_t desc = rd32(c, p);
            if (desc == 0 || !in_data(c, desc, 0x30)) {
                break;
            }
            conv_cobjdesc(c, desc);
            p += 8;
        }
    }
    if (lights != 0) {
        uint32_t p = lights;
        for (guard = 0; guard < 64; guard++) {
            uint32_t list = rd32(c, p);
            if (list == 0 || !in_data(c, list, 8)) {
                break;
            }
            conv_lightlist(c, list);
            p += 4;
        }
    }
    if (fogs != 0) {
        uint32_t p = fogs;
        for (guard = 0; guard < 64; guard++) {
            uint32_t desc = rd32(c, p);
            if (desc == 0 || !in_data(c, desc, 0x14)) {
                break;
            }
            conv_fogdesc(c, desc);
            p += 8;
        }
    }
}

static void conv_static_model(Conv* c, uint32_t off)
{
    uint32_t joint;
    uint32_t animjoint;

    if (!in_data(c, off, 0x10) || !mark(c, off)) {
        return;
    }
    c->st.scene_descs++;
    joint = rd32(c, off + 0x00);
    if (joint != 0) {
        conv_joint(c, joint);
    }
    animjoint = rd32(c, off + 0x04);
    if (animjoint != 0) {
        conv_anim_joint(c, animjoint);
    }
}

/* Gr*.dat `map_head`: the stage's own descriptor table (src/melee/gr/types.h
 * UnkStageDat / UnkStageDat_x8_t).  The game loads item 0's`unk0` as the
 * stage JObj (Ground_GetStageGObj, ground.c:873), item 0's x10 through
 * lb_80013B14 (camera) and x18 through lb_80011AC4 (lights), and x1C through
 * HSD_FogLoadDesc (Ground_801C1E94).  Only the numeric fields of each
 * descriptor are converted here; pointers are relocation targets already in
 * host order.  MatAnim/ShapeAnim chains are stage animation and stay
 * big-endian until a consumer needs them. */
static void conv_stage_maphead(Conv* c, uint32_t off)
{
    uint32_t maps;
    uint32_t count;
    uint32_t i;
    int guard;

    if (!in_data(c, off, 0x30) || !mark(c, off)) {
        return;
    }
    c->st.stage_maps++;
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x0C);
    conv_u32(c, off + 0x14);
    conv_u32(c, off + 0x1C);
    conv_u32(c, off + 0x24);
    conv_u32(c, off + 0x2C);

    maps = rd32(c, off + 0x08);
    count = rd32(c, off + 0x0C);
    if (maps == 0 || count > 256) {
        return;
    }
    for (i = 0; i < count; i++) {
        uint32_t e = maps + i * 0x34;
        uint32_t joint;
        uint32_t arr;
        uint32_t cam;
        uint32_t lights;
        uint32_t fog;

        if (!in_data(c, e, 0x34)) {
            break;
        }
        conv_u32(c, e + 0x24);
        conv_u32(c, e + 0x30);
        joint = rd32(c, e + 0x00);
        if (joint != 0) {
            conv_joint(c, joint);
        }
        arr = rd32(c, e + 0x04);
        for (guard = 0; guard < 128 && arr != 0; guard++) {
            uint32_t a = rd32(c, arr + (uint32_t) guard * 4);
            if (a == 0 || !in_data(c, a, HSD_ANIMJOINT_SIZE)) {
                break;
            }
            conv_anim_joint(c, a);
        }
        cam = rd32(c, e + 0x10);
        if (cam != 0) {
            conv_cobjdesc(c, cam);
        }
        lights = rd32(c, e + 0x18);
        for (guard = 0; guard < 128 && lights != 0; guard++) {
            uint32_t p = lights + (uint32_t) guard * 4;
            uint32_t list;
            if (!in_data(c, p, 4)) {
                break;
            }
            list = rd32(c, p);
            if (list == 0 || !in_data(c, list, 8)) {
                break;
            }
            conv_lightlist(c, list);
        }
        fog = rd32(c, e + 0x1C);
        if (fog != 0) {
            conv_fogdesc(c, fog);
        }
    }
}

/* --------------------------------------------------------------- pipeline */


static void swap_header_and_tables(Conv* c, uint32_t nb_reloc,
                                   uint32_t nb_public, uint32_t nb_extern)
{
    uint32_t i;
    uint32_t off;
    size_t words = nb_reloc * 4 + nb_public * 8 + nb_extern * 8;

    for (i = 0; i < 5; i++) {
        wr32(c->d + i * 4, be32(c->d + i * 4));
    }
    off = HSD_PREFIX_SIZE + (uint32_t) c->data_size;
    for (i = 0; i < words / 4; i++) {
        wr32(c->d + off + i * 4, be32(c->d + off + i * 4));
    }
}

static void convert_relocs(Conv* c, uint32_t reloc_off, uint32_t nb_reloc)
{
    uint32_t i;
    c->st.reloc_total = nb_reloc;
    for (i = 0; i < nb_reloc; i++) {
        uint32_t field = rd32_abs(c, reloc_off + i * 4);
        if (in_data(c, field, 4)) {
            c->reloc[field] = 1;
            wr32(c->data + field, be32(c->data + field));
            c->st.reloc_valid++;
        }
    }
}

static int name_ends_with(const char* name, size_t length, const char* suffix)
{
    size_t n = strlen(suffix);
    return length >= n && memcmp(name + length - n, suffix, n) == 0;
}

static void convert_roots(Conv* c, uint32_t public_off, uint32_t nb_public,
                          uint32_t symbols_off)
{
    uint32_t i;
    for (i = 0; i < nb_public; i++) {
        uint32_t data_off = rd32_abs(c, public_off + i * 8);
        uint32_t symbol_off = rd32_abs(c, public_off + i * 8 + 4);
        const char* name;
        size_t remaining;
        size_t length;

        if ((size_t) symbols_off + symbol_off >= c->size) {
            continue;
        }
        name = (const char*) c->d + symbols_off + symbol_off;
        remaining = c->size - (size_t) symbols_off - symbol_off;
        length = 0;
        while (length < remaining && name[length] != '\0') {
            length++;
        }
        c->st.public_symbols++;

        if (data_off == 0) {
            c->st.roots_unknown++;
            continue;
        }
        if (name_ends_with(name, length, "matanim_joint") ||
            name_ends_with(name, length, "animjoint")) {
            c->st.roots_anim++;
            conv_anim_joint(c, data_off);
        } else if (name_ends_with(name, length, "_joint")) {
            c->st.roots_joint++;
            conv_joint(c, data_off);
        } else if (name_ends_with(name, length, "_figatree")) {
            c->st.roots_figatree++;
            conv_figatree(c, data_off);
        } else if (name_ends_with(name, length, "_scene_data")) {
            c->st.roots_unknown++;
            conv_scene_desc(c, data_off);
        } else if (name_ends_with(name, length, "_scene_models")) {
            c->st.roots_unknown++;
            conv_static_model(c, data_off);
        } else if (length == 8 && memcmp(name, "map_head", 8) == 0) {
            conv_stage_maphead(c, data_off);
        } else {
            c->st.roots_unknown++;
        }
    }
}

static int convert_archive(unsigned char* data, size_t size, Conv* c)
{
    uint32_t file_size = be32(data);
    uint32_t data_size = be32(data + 4);
    uint32_t nb_reloc = be32(data + 8);
    uint32_t nb_public = be32(data + 12);
    uint32_t nb_extern = be32(data + 16);
    uint32_t reloc_off;
    uint32_t public_off;
    uint32_t symbols_off;
    size_t trailer;

    if (file_size != size || data_size > size - HSD_PREFIX_SIZE) {
        return 0;
    }
    trailer = size - HSD_PREFIX_SIZE - data_size;
    if ((size_t) nb_reloc * 4 + (size_t) nb_public * 8 +
            (size_t) nb_extern * 8 >
        trailer) {
        return 0;
    }

    c->d = data;
    c->data = data + HSD_PREFIX_SIZE;
    c->size = size;
    c->data_size = data_size;
    c->st = (HsdConvertStats) { 0 };
    c->depth = 0;
    c->reloc = calloc(data_size ? data_size : 1, 1);
    c->seen = calloc(data_size ? data_size : 1, 1);
    if (c->reloc == NULL || c->seen == NULL) {
        free(c->reloc);
        free(c->seen);
        c->reloc = NULL;
        c->seen = NULL;
        return 0;
    }

    swap_header_and_tables(c, nb_reloc, nb_public, nb_extern);
    reloc_off = HSD_PREFIX_SIZE + data_size;
    public_off = reloc_off + nb_reloc * 4;
    symbols_off = public_off + nb_public * 8 + nb_extern * 8;

    convert_relocs(c, reloc_off, nb_reloc);
    convert_roots(c, public_off, nb_public, symbols_off);

    c->st.ok = c->st.reloc_valid == c->st.reloc_total;
    free(c->reloc);
    free(c->seen);
    c->reloc = NULL;
    c->seen = NULL;
    return 1;
}

/* ------------------------------------------------------------- disk cache */

static uint64_t fnv1a64(const unsigned char* data, size_t size)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    size_t i;
    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

static int cache_directory(char* out, size_t out_size)
{
    const char* env;

    if (getenv("MELEE_NO_ASSET_CACHE") != NULL) {
        return 0;
    }
    env = getenv("MELEE_ASSET_CACHE");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s", env);
        return 1;
    }
    env = getenv("XDG_CACHE_HOME");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s/melee/assets", env);
        return 1;
    }
    env = getenv("HOME");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s/.cache/melee/assets", env);
        return 1;
    }
    return 0;
}

static int ensure_directory(const char* path)
{
    char buffer[768];
    size_t length;
    char* p;

    if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(buffer)) {
        return 0;
    }
    snprintf(buffer, sizeof(buffer), "%s", path);
    length = strlen(buffer);
    if (length != 0 && buffer[length - 1] == '/') {
        buffer[length - 1] = '\0';
    }
    for (p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0700) != 0 && errno != EEXIST) {
                return 0;
            }
            *p = '/';
        }
    }
    return mkdir(buffer, 0700) == 0 || errno == EEXIST;
}

static int cache_file_path(char* out, size_t out_size, uint64_t hash,
                           size_t raw_size)
{
    char dir[640];

    if (!cache_directory(dir, sizeof(dir))) {
        return 0;
    }
    if (!ensure_directory(dir)) {
        return 0;
    }
    snprintf(out, out_size, "%s/hsd-v%u-%016llx-%08zx.img", dir,
             HSD_CONVERTER_VERSION, (unsigned long long) hash, raw_size);
    return 1;
}

static int cache_load(const char* path, unsigned char* data, size_t size,
                      HsdConvertStats* stats, uint64_t hash)
{
    FILE* f = fopen(path, "rb");
    uint32_t header[5];
    uint32_t hash_high;
    int ok = 0;

    if (f == NULL) {
        return 0;
    }
    if (fread(header, sizeof(header), 1, f) == 1 &&
        header[0] == HSD_CACHE_MAGIC &&
        header[1] == HSD_CONVERTER_VERSION && header[2] == (uint32_t) size &&
        header[3] == sizeof(HsdConvertStats) &&
        fread(&hash_high, sizeof(hash_high), 1, f) == 1) {
        uint64_t stored_hash =
            ((uint64_t) hash_high << 32) | (uint64_t) header[4];
        if (stored_hash == hash) {
            unsigned char* blob =
                malloc(sizeof(HsdConvertStats) + size);
            if (blob != NULL &&
                fread(blob, sizeof(HsdConvertStats) + size, 1, f) == 1) {
                memcpy(stats, blob, sizeof(HsdConvertStats));
                memcpy(data, blob + sizeof(HsdConvertStats), size);
                ok = 1;
            }
            free(blob);
        }
    }
    fclose(f);
    return ok;
}

static void cache_store(const char* path, const unsigned char* data,
                        size_t size, const HsdConvertStats* stats,
                        uint64_t hash)
{
    char tmp[820];
    FILE* f;
    int n;

    n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n < 0 || (size_t) n >= sizeof(tmp)) {
        return;
    }
    f = fopen(tmp, "wb");
    if (f == NULL) {
        return;
    }
    {
        uint32_t header[5];
        uint32_t hash_high = (uint32_t) (hash >> 32);
        header[0] = HSD_CACHE_MAGIC;
        header[1] = HSD_CONVERTER_VERSION;
        header[2] = (uint32_t) size;
        header[3] = (uint32_t) sizeof(HsdConvertStats);
        header[4] = (uint32_t) (hash & 0xFFFFFFFFu);
        fwrite(header, sizeof(header), 1, f);
        fwrite(&hash_high, sizeof(hash_high), 1, f);
        fwrite(stats, sizeof(*stats), 1, f);
        fwrite(data, size, 1, f);
    }
    if (fclose(f) == 0) {
        rename(tmp, path);
    } else {
        remove(tmp);
    }
}

/* ------------------------------------------------------------------ public */

int hsd_asset_convert(unsigned char* data, size_t size,
                      HsdConvertStats* stats)
{
    Conv conv;
    uint32_t file_size;
    uint32_t data_size;
    uint64_t hash;
    char path[800];
    int have_cache = 0;

    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
    if (data == NULL || size < HSD_PREFIX_SIZE) {
        return 0;
    }

    /* Already host order (a second parse of the same buffer). */
    file_size = le32(data);
    data_size = le32(data + 4);
    if (file_size == size && data_size <= size - HSD_PREFIX_SIZE) {
        if (stats != NULL) {
            stats->ok = 1;
        }
        return 1;
    }

    hash = fnv1a64(data, size);
    if (cache_file_path(path, sizeof(path), hash, size)) {
        have_cache = 1;
        if (cache_load(path, data, size, &conv.st, hash)) {
            if (stats != NULL) {
                *stats = conv.st;
            }
            return 1;
        }
    }

    memset(&conv, 0, sizeof(conv));
    if (!convert_archive(data, size, &conv)) {
        return 0;
    }
    if (have_cache) {
        cache_store(path, data, size, &conv.st, hash);
    }
    if (stats != NULL) {
        *stats = conv.st;
    }
    return 1;
}

/*
 * Platform entry point: every decompiled `HSD_ArchiveParse` call is routed
 * here by the native/decomp shim (`#define HSD_ArchiveParse ...`).  The
 * conversion runs on the caller's buffer, then the real parser relocates the
 * now host-order pointers.
 */
s32 melee_port_HSD_ArchiveParse(HSD_Archive* archive, u8* src,
                                size_t file_size)
{
    if (src != NULL && file_size >= HSD_PREFIX_SIZE) {
        hsd_asset_convert((unsigned char*) src, file_size, NULL);
    }
    return HSD_ArchiveParse(archive, src, file_size);
}

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

#define HSD_CONVERTER_VERSION 31u
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
#define HSD_MATANIM_SIZE 0x10
#define HSD_MATANIMJOINT_SIZE 0x0C
#define HSD_TEXANIM_SIZE 0x18
#define HSD_RENDERANIM_SIZE 0x08
#define HSD_CHANANIM_SIZE 0x08
#define HSD_SHAPEANIM_SIZE 0x08
#define HSD_SHAPEANIM_DOBJ_SIZE 0x08
#define HSD_SHAPEANIMJOINT_SIZE 0x0C
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

/* ------------------------------------------------- animation descriptors
 * HSD_TexAnim/MatAnim/ShapeAnim are how material and shape animation reach
 * TObjs and shape sets; the walkers so far only followed the base joint
 * trees, so title-screen animated textures stayed big-endian. */

/* HSD_TexAnim: next; id; aobjdesc; ImageDesc** imagetbl; TlutDesc** tluttbl;
 * u16 n_imagetbl; u16 n_tluttbl (0x18). */
static void conv_texanim(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t aobj;
    uint32_t imagetbl;
    uint32_t tluttbl;
    uint16_t n_images;
    uint16_t n_tluts;
    uint32_t i;

    if (!in_data(c, off, HSD_TEXANIM_SIZE) || !mark(c, off)) {
        return;
    }
    next = rd32(c, off + 0x00);
    aobj = rd32(c, off + 0x08);
    imagetbl = rd32(c, off + 0x0C);
    tluttbl = rd32(c, off + 0x10);
    conv_u16(c, off + 0x14);
    conv_u16(c, off + 0x16);
    n_images = rd16(c, off + 0x14);
    n_tluts = rd16(c, off + 0x16);
    if (aobj != 0) {
        conv_aobjdesc(c, aobj);
    }
    if (imagetbl != 0) {
        for (i = 0; i < n_images && i < HSD_MAX_LIST; i++) {
            uint32_t p = imagetbl + i * 4;
            uint32_t img;
            if (!in_data(c, p, 4)) {
                break;
            }
            img = rd32(c, p);
            if (img != 0) {
                conv_imagedesc(c, img);
            }
        }
    }
    if (tluttbl != 0) {
        for (i = 0; i < n_tluts && i < HSD_MAX_LIST; i++) {
            uint32_t p = tluttbl + i * 4;
            uint32_t tlut;
            if (!in_data(c, p, 4)) {
                break;
            }
            tlut = rd32(c, p);
            if (tlut != 0) {
                conv_tlutdesc(c, tlut);
            }
        }
    }
    if (next != 0) {
        conv_texanim(c, next);
    }
}

/* HSD_ChanAnim / HSD_TevRegAnim: next; aobjdesc. */
static void conv_chananim(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t aobj;

    if (!in_data(c, off, HSD_CHANANIM_SIZE) || !mark(c, off)) {
        return;
    }
    next = rd32(c, off + 0x00);
    aobj = rd32(c, off + 0x04);
    if (aobj != 0) {
        conv_aobjdesc(c, aobj);
    }
    if (next != 0) {
        conv_chananim(c, next);
    }
}

/* HSD_RenderAnim: ChanAnim* chananim; TevRegAnim* reganim (same layout). */
static void conv_renderanim(Conv* c, uint32_t off)
{
    uint32_t chananim;
    uint32_t reganim;

    if (!in_data(c, off, HSD_RENDERANIM_SIZE) || !mark(c, off)) {
        return;
    }
    chananim = rd32(c, off + 0x00);
    reganim = rd32(c, off + 0x04);
    if (chananim != 0) {
        conv_chananim(c, chananim);
    }
    if (reganim != 0) {
        conv_chananim(c, reganim);
    }
}

/* HSD_MatAnim: next; aobjdesc; texanim; renderanim. */
static void conv_matanim(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t aobj;
    uint32_t texanim;
    uint32_t renderanim;

    if (!in_data(c, off, HSD_MATANIM_SIZE) || !mark(c, off)) {
        return;
    }
    next = rd32(c, off + 0x00);
    aobj = rd32(c, off + 0x04);
    texanim = rd32(c, off + 0x08);
    renderanim = rd32(c, off + 0x0C);
    if (aobj != 0) {
        conv_aobjdesc(c, aobj);
    }
    if (texanim != 0) {
        conv_texanim(c, texanim);
    }
    if (renderanim != 0) {
        conv_renderanim(c, renderanim);
    }
    if (next != 0) {
        conv_matanim(c, next);
    }
}

/* HSD_MatAnimJoint: child; next; MatAnim* matanim. */
static void conv_matanim_joint(Conv* c, uint32_t off)
{
    uint32_t child;
    uint32_t next;
    uint32_t matanim;

    if (!in_data(c, off, HSD_MATANIMJOINT_SIZE) || !mark(c, off)) {
        return;
    }
    child = rd32(c, off + 0x00);
    next = rd32(c, off + 0x04);
    matanim = rd32(c, off + 0x08);
    if (matanim != 0) {
        conv_matanim(c, matanim);
    }
    if (child != 0) {
        conv_matanim_joint(c, child);
    }
    if (next != 0) {
        conv_matanim_joint(c, next);
    }
}

/* HSD_ShapeAnimDObj: next; ShapeAnim* shapeanim. */
static void conv_shapeanim_dobj(Conv* c, uint32_t off)
{
    uint32_t next;
    uint32_t shapeanim;

    if (!in_data(c, off, HSD_SHAPEANIM_DOBJ_SIZE) || !mark(c, off)) {
        return;
    }
    next = rd32(c, off + 0x00);
    shapeanim = rd32(c, off + 0x04);
    while (shapeanim != 0 && in_data(c, shapeanim, HSD_SHAPEANIM_SIZE)) {
        uint32_t chain_next = rd32(c, shapeanim + 0x00);
        uint32_t aobj = rd32(c, shapeanim + 0x04);
        if (!mark(c, shapeanim)) {
            break;
        }
        if (aobj != 0) {
            conv_aobjdesc(c, aobj);
        }
        shapeanim = chain_next;
    }
    if (next != 0) {
        conv_shapeanim_dobj(c, next);
    }
}

/* HSD_ShapeAnimJoint: child; next; ShapeAnimDObj*. */
static void conv_shapeanim_joint(Conv* c, uint32_t off)
{
    uint32_t child;
    uint32_t next;
    uint32_t dobj;

    if (!in_data(c, off, HSD_SHAPEANIMJOINT_SIZE) || !mark(c, off)) {
        return;
    }
    child = rd32(c, off + 0x00);
    next = rd32(c, off + 0x04);
    dobj = rd32(c, off + 0x08);
    if (dobj != 0) {
        conv_shapeanim_dobj(c, dobj);
    }
    if (child != 0) {
        conv_shapeanim_joint(c, child);
    }
    if (next != 0) {
        conv_shapeanim_joint(c, next);
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
    if (position != 0 && in_data(c, position, 8)) {
        conv_wobjanim(c, position);
    }
    if (interest != 0 && in_data(c, interest, 8)) {
        conv_wobjanim(c, interest);
    }
    if (next != 0 && in_data(c, next, 0x10)) {
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

/* Scene archives expose `Sc*_scene_lights` as a NUL-terminated
 * `LightList**` array (lb_80011AC4 walks it). */
static void conv_lightlist_array(Conv* c, uint32_t off)
{
    int guard;

    for (guard = 0; guard < 128; guard++) {
        uint32_t p = off + (uint32_t) guard * 4;
        uint32_t list;
        if (!in_data(c, p, 4)) {
            break;
        }
        list = rd32(c, p);
        if (list == 0) {
            break;
        }
        if (!in_data(c, list, 8)) {
            break;
        }
        conv_lightlist(c, list);
    }
}

/* ------------------------------------------------- effect PS banks
 * Ef*.dat `eff*DataTable` publics point at two self-relocating bank blobs.
 * `psInitDataBankLocate` (particle.c) does the pointer fix-ups at runtime
 * relative to the bank base, so the converter must only bring the numeric
 * fields (counts, versions, relative offsets, group/command structs) to host
 * order.  Without this the count fields read big-endian and the runtime walks
 * off the end of the bank (EfCoData.dat, gm_Scene_Vs_OnEnter). */

#define HSD_PSTEXGROUP_SIZE 0x1C
#define HSD_PSMAX_CMDS 8192
#define HSD_PSMAX_GROUPS 4096

static void conv_ps_cmd_bank(Conv* c, uint32_t off)
{
    uint16_t version;
    uint32_t count;
    uint32_t i;

    if (!in_data(c, off, 0x10)) {
        return;
    }
    version = be16(c->data + off);
    if (version >= 0x40 && version < 0x44) {
        /* header: version/pad, num, nb_reloc; reloc list at +12 */
        conv_u16(c, off);
        conv_u32(c, off + 4);
        conv_u32(c, off + 8);
        count = rd32(c, off + 8);
        if (count > HSD_PSMAX_CMDS) {
            count = HSD_PSMAX_CMDS;
        }
        for (i = 0; i < count; i++) {
            uint32_t p = off + 12 + i * 4;
            uint32_t cmd;
            if (!in_data(c, p, 4)) {
                break;
            }
            conv_u32(c, p);
            cmd = off + rd32(c, p);
            if (in_data(c, cmd, 12)) {
                conv_u32(c, cmd + 8); /* HSD_PSCmdList.kind */
            }
        }
    } else if (version == 0) {
        /* header: version/pad, count; pointer table at +8 */
        conv_u16(c, off);
        conv_u16(c, off + 2);
        conv_u32(c, off + 4);
        count = rd32(c, off + 4);
        if (count > HSD_PSMAX_CMDS) {
            count = HSD_PSMAX_CMDS;
        }
        for (i = 0; i < count; i++) {
            uint32_t p = off + 8 + i * 4;
            uint32_t cmd;
            if (!in_data(c, p, 4)) {
                break;
            }
            conv_u32(c, p);
            cmd = off + rd32(c, p);
            if (in_data(c, cmd, 12)) {
                conv_u32(c, cmd + 8);
            }
        }
    }
}

static void conv_ps_tex_bank(Conv* c, uint32_t off)
{
    uint16_t version;
    uint32_t num_groups;
    uint32_t k;

    if (!in_data(c, off, 0x10)) {
        return;
    }
    version = be16(c->data + off);
    if (version != 0) {
        return; /* 0x40-style tex banks are not used by the retail data */
    }
    conv_u32(c, off); /* version | num_groups */
    num_groups = rd32(c, off) & 0xFFFFu;
    if (num_groups > 64) {
        num_groups = 64;
    }
    for (k = 0; k < num_groups; k++) {
        uint32_t gp = off + 4 + k * 4;
        uint32_t g;
        uint32_t num;
        uint32_t fmt;
        uint32_t palnum;
        uint32_t table_count;
        uint32_t j;

        if (!in_data(c, gp, 4)) {
            break;
        }
        conv_u32(c, gp);
        g = off + rd32(c, gp);
        if (!in_data(c, g, HSD_PSTEXGROUP_SIZE)) {
            continue;
        }
        conv_u32(c, g + 0x00);
        conv_u32(c, g + 0x04);
        conv_u32(c, g + 0x08);
        conv_u32(c, g + 0x0C);
        conv_u32(c, g + 0x10);
        conv_u16(c, g + 0x14);
        conv_u16(c, g + 0x16);
        num = rd32(c, g + 0x00);
        fmt = rd32(c, g + 0x04);
        palnum = rd16(c, g + 0x14);
        table_count = num;
        if (fmt == 8 || fmt == 9 || fmt == 10) {
            if (rd16(c, g + 0x16) & 1) {
                table_count += 1;
            } else if (palnum != 0) {
                table_count += palnum;
            } else {
                table_count *= 2;
            }
        }
        if (table_count > 1024) {
            table_count = 1024;
        }
        for (j = 0; j < table_count; j++) {
            uint32_t tp = g + 0x18 + j * 4;
            if (!in_data(c, tp, 4)) {
                break;
            }
            conv_u32(c, tp);
        }
    }
}

/* MapCollData (mp/types.h): own pointers are relocation targets already in
 * host order, but every count/coordinate is numeric.  mpLibLoad and the
 * collision code read them directly, so convert the header and the
 * verts/lines/joints arrays. */
#define MAPCOLL_SIZE 0x30
#define MAPLINE_SIZE 0x10
#define MAPJOINT_SIZE 0x28

static void conv_map_line(Conv* c, uint32_t off)
{
    int i;
    for (i = 0; i < MAPLINE_SIZE / 2; i++) {
        conv_u16(c, off + (uint32_t) i * 2);
    }
}

static void conv_map_joint(Conv* c, uint32_t off)
{
    int i;
    for (i = 0; i < 10; i++) {
        conv_u16(c, off + (uint32_t) i * 2);
    }
    for (i = 0; i < 4; i++) {
        conv_u32(c, off + 0x14 + (uint32_t) i * 4);
    }
    conv_u16(c, off + 0x24);
    conv_u16(c, off + 0x26);
}

static void conv_coll_data(Conv* c, uint32_t off)
{
    uint32_t verts;
    uint32_t lines;
    uint32_t joints;
    int vert_count;
    int line_count;
    int joint_count;
    int i;

    if (!in_data(c, off, MAPCOLL_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x0C);
    for (i = 0; i < 8; i++) {
        conv_u16(c, off + 0x10 + (uint32_t) i * 2);
    }
    conv_u32(c, off + 0x28);
    /* +0x2C is not converted: in the stage archives map_ptcl/map_texg start
     * exactly there and their version word must stay big-endian for
     * conv_ps_cmd_bank/conv_ps_tex_bank (GrZe coll_data @0x5ccb4,
     * map_ptcl @0x5cce0). */

    verts = rd32(c, off + 0x00);
    lines = rd32(c, off + 0x08);
    joints = rd32(c, off + 0x24);
    vert_count = (int) rd32(c, off + 0x04);
    line_count = (int) rd32(c, off + 0x0C);
    joint_count = (int) rd32(c, off + 0x28);
    if (vert_count < 0 || vert_count > 8192) {
        vert_count = 0;
    }
    if (line_count < 0 || line_count > 8192) {
        line_count = 0;
    }
    if (joint_count < 0 || joint_count > 2048) {
        joint_count = 0;
    }
    if (verts != 0) {
        for (i = 0; i < vert_count * 2; i++) {
            conv_u32(c, verts + (uint32_t) i * 4); /* f32 x/y */
        }
    }
    if (lines != 0) {
        for (i = 0; i < line_count; i++) {
            conv_map_line(c, lines + (uint32_t) i * MAPLINE_SIZE);
        }
    }
    if (joints != 0) {
        for (i = 0; i < joint_count; i++) {
            conv_map_joint(c, joints + (uint32_t) i * MAPJOINT_SIZE);
        }
    }
}

/* Article targets in ItCo.dat: attributes, hurtbones, model desc, dynamics
 * and the per-state joint tables.  All counts/floats are big-endian; the
 * Article itself is six pointers (relocation targets, already host order). */
#define ITEMATTR_SIZE 0x84
#define ITHURTBONEDESC_SIZE 0x20
#define ITMODELDESC_SIZE 0x10
#define BONEDYNAMICSDESC_SIZE 0x18

/* ItemAttr: two bytes of bitfields, then a dense run of 4-byte fields from
 * +0x04 to +0x80 (floats, count/type ints, two itECBs and two Vec2s). */
static void conv_item_attr(Conv* c, uint32_t off)
{
    uint32_t i;

    if (!in_data(c, off, ITEMATTR_SIZE) || !mark(c, off)) {
        return;
    }
    for (i = 0x04; i <= 0x80; i += 4) {
        conv_u32(c, off + i);
    }
}

/* ItHurtBoneList { s32 count; ItHurtBoneDesc* descs; }, desc =
 * { enum_t bone_id; Vec3 a; Vec3 b; f32 scale; } (0x20). */
static void conv_it_hurtbone_list(Conv* c, uint32_t off)
{
    uint32_t descs;
    int count;
    int i;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x00);
    descs = rd32(c, off + 0x04);
    count = (int) rd32(c, off + 0x00);
    if (descs != 0 && count > 0 && count <= 16) {
        for (i = 0; i < count; i++) {
            uint32_t d = descs + (uint32_t) i * ITHURTBONEDESC_SIZE;
            int k;
            if (!in_data(c, d, ITHURTBONEDESC_SIZE)) {
                break;
            }
            conv_u32(c, d + 0x00);
            for (k = 0; k < 7; k++) {
                conv_u32(c, d + 0x04 + (uint32_t) k * 4);
            }
        }
    }
}

/* ItemModelDesc { HSD_Joint* joint; u32 bone_count; s32 attach_id; u8 bits }. */
static void conv_item_model_desc(Conv* c, uint32_t off)
{
    uint32_t joint;

    if (!in_data(c, off, ITMODELDESC_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x08);
    joint = rd32(c, off + 0x00);
    if (joint != 0 && in_data(c, joint, HSD_JOINT_SIZE)) {
        conv_joint(c, joint);
    }
}

/* ItemDynamics { int count; BoneDynamicsDesc* dyn_descs }. */
static void conv_item_dynamics(Conv* c, uint32_t off)
{
    uint32_t descs;
    int count;
    int i;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x00);
    descs = rd32(c, off + 0x04);
    count = (int) rd32(c, off + 0x00);
    if (descs != 0 && count > 0 && count <= 64) {
        for (i = 0; i < count; i++) {
            uint32_t d = descs + (uint32_t) i * BONEDYNAMICSDESC_SIZE;
            if (!in_data(c, d, BONEDYNAMICSDESC_SIZE)) {
                break;
            }
            conv_u32(c, d + 0x00);                       /* bone_id */
            conv_u32(c, d + 0x08);                       /* count */
            conv_u32(c, d + 0x0C);                       /* pos.x */
            conv_u32(c, d + 0x10);
            conv_u32(c, d + 0x14);
        }
    }
}

/* ItemStateArray: 8 ItemStateDesc { AnimJoint*; MatAnimJoint*;
 * ShapeAnimJoint*; UNK script } — pointer targets need their own walks. */
static void conv_item_state_array(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 8 * 0x10) || !mark(c, off)) {
        return;
    }
    for (i = 0; i < 8; i++) {
        uint32_t st = off + (uint32_t) i * 0x10;
        uint32_t anim = rd32(c, st + 0x00);
        uint32_t mat = rd32(c, st + 0x04);
        uint32_t shape = rd32(c, st + 0x08);
        if (anim != 0 && in_data(c, anim, HSD_ANIMJOINT_SIZE)) {
            conv_anim_joint(c, anim);
        }
        if (mat != 0 && in_data(c, mat, HSD_MATANIMJOINT_SIZE)) {
            conv_matanim_joint(c, mat);
        }
        if (shape != 0 && in_data(c, shape, HSD_SHAPEANIMJOINT_SIZE)) {
            conv_shapeanim_joint(c, shape);
        }
    }
}

/* Article { ItemAttr*; special*; ItHurtBoneList*; ItemStateArray*;
 * ItemModelDesc*; ItemDynamics* } — six relocation targets. */
static void conv_article(Conv* c, uint32_t off)
{
    uint32_t attr;
    uint32_t hurt;
    uint32_t states;
    uint32_t model;
    uint32_t dynamics;

    if (!in_data(c, off, 0x18) || !mark(c, off)) {
        return;
    }
    attr = rd32(c, off + 0x00);
    hurt = rd32(c, off + 0x08);
    states = rd32(c, off + 0x0C);
    model = rd32(c, off + 0x10);
    dynamics = rd32(c, off + 0x14);
    if (attr != 0) {
        conv_item_attr(c, attr);
    }
    if (hurt != 0) {
        conv_it_hurtbone_list(c, hurt);
    }
    if (states != 0) {
        conv_item_state_array(c, states);
    }
    if (model != 0) {
        conv_item_model_desc(c, model);
    }
    if (dynamics != 0) {
        conv_item_dynamics(c, dynamics);
    }
}

/* Common/character/pokemon Article* arrays.  Counts come from the item kind
 * enums (it/forward.h): common = It_Kind_Kuriboh (43), character =
 * It_PKind_Start - It_Kind_Kuriboh (118), pokemon =
 * It_Kind_Old_Kuri - It_PKind_Start (47). */
static void conv_article_array(Conv* c, uint32_t off, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        uint32_t p = off + (uint32_t) i * 4;
        uint32_t article;
        if (!in_data(c, p, 4)) {
            break;
        }
        article = rd32(c, p);
        if (article == 0) {
            continue;
        }
        conv_article(c, article);
    }
}

/* PlCo.dat `ftLoadCommonData`: an array of table pointers.  The first is
 * the big ftCommonData struct (0x818 of 4-byte floats/ints), and index 4 is
 * the per-kind FighterPartsTable array whose parts_num is read as a loop
 * bound (ftParts_80074E58). */
#define FTCOMMONDATA_SIZE 0x818

static void conv_ft_common_data(Conv* c, uint32_t off)
{
    uint32_t common;
    uint32_t parts;
    uint32_t i;

    if (!in_data(c, off, 23 * 4) || !mark(c, off)) {
        return;
    }
    common = rd32(c, off + 0x00);
    parts = rd32(c, off + 4 * 4);
    if (common != 0 && in_data(c, common, FTCOMMONDATA_SIZE)) {
        for (i = 0; i < FTCOMMONDATA_SIZE; i += 4) {
            conv_u32(c, common + i);
        }
    }
    if (parts != 0) {
        for (i = 0; i < 64; i++) {
            uint32_t p = parts + i * 4;
            uint32_t table;
            if (!in_data(c, p, 4)) {
                break;
            }
            table = rd32(c, p);
            if (table == 0) {
                continue;
            }
            if (in_data(c, table, 12)) {
                conv_u32(c, table + 0x08); /* parts_num */
            }
        }
    }
}

/* Pl*.dat `ftData`: mostly relocation targets, but the xC/x14
 * Fighter_WaitAnimData arrays carry FigaTree offsets (x4/x8) that are copied
 * verbatim into the runtime and read as sizes (ftData_80085A14 asserts when
 * they stay big-endian).  Also the x8->x0 model_num and a few leaf structs. */
#define FT_WAITANIM_SIZE 0x18

static void conv_ft_data(Conv* c, uint32_t off)
{
    uint32_t x8;
    uint32_t arr;
    uint32_t x30;
    uint32_t x34;
    uint32_t x44;
    uint32_t x50;
    int i;

    if (!in_data(c, off, 0x60) || !mark(c, off)) {
        return;
    }
    x8 = rd32(c, off + 0x08);
    x30 = rd32(c, off + 0x30);
    /* x5C is the costume joint tree (its MObj rendermodes feed DObjLoad). */
    {
        uint32_t joint = rd32(c, off + 0x5C);
        if (joint != 0 && in_data(c, joint, HSD_JOINT_SIZE)) {
            conv_joint(c, joint);
        }
    }
    x34 = rd32(c, off + 0x34);
    x44 = rd32(c, off + 0x44);
    x50 = rd32(c, off + 0x50);
    conv_u32(c, off + 0x54);

    /* x8 can legitimately be data offset 0 (G-023): the ftData_x8 tables of
     * PlMr.dat live at the start of the data section. */
    if (in_data(c, x8, 0x18)) {
        uint32_t cost_tbl;
        int n_tobjs;
        int k;
        conv_u32(c, x8 + 0x00); /* FtPartsDesc.model_num */
        conv_u32(c, x8 + 0x08); /* ftData_x8_x8.x8 */
        /* ftData_x8_x8.xC: per-costume u16 arrays of TObj indices (the
         * values are numeric and looked up in the model tree). */
        n_tobjs = (int) rd32(c, x8 + 0x08);
        cost_tbl = rd32(c, x8 + 0x0C);
        if (cost_tbl != 0 && n_tobjs > 0 && n_tobjs <= 64) {
            for (k = 0; k < 8; k++) {
                uint32_t p = cost_tbl + (uint32_t) k * 4;
                uint32_t arr;
                int j;
                if (!in_data(c, p, 4)) {
                    break;
                }
                arr = rd32(c, p);
                if (arr == 0) {
                    continue;
                }
                for (j = 0; j < n_tobjs; j++) {
                    if (!in_data(c, arr + (uint32_t) j * 2, 2)) {
                        break;
                    }
                    conv_u16(c, arr + (uint32_t) j * 2);
                }
            }
        }
    }
    /* xC and x14 are Fighter_WaitAnimData arrays; convert every entry that
     * looks in-bounds (the per-kind count lives in compiled data). */
    arr = rd32(c, off + 0x0C);
    for (i = 0; arr != 0 && i < 512; i++) {
        uint32_t e = arr + (uint32_t) i * FT_WAITANIM_SIZE;
        if (!in_data(c, e, FT_WAITANIM_SIZE)) {
            break;
        }
        conv_u32(c, e + 0x04);
        conv_u32(c, e + 0x08);
    }
    arr = rd32(c, off + 0x14);
    for (i = 0; arr != 0 && i < 512; i++) {
        uint32_t e = arr + (uint32_t) i * FT_WAITANIM_SIZE;
        if (!in_data(c, e, FT_WAITANIM_SIZE)) {
            break;
        }
        conv_u32(c, e + 0x04);
        conv_u32(c, e + 0x08);
    }
    if (x30 != 0 && in_data(c, x30, 8)) {
        conv_u32(c, x30 + 0x00); /* hurtbox init count */
    }
    if (x34 != 0 && in_data(c, x34, 8)) {
        conv_u32(c, x34 + 0x04); /* scale */
    }
    /* ftData_x44_t: six s16 then four f32 (0x1C). */
    if (x44 != 0 && in_data(c, x44, 0x1C)) {
        for (i = 0; i < 6; i++) {
            conv_u16(c, x44 + (uint32_t) i * 2);
        }
        for (i = 0; i < 4; i++) {
            conv_u32(c, x44 + 0x0C + (uint32_t) i * 4);
        }
    }
    /* x50: Vec2 array with count at +0x54. */
    if (x50 != 0) {
        int count = (int) rd32(c, off + 0x54);
        if (count > 0 && count <= 256) {
            for (i = 0; i < count * 2; i++) {
                if (!in_data(c, x50 + (uint32_t) i * 4, 4)) {
                    break;
                }
                conv_u32(c, x50 + (uint32_t) i * 4);
            }
        }
    }
}

/* ItCo.dat (US: ItCo.usd) `itPublicData`: ItemCommonData limits plus the
 * Article tables.  Item_80266FCC copies the ItemCommonData limits into the
 * spawn budget; big-endian limits make it reject every spawn. */
#define ITEMCOMMON_SIZE 0x160

static void conv_item_common_data(Conv* c, uint32_t off)
{
    uint32_t i;

    if (!in_data(c, off, ITEMCOMMON_SIZE) || !mark(c, off)) {
        return;
    }
    for (i = 0; i < 0x48; i += 4) {
        conv_u32(c, off + i);
    }
    /* +0x48 is a byte; the rest is a dense run of 4-byte fields. */
    for (i = 0x4C; i < ITEMCOMMON_SIZE; i += 4) {
        conv_u32(c, off + i);
    }
}

static void conv_it_public_data(Conv* c, uint32_t off)
{
    uint32_t common;
    uint32_t x10;

    if (!in_data(c, off, 0x18) || !mark(c, off)) {
        return;
    }
    common = rd32(c, off + 0x00);
    x10 = rd32(c, off + 0x10);
    if (common != 0) {
        conv_item_common_data(c, common);
    }
    {
        uint32_t x4 = rd32(c, off + 0x04);
        uint32_t x8 = rd32(c, off + 0x08);
        uint32_t xC = rd32(c, off + 0x0C);
        if (x4 != 0) {
            conv_article_array(c, x4, 43);
        }
        if (x8 != 0) {
            conv_article_array(c, x8, 118);
        }
        if (xC != 0) {
            conv_article_array(c, xC, 47);
        }
    }
    if (x10 != 0 && in_data(c, x10, 0x1C)) {
        /* it_804D6D40_t: { s32 x0; f32 x4; f32 x8; f32 xC; f32 x10;
         * f32 x14; f32 x18; } (it_3F14.h) */
        int i;
        for (i = 0; i < 7; i++) {
            conv_u32(c, x10 + (uint32_t) i * 4);
        }
    }
}

/* eff*DataTable { void* cmd_bank; void* tex_bank; } */
static void conv_ef_dat(Conv* c, uint32_t off)
{
    uint32_t cmd;
    uint32_t tex;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    cmd = rd32(c, off + 0x00);
    tex = rd32(c, off + 0x04);
    if (cmd != 0 && in_data(c, cmd, 0x10)) {
        conv_ps_cmd_bank(c, cmd);
    }
    if (tex != 0 && in_data(c, tex, 0x10)) {
        conv_ps_tex_bank(c, tex);
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
    /* unk0/unk4: Ground_801C34AC's { void* joint; s16* pairs; s32 count; }
     * entry table (joint ordering used by stage setup). */
    {
        uint32_t entries = rd32(c, off + 0x00);
        uint32_t count = rd32(c, off + 0x04);
        uint32_t ei;
        if (entries != 0 && count <= 4096) {
            for (ei = 0; ei < count; ei++) {
                uint32_t e = entries + ei * 12;
                uint32_t pairs;
                int pair_count;
                int pj;
                if (!in_data(c, e, 12)) {
                    break;
                }
                pairs = rd32(c, e + 4);
                conv_u32(c, e + 8);
                pair_count = (int) rd32(c, e + 8);
                if (pairs != 0 && pair_count > 0 && pair_count <= 2048) {
                    for (pj = 0; pj < pair_count; pj++) {
                        conv_u16(c, pairs + (uint32_t) pj * 2);
                    }
                }
            }
        }
    }
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
        /* GrJoint[]: three s16 per entry (stage joint flags). */
        {
            uint32_t grjoints = rd32(c, e + 0x20);
            int grcount = (int) rd32(c, e + 0x24);
            int gj;
            if (grjoints != 0 && grcount > 0 && grcount <= 4096) {
                for (gj = 0; gj < grcount; gj++) {
                    conv_u16(c, grjoints + (uint32_t) gj * 6);
                    conv_u16(c, grjoints + (uint32_t) gj * 6 + 2);
                    conv_u16(c, grjoints + (uint32_t) gj * 6 + 4);
                }
            }
        }
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

        /* data offset 0 is a valid target (see G-023); the name dispatch
         * below decides whether it is a descriptor class we walk. */
        if (name_ends_with(name, length, "matanim_joint")) {
            c->st.roots_anim++;
            conv_matanim_joint(c, data_off);
        } else if (name_ends_with(name, length, "shapeanim_joint")) {
            c->st.roots_anim++;
            conv_shapeanim_joint(c, data_off);
        } else if (name_ends_with(name, length, "animjoint")) {
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
        } else if (name_ends_with(name, length, "_camera")) {
            /* GmTtAll/Mn*: Sc*_cam_int1_camera */
            conv_cobjdesc(c, data_off);
        } else if (name_ends_with(name, length, "_scene_lights")) {
            /* Sc*_scene_lights: NUL-terminated LightList** array */
            conv_lightlist_array(c, data_off);
        } else if (name_ends_with(name, length, "_fog")) {
            conv_fogdesc(c, data_off);
        } else if (name_ends_with(name, length, "DataTable")) {
            /* Ef*.dat eff*DataTable: cmd/tex PS bank pair */
            conv_ef_dat(c, data_off);
        } else if (length >= 16 &&
                   memcmp(name, "ftLoadCommonData", 16) == 0) {
            conv_ft_common_data(c, data_off);
        } else if (length >= 6 && memcmp(name, "ftData", 6) == 0) {
            conv_ft_data(c, data_off);
        } else if (length == 12 && memcmp(name, "itPublicData", 12) == 0) {
            conv_it_public_data(c, data_off);
        } else if (length == 9 && memcmp(name, "coll_data", 9) == 0) {
            conv_coll_data(c, data_off);
        } else if (length == 8 && memcmp(name, "map_ptcl", 8) == 0) {
            conv_ps_cmd_bank(c, data_off);
        } else if (length == 8 && memcmp(name, "map_texg", 8) == 0) {
            conv_ps_tex_bank(c, data_off);
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

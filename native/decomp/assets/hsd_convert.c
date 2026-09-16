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
#include <unistd.h>

#include <sysdolphin/baselib/archive.h>

#define HSD_CONVERTER_VERSION 130u
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
#define HSD_SPLINE_SIZE 0x18
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
#define FT_KIND_MAX 33
#define FT_CPU_ATTACK_ENTRY_SIZE 0x24

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

/* Per-stage `yakumono_param` layout.  The archive is identified by its own
 * `Grd<Stage>*` public symbols (P-662; G-130/P-661 started this for GrYt).
 * Several stages reuse another stage's textures and thus another stage's
 * symbols (GrVe carries GrdVenom and GrdCorneria names, the adventure routes
 * carry GrdDonkey and GrdCastle names), so the marker table is ordered most
 * specific first and the scan checks every marker before giving up. */
enum {
    STAGE_PARAM_NONE = 0,
    STAGE_PARAM_YORSTER,
    STAGE_PARAM_CORNERIA,
    STAGE_PARAM_IZUMI,
    STAGE_PARAM_KONGO,
    STAGE_PARAM_STORY,
    STAGE_PARAM_VENOM,
    STAGE_PARAM_ONETT,
    STAGE_PARAM_INISHIE1,
    STAGE_PARAM_CASTLE,
    STAGE_PARAM_PSTADIUM,
    STAGE_PARAM_KRAID,
    STAGE_PARAM_MUTECITY,
    STAGE_PARAM_FIGUREGET,
    STAGE_PARAM_BIGBLUE,
    STAGE_PARAM_OLDPUPUPU,
    STAGE_PARAM_OLDKONGO,
    STAGE_PARAM_GREENS,
    STAGE_PARAM_RCRUISE,
    STAGE_PARAM_INISHIE2,
    STAGE_PARAM_GARDEN,
    STAGE_PARAM_OLDYOSHI,
    STAGE_PARAM_ICEMT,
};

typedef struct StageParamMarker {
    const char* marker;
    int layout;
} StageParamMarker;

static const StageParamMarker stage_param_markers[] = {
    { "GrdVenomBase", STAGE_PARAM_VENOM },
    { "GrdCorneriaAwbody", STAGE_PARAM_CORNERIA },
    { "GrdIzumiBulbon", STAGE_PARAM_IZUMI },
    { "GrdDonkeyKareki", STAGE_PARAM_KONGO },
    { "GrdStory", STAGE_PARAM_STORY },
    { "GrdOnett", STAGE_PARAM_ONETT },
    { "GrdInishie1", STAGE_PARAM_INISHIE1 },
    { "GrdYorster", STAGE_PARAM_YORSTER },
    { "GrdCastleCast", STAGE_PARAM_CASTLE },
    /* GrPs.dat and GrPs3.dat, but NOT GrHr.dat.  Three archives on the disc
     * carry `GrdPStadium*` names: the Home-Run Contest stage reuses Pokemon
     * Stadium's textures, while its own `yakumono_param` is floats (30.0f,
     * 8.0f, -10.18f) on a different struct.  Only the marker has to
     * discriminate, and it must be a **public**, not an external -- the scan
     * below walks `nb_public` only, so the obvious `GrdPStadiumRock_TopN_joint`
     * would never match.  Of the three publics GrPs/GrPs3 have and GrHr does
     * not, this is one; checked against every Gr*.dat on the disc.  Kept last
     * so GrHr keeps matching `GrdYorster` exactly as it does today. */
    { "GrdPStadiumSteelK", STAGE_PARAM_PSTADIUM },
    /* P-708's priority three: the stages whose intro countdown drives the
     * looping ambient (`Ground_801C5440`), the same shape as Peach's Castle
     * (P-707) and Pokemon Stadium (P-738).  Each marker is a **public** and
     * was checked to be unique across every Gr*.dat that has a
     * `yakumono_param`. */
    { "GrdKraidAntenna1", STAGE_PARAM_KRAID },
    { "GrdFzeroAdver1", STAGE_PARAM_MUTECITY },
    { "GrdBigBlueArch2", STAGE_PARAM_BIGBLUE },
    /* P-770: Dream Land.  `GrdOldpupupu` appears in 62 of GrOp.dat's 72
     * publics and in **no other archive on the disc**, and GrOp.dat matches
     * none of the markers above, which is why it was falling through to the
     * raw fallback. */
    { "GrdOldpupupu", STAGE_PARAM_OLDPUPUPU },
    /* P-791: the Classic trophy bonus stage.  `GrdFigureget` appears in 44 of
     * GrNFg.dat's publics and in **no other archive on the disc**, and GrNFg
     * matched none of the markers above, so its parameters were being left
     * raw. */
    { "GrdFigureget", STAGE_PARAM_FIGUREGET },
    /* P-708's remaining audit.  Each marker was checked to appear in exactly
     * one archive on the disc, and each of these five matched none of the
     * markers above -- which is why they were on the raw fallback.  Note
     * `GedOldkongo`: the Kongo Jungle N64 publics really are spelled with a
     * `Ged` prefix, not `Grd`. */
    { "GedOldkongo", STAGE_PARAM_OLDKONGO },
    { "GrdGreensGround", STAGE_PARAM_GREENS },
    { "GrdRCruiseShip", STAGE_PARAM_RCRUISE },
    { "GrdInishie2Wa", STAGE_PARAM_INISHIE2 },
    { "GrdGardenKoya", STAGE_PARAM_GARDEN },
    { "GrdOldyoshi", STAGE_PARAM_OLDYOSHI },
    { "GrdIcemt", STAGE_PARAM_ICEMT },
};

typedef struct Conv {
    unsigned char* d;      /* archive base */
    unsigned char* data;   /* data section (archive base + 0x20) */
    size_t size;
    size_t data_size;
    unsigned char* reloc; /* one byte per data offset: relocation target */
    unsigned char* seen;  /* one byte per data offset: walked */
    unsigned char* num;   /* one byte per data offset: numeric field done */
    HsdConvertStats st;
    int depth;
    int stage_layout; /* selected `yakumono_param` layout */
    uint32_t reloc_off; /* relocation table, for next_pointed_at_after */
    uint32_t nb_reloc;
    uint32_t public_off; /* public symbol table, for next_public_after */
    uint32_t nb_public;
} Conv;

static uint32_t next_pointed_at_after(Conv* c, uint32_t off);
static uint32_t next_public_after(Conv* c, uint32_t public_off,
                                  uint32_t nb_public, uint32_t off);

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

static float be_f32(const unsigned char* p)
{
    uint32_t v = be32(p);
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
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
    /* `off + need` can wrap on the 32-bit product (e.g. a
     * 0xFFFFFFE0 fogadjdesc offset passes `off + 0x44 <= data_size`), which
     * then indexes c->num/c->reloc before the heap buffer.  Compare against
     * the remaining size instead. */
    return (size_t) off <= c->data_size &&
           need <= c->data_size - (size_t) off;
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
    /* Descriptor fields are 4-aligned; an unaligned offset means a walker
     * misidentified data (and a write would cross pointer boundaries). */
    if (!in_data(c, off, 4) || (off & 3u) || c->reloc[off] || c->num[off]) {
        return;
    }
    c->num[off] = 1;
    wr32(c->data + off, be32(c->data + off));
}

static void conv_u16(Conv* c, uint32_t off)
{
    /* A u16 field never shares a word with a relocation target: if the word
     * is a pointer, this is a walk misinterpreting data, and writing would
     * corrupt the pointer's halves.  Unaligned offsets are the same class. */
    if (!in_data(c, off, 2) || (off & 1u) || c->num[off] ||
        c->reloc[off & ~3u])
    {
        return;
    }
    c->num[off] = 1;
    wr16(c->data + off, be16(c->data + off));
}

/* A pointer field the relocation table does not name is not a pointer.
 *
 * `ItemStateDesc.x4_matanim_joint` and `.x8_shapeanim_joint` are **extern
 * patch sites** in several archives -- the linked list `HSD_ArchiveLocateExtern`
 * walks, where each site holds the *offset of the next site* and the game
 * patches them all to NULL at load (`lbArchive_InitializeDAT`).  Since
 * `convert_extern_chains` byte-swaps those links (P-771), they now read as
 * plausible in-range data offsets, and following one walks straight into
 * unrelated structures: in `GrCn.dat` this marched a `conv_matanim_joint`
 * chain through four sites into `conv_texanim(0x5fc68)`, which is the Arwing
 * laser's **model root joint**.  Marking it there made the real `conv_joint`
 * bail, so the PObj stayed big-endian and `GXSetVtxDesc` segfaulted (P-782).
 *
 * The relocation table separates the two exactly, the same way it does for
 * `AObjDesc.obj_id` in P-786: a real pointer field is in it, a patch site or
 * a numeric id is not.  This is the narrow fix at the site that produced a
 * crash; the general form is a `follow()` helper used by every walker that
 * dereferences a descriptor field, and the P-782 row argues for it. */
static uint32_t follow_ptr(Conv* c, uint32_t field)
{
    if (!in_data(c, field, 4) || !c->reloc[field]) {
        return 0;
    }
    return rd32(c, field);
}


static void conv_vtxdesc(Conv* c, uint32_t off);
static void conv_pobj(Conv* c, uint32_t off);
static void conv_tobj(Conv* c, uint32_t off);
static void conv_mobj(Conv* c, uint32_t off);
static void conv_dobj(Conv* c, uint32_t off);
static void conv_joint(Conv* c, uint32_t off);
static void conv_robjdesc(Conv* c, uint32_t off);
static void conv_spline(Conv* c, uint32_t off);
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
static void conv_static_model_full(Conv* c, uint32_t off);
static void conv_stage_maphead(Conv* c, uint32_t off);
static void conv_shapeanim_joint(Conv* c, uint32_t off);
static void conv_dynamic_models(Conv* c, uint32_t off);
static void conv_regclear_spawn_table(Conv* c, uint32_t off);
static void conv_yorster_param(Conv* c, uint32_t off);
static void conv_article(Conv* c, uint32_t off, int item_kind);

/* DWARF: HSD_ImageDesc */
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

/* DWARF: HSD_TlutDesc */
static void conv_tlutdesc(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_TLUTDESC_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x08);
    conv_u16(c, off + 0x0C);
}

/* DWARF: HSD_TexLODDesc */
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

/* DWARF: HSD_TObjTevDesc */
static void conv_tobjtevdesc(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_TOBJTEVDESC_SIZE) || !mark(c, off)) {
        return;
    }
    /* 16 u8 fields + three GXColor runs are bytes; only `active`. */
    conv_u32(c, off + 0x1C);
}

/* DWARF: HSD_TObjDesc */
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

/* DWARF: HSD_MObjDesc */
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

/* DWARF: HSD_VtxDescList */
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
/* DWARF: HSD_ShapeSetDesc */
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

/* DWARF: HSD_PObjDesc */
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

/* DWARF: HSD_DObjDesc */
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

/* DWARF: HSD_RvalueList */
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

/* DWARF: HSD_RObjDesc */
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

/* DWARF: HSD_Joint */
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
    } else if ((flags & HSD_JOBJ_SPLINE) && !(flags & HSD_JOBJ_PTCL) &&
               u != 0) {
        /* The +0x10 union is an HSD_Spline for these joints, and skipping it
         * left the whole spline big-endian: `numcv`, `tension`, the Vec3
         * control points and the precomputed arc-length tables.  Evaluating
         * that spline returns NaN, and on Mute City the NaN went straight
         * into the collision mesh -- grMuteCity_801F0D20 feeds the spline
         * point to mpLineSetPos, so groundCollVtx positions became NaN and
         * mpJointUpdateDynamics asserted on a line it could not classify.
         * NaN fails every comparison, so it reaches the final `else` that
         * is meant for a zero-length line (P-769). */
        conv_spline(c, u);
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

/* HSD_Spline (spline.h:6): { u8 type; s16 numcv; f32 tension; Vec3* cv;
 * f32 totalLength; f32* segLength; f32 (*segPoly)[5] }.
 *
 * The three arrays have type-dependent lengths -- `cv` is indexed
 * `[idx]`, `[idx*3]` or `[idx]-1` depending on `type` (spline.c:91-131), and
 * a bezier reads four control points per segment -- so a per-type count would
 * be a guess, and guessing a fighter/stage array length is what P-739 was.
 * Bound each one by the next thing anything points at, which is exact and is
 * how the rest of this file bounds an untyped block. */
/* DWARF: HSD_Spline */
static void conv_spline(Conv* c, uint32_t off)
{
    static const uint32_t arrays[] = { 0x08, 0x10, 0x14 };
    size_t a;

    if (!in_data(c, off, HSD_SPLINE_SIZE) || !mark(c, off)) {
        return;
    }
    /* `type` is a u8 at +0x00 with a pad byte after it; numcv is the s16 at
     * +0x02, so the first word is not a plain u32. */
    conv_u16(c, off + 0x02);
    conv_u32(c, off + 0x04); /* tension */
    conv_u32(c, off + 0x0C); /* totalLength */
    for (a = 0; a < ARRAY_SIZE(arrays); a++) {
        uint32_t arr = rd32(c, off + arrays[a]);
        uint32_t end;
        uint32_t w;
        if (arr == 0 || !in_data(c, arr, 4)) {
            continue;
        }
        end = next_pointed_at_after(c, arr);
        if (end <= arr) {
            continue;
        }
        for (w = 0; arr + w + 4 <= end; w += 4) {
            conv_u32(c, arr + w);
        }
    }
}

/* DWARF: HSD_AObjDesc */
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
    /* `obj_id` is a plain numeric ID on AnimJoint tracks and a **JObj offset**
     * on the WObj/Light tracks -- `HSD_AObjLoadDesc` (aobj.c:199) looks the ID
     * up and falls back to `HSD_JObjLoadJoint((void*) obj_id)` when it misses.
     * The relocation table tells the two apart exactly, and that is the whole
     * rule: an ID is a number and never a relocation field, a JObj offset
     * always is.  `conv_aobjdesc_ref` already did this for the roots known to
     * carry the second kind, but the same descriptors reach here through
     * ordinary AnimJoint trees too, and there the joint stayed big-endian --
     * `JObjLoad` then walked into `HSD_MObjLoadDesc` with a garbage material
     * and segfaulted on entry to every Target Test stage (P-786). */
    if (c->reloc[off + 0x0C]) {
        uint32_t obj = rd32(c, off + 0x0C);
        if (obj != 0 && in_data(c, obj, HSD_JOINT_SIZE)) {
            conv_joint(c, obj);
        }
    }
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
/* DWARF: HSD_AObjDesc */
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
/* DWARF: HSD_TexAnim */
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
    /* id is a GXTexMapID enum, not a reloc target: without the swap an
     * id of 1 reads back as 0x01000000 and lookupTextureAnim never binds
     * TEXMAP1 TexAnims (title logo fire, P-650). id 0 is unaffected. */
    conv_u32(c, off + 0x04);
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
/* DWARF: HSD_ChanAnim */
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
/* DWARF: HSD_RenderAnim */
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
/* DWARF: HSD_MatAnim */
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
/* DWARF: HSD_MatAnimJoint */
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

/* DynamicModelDesc { HSD_Joint*; HSD_AnimJoint**; HSD_MatAnimJoint**;
 * HSD_ShapeAnimJoint** } (src/melee/sc/types.h).  `SceneDesc.models` and the
 * `.scemdls` sections (IfAll "Stc_scemdls" et al.) are NULL-terminated arrays
 * of those descriptors; HSD_JObjAddAnimAll walks the anim arrays. */
/* One `DynamicModelDesc`.  Factored out of `conv_dynamic_models` so the
 * stage's `quake_model_set` can reach it: that public is a **single** desc,
 * not the NULL-terminated array of desc *pointers* the scene sections use --
 * `grLib_801C9AF4` reads `stage_info.quake_model_set->joint` and
 * `->anims[quake_idx]` directly (grlib.c:240,249). */
/* DWARF: DynamicModelDesc */
static void conv_dynamic_model_desc(Conv* c, uint32_t desc)
{
    uint32_t arr;
    int k;

    if (desc == 0 || !in_data(c, desc, 0x10)) {
        return;
    }
    if (rd32(c, desc + 0x00) != 0) {
        conv_joint(c, rd32(c, desc + 0x00));
    }
    arr = rd32(c, desc + 0x04);
    for (k = 0; k < 64 && arr != 0; k++) {
        uint32_t a = rd32(c, arr + (uint32_t) k * 4);
        if (a == 0 || !in_data(c, a, HSD_ANIMJOINT_SIZE)) {
            break;
        }
        conv_anim_joint(c, a);
    }
    arr = rd32(c, desc + 0x08);
    for (k = 0; k < 64 && arr != 0; k++) {
        uint32_t a = rd32(c, arr + (uint32_t) k * 4);
        if (a == 0 || !in_data(c, a, HSD_MATANIMJOINT_SIZE)) {
            break;
        }
        conv_matanim_joint(c, a);
    }
    arr = rd32(c, desc + 0x0C);
    for (k = 0; k < 64 && arr != 0; k++) {
        uint32_t a = rd32(c, arr + (uint32_t) k * 4);
        if (a == 0 || !in_data(c, a, HSD_SHAPEANIMJOINT_SIZE)) {
            break;
        }
        conv_shapeanim_joint(c, a);
    }
}

static void conv_dynamic_models(Conv* c, uint32_t off)
{
    uint32_t p = off;
    int guard;

    if (!in_data(c, off, 4) || !mark(c, off)) {
        return;
    }
    for (guard = 0; guard < 64; guard++) {
        uint32_t desc;
        uint32_t arr;
        int k;

        if (!in_data(c, p, 4)) {
            break;
        }
        desc = rd32(c, p);
        if (desc == 0 || !in_data(c, desc, 0x10)) {
            break;
        }
        if (rd32(c, desc + 0x00) != 0) {
            conv_joint(c, rd32(c, desc + 0x00));
        }
        arr = rd32(c, desc + 0x04);
        for (k = 0; k < 64 && arr != 0; k++) {
            uint32_t a = rd32(c, arr + (uint32_t) k * 4);
            if (a == 0 || !in_data(c, a, HSD_ANIMJOINT_SIZE)) {
                break;
            }
            conv_anim_joint(c, a);
        }
        arr = rd32(c, desc + 0x08);
        for (k = 0; k < 64 && arr != 0; k++) {
            uint32_t a = rd32(c, arr + (uint32_t) k * 4);
            if (a == 0 || !in_data(c, a, HSD_MATANIMJOINT_SIZE)) {
                break;
            }
            conv_matanim_joint(c, a);
        }
        arr = rd32(c, desc + 0x0C);
        for (k = 0; k < 64 && arr != 0; k++) {
            uint32_t a = rd32(c, arr + (uint32_t) k * 4);
            if (a == 0 || !in_data(c, a, HSD_SHAPEANIMJOINT_SIZE)) {
                break;
            }
            conv_shapeanim_joint(c, a);
        }
        p += 4;
    }
}

/* HSD_ShapeAnimDObj: next; ShapeAnim* shapeanim. */
/* DWARF: HSD_ShapeAnimDObj */
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
/* DWARF: HSD_ShapeAnimJoint */
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

/* DWARF: HSD_AnimJoint */
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

/* DWARF: HSD_RObjAnimJoint */
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

/* DWARF: FigaTrack */
static void conv_figatrack(Conv* c, uint32_t off)
{
    if (!in_data(c, off, HSD_FIGATRACK_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00);
    conv_u16(c, off + 0x02);
    /* obj_type/frac_value/frac_slope bytes and the ad stream stay. */
}

/* DWARF: FigaTree */
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

/* DWARF: HSD_WObjDesc */
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

/* DWARF: HSD_CObjDesc partial -- the union's largest variant is the 0x40
 * frustum descriptor; the common header is 0x30 and the projection-specific
 * tail is read only once `projection` says which variant this is. */
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

/* DWARF: HSD_LightDesc */
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
/* DWARF: HSD_WObjAnim */
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
/* DWARF: HSD_LightAnim */
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
/* DWARF: LightList */
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

/* One `HSD_PSCmdList` header (psstructs.h:58).
 *
 * Only `kind` used to be converted, so every other field reached
 * `hsd_8039DAD4` big-endian: `life` 12 read as 0x0C00 = 3072, `size` and
 * `random` as denormals, and `type`/`texGroup` as byte-swapped u16 -- and
 * `texGroup` indexes `psTexGroupArray[bank][]`.  The generator seeds
 * `gen->count` from `random`, and `generator.c:574` is
 * `while (gen->count >= 1.0F)`, so one effect asked for ~4.3e8 particles;
 * `HSD_ObjAllocAddFree` grows the pool 152 bytes at a time out of the HSD
 * heap, which died in well under a second (P-742/G-179).
 *
 * The trailing `cmdList[]` at +0x3C is a **byte stream** and must stay raw,
 * exactly like the fighter subaction scripts in G-178: swapping it would
 * trade this crash for silently wrong particle behaviour. */
#define HSD_PSCMDLIST_HEADER 0x3C

/* DWARF: HSD_PSCmdList partial -- the trailing cmdList[] byte stream at
 * +0x3C must stay raw, see HSD_PSCMDLIST_HEADER above. */
static void conv_ps_cmd_list(Conv* c, uint32_t cmd)
{
    int i;

    if (!in_data(c, cmd, HSD_PSCMDLIST_HEADER)) {
        return;
    }
    conv_u16(c, cmd + 0x00); /* type */
    conv_u16(c, cmd + 0x02); /* texGroup */
    conv_u16(c, cmd + 0x04); /* genLife */
    conv_u16(c, cmd + 0x06); /* life */
    conv_u32(c, cmd + 0x08); /* kind */
    /* grav, fric, vx, vy, vz, radius, angle, random, size, param1..3 */
    for (i = 0; i < 12; i++) {
        conv_u32(c, cmd + 0x0C + (uint32_t) i * 4);
    }
}

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
            /* The runtime skips a zero entry (`if (ptr[3] != 0)`); without
             * the same check `off + 0` would convert the bank header. */
            if (rd32(c, p) != 0) {
                cmd = off + rd32(c, p);
                conv_ps_cmd_list(c, cmd);
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
            if (rd32(c, p) != 0) {
                cmd = off + rd32(c, p);
                conv_ps_cmd_list(c, cmd);
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

/* DWARF: MapLine */
static void conv_map_line(Conv* c, uint32_t off)
{
    int i;
    for (i = 0; i < MAPLINE_SIZE / 2; i++) {
        conv_u16(c, off + (uint32_t) i * 2);
    }
}

/* DWARF: MapJoint */
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

/* DWARF: MapCollData */
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
    /* dynamic_start/dynamic_count at +0x20/+0x22 (MapCollData): mpLibLoad
     * fills groundCollLine[dynamic_start..] from them every stage load. */
    conv_u16(c, off + 0x20);
    conv_u16(c, off + 0x22);
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

/* Gr*.dat `grGroundParam`: GroundParam (src/melee/gr/types.h).  A mix of f32,
 * s16 and s32 fields.  The s16 arrays at +0x6A (35 entries) and in each
 * StageParam row are read as raw s16 by Ground_801C28CC, and Ground_801C0498
 * returns `y` as the stage root scale, so leaving them big-endian makes the
 * stage root scale 4.6e-41 and every spawn point NaN. */
#define GROUNDPARAM_SIZE 0xDC
#define GROUNDPARAM_STAGE_S16 35
#define STAGEPARAM_SIZE 0x64
#define STAGEPARAM_S16 37

/* DWARF: StageParam */
static void conv_stage_param(Conv* c, uint32_t off)
{
    int i;

    conv_u32(c, off + 0x00); /* stkind */
    conv_u32(c, off + 0x04);
    conv_u32(c, off + 0x08);
    conv_u32(c, off + 0x0C);
    conv_u32(c, off + 0x10);
    conv_u16(c, off + 0x14);
    conv_u16(c, off + 0x16);
    conv_u16(c, off + 0x18);
    for (i = 0; i < STAGEPARAM_S16; i++) {
        conv_u16(c, off + 0x1A + (uint32_t) i * 2);
    }
}

/* DWARF: GroundParam */
static void conv_ground_param(Conv* c, uint32_t off)
{
    uint32_t rows;
    int count;
    int i;

    if (!in_data(c, off, GROUNDPARAM_SIZE) || !mark(c, off)) {
        return;
    }
    c->st.ground_params++;
    conv_u32(c, off + 0x00); /* y f32: stage root scale */
    conv_u16(c, off + 0x04);
    conv_u16(c, off + 0x08);
    conv_u16(c, off + 0x0A);
    conv_u32(c, off + 0x0C);
    conv_u32(c, off + 0x10);
    conv_u32(c, off + 0x14);
    conv_u32(c, off + 0x18);
    conv_u32(c, off + 0x1C);
    conv_u32(c, off + 0x20);
    conv_u32(c, off + 0x24);
    conv_u32(c, off + 0x28);
    conv_u16(c, off + 0x2E);
    conv_u32(c, off + 0x30);
    conv_u32(c, off + 0x34);
    conv_u32(c, off + 0x38);
    conv_u32(c, off + 0x3C);
    conv_u32(c, off + 0x40);
    conv_u32(c, off + 0x44);
    conv_u32(c, off + 0x48);
    conv_u32(c, off + 0x50);
    conv_u32(c, off + 0x54);
    conv_u32(c, off + 0x58);
    conv_u32(c, off + 0x5C);
    conv_u32(c, off + 0x60);
    conv_u32(c, off + 0x64);
    conv_u16(c, off + 0x68);
    for (i = 0; i < GROUNDPARAM_STAGE_S16; i++) {
        conv_u16(c, off + 0x6A + (uint32_t) i * 2);
    }
    /* xB8..xD8 are GXColors (byte data, no swap). */
    rows = rd32(c, off + 0xB0);
    conv_u32(c, off + 0xB4);
    count = (int) rd32(c, off + 0xB4);
    if (rows != 0 && count > 0 && count <= 256) {
        for (i = 0; i < count; i++) {
            uint32_t p = rows + (uint32_t) i * STAGEPARAM_SIZE;
            if (!in_data(c, p, STAGEPARAM_SIZE)) {
                break;
            }
            conv_stage_param(c, p);
        }
    }
}

/* Gr*.dat `itemdata`: NULL-terminated GroundItemData* array
 * ({ s32 unk0; Article* unk4 }, src/melee/gr/types.h).  The `unk4` pointer is
 * a relocation target (already host order); `unk0` is the item kind and must
 * be swapped or it_8026B40C receives 0xnn000000.
 *
 * `unk4` is the stage's own Article, and it has to be walked like any other
 * (P-762).  ground.c:488 hands each one to it_8026B40C, which files it in
 * it_804A0F60[kind - It_Kind_Old_Kuri]; the item spawn path then reads
 * `article->x10_modelDesc->x0_joint` (item.c:578) and loads that joint tree.
 * Nothing else in the archive points at those Articles, so leaving them here
 * left an entire model tree big-endian: on Great Bay the Tingle balloon's
 * HSD_PObjDesc kept its `n_display`/`flags` u16 pair swapped, `flags` lost
 * POBJ_ENVELOPE (0x2000), and since POBJ_SKIN is `0 << 12` a PObj with no
 * type bits *is* a skin -- so HSD_PObjResolveRefs took the skin branch, passed
 * the envelope-list pointer to HSD_IDGetData as a joint ID, got NULL and
 * asserted at pobj.c:411.  Roughly 1 match in 6 picked the stage. */
static void conv_itemdata(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 4) || !mark(c, off)) {
        return;
    }
    c->st.itemdata++;
    for (i = 0; i < 256; i++) {
        uint32_t entry = off + (uint32_t) i * 4;
        uint32_t p;
        uint32_t article;
        if (!in_data(c, entry, 4)) {
            break;
        }
        p = rd32(c, entry);
        if (p == 0 || !in_data(c, p, 8)) {
            break;
        }
        conv_u32(c, p + 0x00);
        article = rd32(c, p + 0x04);
        if (article != 0) {
            /* unk0 is host order by now, so it is the It_Kind the article is
             * registered under -- which is what conv_article needs to
             * recognise per-kind special attributes. */
            conv_article(c, article, (int) rd32(c, p + 0x00));
        }
    }
}

/* GmKumite.dat `gmKumiteSystemTable*`: `RegClearSpawnEntry[]` (0x10 bytes:
 * { s32 kind; u8 x4..x7; f32 x8; f32 xC }, gm_181A.c:28).  gm_80182174
 * copies rows into the runtime table until the 999 sentinel, so the table
 * length is not stored; walk until x0 == 0x3E7 with a hard bound. */
static void conv_regclear_spawn_table(Conv* c, uint32_t off)
{
    int i;

    for (i = 0; i < 4096; i++) {
        uint32_t e = off + (uint32_t) i * 0x10;
        uint32_t kind;
        if (!in_data(c, e, 0x10)) {
            break;
        }
        conv_u32(c, e + 0x00);
        conv_u32(c, e + 0x08);
        conv_u32(c, e + 0x0C);
        kind = rd32(c, e + 0x00);
        if (kind == 0x3E7) {
            break;
        }
    }
}

/* GrYt.dat (Yoshi's Story) `yakumono_param`: YorsterParams
 * { f32 x00; f32 x04; f32 x08; f32 x0C; s32 x10; s32 x14; s32 x18; s32 x1C }
 * (gryorster.c:61).  grYorster_802024F0 uses x00 as the bump threshold and
 * x10 as the bump velocity; left big-endian, x00 reads as a huge negative
 * float (every contact passes the test) and x10 as a denormal ~0, so a
 * fighter hitting a Lucky Block from below is stopped and stuck in place. */
static void conv_yorster_param(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x20) || !mark(c, off)) {
        return;
    }
    for (i = 0; i < 8; i++) {
        conv_u32(c, off + (uint32_t) i * 4);
    }
}

static void conv_u32_range(Conv* c, uint32_t off, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        conv_u32(c, off + (uint32_t) i * 4);
    }
}

static void conv_u16_range(Conv* c, uint32_t off, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        conv_u16(c, off + (uint32_t) i * 2);
    }
}

/* GrCn.dat (Corneria) `yakumono_param` (grcorneria.c:41): twenty f32, two
 * isolated f32 at +0x68/+0x70, four s32 and a trailing f32; +0x84 is a
 * relocation-backed pointer and stays host order. */
static void conv_corneria_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x8C) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 20);
    conv_u32(c, off + 0x68);
    conv_u32(c, off + 0x70);
    conv_u32_range(c, off + 0x74, 4);
    conv_u32(c, off + 0x88);
}

/* GrIz.dat (Icicle Mountain/Izumi) `yakumono_param` (grizumi.c): f32 x00,
 * s32 x04, then twenty f32 through +0x50. */
static void conv_izumi_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x54) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 21);
}

/* GrKg.dat (Kongo Jungle) `yakumono_param` (grkongo.h:26): seventeen f32,
 * eight s16, four f32, two s32, six f32, the +0x84 pointer, thirteen f32. */
static void conv_kongo_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0xBC) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 17);
    conv_u16_range(c, off + 0x44, 8);
    conv_u32_range(c, off + 0x54, 4);
    conv_u32(c, off + 0x64);
    conv_u32(c, off + 0x68);
    conv_u32_range(c, off + 0x6C, 6);
    conv_u32_range(c, off + 0x88, 13);
}

/* GrSt.dat (Yoshi's Story) `yakumono_param` (grstory.c): three f32 then the
 * six-float vpos table. */
static void conv_story_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x24) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 9);
}

/* GrVe.dat (Venom) `yakumono_param` (grvenom.c): five f32, then +0x2C and
 * +0x34; those two are the fields the stage reads, the surrounding runs are
 * unnamed in the decomp; +0x38 is a pointer. */
static void conv_venom_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x3C) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 5);
    conv_u32(c, off + 0x2C);
    conv_u32(c, off + 0x34);
}

/* GrOt.dat (Onett) `yakumono_param` (gronett.c:88): twenty-six f32. */
static void conv_onett_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x68) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 26);
}

/* GrI1.dat (Inishie 1) `yakumono_param` (grinishie1.c:116): five f32, six
 * s16, three f32, two Vec3, four f32. */
static void conv_inishie1_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x54) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 5);
    conv_u16_range(c, off + 0x14, 6);
    conv_u32_range(c, off + 0x20, 3);
    conv_u32_range(c, off + 0x2C, 6);
    conv_u32_range(c, off + 0x44, 4);
}

/* GrCs.dat (Peach's Castle) `yakumono_param` (grcastle.c:121): eight s16,
 * three f32, eight f32, three s16, three f32, two s16, nine
 * { s16 timer; f32 speed; Vec3 rot } entries, a f32, the +0x114 pointer
 * (left to the relocation pass), four f32, four s16 and four f32.  The
 * `entries[].x0` values are the per-map intro timers `grCastle_801CE578`
 * counts down before it runs the castle animation and stops the looping
 * stage ambient with `Ground_801C5544`; left big-endian they read negative
 * or far too large, so the intro never completes and `castle.ssm` 0x53025
 * loops for the whole match (P-707). */
static void conv_castle_param(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x144) || !mark(c, off)) {
        return;
    }
    conv_u16_range(c, off + 0x00, 8);
    conv_u32_range(c, off + 0x10, 3);
    conv_u32_range(c, off + 0x20, 8);
    conv_u16_range(c, off + 0x40, 3);
    conv_u32_range(c, off + 0x48, 3);
    conv_u16(c, off + 0x54);
    conv_u16(c, off + 0x58);
    for (i = 0; i < 9; i++) {
        uint32_t e = off + 0x5C + (uint32_t) i * 0x14;
        conv_u16(c, e + 0x00);
        conv_u32_range(c, e + 0x04, 4);
    }
    conv_u32(c, off + 0x110);
    conv_u32_range(c, off + 0x118, 4);
    conv_u16_range(c, off + 0x12C, 4);
    conv_u32_range(c, off + 0x134, 4);
}

/* GrCs.dat/GrRc.dat `dynamicsdata_*` publics: a `DynamicsDesc`
 * { DynamicsData* data; u32 count; Vec3 pos } whose `data` points at `count`
 * 0x3C-byte source records.  lb_80011710 copies the record floats into the
 * runtime list; left big-endian, `count` reads as 0x0n000000 and
 * lb_8000FD48 walks the whole dynamics pool (grCastle_801CD658). */
/* DWARF: DynamicsDesc */
static void conv_dynamics_desc(Conv* c, uint32_t off)
{
    uint32_t data;
    int count;
    int i;

    if (!in_data(c, off, 0x14) || !mark(c, off)) {
        return;
    }
    /* **Nine words, not five.**  This object is declared `DynamicsDesc` --
     * `{ void* data; int count; Vec3 pos }`, which is 0x14 -- but every stage's
     * `on_touch_line` hands the same pointer to `lbColl_80008D30`, which reads
     * it as `lbColl_80008D30_arg1` (lb/forward.h:103): nine `u32`, `state,
     * damage, kb_angle, unkC, unk10, unk14, element, sfx_severity, sfx_kind`.
     * The two views agree where they overlap -- `count` and `damage` are the
     * same word at +0x04 -- and diverge after +0x10, which is exactly where
     * this walker used to stop.
     *
     * Both halves bit the owner at once on Mute City.  The damage word read
     * `0x08000000` for the `int` 8, so a buried fighter took 134217728
     * environment damage and `ftcoll.c:229` asserted (P-783); with that fixed
     * the still-raw `sfx_kind` indexed `lbColl_803B9880[kind * 3 + severity]`
     * -- a 28-byte table -- at 117440512, which segfaults in
     * `lbColl_80005BB0` and, where it does not, hands `lbAudioAx_80024184` a
     * garbage sound id.  That is the audio clipping and going wild on an item
     * pickup (P-785).
     *
     * `conv_u32` leaves relocation fields alone, so covering +0x00 is safe
     * even where `data` really is a pointer, and `data` is read back *after*
     * the conversion rather than before. */
    if (in_data(c, off, 0x24)) {
        conv_u32_range(c, off, 9);
    } else {
        conv_u32_range(c, off, 5);
    }
    /* `data` may legally be 0: it is the data-section base for the first
     * record block (GrCs.dat flag3 stores offset 0), not a null pointer. */
    data = rd32(c, off + 0x00);
    count = (int) rd32(c, off + 0x04);
    if (count <= 0 || count > 64 || !in_data(c, data, (size_t) count * 0x3C)) {
        return;
    }
    for (i = 0; i < count; i++) {
        conv_u32_range(c, data + (uint32_t) i * 0x3C, 15);
    }
}

/* GrPs.dat/GrPs3.dat (Pokemon Stadium) `yakumono_param`
 * (`grPStadium_YakumonoParam`, grpstadium.c:40): seven s32, a
 * { u8 r, g, b } monitor tint plus one pad byte, ten u32 and five s16.
 *
 * The u32 run at +0x20 is the jumbotron display state machine's dwell times,
 * and +0x38/+0x3C are the `randi_between_2` bounds `grStadium_801D2528` uses
 * for state 7, the 640x406 live feed.  Left big-endian, 600 and 1200 read as
 * 0x58020000 and 0xB0040000, so the countdown `grStadium_801D2344` decrements
 * starts negative and its `xE0-- < 0` branch is taken on the very first
 * frame.  That branch picks a new state and `break`s **before** clearing the
 * feed wrapper's `flag`, so `grStadium_801D2FD0` never runs its
 * `GXCopyTex`, and the monitor samples the buffer `HSD_MemAlloc` returned --
 * uninitialised heap decoded as RGB565 (P-738).  The same shape freezes the
 * 124x80 close-up in state 8.
 *
 * +0x1C is three colour bytes (150, 180, 160) and must stay untouched; it is
 * the diffuse colour `grStadium_801D21E4` gives the monitor material. */
static void conv_pstadium_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x52) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off + 0x00, 7);
    /* +0x1C: u8 r, g, b and a pad byte -- no swap. */
    conv_u32_range(c, off + 0x20, 10);
    conv_u16_range(c, off + 0x48, 5);
}

/* GrKr.dat (Brinstar Depths / Kraid) `yakumono_param`
 * (`grKraid_YakumonoParam`, grkraid.c:12): thirteen 4-byte fields --
 * `map_time_min/max/acl`, `map_rot_spd_min/max`, `kraid_wait_time(_add)` and
 * `kraid_pos_x[6]`.  Verified against the raw archive: 150/240/180,
 * 0.2/0.3, 120/300, then -60/-30/0/30/60/0. */
static void conv_kraid_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x34) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off, 13);
}

/* GrMc.dat (Mute City) `yakumono_param` (`grMc_YakumonoParam`,
 * grmutecity.c:341): four pointers, then 4-byte fields to +0x4C.  The
 * pointers are relocation targets and `conv_u32` leaves those alone, so the
 * range can simply cover the whole block.  The decomp names only +0x2C..0x4C
 * and calls +0x10..0x2B padding, but the raw bytes there are floats
 * (-12, 2, 15) like the rest, and nothing reads them either way. */
/* `x8` and `xC` are the two descriptors `grMuteCity_801F2BBC`
 * (grmutecity.c:1987) hands back, and **nothing else in GrMc.dat points at
 * them**, so without this they have no walker at all. */
static void conv_mutecity_param(Conv* c, uint32_t off)
{
    uint32_t dyn;

    if (!in_data(c, off, 0x50) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off, 20);
    dyn = rd32(c, off + 0x08);
    if (dyn != 0) {
        conv_dynamics_desc(c, dyn);
    }
    dyn = rd32(c, off + 0x0C);
    if (dyn != 0) {
        conv_dynamics_desc(c, dyn);
    }
}

/* GrBb.dat (Big Blue) `yakumono_param` (`grBb_YakumonoParam`,
 * grbigblue.static.h:20): 0x144 of 4-byte fields, ending at `x140_scale`,
 * which is exactly where the next pointed-at object starts.  Two small
 * unnamed gaps (+0x64, +0x114) are 4-byte aligned and unread. */
static void conv_bigblue_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x144) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off, 0x144 / 4);
}

/* GrOp.dat (Dream Land) `yakumono_param` (`struct grOldpupupu_YakumonoParam`,
 * groldpupupu.c:25): four `s16`, two `int`, then nine `f32` -- 0x34 bytes,
 * which is exactly the symbol's extent in the archive.
 *
 * It is **not** a flat `conv_u32_range` like Mute City or Big Blue: the first
 * two words are four `s16`, and swapping them as `u32` would exchange the
 * pairs.  `x0` and `x2` are the cloud respawn timers, 3000 and 4000 frames.
 * Left big-endian they read -18421 and -24561, a negative respawn never
 * gates, and `groldpupupu.c:408` spawns cloud objects **every frame** -- 448
 * of them -- until `HSD_MemAlloc` fails with 512 bytes free and
 * `memory.c:55` asserts.  It looked like a heap-sizing bug and is not; the
 * arena is a correct 24 MB (P-770, 26 of the matrix failures).
 *
 * No `DWARF:` annotation: the struct is declared inside `groldpupupu.c`, not
 * a header, so `dwarf_types.c` cannot include it -- the same gap recorded for
 * `ItCollDynamics`. Confirmed instead against the archive: the raw fields read
 * 3000, 4000, 30, 0, 600, 1200, 0.2, -17, 76, -18, -74, 40, -10, 180, 360,
 * every one of them plausible, and the symbol's extent is 0x34 to the byte. */
static void conv_oldpupupu_param(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x34) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00); /* x0: min respawn frames */
    conv_u16(c, off + 0x02); /* x2: max respawn frames */
    conv_u16(c, off + 0x04); /* x4 */
    conv_u16(c, off + 0x06); /* x6 */
    conv_u32(c, off + 0x08); /* x8:  int */
    conv_u32(c, off + 0x0C); /* xC:  int */
    for (i = 0x10; i <= 0x30; i += 4) {
        conv_u32(c, off + (uint32_t) i); /* x10..x30: f32 */
    }
}

/* GrOk.dat (Kongo Jungle N64) `yakumono_param` (`grOldKongo_YakumonoParam`,
 * groldkongo.c:25): 0x70, exactly the symbol's extent.  Two `s16` bird
 * timers, a run of `f32`, **eight `s16` barrel-direction weights** at
 * +0x2C..+0x3A, a run of `s32`/`f32`, and a **pointer at +0x6C** which the
 * relocation pass owns -- `conv_u32` refuses it, but the loop stops before it
 * anyway so the intent is on the record.  Raw values check out: 4000/3000
 * frames, 20.0, 479, 480, 15, 180, 360, 4, 4, 90, 110, weights 1/1/10/50/
 * 10/1/1/1, 300, 600, 0.005, 1.0, 3000, 6000.  This is the last of P-708's
 * `Ground_801C5440` priority list. */
static void conv_oldkongo_param(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x70) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00); /* rframe_bird_wait_a */
    conv_u16(c, off + 0x02); /* rframe_bird_wait_b */
    for (i = 0x04; i <= 0x28; i += 4) {
        conv_u32(c, off + (uint32_t) i); /* f32 run */
    }
    for (i = 0x2C; i <= 0x3A; i += 2) {
        conv_u16(c, off + (uint32_t) i); /* rrate_barrel_* weights */
    }
    for (i = 0x3C; i <= 0x68; i += 4) {
        conv_u32(c, off + (uint32_t) i); /* s32 / f32 run */
    }
    /* +0x6C is `void* x6C`: a relocation target, already host order. */
}

/* GrGr.dat (Green Greens) `yakumono_param` (`grGreens_YakumonoParam`,
 * grgreens.c): 0x7C of 4-byte fields, exactly the symbol's extent -- block
 * timers 30/150, wind timers 800/1800, wind speed 0.24, bounds 0/90/40/-10. */
static void conv_greens_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x7C) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off, 0x7C / 4);
}

/* GrRc.dat (Rainbow Cruise) `yakumono_param` (`grRCruise_YakumonoParam`,
 * grrcruise.c): 0x48 of 4-byte fields, exactly the symbol's extent --
 * 0.3, 3.0, 0.5, then timers 200/120/4/200/4/30/60. */
static void conv_rcruise_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x48) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off, 0x48 / 4);
}

/* GrGd.dat (Garden) `yakumono_param` (`grGarden_YakumonoParam`, grgarden.c):
 * 0x20 of 4-byte fields, exactly the symbol's extent -- -40.0, -3.0, then
 * 2, 8, 80, 640, and -30.0, 30.0. */
static void conv_garden_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x20) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off, 0x20 / 4);
}

/* GrI2.dat (Inishie2) `yakumono_param` (`grInishie2_YakumonoParam`,
 * grinishie2.c): 0x4C, exactly the symbol's extent, and **not** flat -- ten
 * `s16` at +0x00..+0x13, two `Vec3` and an `f32` and two more `Vec3` as a
 * `f32` run at +0x14..+0x47, then two `s16` at +0x48/+0x4A.  Raw: 50/450,
 * 600/300, 600/600, 600/600, 265/410, then 30.0, 28.5, 0, -30.0, 28.5, 0,
 * 0, 30.0, 80.0, 0, -30.0, 80.0, 0, then 5/3. */
static void conv_inishie2_param(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x4C) || !mark(c, off)) {
        return;
    }
    for (i = 0x00; i <= 0x12; i += 2) {
        conv_u16(c, off + (uint32_t) i);
    }
    for (i = 0x14; i <= 0x44; i += 4) {
        conv_u32(c, off + (uint32_t) i); /* Vec3 / f32 run */
    }
    conv_u16(c, off + 0x48);
    conv_u16(c, off + 0x4A);
}

/* GrOy.dat (Yoshi's Island N64) `yakumono_param`: an **anonymous** struct in
 * groldyoshi.c:54 -- two `s16`, three `f32`, then five `s16`.  0x1C with the
 * trailing pad, exactly the symbol's extent.  Raw: 120, 180, 0.15, 0.15,
 * 6.0, 30, 30, **3000, 4000**, 30 -- the same cloud-timer shape as Dream
 * Land's `x0`/`x2`, which is what P-770 crashed on, so this one had the same
 * failure latent. */
static void conv_oldyoshi_param(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x1C) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00);
    conv_u16(c, off + 0x02);
    conv_u32(c, off + 0x04); /* f32 */
    conv_u32(c, off + 0x08); /* f32 */
    conv_u32(c, off + 0x0C); /* f32 */
    for (i = 0x10; i <= 0x18; i += 2) {
        conv_u16(c, off + (uint32_t) i);
    }
}

/* An `s16` list terminated by -1, the shape `gricemt.c` uses for its three
 * `s16*` tables (`field_ixs[i] == id` scans until the sentinel).  Bounded by
 * the terminator *and* by the object's extent, so a table whose -1 is missing
 * cannot run into the next object. */
static void conv_s16_list(Conv* c, uint32_t off)
{
    uint32_t end;
    uint32_t p;

    if (off == 0 || !in_data(c, off, 2)) {
        return;
    }
    end = next_pointed_at_after(c, off);
    {
        uint32_t pub = next_public_after(c, c->public_off, c->nb_public, off);
        if (pub < end) {
            end = pub;
        }
    }
    for (p = off; p + 2 <= end; p += 2) {
        uint32_t w = rd16(c, p);
        conv_u16(c, p);
        if (w == 0xFFFFu) {
            break;
        }
    }
}

/* GrIm.dat (Icicle Mountain) `yakumono_param` (`grIceMt_YakumonoParam`,
 * gricemt.c:66).
 *
 * **This one is deliberately partial, and the reason is on the record.** The
 * declared struct and the archive agree on the layout -- the three `s16*`
 * fields land on relocation entries at exactly +0xAC/+0xB0/+0xB4, which is
 * where the declaration predicts them, and that is a strong confirmation.
 * But they disagree about `x4`: the declaration says `s16` at +0x04, while
 * the bytes there read -0.15f and continue a clean float ramp into +0x08.
 * **`yakumono_param->x4` is never read anywhere in `gricemt.c`**, so rather
 * than guess a type, it is skipped -- converting it would change a word no
 * one looks at, and getting it wrong would be a silent corruption.
 *
 * Everything the engine actually reads is converted: `x0`/`x2` (frame
 * counts), `x3A`, `x3C`/`x40`, `ft_max_y`, `x9C`/`xA0`, `xA4`/`xA6`/`xA8`,
 * `xB8`, the `u16 kind` of the single `grZakoGenerator_SpawnDesc` at +0xBC
 * (its `x2`/`respawn` are `u8` and stay put), and the three `s16` tables.
 *
 * **Nothing past +0xBC is touched.** The declaration says four `f32` at
 * +0xC0..+0xCC, but the bytes there are `002e0001` repeated for another 0x54
 * -- plainly not floats, and `grZakoGenerator_801CAE04(&yakumono_param->xBC)`
 * takes the address of **one** desc, not an array. That region cannot be
 * named from the decompilation, so it is left alone (P-758's rule). */
static void conv_icemt_param(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0xC0) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00);
    conv_u16(c, off + 0x02);
    /* +0x04 skipped: type unresolved and never read. */
    for (i = 0x08; i <= 0x30; i += 4) {
        conv_u32(c, off + (uint32_t) i); /* f32 ramp */
    }
    for (i = 0x34; i <= 0x3A; i += 2) {
        conv_u16(c, off + (uint32_t) i);
    }
    for (i = 0x3C; i <= 0x94; i += 4) {
        conv_u32(c, off + (uint32_t) i); /* f32 run */
    }
    conv_u16(c, off + 0x98); /* ft_max_y */
    conv_u16(c, off + 0x9A);
    conv_u32(c, off + 0x9C); /* f32 */
    conv_u32(c, off + 0xA0); /* f32 */
    conv_u16(c, off + 0xA4);
    conv_u16(c, off + 0xA6);
    conv_u16(c, off + 0xA8);
    /* +0xAC/+0xB0/+0xB4 are `s16*`: relocation targets, already host order. */
    conv_u16(c, off + 0xB8);
    conv_u16(c, off + 0xBC); /* grZakoGenerator_SpawnDesc.kind */
    for (i = 0xAC; i <= 0xB4; i += 4) {
        if (c->reloc[off + (uint32_t) i]) {
            conv_s16_list(c, rd32(c, off + (uint32_t) i));
        }
    }
}

/* Gr*.dat `yakumono_param` fallback: stage-specific dynamic-object parameters
 * whose layout this converter does not know yet.  For Zebes the word at +0x2C
 * is a relocation target to a bury descriptor stored directly before the
 * symbol (`ftCo_800C08A0` reads its `count` as the acid damage), so the sign
 * of the Zebes layout is that the gap to it is exactly one descriptor.  The
 * check is self-validating: unknown stages are marked but left big-endian.
 *
 * **That gap is 0x24, not `sizeof(DynamicsDesc)`.**  The object is declared
 * `DynamicsDesc` (0x14) but `ftCo_800C08A0` hands it to `lbColl_80008D30`,
 * which reads nine `u32` -- see `conv_dynamics_desc` for the full argument --
 * and the archive agrees: Zebes leaves 0x24 between the descriptor and the
 * parameter block, which is the nine-word view's size. */
static void conv_yakumono_param(Conv* c, uint32_t off)
{
    uint32_t desc;

    if (!in_data(c, off, 0x2C + 4) || !mark(c, off)) {
        return;
    }
    desc = rd32(c, off + 0x2C);
    if (desc == off - 0x24 && in_data(c, off, 0x190)) {
        int i;
        /* Nine words, not five.  Stopping at +0x10 left `element`,
         * `sfx_severity` and `sfx_kind` big-endian, so the acid's
         * `sfx_kind = 8` reached `lbColl_80005BB0` as 0x08000000 and
         * `lbColl_803B9880[kind * 3 + severity]` read 402653208 entries past
         * a 42-entry table: SIGSEGV the first time the acid buried a fighter
         * (P-796).  P-785 fixed exactly this in `conv_dynamics_desc` and this
         * second copy of the walk was missed. */
        conv_u32_range(c, desc, 9);
        conv_u32(c, off + 0x00);
        conv_u32(c, off + 0x04);
        conv_u32(c, off + 0x08);
        conv_u32(c, off + 0x0C);
        conv_u32(c, off + 0x10); /* s32 */
        for (i = 0x30; i <= 0x9C; i += 4) {
            conv_u32(c, off + (uint32_t) i); /* f32 range */
        }
        for (i = 0; i < 30 * 8; i += 2) {
            conv_u16(c, off + 0xA0 + (uint32_t) i); /* acid level entries */
        }
    }
}

/* Does `off` look like a stage's touch-line descriptor?
 *
 * The Target Test stages keep theirs in `yakumono_param` and nothing else in
 * the archive points at them, exactly like Mute City's -- `grTMewtwo_UnkStruct`
 * (grtmewtwo.c:17) is **eight** `DynamicsDesc*` and nothing else, and Ganon's
 * and Jigglypuff's are three and one.  Left raw, a buried fighter takes the
 * `damage` word byte-reversed: `0x0A000000` for 10, which asserts at
 * `ftcoll.c:229` on entry to a Target Test stage (P-787).
 *
 * Shape-tested rather than keyed per stage, because there are 27 of these
 * archives and only a marker symbol would distinguish them.  The test is
 * strict on purpose: `conv_dynamics_desc` writes nine words, so a false
 * positive corrupts rather than skips.  A real one is nine plain numbers --
 * `state, damage, kb_angle, unkC, unk10, unk14, element, sfx_severity,
 * sfx_kind` -- so **none of the nine may be a relocation field**, and all nine
 * must be small non-negative when read big-endian.  A joint, a float block or
 * a pointer table fails that immediately; floats in particular carry a large
 * exponent in the top bits.
 *
 * It is also idempotent: once converted the words are host order, so reading
 * them big-endian gives large values and the test declines, which keeps a
 * second visit from re-swapping. */
static int looks_like_touch_line_desc(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x24)) {
        return 0;
    }
    for (i = 0; i < 9; i++) {
        uint32_t w = off + (uint32_t) i * 4;
        if (c->reloc[w] || be32(c->data + w) > 0xFFFFu) {
            return 0;
        }
    }
    return 1;
}

/* Follow every pointer in an otherwise unrecognised `yakumono_param` that
 * points at one of those descriptors.  Bounded by the symbol's own extent,
 * the way the rest of this file bounds an untyped block. */
static void conv_yakumono_touch_lines(Conv* c, uint32_t off)
{
    uint32_t end = next_pointed_at_after(c, off);
    uint32_t pub = next_public_after(c, c->public_off, c->nb_public, off);
    uint32_t w;

    if (pub < end) {
        end = pub;
    }
    if (end <= off || end - off > 0x400) {
        return;
    }
    for (w = off; w + 4 <= end; w += 4) {
        uint32_t target = follow_ptr(c, w);
        if (target != 0 && looks_like_touch_line_desc(c, target)) {
            conv_dynamics_desc(c, target);
        }
    }
}

/* GrNFg.dat (the Classic trophy bonus stage) `yakumono_param`
 * (`grFigureGet_Params`, grfigureget.c:30): three `s32` then three `f32`,
 * 0x18 bytes, which is exactly what the archive holds -- the raw words read
 * `140, 80, 3, -84.0f, 84.0f, 50.0f` and the next structure starts at +0x18.
 *
 * Left raw, `x8` -- the number of trophies to drop -- read `0x03000000`
 * instead of 3, so the spawn loop never finished, and `x0` (the delay before
 * the next drop) read `0x8C000000`, about **-1.9 billion**.  The stage sets
 * that delay after the first trophy and then only ever decrements it, so
 * **exactly one trophy spawned** -- at a nonsense position, since `xC`/`x10`
 * bound its x and both were denormals -- and none ever followed.  The owner
 * had to jump off the stage to end the round (P-791). */
static void conv_figureget_param(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x18) || !mark(c, off)) {
        return;
    }
    conv_u32_range(c, off, 6);
}

static void conv_stage_yakumono(Conv* c, uint32_t off)
{
    c->st.yakumono_params++;
    switch (c->stage_layout) {
    case STAGE_PARAM_YORSTER:
        conv_yorster_param(c, off);
        break;
    case STAGE_PARAM_CORNERIA:
        conv_corneria_param(c, off);
        break;
    case STAGE_PARAM_IZUMI:
        conv_izumi_param(c, off);
        break;
    case STAGE_PARAM_KONGO:
        conv_kongo_param(c, off);
        break;
    case STAGE_PARAM_STORY:
        conv_story_param(c, off);
        break;
    case STAGE_PARAM_VENOM:
        conv_venom_param(c, off);
        break;
    case STAGE_PARAM_ONETT:
        conv_onett_param(c, off);
        break;
    case STAGE_PARAM_INISHIE1:
        conv_inishie1_param(c, off);
        break;
    case STAGE_PARAM_CASTLE:
        conv_castle_param(c, off);
        break;
    case STAGE_PARAM_PSTADIUM:
        conv_pstadium_param(c, off);
        break;
    case STAGE_PARAM_KRAID:
        conv_kraid_param(c, off);
        break;
    case STAGE_PARAM_MUTECITY:
        conv_mutecity_param(c, off);
        break;
    case STAGE_PARAM_FIGUREGET:
        conv_figureget_param(c, off);
        break;
    case STAGE_PARAM_OLDPUPUPU:
        conv_oldpupupu_param(c, off);
        break;
    case STAGE_PARAM_OLDKONGO:
        conv_oldkongo_param(c, off);
        break;
    case STAGE_PARAM_GREENS:
        conv_greens_param(c, off);
        break;
    case STAGE_PARAM_RCRUISE:
        conv_rcruise_param(c, off);
        break;
    case STAGE_PARAM_INISHIE2:
        conv_inishie2_param(c, off);
        break;
    case STAGE_PARAM_GARDEN:
        conv_garden_param(c, off);
        break;
    case STAGE_PARAM_OLDYOSHI:
        conv_oldyoshi_param(c, off);
        break;
    case STAGE_PARAM_ICEMT:
        conv_icemt_param(c, off);
        break;
    case STAGE_PARAM_BIGBLUE:
        conv_bigblue_param(c, off);
        break;
    default:
        conv_yakumono_param(c, off);
        conv_yakumono_touch_lines(c, off);
        break;
    }
}

/* GmEvent.dat `sqEventInitDataLevelTbl`: 51 pointers to per-event-level
 * `gm_804D6900_t` (gmevent.c:116).  Each level carries four descriptor
 * pointers; the descriptor numerics are big-endian and the evinit rule block
 * packs its flags MSB-first the way MWCC does.  The table length is derived
 * from the relocation run (the entry past the last level is not a pointer). */
#define EV_MAX_LEVELS 64

static void conv_event_init_flags(Conv* c, uint32_t off)
{
    u8 a;
    u8 b;

    /* gm_evinit's u32 flags: console byte A = x0_0:3, x0_3:3, x0_6, x0_7
     * MSB-first; console byte B = x1_0..x1_4, x1_5:3.  GCC allocates the
     * same declarations LSB-first, so repack each byte (G-082's transform,
     * not a byte swap). */
    if (!in_data(c, off, 4) || (off & 3u) || c->reloc[off] || c->num[off]) {
        return;
    }
    c->num[off] = 1;
    a = c->data[off];
    b = c->data[off + 1];
    c->data[off] =
        (u8) ((a >> 5) | (((a >> 2) & 7u) << 3) | (((a >> 1) & 1u) << 6) |
              ((a & 1u) << 7));
    c->data[off + 1] =
        (u8) (((b >> 7) & 1u) | (((b >> 6) & 1u) << 1) |
              (((b >> 5) & 1u) << 2) | (((b >> 4) & 1u) << 3) |
              (((b >> 3) & 1u) << 4) | ((b & 7u) << 5));
}

static void conv_event_evinit(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x28) || !mark(c, off)) {
        return;
    }
    conv_event_init_flags(c, off + 0x00);
    conv_u16(c, off + 0x06); /* stkind */
    conv_u32(c, off + 0x08); /* time_limit */
    conv_u32(c, off + 0x10); /* x10: u64 */
    conv_u32(c, off + 0x14);
    conv_u32(c, off + 0x18); /* x18: s32 */
    conv_u32(c, off + 0x1C); /* f32 */
    conv_u32(c, off + 0x20); /* game_speed */
    conv_u32(c, off + 0x24); /* f32 */
}

static void conv_event_evbonus(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x18) || !mark(c, off)) {
        return;
    }
    conv_u32(c, off + 0x08);
    conv_u32(c, off + 0x0C);
    conv_u32(c, off + 0x10);
}

static void conv_event_stage_table(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x28) || !mark(c, off)) {
        return;
    }
    for (i = 0; i < 7; i++) {
        conv_u16(c, off + 0x02 + (uint32_t) i * 2); /* stage ids */
    }
    /* entries[6] are relocation-backed pointers and stay host order. */
}

static void conv_event_player_init(Conv* c, uint32_t off)
{
    if (!in_data(c, off, 0x1C) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x0C);
    conv_u16(c, off + 0x0E);
    conv_u32(c, off + 0x10);
    conv_u32(c, off + 0x14);
    conv_u32(c, off + 0x18);
}

static void conv_event_level(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, 0x28) || !mark(c, off)) {
        return;
    }
    c->st.roots_unknown++;
    /* +0x04 x4 is a dual-use pointer: numeric {x0,x4} for the level-0 timer
     * and a character-kind byte list for multi-opponent levels, so its
     * target is left as-is. */
    conv_event_evinit(c, rd32(c, off + 0x08));
    conv_event_evbonus(c, rd32(c, off + 0x0C));
    conv_event_stage_table(c, rd32(c, off + 0x10));
    for (i = 0; i < 5; i++) {
        conv_event_player_init(c, rd32(c, off + 0x14 + (uint32_t) i * 4));
    }
}

static void conv_event_level_table(Conv* c, uint32_t off)
{
    int i;

    for (i = 0; i < EV_MAX_LEVELS; i++) {
        uint32_t field = off + (uint32_t) i * 4;
        if (!in_data(c, field, 4) || !c->reloc[field]) {
            break;
        }
        conv_event_level(c, rd32(c, field));
    }
}

/* GmIntEz.dat `gmIntroEasyTable` (gm_1832.c:119): the Classic-mode intro
 * layout table.  Every field is f32; the two pad runs and the u8 fields
 * between the rows stay as-is. */
static void conv_intro_easy_table(Conv* c, uint32_t off)
{
    int i;
    int j;

    if (!in_data(c, off, 0x9B8) || !mark(c, off)) {
        return;
    }
    c->st.roots_unknown++;
    for (i = 0; i < 3; i++) { /* x00[2], x18[3], x3C[4] slot rows */
        int count = i == 0 ? 2 : (i == 1 ? 3 : 4);
        for (j = 0; j < count; j++) {
            conv_u32_range(c, off + (i == 0 ? 0x00
                                            : (i == 1 ? 0x18 : 0x3C)) +
                                   (uint32_t) j * 0xC,
                           3);
        }
    }
    for (j = 0; j < 28; j++) { /* x6C ClassicCharLayout */
        uint32_t p = off + 0x6C + (uint32_t) j * 0x1C;
        conv_u32(c, p + 0x00);
        conv_u32(c, p + 0x04);
        conv_u32_range(c, p + 0x08, 3);
    }
    for (j = 0; j < 25; j++) { /* x37C ClassicTeamEntry */
        uint32_t p = off + 0x37C + (uint32_t) j * 0x14;
        conv_u32_range(c, p, 3);
    }
    for (j = 0; j < 3; j++) { /* x57C ClassicSplashRow */
        conv_u32_range(c, off + 0x57C + (uint32_t) j * 0x30, 12);
    }
    for (i = 0; i < 3; i++) { /* x630/x654/x678 */
        int count = i == 0 ? 3 : (i == 1 ? 3 : 4);
        for (j = 0; j < count; j++) {
            conv_u32_range(c, off + (i == 0 ? 0x630
                                            : (i == 1 ? 0x654 : 0x678)) +
                                   (uint32_t) j * 0xC,
                           3);
        }
    }
    for (j = 0; j < 28; j++) { /* x6A8 ClassicCharLayout */
        uint32_t p = off + 0x6A8 + (uint32_t) j * 0x1C;
        conv_u32(c, p + 0x00);
        conv_u32(c, p + 0x04);
        conv_u32_range(c, p + 0x08, 3);
    }
}

/* Article targets in ItCo.dat: attributes, hurtbones, model desc, dynamics
 * and the per-state joint tables.  All counts/floats are big-endian; the
 * Article itself is six pointers (relocation targets, already host order). */
#define ITEMATTR_SIZE 0x84
#define ITHURTBONEDESC_SIZE 0x20
#define ITMODELDESC_SIZE 0x10
#define BONEDYNAMICSDESC_SIZE 0x18
#define DYNAMICPARAM_SIZE 0x3C

/* BoneDynamicsDesc { bone_id; DynamicsDesc { data, count, pos } }.  The
 * source `data` is not a runtime DynamicsData linked list: lb_80011710 views
 * it as `count` packed lb_00F9_UnkDesc1Inner records (15 f32 words each) and
 * copies their solver parameters into the runtime list. */
/* DWARF: BoneDynamicsDesc */
static void conv_bone_dynamics_desc(Conv* c, uint32_t off)
{
    uint32_t data;
    int count;
    int i;
    int w;

    if (!in_data(c, off, BONEDYNAMICSDESC_SIZE)) {
        return;
    }
    conv_u32(c, off + 0x00); /* bone_id */
    conv_u32(c, off + 0x08); /* dyn_desc.count */
    conv_u32(c, off + 0x0C); /* dyn_desc.pos */
    conv_u32(c, off + 0x10);
    conv_u32(c, off + 0x14);

    data = rd32(c, off + 0x04);
    count = (int) rd32(c, off + 0x08);
    if (data == 0 || count <= 0 || count > 64 ||
        !in_data(c, data, (size_t) count * DYNAMICPARAM_SIZE))
    {
        return;
    }
    for (i = 0; i < count; i++) {
        uint32_t param = data + (uint32_t) i * DYNAMICPARAM_SIZE;
        for (w = 0; w < DYNAMICPARAM_SIZE; w += 4) {
            conv_u32(c, param + (uint32_t) w);
        }
    }
}

/* ItemAttr: two bytes of bitfields, then a dense run of 4-byte fields from
 * +0x04 to +0x80 (floats, count/type ints, two itECBs and two Vec2s). */
/* DWARF: ItemAttr */
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
/* DWARF: ItHurtBoneList */
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
/* DWARF: ItemModelDesc */
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
/* `ItCollDynamicsDesc { s32 bone_id; Vec3 offset; f32 size; }` -- five 4-byte
 * fields, no byte members (itcoll.c:66).  Not `BoneDynamicsDesc`, which is
 * 0x18 and a different shape, so the two arrays cannot share a loop.
 *
 * No `DWARF:` annotation is possible for this or for `ItCollDynamics`: both
 * are declared inside `itcoll.c`, not a header, so `dwarf_types.c` cannot
 * include them and the cross-check has nothing to compare against.
 * Re-declaring them in `dwarf_types.c` would only check my transcription
 * against itself.  P-757's row already records this class of gap. */
#define ITCOLLDYNAMICSDESC_SIZE 0x14

/* The `ItCollDynamics` half of `Article.x14_dynamics`.
 *
 * This is deliberately a second walker rather than four more lines in
 * `conv_item_dynamics`, and `decomp_layout` is the reason: that walker is
 * annotated `ItemDynamics`, which is 8 bytes, so reading +0x08 and +0x0C
 * inside it failed the cross-check as "past the end of ItemDynamics".  The
 * gate was right -- the object is not an `ItemDynamics`, it is two structs
 * overlaid, and the walkers should say so.  Splitting keeps the first half
 * honestly checked against DWARF and confines the unverifiable half here.
 *
 * `it_8027163C` casts `x14_dynamics` to `ItCollDynamics*` unconditionally and
 * reads `count` whenever the pointer is non-NULL, so every one of these
 * objects is 0x10 bytes as far as the engine is concerned.  It is still
 * bounded by evidence rather than by that assumption: the extent has to reach
 * 0x10 with nothing else starting inside it, and the descs pointer has to be
 * a relocation field. */
static void conv_itcoll_dynamics(Conv* c, uint32_t off)
{
    uint32_t descs;
    uint32_t end;
    int count;
    int i;

    if (!in_data(c, off, 0x10)) {
        return;
    }
    end = next_pointed_at_after(c, off);
    {
        uint32_t pub = next_public_after(c, c->public_off, c->nb_public, off);
        if (pub < end) {
            end = pub;
        }
    }
    if (end - off < 0x10) {
        return;
    }
    conv_u32(c, off + 0x08); /* ItCollDynamics.count */
    if (!c->reloc[off + 0x0C]) {
        return;
    }
    descs = rd32(c, off + 0x0C);
    count = (int) rd32(c, off + 0x08);
    if (descs != 0 && count > 0 && count <= 64) {
        for (i = 0; i < count; i++) {
            uint32_t d = descs + (uint32_t) i * ITCOLLDYNAMICSDESC_SIZE;
            if (!in_data(c, d, ITCOLLDYNAMICSDESC_SIZE)) {
                break;
            }
            conv_u32(c, d + 0x00); /* bone_id  */
            conv_u32(c, d + 0x04); /* offset.x */
            conv_u32(c, d + 0x08); /* offset.y */
            conv_u32(c, d + 0x0C); /* offset.z */
            conv_u32(c, d + 0x10); /* size     */
        }
    }
}

/* `Article.x14_dynamics` is read through **two** structs, and the on-disc
 * object is the union of both:
 *
 *   ItemDynamics   { int count; BoneDynamicsDesc* dyn_descs; }   (it/types.h:145)
 *   ItCollDynamics { u8 _pad[8]; s32 count; ItCollDynamicsDesc* descs; }
 *                                                                (itcoll.c:72)
 *
 * `ItCollDynamics::_pad[8]` *is* the `ItemDynamics` pair, so the second count
 * lives at +0x08 and its descs at +0x0C.  This walked only the first pair, so
 * the second count stayed big-endian: on a Party Ball the four words read
 * `1`, `0x80337c10`, **`0x01000000`**, `0x80337c28`, and `it_8027163C`
 * compares that 16,777,216 against 2 and fires
 * `itcoll.c:1050 "item dynamics hit num over!"` -- 81 of 754 matrix runs once
 * items were enabled, and the owner hit it in live play (P-778).
 *
 * The second half is `conv_itcoll_dynamics` above.  Keeping this one's own
 * `in_data(c, off, 8)` means a short object at the very end of the data
 * section still gets the half it is entitled to instead of losing both. */
/* DWARF: ItemDynamics */
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
            conv_bone_dynamics_desc(c, d);
        }
    }
    conv_itcoll_dynamics(c, off);
}

/* ItemStateDesc { AnimJoint*; MatAnimJoint*; ShapeAnimJoint*; UNK script }.
 * ItemStateArray is declared with eight entries in the decomp, but the DAT
 * stores the states used by each article (some have more than eight), followed
 * by up to 15 bytes of alignment padding and then the Article.  Walking eight
 * entries unconditionally interprets following metadata as animation roots
 * for short arrays and misses states in long arrays. */
static void conv_item_state_array(Conv* c, uint32_t off, int count)
{
    int i;

    if (count <= 0 || count > 64 ||
        !in_data(c, off, (size_t) count * 0x10) || !mark(c, off)) {
        return;
    }
    for (i = 0; i < count; i++) {
        uint32_t st = off + (uint32_t) i * 0x10;
        uint32_t anim = follow_ptr(c, st + 0x00);
        uint32_t mat = follow_ptr(c, st + 0x04);
        uint32_t shape = follow_ptr(c, st + 0x08);
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
/* It_Kind_Foods special attributes: { s32 x0; HSD_Joint* x4; s32 x8; s32 xC }
 * entries, entry 0's `x0` is the random range (28).  `x4` is a relocation
 * target (left to HSD_ArchiveParse); the other fields are big-endian and the
 * spawner reads them directly (unconverted `x0` makes HSD_Randi run wild and
 * the joint look invalid). */
#define ITEM_KIND_FOODS 18

static void conv_it_food_attrs(Conv* c, uint32_t off)
{
    uint32_t count;
    uint32_t i;

    if (!in_data(c, off, 16) || !mark(c, off)) {
        return;
    }
    count = rd32(c, off);
    if (count == 0 || count > 64) {
        count = 1;
    }
    for (i = 0; i < count; i++) {
        uint32_t e = off + i * 16;
        if (!in_data(c, e, 16)) {
            break;
        }
        conv_u32(c, e + 0x00);
        conv_u32(c, e + 0x08);
        conv_u32(c, e + 0x0C);
        /* +0x04 is an HSD_Joint* (itFoodsAttributes.x4); the relocation pass
         * owns the word, but the tree behind it is ours (P-748). */
        {
            uint32_t joint = rd32(c, e + 0x04);
            if (joint != 0 && in_data(c, joint, HSD_JOINT_SIZE)) {
                conv_joint(c, joint);
            }
        }
    }
}

/* Item kinds whose special-attribute struct is not a dense run of 4-byte
 * fields (it/itCommonItems.h).  Every other `*Attributes` struct in `it/` is
 * f32/s32/u32 throughout -- checked over all of them. */
#define ITEM_KIND_OCTAROCK 45
#define ITEM_KIND_OCTAROCK_STONE 47
#define ITEM_KIND_WHISPY_APPLE 225
#define ITEM_KIND_WHISPY_HEAL_APPLE 226

/* `Article::x4_specialAttributes`, the per-kind attribute block.
 *
 * Only the Foods layout used to be converted, so every projectile read its
 * attributes big-endian.  Link's arrow took its launch velocity from them and
 * got 2.67e23 -- it spawned, drew nothing anyone could see, flew nowhere and
 * hit nothing (P-743).  The struct differs per item kind and most have no
 * declared size here, so the block is bounded by `next_pointed_at_after()`,
 * the same rule the fighter attribute blocks use (G-178), and converted as
 * dense 4-byte fields.
 *
 * The two kinds with sub-word fields are spelled out rather than skipped,
 * because a u32 walk over `u8 x0[4]` would reverse four independent bytes. */
/* The article attribute blocks that hold `HSD_Joint*` fields, and where.
 *
 * A joint reached only through one of these is converted by nobody: the
 * relocation pass turns the word into a pointer, `conv_u32` correctly leaves
 * that word alone, and then the tree behind it is never walked.  Link's
 * hookshot is the one that bit -- `it_link_get_joint` does
 * `HSD_JObjLoadJoint(attr->x54)` on a big-endian joint, whose flags then say
 * "I have a child" when the pointer is null, and `HSD_JObjResolveRefs`
 * asserts (`jobj.c:694`).  Grabbing anyone killed the game (P-748/G-183).
 *
 * Per kind rather than "convert every relocation target in the block":
 * `conv_joint` does not validate that what it is given is a joint, so a
 * pointer to anything else would be corrupted rather than skipped. */
typedef struct ArticleJointFields {
    int kind;
    uint32_t size;    /* bytes that must be in_data before trusting offsets */
    uint32_t off[3];  /* joint pointer offsets, 0 terminates */
} ArticleJointFields;

#define ITEM_KIND_LINK_HSHOT 62
#define ITEM_KIND_CLINK_HSHOT 63
#define ITEM_KIND_NESS_YOYO 102
#define ITEM_KIND_CLIMBERS_STRING 113

static const ArticleJointFields article_joint_fields[] = {
    /* itLinkHookshotAttributes (itCharItems.h:385): three chain joints. */
    { ITEM_KIND_LINK_HSHOT, 0x60, { 0x54, 0x58, 0x5C } },
    { ITEM_KIND_CLINK_HSHOT, 0x60, { 0x54, 0x58, 0x5C } },
    /* itYoyoAttributes (itYoyo.h): string and yoyo joints.  x58 is an
     * HSD_MatAnimJoint and has its own walk. */
    { ITEM_KIND_NESS_YOYO, 0x60, { 0x50, 0x54, 0 } },
    /* itClimbersStringAttributes (itCharItems.h:40). */
    { ITEM_KIND_CLIMBERS_STRING, 0x2C, { 0x24, 0x28, 0 } },
};

static void conv_article_attr_joints(Conv* c, uint32_t special, int item_kind)
{
    size_t k;

    for (k = 0; k < ARRAY_SIZE(article_joint_fields); k++) {
        const ArticleJointFields* a = &article_joint_fields[k];
        int i;
        if (a->kind != item_kind || !in_data(c, special, a->size)) {
            continue;
        }
        for (i = 0; i < 3 && a->off[i] != 0; i++) {
            uint32_t joint = rd32(c, special + a->off[i]);
            if (joint != 0 && in_data(c, joint, HSD_JOINT_SIZE)) {
                conv_joint(c, joint);
            }
        }
        if (item_kind == ITEM_KIND_NESS_YOYO &&
            in_data(c, special, 0x5C))
        {
            uint32_t matanim = rd32(c, special + 0x58);
            if (matanim != 0) {
                conv_matanim_joint(c, matanim);
            }
        }
        return;
    }
}

/* Does `off` look like an `HSD_Joint` nobody has converted yet?
 *
 * Needed because `ftData->x48_items` articles reach `conv_article` with the
 * item kind unknown (-1), so the per-kind table above cannot help, and
 * `conv_joint` does not validate what it is handed -- pointing it at a
 * non-joint would corrupt rather than skip.  The discriminator is the scale
 * triple: read big-endian it is (1, 1, 1) for every joint in these blocks,
 * and essentially never that for anything else.  A joint some other root has
 * already converted fails this test and is skipped, which is correct: it is
 * already done, and `mark()` would refuse it anyway. */
static int looks_like_unconverted_joint(Conv* c, uint32_t off)
{
    static const uint32_t links[] = { 0x00, 0x08, 0x0C, 0x10, 0x38, 0x3C };
    size_t i;

    if (!in_data(c, off, HSD_JOINT_SIZE)) {
        return 0;
    }
    if (c->reloc[off + 0x04]) {
        return 0; /* flags is a plain word, never a relocation */
    }
    for (i = 0; i < ARRAY_SIZE(links); i++) {
        uint32_t w = off + links[i];
        if (rd32(c, w) != 0 && !c->reloc[w]) {
            return 0; /* a link field that is neither null nor a pointer */
        }
    }
    for (i = 0; i < 3; i++) {
        uint32_t bits = be32(c->data + off + 0x20 + (uint32_t) i * 4);
        float f;
        memcpy(&f, &bits, sizeof(f));
        if (!(f > 1.0e-4f && f < 1.0e4f)) {
            return 0;
        }
    }
    return 1;
}

/* Mr. Game & Watch's articles keep a part-visibility descriptor behind
 * `specialAttributes[0]`, and no walker reached it.
 *
 * `itgamewatchturtle.c:36` takes `attr = article->x4_specialAttributes` and
 * `Item_AttachGameWatchArticle` passes **`attr[0]`** to `it_8027CE64`, which
 * stores it in `item->xDD4_itemVar.gamewatch.attr`.  `it_8026EECC_VARS`
 * (itdraw.c:146) then reads it as `{ u16 x0; u8* x4; u16 x8; u8* xC }` -- two
 * `{ count, bone-index list }` pairs, 0x10 bytes.  Both pointers relocate
 * fine; both counts were raw, so `it_8026EC54` got `arg1 = 1280` for the
 * `u16` 5 and walked 1280 entries of a 5-entry list into
 * `ip->xBBC_dynamicBoneTable->bones[]` (P-776).
 *
 * Keyed on shape rather than on the fighter's name, and the shape is exact
 * enough to be safe: two counts that are **not** relocation fields alternating
 * with two that are, both counts plausible, and both lists in range.  A name
 * key would have to cover `PlGw.dat` and Kirby's copy of the same article in
 * `PlKb.dat` (`itkirbygamewatchchefpan.c:27`), which is why the row first read
 * "Kirby"; the shape covers both without guessing which archives carry it. */
static void conv_gamewatch_vis_pair(Conv* c, uint32_t special)
{
    uint32_t d;
    unsigned n0;
    unsigned n1;

    d = follow_ptr(c, special);
    if (d == 0 || !in_data(c, d, 0x10)) {
        return;
    }
    if (c->reloc[d + 0x00] || !c->reloc[d + 0x04] || c->reloc[d + 0x08] ||
        !c->reloc[d + 0x0C]) {
        return;
    }
    n0 = be16(c->data + d + 0x00);
    n1 = be16(c->data + d + 0x08);
    if (n0 < 1 || n0 > 64 || n1 < 1 || n1 > 64) {
        return;
    }
    /* The two `u8*` lists must actually hold that many bytes. */
    if (!in_data(c, rd32(c, d + 0x04), n0) ||
        !in_data(c, rd32(c, d + 0x0C), n1)) {
        return;
    }
    conv_u16(c, d + 0x00);
    conv_u16(c, d + 0x08);
}

static void conv_article_special_attrs(Conv* c, uint32_t special,
                                       int item_kind)
{
    uint32_t size;
    uint32_t i;

    conv_article_attr_joints(c, special, item_kind);
    conv_gamewatch_vis_pair(c, special);
    if (item_kind == ITEM_KIND_FOODS) {
        conv_it_food_attrs(c, special);
        return;
    }
    if (item_kind == ITEM_KIND_WHISPY_APPLE ||
        item_kind == ITEM_KIND_WHISPY_HEAL_APPLE)
    {
        /* itWhispyAppleAttributes: u8 x0[4], s32, s32, u8 xC[8], f32, f32. */
        if (!in_data(c, special, 0x1C)) {
            return;
        }
        conv_u32(c, special + 0x04);
        conv_u32(c, special + 0x08);
        conv_u32(c, special + 0x14);
        conv_u32(c, special + 0x18);
        return;
    }
    if (item_kind == ITEM_KIND_OCTAROCK ||
        item_kind == ITEM_KIND_OCTAROCK_STONE)
    {
        /* itOctarockAttributes: s32* x0 (a relocation target, left alone by
         * conv_u32), six f32, s16 x1C. */
        if (!in_data(c, special, 0x20)) {
            return;
        }
        for (i = 0x04; i <= 0x18; i += 4) {
            conv_u32(c, special + i);
        }
        conv_u16(c, special + 0x1C);
        return;
    }
    size = next_pointed_at_after(c, special) - special;
    if (size > 0x400) {
        size = 0x400;
    }
    for (i = 0; i + 4 <= size; i += 4) {
        conv_u32(c, special + i);
    }
    if (item_kind < 0) {
        /* The per-fighter `ftData->x48_items` run, where the kind is not
         * known.  Link's hookshot lives here, and its three chain joints were
         * left big-endian: `flags` 0x40100080 read the other way round is
         * 0x80001040, which has `JOBJ_INSTANCE` set, so `HSD_JObjResolveRefs`
         * looked the child up as an ID, got nothing and asserted -- grabbing
         * anyone killed the game (P-748/G-183). */
        for (i = 0; i + 4 <= size; i += 4) {
            uint32_t w = special + i;
            uint32_t target;
            if (!c->reloc[w]) {
                continue;
            }
            target = rd32(c, w);
            if (target != 0 && looks_like_unconverted_joint(c, target)) {
                conv_joint(c, target);
            }
        }
    }
}

/* DWARF: Article */
static void conv_article(Conv* c, uint32_t off, int item_kind)
{
    uint32_t attr;
    uint32_t special;
    uint32_t hurt;
    uint32_t states;
    uint32_t model;
    uint32_t dynamics;

    if (!in_data(c, off, 0x18) || !mark(c, off)) {
        return;
    }
    attr = rd32(c, off + 0x00);
    special = rd32(c, off + 0x04);
    hurt = rd32(c, off + 0x08);
    states = rd32(c, off + 0x0C);
    model = rd32(c, off + 0x10);
    dynamics = rd32(c, off + 0x14);
    if (attr != 0) {
        conv_item_attr(c, attr);
    }
    if (special != 0) {
        conv_article_special_attrs(c, special, item_kind);
    }
    if (hurt != 0) {
        conv_it_hurtbone_list(c, hurt);
    }
    if (states != 0) {
        uint32_t bytes = off > states ? off - states : 0;
        int state_count = bytes >= 0x10 ? (int) (bytes / 0x10) : 0;
        conv_item_state_array(c, states, state_count);
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
 * It_Kind_Old_Kuri - It_PKind_Start (47).  `first_kind` is the It_Kind of
 * entry 0 so per-kind special attributes can be recognised. */
/* `itPublicData`'s three `Article*` tables.  The array base is marked even
 * though the array itself holds only pointers and has nothing to byte-swap:
 * `measure_coverage` counts every relocation target whose first word is a
 * pointer as a descriptor, so an unmarked base reports as unwalked when the
 * converter has in fact walked all of it.  `ItCo.dat`'s 118-entry table at
 * 0x4ec8 was the single worst entry on the P-758 worklist for that reason --
 * 3013 words and 2,368 "changeable" -- when the array is 118 pointers ending
 * at 0x50a0 and every one of them is followed.  The number was the report's,
 * not the data's. */
static void conv_article_array(Conv* c, uint32_t off, int count, int first_kind)
{
    int i;

    if (!in_data(c, off, 4)) {
        return;
    }
    mark(c, off);
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
        conv_article(c, article, first_kind + i);
    }
}

/* Fighter_804D64FC's seven selection tables contain 0x24-byte
 * ftCo_AttackEntry records, terminated by a zero command.  Every field is a
 * 32-bit integer or float; the command scripts referenced by x0 are byte
 * streams and deliberately remain untouched. */
static void conv_ft_cpu_attack_list(Conv* c, uint32_t off)
{
    int i;

    if (!in_data(c, off, FT_CPU_ATTACK_ENTRY_SIZE) || !mark(c, off)) {
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t entry = off + (uint32_t) i * FT_CPU_ATTACK_ENTRY_SIZE;
        uint32_t cmd;
        int word;

        if (!in_data(c, entry, FT_CPU_ATTACK_ENTRY_SIZE)) {
            break;
        }
        cmd = be32(c->data + entry);
        for (word = 0; word < FT_CPU_ATTACK_ENTRY_SIZE; word += 4) {
            conv_u32(c, entry + (uint32_t) word);
        }
        if (cmd == 0) {
            break;
        }
    }
}

/* PlCo.dat pData[22] (`Fighter_804D64FC`, fighter.h) is the CPU attack
 * database.  Fields x4..x1C point to seven FighterKind-indexed arrays of
 * attack lists; x20 is the per-kind distance threshold and x24 contains six
 * held-weapon reach bonuses. */
static void conv_ft_cpu_data(Conv* c, uint32_t off)
{
    static const uint32_t attack_fields[] = {
        0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C,
    };
    size_t fi;

    if (!in_data(c, off, 0x28) || !mark(c, off)) {
        return;
    }
    for (fi = 0; fi < ARRAY_SIZE(attack_fields); fi++) {
        uint32_t table = rd32(c, off + attack_fields[fi]);
        int kind;

        if (table == 0 || !in_data(c, table, FT_KIND_MAX * 4)) {
            continue;
        }
        for (kind = 0; kind < FT_KIND_MAX; kind++) {
            uint32_t slot = table + (uint32_t) kind * 4;
            uint32_t list = rd32(c, slot);

            /* Relocation provenance prevents a malformed table from turning
             * numeric data into a walk.  A relocated zero is data offset 0,
             * not NULL (G-023); Mario's ground-attack list is exactly that
             * first data object. */
            if (c->reloc[slot]) {
                conv_ft_cpu_attack_list(c, list);
            }
        }
    }
    {
        uint32_t thresholds = rd32(c, off + 0x20);
        uint32_t weapon_reach = rd32(c, off + 0x24);
        int i;

        if (thresholds != 0 && in_data(c, thresholds, FT_KIND_MAX * 4)) {
            for (i = 0; i < FT_KIND_MAX; i++) {
                conv_u32(c, thresholds + (uint32_t) i * 4);
            }
        }
        if (weapon_reach != 0 && in_data(c, weapon_reach, 6 * 4)) {
            for (i = 0; i < 6; i++) {
                conv_u32(c, weapon_reach + (uint32_t) i * 4);
            }
        }
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
    uint32_t hidden;
    uint32_t i;

    if (!in_data(c, off, 23 * 4) || !mark(c, off)) {
        return;
    }
    common = rd32(c, off + 0x00);
    parts = rd32(c, off + 4 * 4);
    hidden = rd32(c, off + 5 * 4);
    if (common != 0 && in_data(c, common, FTCOMMONDATA_SIZE)) {
        for (i = 0; i < FTCOMMONDATA_SIZE; i += 4) {
            conv_u32(c, common + i);
        }
    }
    /* Both arrays are indexed by FighterKind (33 entries).  Walking past the
     * end lands in the neighbouring hidden-table structs and converts their
     * fields a second time. */
    if (parts != 0) {
        for (i = 0; i < FT_KIND_MAX; i++) {
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
    /* pData[8] (`Fighter_804D6534`, fighter.c:196) is the respawn/rebirth
     * platform pair: slot 0 is the joint passed to ftCommon_SetAccessory and
     * slot 1 the animation passed to ftCommon_8007E690 (ft_0D4D.c:139,148).
     * Both halves are HSD trees in PlCo.dat and stay big-endian without this
     * walk, so the platform loses its scale/rotation (P-689). */
    {
        uint32_t pair = rd32(c, off + 8 * 4);
        if (pair != 0 && in_data(c, pair, 8)) {
            uint32_t joint = rd32(c, pair + 0x00);
            uint32_t anim = rd32(c, pair + 0x04);
            if (joint != 0 && in_data(c, joint, HSD_JOINT_SIZE)) {
                conv_joint(c, joint);
            }
            if (anim != 0) {
                conv_anim_joint(c, anim);
            }
        }
    }
    /* pData[1] (`Fighter_804D6550`, fighter.c:189) is the item-throw attribute
     * table: `ftCo_ItemThrowAttrs { f32 speed; f32 angle; f32 mul }`, indexed
     * by `motion_id - ftCo_MS_LightThrowF` from motion 94 up.  Left
     * big-endian, `ftCo_80095D5C` built a throw velocity of about 1e23, and
     * `it_8026B1D4` squares it -- 9.93e22 squared is 9.87e45, past what a
     * float holds -- so the item's damage came out `inf` and `ftcoll.c:1296`
     * asserted.  The owner hit that three times (P-779).
     *
     * Bounded by `next_pointed_at_after`, not by a guessed entry count: the
     * table's length is not stored anywhere, and over-running a fighter table
     * is what P-739 was.  Its consumer reads `*(float*)(array_element -
     * 0x468)`, which looks like a G-176 cross-symbol overlay and is not --
     * `ftCo_MS_LightThrowF` is 94 and `94 * 12 == 0x468`, so that is just the
     * array index with the bias folded into the offset. */
    {
        uint32_t table = rd32(c, off + 1 * 4);
        if (table != 0) {
            uint32_t end = next_pointed_at_after(c, table);
            uint32_t pub =
                next_public_after(c, c->public_off, c->nb_public, table);
            uint32_t e;
            if (pub < end) {
                end = pub;
            }
            for (e = table; e + 12 <= end; e += 12) {
                if (!in_data(c, e, 12)) {
                    break;
                }
                conv_u32(c, e + 0x00);
                conv_u32(c, e + 0x04);
                conv_u32(c, e + 0x08);
            }
        }
    }
    /* pData[16] is the trophy-platform accessory joint
     * (`Fighter_804D6514`, ftCommon_SetAccessory) and pData[20]
     * (`Fighter_804D6504`) is another shared model; both are HSD_Joint trees
     * in PlCo.dat and their MObj rendering state must be converted. */
    {
        uint32_t acc = rd32(c, off + 16 * 4);
        uint32_t mdl = rd32(c, off + 20 * 4);
        if (acc != 0 && in_data(c, acc, HSD_JOINT_SIZE)) {
            conv_joint(c, acc);
        }
        if (mdl != 0 && in_data(c, mdl, HSD_JOINT_SIZE)) {
            conv_joint(c, mdl);
        }
    }
    /* Fighter_804D6540 (pData[5]): per-kind { {u8 part,x1,x2,depth}*; int n }
     * hidden-part lists.  ftParts_8007506C skips a part when it is listed, so
     * an unconverted count makes the tree walk and parts_num diverge.
     *
     * The table has one slot *past* the fighter kinds, at Ft_Kind_None (33,
     * which is also Ft_Kind_Max).  It is not padding: a fighter animating
     * another fighter's tree -- Kirby with a copy ability, a transformation
     * -- reaches ftAnim_8006FCE4 with that kind and indexes it.  Walking only
     * FT_KIND_MAX entries left its count big-endian, so
     * `for (i = 0; i < temp_r3->x4; i++)` ran 16,777,216 times (1 byte-
     * swapped) and walked off the end of memory.  P-754. */
    if (hidden != 0) {
        for (i = 0; i <= FT_KIND_MAX; i++) {
            uint32_t p = hidden + i * 4;
            uint32_t table;
            if (!in_data(c, p, 4)) {
                break;
            }
            table = rd32(c, p);
            if (table == 0) {
                continue;
            }
            if (in_data(c, table, 8)) {
                conv_u32(c, table + 0x04); /* x4 count */
            }
        }
    }
    {
        uint32_t cpu = rd32(c, off + 22 * 4);
        if (cpu != 0) {
            conv_ft_cpu_data(c, cpu);
        }
    }
}

/* Pl*.dat `ftData`: mostly relocation targets, but the xC/x14
 * Fighter_WaitAnimData arrays carry FigaTree offsets (x4/x8) that are copied
 * verbatim into the runtime and read as sizes (ftData_80085A14 asserts when
 * they stay big-endian).  Also the x8->x0 model_num and a few leaf structs. */
#define FT_WAITANIM_SIZE 0x18
#define FT_DATA_X1C_SIZE 0x0C

/* FtPartsVisLookup { int count; TempS* } with TempS { int count; u8* }.
 * The lookups hang off ftData_x8.x0.vis_table[costume][4].  `vis->xC[idx]`
 * is itself an array of one lookup per model (ftParts_80074D7C indexes it by
 * `vis->model_num`), and each lookup's TempS array holds `count` groups; both
 * counts are numeric and must be swapped or the DObj loop runs off the list. */
static void conv_ft_vis_lookup(Conv* c, uint32_t off, int model_num)
{
    int m;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    for (m = 0; m < model_num; m++) {
        uint32_t entry = off + (uint32_t) m * 8;
        uint32_t temps;
        int count;
        int i;
        if (!in_data(c, entry, 8)) {
            break;
        }
        conv_u32(c, entry + 0x00);
        count = (int) rd32(c, entry + 0x00);
        temps = rd32(c, entry + 0x04);
        if (temps != 0 && count > 0 && count <= 64) {
            for (i = 0; i < count; i++) {
                uint32_t t = temps + (uint32_t) i * 8;
                if (!in_data(c, t, 8) || !mark(c, t)) {
                    break;
                }
                conv_u32(c, t + 0x00);
                /* the u8 DObj-index list at t+4 is byte data */
            }
        }
    }
}

/* Fighter_WaitAnimData.x10_animCurrFlags is assigned to `Fighter.x594_s32`,
 * a union of MSB-first bitfield overlays (x594_b0..b7, x596_bits, x594_bits,
 * x597_bits).  MWCC packs the fields from the MSB of the big-endian word;
 * GCC reads LSB-first.  Repack the word-view fields (x594_bits, x596_bits.x7,
 * x597_bits) so the engine's part-vis masks and the animation kind read the
 * console values, and bit-reverse the top byte into the low byte so the
 * byte-view flags (x594_b1_loop, x594_b0/b5/b7) read the console bits too —
 * without that, walk/run animations never get AOBJ_LOOP and freeze after one
 * cycle. */
static void conv_waitanim_flags(Conv* c, uint32_t off)
{
    uint32_t w;
    uint32_t h;
    uint32_t top;
    uint32_t low = 0;
    int b;

    if (!in_data(c, off, 4) || c->num[off] || c->reloc[off]) {
        return;
    }
    c->num[off] = 1;
    w = be32(c->data + off);
    top = (w >> 24) & 0xFF;
    for (b = 0; b < 8; b++) {
        if (top & (1u << b)) {
            low |= 1u << (7 - b);
        }
    }
    h = low | (((w >> 22) & 3u) << 8) |
        (((w >> 9) & 0x1fff) << 10) | (((w >> 6) & 7) << 23) |
        ((w & 0x3f) << 26);
    wr32(c->data + off, h);
}

/* `ftData->x48_items` is *almost* an `Article*` array, and the walk below
 * proves each entry looks like one before following it.  A few characters keep
 * something else in a slot, and the decompilation says exactly which:
 *
 *   - Mr. Game & Watch: `items[10]` is an `FtPartsVisLookup[]`.
 *     `ftGw_Init_OnLoad` (ftgamewatch.c:540) assigns it to `fp->x5AC.xC[4]`,
 *     the fifth part-visibility group, and nothing else in the archive points
 *     at it.  Left unconverted, its `TempS.x0` count read 0x0B000000 --
 *     big-endian 11 -- so `ftParts_80074D7C` indexed a 116-entry DObj list
 *     with 202 and handed the garbage to `HSD_DObjSetFlags`.  Every match
 *     with G&W in it died in the shadow pass (P-765).
 *
 * **A shape heuristic does not work here**, which is why this is keyed on the
 * symbol name instead.  G&W's lookup array passes the Article test -- its
 * first word is a plausible `ItemAttr*` -- so the generic walk happily
 * converted it as an Article.  The decompilation names the slot, so use that
 * rather than guessing from the bytes. */
static int ft_x48_vis_lookup_slot(const char* name, size_t name_len)
{
    if (name_len >= 15 && memcmp(name, "ftDataGamewatch", 15) == 0) {
        return 10;
    }
    return -1;
}

/* The other four fighters that keep something besides an `Article` in an
 * `x48_items` slot -- and unlike G&W's lookup array, what they keep is a
 * **model**, so leaving it unconverted crashes rather than merely misbehaves.
 *
 * Three of them hand the slot straight to `ftCommon_SetAccessory`, which is
 * `HSD_JObjLoadJoint` (ftcommon.c:1003).  `DObjLoad` switches on
 * `mobj->rendermode & 0x60000000` and panics on the fourth combination
 * (dobj.c:194), so a big-endian `rendermode` is a guaranteed `HSD_Panic` the
 * first time the move is used: the owner's report was
 * `0x31001060`, which is `0x60100031` -- a perfectly ordinary rendermode --
 * read from the wrong end.
 *
 *   - Samus  `items[4]`: `UNK_SAMUS_S1`, the throw grapple beam
 *     (`ftSs_Init_CreateThrowGrappleBeam`, ftsamus.c:352).  Four pointers,
 *     walked below.
 *   - Kirby  `items[4]`: an `HSD_Joint*` (`ftKb_SpecialN_800F5898`,
 *     ftkirbyspecials.c:206 -> `ftCo_ThrownKirby.c:126`).
 *   - Yoshi  `items[3]`: an `HSD_Joint*` (`ftYs_SpecialN_8012CDD4`,
 *     ftyoshispecialn.c:122 -> `ftCo_YoshiEgg.c:108`).
 *   - Sheik  `items[4]` and `items[5]`: the chain joints
 *     `ftSk_SpecialS_80110610` (ftseakspecials.c:84) picks between by motion
 *     state and reads `item[2]` out of.
 *
 * All four are keyed on the symbol name for the reason the G&W comment above
 * gives: the bytes do not distinguish them.  Each of the three plain slots
 * has the same shape on disc -- `class_name` NULL, a small flags word read
 * from the wrong end, a child pointer, `next` NULL -- which is an ordinary
 * `HSD_JObjDesc` root and nothing an Article test can tell apart. */
enum {
    FT_X48_ARTICLE = 0,
    FT_X48_JOINT,      /* HSD_Joint* -> HSD_JObjLoadJoint */
    FT_X48_SAMUS_BEAM  /* UNK_SAMUS_S1 */
};

static int ft_x48_named_slot(const char* name, size_t name_len, int k)
{
    if (name_len >= 11 && memcmp(name, "ftDataSamus", 11) == 0) {
        return k == 4 ? FT_X48_SAMUS_BEAM : FT_X48_ARTICLE;
    }
    if (name_len >= 11 && memcmp(name, "ftDataKirby", 11) == 0) {
        return k == 4 ? FT_X48_JOINT : FT_X48_ARTICLE;
    }
    if (name_len >= 11 && memcmp(name, "ftDataYoshi", 11) == 0) {
        return k == 3 ? FT_X48_JOINT : FT_X48_ARTICLE;
    }
    if (name_len >= 10 && memcmp(name, "ftDataSeak", 10) == 0) {
        return (k == 4 || k == 5) ? FT_X48_JOINT : FT_X48_ARTICLE;
    }
    return FT_X48_ARTICLE;
}

/* `UNK_SAMUS_S1` (ftSamus/types.h:80): the four things
 * `ftSs_Init_CreateThrowGrappleBeam` uses, in the order it uses them.  Every
 * field is a pointer, so the relocation pass has already put the words
 * themselves in host order -- what this walker is for is *following* them,
 * which nothing did. */
static void conv_ft_samus_grapple(Conv* c, uint32_t off)
{
    uint32_t joint;
    uint32_t anims;
    uint32_t anim;
    uint32_t matanim;
    int i;

    if (!in_data(c, off, 0x10) || !mark(c, off)) {
        return;
    }
    joint = rd32(c, off + 0x00);   /* x0_joint        -> SetAccessory */
    anims = rd32(c, off + 0x04);   /* x4_anim_joints  -> [msid - ThrowF] */
    anim = rd32(c, off + 0x08);    /* x8_anim_joint   -> JObjAddAnimAll */
    matanim = rd32(c, off + 0x0C); /* xC_matanim_joint  (same call)     */

    if (joint != 0) {
        conv_joint(c, joint);
    }
    /* Four entries, not a guessed run: the index is `motion_state -
     * ftCo_MS_ThrowF` and the four throws (F, B, Hi, Lw) are consecutive
     * motion states (ftCommon/forward.h:508). */
    if (anims != 0) {
        for (i = 0; i < 4; i++) {
            uint32_t e = anims + (uint32_t) i * 4;
            uint32_t aj;
            if (!in_data(c, e, 4) || !c->reloc[e]) {
                break;
            }
            aj = rd32(c, e);
            if (aj != 0) {
                conv_anim_joint(c, aj);
            }
        }
    }
    if (anim != 0) {
        conv_anim_joint(c, anim);
    }
    if (matanim != 0) {
        conv_matanim_joint(c, matanim);
    }
}

/* One entry of `ftData->x1C`, the part-animation table:
 *
 *     ftData_x1C { u16 x0; u16 x2; u8* x4; HSD_AnimJoint** x8; }
 *
 * (`decomp/src/melee/ft/types.h:717`).  `x0` is the first Fighter_Part and
 * `x2` the part-list count.
 *
 * `x4` is `x2` **bytes** of part indices -- `ftAnim_80070F28` and
 * `ftAnim_800707B0` read them as `u8` -- so it is byte data and stays
 * big-endian.
 *
 * `x8` is the array `ftAnim_ApplyPartAnim` (ftanim.c:1281) indexes with
 * `Fighter_x8B0_t.x11` to get the `HSD_AnimJoint` it hands to
 * `ftAnim_80070904`, and **nothing in the archive records its length**: `x2`
 * bounds `x4`, not this.  That is why it was never followed, and it is the
 * head of P-758: every `HSD_AObjDesc`/`HSD_FObjDesc` chain hanging off these
 * joints stayed big-endian across the 34 `PlXx.dat` files -- about 20,600
 * descriptors, 82% of the cold words in the `Pl*` family.
 *
 * Bound it by evidence, two ways at once and never by a guessed count.
 * Walking one element past a fighter table is what P-739 was, and what
 * follows these arrays is the `CMD_BE` command scripts, so a wrong bound
 * trades a crash for silently wrong data:
 *
 *   - stop at the first slot that is not a relocation field.  In `PlMr.dat`
 *     the three arrays at 0x8d14/0x8d34/0x8d48 hold four pointers each and
 *     are followed by the `21 22 23 ... 2d` part-index runs and by zeros,
 *     none of which is a relocation;
 *   - stop at the first offset something else points at, which is where the
 *     next object starts even if the pointers run on with no gap.  This one
 *     earns its keep: `PlMr.dat`'s 0x8d54 holds a pointer and continues the
 *     relocation run, but an article field at 0x8d84 points at it, and
 *     0xfc68 -- what it points to -- fails every `HSD_AnimJoint` test;
 *   - **stop at the next public symbol.**  A relocation run walks straight
 *     into a neighbouring object that nothing *points* at, because the game
 *     reaches it by name instead -- and `ftData` itself is exactly that
 *     object.  In `PlCa.dat` the third array is at 0x99f8 and
 *     `ftDataCaptain` is at 0x9a04, three slots later, so without this the
 *     walk reads `ftData->x0`, `->x4`, `->xC` ... as animation joints,
 *     `ftCo_DatAttrs` and the `x1C` table among them.  Falcon, Donkey and
 *     both wireframes do it.  `next_public_after` exists for exactly this
 *     and says so: clamp to the next symbol rather than trust a run. */
/* DWARF: ftData_x1C */
static void conv_ft_part_anim(Conv* c, uint32_t off)
{
    uint32_t anims;
    uint32_t end;
    uint32_t p;

    if (!in_data(c, off, FT_DATA_X1C_SIZE) || !mark(c, off)) {
        return;
    }
    conv_u16(c, off + 0x00); /* x0: first Fighter_Part */
    conv_u16(c, off + 0x02); /* x2: part-list count    */
    /* +0x04 is the u8 part-index run -- byte data, never swapped. */
    if (!c->reloc[off + 0x08]) {
        return;
    }
    anims = rd32(c, off + 0x08);
    if (anims == 0 || !in_data(c, anims, 4)) {
        return;
    }
    end = next_pointed_at_after(c, anims);
    {
        uint32_t pub =
            next_public_after(c, c->public_off, c->nb_public, anims);
        if (pub < end) {
            end = pub;
        }
    }
    for (p = anims; p + 4 <= end && c->reloc[p]; p += 4) {
        uint32_t animjoint = rd32(c, p);
        if (animjoint != 0) {
            conv_anim_joint(c, animjoint);
        }
    }
}

/* ftDynamics: dynamicsNum, ftDynamicBones*, x4, x8, x10.  Each
 * ArticleDynamicBones entry is a BoneDynamicsDesc (0x18):
 * { enum_t bone_id; DynamicsData* data; u32 count; Vec3 pos }.
 * ftCo_8009CF84 indexes fp->parts by bone_id, so an unconverted bone_id
 * walks off the part list.
 *
 * Factored out of `conv_ft_data` so `conv_kirby_hat` can reach it too: a
 * Kirby copy archive's five `hat_dynamics[]` are the same struct. */
/* DWARF: ftDynamics */
static void conv_ft_dynamics(Conv* c, uint32_t dyn)
{
    uint32_t bones;
    uint32_t dyn_x8;
    int n;
    int m;
    int i;

    if (dyn == 0 || !in_data(c, dyn, 0x14)) {
        return;
    }
    conv_u32(c, dyn + 0x00);
    conv_u32(c, dyn + 0x08);
    n = (int) rd32(c, dyn + 0x00);
    bones = rd32(c, dyn + 0x04);
    if (bones != 0 && n > 0 && n <= 16) {
        for (i = 0; i < n; i++) {
            uint32_t e = bones + (uint32_t) i * 0x18;
            if (!in_data(c, e, 0x18)) {
                break;
            }
            conv_bone_dynamics_desc(c, e);
        }
    }
    /* dyn->x8 is a second ftData_x38 array (ftColl_8007B320 walks
     * fp->x1670 through it), distinct from ftData->x38. */
    m = (int) rd32(c, dyn + 0x08);
    dyn_x8 = rd32(c, dyn + 0x0C);
    if (dyn_x8 != 0 && m > 0 && m <= 16) {
        for (i = 0; i < m; i++) {
            uint32_t e = dyn_x8 + (uint32_t) i * 0x14;
            int w;
            if (!in_data(c, e, 0x14)) {
                break;
            }
            for (w = 0; w < 0x14; w += 4) {
                conv_u32(c, e + (uint32_t) w);
            }
        }
    }
}

/* `ftDataKirbyCopy<X>` is **not an `ftData`**, and this is P-755.
 *
 * `ftKb_SpecialN_800EED50` (ftkirby.c:2773) loads each `PlKbCp*.dat` into
 * `((HSD_Archive**) &ft_80459B88)[kind]`, i.e. `ft_80459B88.hats[kind]`,
 * which is a **`KirbyHatStruct`** (ft/types.h:2020):
 *
 *     { HSD_Joint* hat_joint; FtPartsDesc desc; ftDynamics* hat_dynamics[5]; }
 *
 * The root dispatch matched them on the `"ftData"` prefix and handed them to
 * `conv_ft_data`, which then read +0x08 as `ftData->x8`, +0x0C/+0x14 as the
 * `Fighter_WaitAnimData` arrays, +0x1C as the part-animation table and so on
 * -- every offset meaning something else.  `PlKbCpCl.dat` shows it plainly:
 * +0x00 is a textbook `HSD_Joint` (scale 1,1,1 at +0x20), +0x04 reads
 * `0x01000000` -- **big-endian 1, the `FtPartsDesc.model_num` nobody
 * converted** -- and +0x0C..+0x14 are three `ftDynamics`.
 *
 * That unconverted `model_num` is exactly P-755: `ftParts_8007487C`
 * (ftparts.c:518) does `vis->model_num = desc->model_num` and reports
 * "fighter parts model num over!" when it exceeds 11.  16,777,216 exceeds 11.
 *
 * The `FtPartsDesc` is **embedded at +0x04**, not behind a pointer the way
 * `ftData->x8` holds it, which is why the normal fighter path converts its
 * `model_num` and this one never did.
 *
 * **Four of the 24 do not have this layout and are deliberately left alone:**
 * `PlKbCpDk`, `PlKbCpFc`, `PlKbCpMt` and `PlKbCpPr` put their relocations at
 * +0x04/+0x0C/+0x14 instead of +0x00/+0x08, so whatever they are, they are
 * not a `KirbyHatStruct` -- read as one, their `model_num` comes out
 * 1,275,068,416.  The guard below is self-validating in the same spirit as
 * the `yakumono_param` fallback: `hat_joint` must be a relocation or null,
 * `model_num` must **not** be a relocation and must satisfy the engine's own
 * bound (`ftParts_8007487C` reports above 11), and `vis_table` must be a
 * relocation or null.  A root that fails is skipped rather than guessed at,
 * and recorded in P-755's row. */
/* DWARF: KirbyHatStruct */
static void conv_kirby_hat(Conv* c, uint32_t off)
{
    uint32_t joint;
    uint32_t vis_table;
    int n_models;
    int i;

    if (!in_data(c, off, 0x20)) {
        return;
    }
    /* Self-validating: refuse a root whose shape contradicts the type. */
    if (c->reloc[off + 0x04] || be32(c->data + off + 0x04) > 11u) {
        return;
    }
    if (rd32(c, off + 0x00) != 0 && !c->reloc[off + 0x00]) {
        return;
    }
    if (rd32(c, off + 0x08) != 0 && !c->reloc[off + 0x08]) {
        return;
    }
    if (!mark(c, off)) {
        return;
    }
    joint = rd32(c, off + 0x00);
    if (joint != 0) {
        conv_joint(c, joint);
    }
    conv_u32(c, off + 0x04); /* FtPartsDesc.model_num */
    n_models = (int) rd32(c, off + 0x04);
    if (n_models < 0 || n_models > 12) {
        n_models = 0;
    }
    /* FtPartsDesc.vis_table, the same `void* (*)[4]` per-costume table
     * `conv_ft_data` walks, and bounded the same way: every real slot is a
     * relocation target and the first non-pointer word is past the end
     * (P-764).
     *
     * **The two tests have to be in this order, and this one used to have
     * them the other way round.**  A legitimately-NULL slot is not a
     * relocation target, so testing `c->reloc[p]` first ended the walk at the
     * first hole -- and the holes are not rare or late: measured across the
     * disc, 13 of the 17 tables stop on a NULL slot, most of them at
     * **costume 0, column 3**, with between 2 and 14 relocation-backed slots
     * still behind them.  So only costume 0's first three lookups were ever
     * converted and every later costume's part-visibility and TObj-index
     * selection stayed big-endian, which is why the opponent rendered in the
     * wrong costume colour while player 1 was right (P-820).  NULL is a legal
     * hole; only a non-NULL word that is not a pointer is past the end. */
    vis_table = rd32(c, off + 0x08);
    if (vis_table != 0) {
        int costume;
        int ended = 0;
        for (costume = 0; costume < 8 && !ended; costume++) {
            int col;
            for (col = 0; col < 4; col++) {
                uint32_t p = vis_table +
                             ((uint32_t) costume * 4 + (uint32_t) col) * 4;
                uint32_t lookup;
                if (!in_data(c, p, 4)) {
                    ended = 1;
                    break;
                }
                lookup = rd32(c, p);
                if (lookup == 0) {
                    continue; /* a legal hole, not the end of the table */
                }
                if (!c->reloc[p]) {
                    ended = 1;
                    break;
                }
                conv_ft_vis_lookup(c, lookup, n_models);
            }
        }
    }
    /* **`hat_dynamics[]` is deliberately not walked.**  Its name is wrong:
     * the slots are overloaded per fighter kind and only one of the uses in
     * `ftkirby.c` is actually an `ftDynamics`.
     *
     *   [0] `it_8026B3F8((Article*) hat->hat_dynamics[0], ...)`  (3751-3815)
     *   [1] `u32 mask = (u32) hat->hat_dynamics[1];`             (2851, 3153)
     *   [2] `HSD_Joint* root = (HSD_Joint*) hat->hat_dynamics[2];`   (3023)
     *   [3] `lookup = (FtPartsVisLookup*) hat->hat_dynamics[3];`     (3751)
     *   [4] `hats[Pichu]->hat_dynamics[4]->ftDynamicBones`           (2683)
     *       but also `*(u32*) ((u8*) hat->hat_dynamics[4] + 8)`      (3756)
     *
     * Slot [1] is not even a pointer.  This is the same trap as G&W's
     * `ftData->x48_items[10]` (P-765), where a slot that passes the Article
     * test is really an `FtPartsVisLookup[]` -- and that row's conclusion
     * applies here too: **a shape heuristic does not work, the slot has to be
     * keyed on what the decompilation says per kind.**  That is a table of
     * per-fighter special cases and is left for a follow-up; walking these as
     * `ftDynamics` would corrupt four uses out of five.  `conv_ft_dynamics`
     * is factored out and ready for whoever writes it.
     *
     * `i` is unused for now. */
    (void) i;
}

static void conv_ft_data(Conv* c, uint32_t off, const char* name,
                         size_t name_len)
{
    uint32_t x8;
    uint32_t x30;
    uint32_t x34;
    uint32_t x44;
    uint32_t x50;
    int i;

    if (!in_data(c, off, 0x60) || !mark(c, off)) {
        return;
    }
    x8 = rd32(c, off + 0x08);
    {
        /* x0 is `ftCo_DatAttrs`, x4 the per-character `ft??_DatAttrs`: both
         * dense 4-byte floats/ints (ftMr_Init_OnLoad reads item kinds out of
         * the first).
         *
         * This used to walk 0x424 bytes from x0 alone.  0x424 is the size
         * `fighter.c:146` gives `fighter_dat_attrs_alloc_data`, the runtime
         * *backup* block -- it is not the size of the struct in the archive,
         * which is 0x184.  The extra 0x2A0 bytes ran through x4 (which is why
         * the character attributes came out converted at all) and then off
         * the end of it into whatever followed.  In every Pl*.dat what
         * follows is the fighter's special-move **command scripts**: for
         * PlLk.dat, `ftDataLink->x0` is 0x33DC, so the walk reached 0x3800 and
         * byte-swapped the scripts at 0x363C (SpecialNStart) and 0x36F0
         * (SpecialNEnd).  Those are `CMD_BE` -- the engine reads the raw
         * big-endian command words -- so every opcode in them decoded as
         * garbage, the interpreter hit `op=0` and stopped on the first word,
         * and no subaction event in any special move ever ran.  Hence no
         * projectiles from anyone (P-739/G-178).
         *
         * Bound both objects by the next offset anything points at, which is
         * exact and needs no per-character size table, and cap x0 at the
         * struct size the decompilation declares. */
        uint32_t attrs = rd32(c, off + 0x00);
        uint32_t ext = rd32(c, off + 0x04);
        uint32_t size;
        uint32_t ai;

        if (attrs != 0) {
            size = next_pointed_at_after(c, attrs) - attrs;
            if (size > 0x184) {
                size = 0x184; /* sizeof(ftCo_DatAttrs) */
            }
            for (ai = 0; ai + 4 <= size; ai += 4) {
                conv_u32(c, attrs + ai);
            }
        }
        if (ext != 0) {
            /* No declared size: the type is per character (ftMario_DatAttrs,
             * ftLk_DatAttrs, ...).  The bound is the size, and the cap is
             * only a guard against a missing successor -- Kirby's is 0x424,
             * the largest on the disc. */
            size = next_pointed_at_after(c, ext) - ext;
            if (size > 0x424) {
                size = 0x424;
            }
            for (ai = 0; ai + 4 <= size; ai += 4) {
                conv_u32(c, ext + ai);
            }
        }
    }
    /* x20 is the guard blend pose: `ftData_x20 { HSD_Joint** x0; f32 x8; }`
     * (ft/types.h:700).  Nothing walked it, so the joint tree stayed
     * big-endian: every joint came out with `scale = 4.6006e-41`, which is
     * 1.0f byte-reversed.
     *
     * `ftCo_Guard.c` blends that pose into the fighter through
     * `ftAnim_80070108`/`8006FA58` -> `lb_8000C868`, which reads the raw
     * `HSD_Joint`'s position/rotation/scale.  The result went into the anim
     * skeleton and then, via `ftAnim_8006FE9C` -> `lb_8000C490`, into the
     * shield joint -- whose matrix `efLib_Update` uses for the bubble's
     * scale.  A degenerate matrix there makes the bubble NaN, so shielding
     * hid the fighter (correctly) and drew no bubble (P-747/G-182).
     *
     * Note the decomp types `x0` as `HSD_Joint**` and reads `x0[2]`; that is
     * the root joint's own `child` field at +0x08, not a third array entry.
     * Converting the tree from the root covers it. */
    {
        uint32_t x20 = rd32(c, off + 0x20);
        if (x20 != 0 && in_data(c, x20, 8)) {
            uint32_t root = rd32(c, x20 + 0x00);
            conv_u32(c, x20 + 0x04);
            if (root != 0 && in_data(c, root, HSD_JOINT_SIZE)) {
                conv_joint(c, root);
            }
        }
    }
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
    /* x54 is a relocation-backed pointer to the per-costume part table (five
     * ints) that ftCo_8009F834 reads when a command's bone id is 0x8D;
     * left big-endian the entries are byte-reversed ints (P-685). */
    {
        uint32_t part_tbl = rd32(c, off + 0x54);
        if (part_tbl != 0 && in_data(c, part_tbl, 5 * 4)) {
            conv_u32_range(c, part_tbl, 5);
        }
    }

    /* x8 can legitimately be data offset 0 (G-023): the ftData_x8 tables of
     * PlMr.dat live at the start of the data section. */
    if (in_data(c, x8, 0x18)) {
        uint32_t cost_tbl;
        int n_tobjs;
        int n_models;
        int k;
        conv_u32(c, x8 + 0x00); /* FtPartsDesc.model_num */
        conv_u32(c, x8 + 0x08); /* ftData_x8_x8.x8 */
        n_models = (int) rd32(c, x8 + 0x00);
        if (n_models < 0 || n_models > 12) {
            n_models = 0;
        }
        {
            uint32_t vis_table = rd32(c, x8 + 0x04);
            int costume;
            int ended = 0;
            if (vis_table != 0) {
                for (costume = 0; costume < 8 && !ended; costume++) {
                    int col;
                    for (col = 0; col < 4; col++) {
                        uint32_t p = vis_table +
                                     ((uint32_t) costume * 4 +
                                      (uint32_t) col) * 4;
                        uint32_t lookup;
                        if (!in_data(c, p, 4)) {
                            ended = 1;
                            break;
                        }
                        lookup = rd32(c, p);
                        if (lookup == 0) {
                            continue;
                        }
                        /* Every real vis_table slot is a relocation target;
                         * the first non-pointer word is past the table (the
                         * costume TObj array follows it).
                         *
                         * `ended` stops the *outer* loop too.  Breaking only
                         * the inner one left `costume` free to advance past
                         * the end of the table and find a later word that
                         * happened to be a relocation target -- for Pichu the
                         * `ftData_x8_x8.xC` costume table, three words on.
                         * conv_ft_vis_lookup then read those TObj-index
                         * arrays as FtPartsVisLookup entries and `conv_u32`
                         * byte-swapped the words that hold two u16 indices
                         * each, so index 2 came back as 0x0200 = 512 and
                         * `ftParts_80075240` asserted "can't find tobj!"
                         * before the match started (P-764).  Fighters with
                         * fewer costumes have shorter tables, which is why
                         * only Roy, Pichu and Ganondorf hit it. */
                        if (!c->reloc[p]) {
                            ended = 1;
                            break;
                        }
                        conv_ft_vis_lookup(c, lookup, n_models);
                    }
                }
            }
        }
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
                /* **Eight is the cap, not the count.**  A fighter with fewer
                 * costumes ends this table early, and the words after it are
                 * whatever the archive put there.  Without this check `arr`
                 * is ordinary data used as an offset, and the inner loop
                 * byte-swaps `n_tobjs` u16 wherever it lands: in `PlEm.dat`
                 * it landed on the **symbol string** at 0x18 and transposed
                 * its first two halfwords, turning
                 * `PlyEmblem5K_Share_ACTION_WallDamage_figatree` into
                 * `lPEymblem5K_...`.  `ftData_80085CD8` then looked that name
                 * up in the animation archive it had just DMA'd in, got NULL,
                 * and left `fp->x590` NULL -- so the joints were never
                 * rebound and the previous animation's FObjs kept playing
                 * over the new data (P-797, G-203).  A slot that is not a
                 * relocation target is not a pointer; stop there. */
                if (!c->reloc[p]) {
                    break;
                }
                arr = rd32(c, p);
                if (arr == 0) {
                    continue;
                }
                for (j = 0; j < n_tobjs; j++) {
                    uint32_t u = arr + (uint32_t) j * 2;
                    if (!in_data(c, u, 2)) {
                        break;
                    }
                    /* If the 4-byte word containing this u16 is a relocation
                     * target, the reloc pass already byte-swapped it (two
                     * u16s at once); converting again would undo it. */
                    if (!c->reloc[u & ~3u]) {
                        conv_u16(c, u);
                    }
                }
            }
        }
    }
    /* ftData->x1C is a table of `ftData_x1C*` part-animation descriptors.
     * Fighter.x8B0 has five runtime slots, so five is the cap, but fighter
     * archives serialize only a leading run.  conv_ft_part_anim converts each
     * descriptor's two u16 and follows its `x8` animation-joint array; before
     * it did the latter, the AObj/FObj chains under those joints were never
     * reached (P-758's head item).
     *
     * **A relocation slot is not enough to bound this table.**  In PlMr.dat
     * the table is at 0x2534 and holds three entries, and the word straight
     * after it -- 0x2540 -- is `ftData->x20`, an `ftData_x20 { HSD_Joint**
     * x0; f32 x8; }` whose `x0` is a relocation field like the slots are.  A
     * `c->reloc[slot]` bound runs into it and reads an `HSD_Joint**` array as
     * a fourth part-animation descriptor, which is the P-739 shape.  Bound it
     * by `next_pointed_at_after()` as well: `ftData->x20` points at 0x2540,
     * so the table ends there, at three. */
    {
        uint32_t table = rd32(c, off + 0x1C);
        if (table != 0 && in_data(c, table, 4)) {
            uint32_t table_end = next_pointed_at_after(c, table);
            for (i = 0; i < 5; i++) {
                uint32_t slot = table + (uint32_t) i * 4;
                uint32_t entry;
                if (slot + 4 > table_end || !in_data(c, slot, 4) ||
                    !c->reloc[slot])
                {
                    break;
                }
                entry = rd32(c, slot);
                if (entry == 0 || !in_data(c, entry, FT_DATA_X1C_SIZE)) {
                    break;
                }
                conv_ft_part_anim(c, entry);
            }
        }
    }
    /* xC and x14 are Fighter_WaitAnimData arrays; convert every entry.  The
     * arrays have different lengths (the compiled count is per fighter), so
     * bound each one by the closest ftData pointer/field after its start;
     * walking past the array corrupts the ftData struct itself. */
    {
        static const uint32_t wa_fields[] = {
            0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C,
            0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x58, 0x5C,
        };
        int pass;
        for (pass = 0; pass < 2; pass++) {
            uint32_t start = rd32(c, off + (pass == 0 ? 0x0C : 0x14));
            uint32_t end = 0;
            size_t fi;
            int count;

            if (start == 0) {
                continue;
            }
            for (fi = 0; fi < ARRAY_SIZE(wa_fields); fi++) {
                uint32_t p = rd32(c, off + wa_fields[fi]);
                if (p > start && (end == 0 || p < end)) {
                    end = p;
                }
            }
            if (off > start && (end == 0 || off < end)) {
                end = off;
            }
            if (end == 0) {
                end = (uint32_t) c->data_size;
            }
            /* Floor: the array ends at the next structure, which need not be
             * 0x18-aligned. */
            count = (int) ((end - start) / FT_WAITANIM_SIZE);
            if (count > 512) {
                count = 512;
            }
            /* The pointer-value bound is only an upper bound: the words after
             * the last entry can belong to other structures.  A WaitAnimData
             * entry carries a name pointer (x0) and a command-script pointer
             * (xC); an empty slot has both zero.  Stop at the first record
             * with neither, or the walk byte-swaps unrelated data -- for Fox
             * it lands in the part-animation x8 arrays and turns valid
             * AnimJoint pointers into garbage (P-652). */
            for (i = 1; i < count; i++) {
                uint32_t e = start + (uint32_t) i * FT_WAITANIM_SIZE;
                if (!in_data(c, e, FT_WAITANIM_SIZE)) {
                    break;
                }
                if (!c->reloc[e] && rd32(c, e) != 0) {
                    count = i;
                    break;
                }
            }
            for (i = 0; i < count; i++) {
                uint32_t e = start + (uint32_t) i * FT_WAITANIM_SIZE;
                if (!in_data(c, e, FT_WAITANIM_SIZE)) {
                    break;
                }
                conv_u32(c, e + 0x04);
                conv_u32(c, e + 0x08);
                conv_waitanim_flags(c, e + 0x10);
            }
        }
    }
    if (x30 != 0 && in_data(c, x30, 8)) {
        int count;
        uint32_t inits;
        int hi;
        conv_u32(c, x30 + 0x00); /* hurtbox init count */
        count = (int) rd32(c, x30 + 0x00);
        inits = rd32(c, x30 + 0x04);
        /* ftHurtboxInit (ftCommon/types.h): six 4-byte fields + two Vec3,
         * 0x28 bytes; bone_idx/offsets feed ftColl_8007B320. */
        if (inits != 0 && count > 0 && count <= 16) {
            for (hi = 0; hi < count; hi++) {
                uint32_t e = inits + (uint32_t) hi * 0x28;
                int w;
                if (!in_data(c, e, 0x28)) {
                    break;
                }
                for (w = 0; w < 0x28; w += 4) {
                    conv_u32(c, e + (uint32_t) w);
                }
            }
        }
    }
    conv_ft_dynamics(c, rd32(c, off + 0x2C));
    /* x34: { Fighter_Part x0; f32 scale } (0x08). */
    if (x34 != 0 && in_data(c, x34, 8)) {
        conv_u32(c, x34 + 0x00); /* Fighter_Part part index */
        conv_u32(c, x34 + 0x04); /* scale */
    }
    /* x40: itPickup, three Vec4 grab offsets (0x30) copied verbatim into
     * Fighter.x294_itPickup by ftCo_800D0FA0/ftCo_800D105C.  The compiled
     * pickup check (ftpickupitem_80094150) and the held-item draw offset
     * (ftdrawcommon) read the floats, so leaving them big-endian puts the
     * grab volume at the denormal `x0` the host reads (effectively the
     * origin) and misplaces held items. */
    {
        uint32_t pickup = rd32(c, off + 0x40);
        if (pickup != 0 && in_data(c, pickup, 0x30)) {
            for (i = 0; i < 12; i++) {
                conv_u32(c, pickup + (uint32_t) i * 4);
            }
        }
    }
    /* x4C_sfx: FtSFX, three FtSFXArr* plus eleven s32 sound ids that
     * ft_PlaySFX/ft_800881D8 pass straight to the synth.  The decomp types
     * +0x1C as `int`, but the archive stores a third FtSFXArr pointer there
     * (it is a relocation target and ftCo_Damage assigns it to an UNK_T);
     * only walk it as an array when the field really is relocated.  Each
     * FtSFXArr { int num; s32* sfx_ids } randomises with HSD_Randi(num), so
     * its count and id array are numeric too. */
    {
        uint32_t sfx = rd32(c, off + 0x4C);
        if (sfx != 0 && in_data(c, sfx, 0x38)) {
            static const uint32_t arr_fields[] = { 0x00, 0x1C, 0x20 };
            int k;
            for (k = 0x04; k <= 0x18; k += 4) {
                conv_u32(c, sfx + (uint32_t) k);
            }
            for (k = 0x24; k <= 0x34; k += 4) {
                conv_u32(c, sfx + (uint32_t) k);
            }
            for (k = 0; k < 3; k++) {
                uint32_t at = arr_fields[k];
                uint32_t arr;
                int n;
                int j;
                if (at == 0x1C && !c->reloc[sfx + at]) {
                    continue; /* an s32 sound id in this archive */
                }
                arr = rd32(c, sfx + at);
                if (arr == 0 || !in_data(c, arr, 8)) {
                    continue;
                }
                conv_u32(c, arr + 0x00);
                n = (int) rd32(c, arr + 0x00);
                if (n <= 0 || n > 64) {
                    continue;
                }
                {
                    uint32_t ids = rd32(c, arr + 0x04);
                    for (j = 0; j < n; j++) {
                        uint32_t u = ids + (uint32_t) j * 4;
                        if (!in_data(c, u, 4)) {
                            break;
                        }
                        conv_u32(c, u);
                    }
                }
            }
        }
    }
    /* x48_items: per-fighter special-item Article array (Ness PK items,
     * Peach's Toad/turnip, Game & Watch's judgement items, ...).  Every
     * non-NULL entry is a relocation target; NULL holes are legal because
     * the table is indexed by item kind.  The run ends at the first
     * non-NULL slot that is not relocation-backed.  Only entries whose attr
     * really looks like an ItemAttr are walked: after the run some fighters
     * (Kirby, Yoshi, Pichu, Samus) have other pointer tables whose words
     * would corrupt unrelated data if treated as Article sub-tables. */
    {
        uint32_t items = rd32(c, off + 0x48);
        int vis_slot = ft_x48_vis_lookup_slot(name, name_len);
        /* Clamp the run to the next public symbol, for the reason
         * `next_public_after` was written: a relocation run walks straight
         * into a neighbouring object the game reaches by name.  `ftData`
         * itself is that object here -- in `PlSs.dat` the array is at 0x9a68
         * and `ftDataSamus` at 0x9a7c, five slots later, so the moment a slot
         * below stops breaking the loop, k=5 reads `ftData->x0`. */
        uint32_t items_end =
            items != 0
                ? next_public_after(c, c->public_off, c->nb_public, items)
                : 0;
        if (items != 0) {
            int k;
            for (k = 0; k < 32; k++) {
                uint32_t slot = items + (uint32_t) k * 4;
                uint32_t article;
                int named;
                uint32_t attr;
                float f4;
                float sc;
                uint32_t states;

                if (!in_data(c, slot, 4) || slot >= items_end) {
                    break;
                }
                article = rd32(c, slot);
                if (article == 0) {
                    continue; /* empty item kind slot */
                }
                if (!c->reloc[slot] || !in_data(c, article, 0x18)) {
                    break;
                }
                named = ft_x48_named_slot(name, name_len, k);
                if (named == FT_X48_JOINT) {
                    conv_joint(c, article);
                    continue;
                }
                if (named == FT_X48_SAMUS_BEAM) {
                    conv_ft_samus_grapple(c, article);
                    continue;
                }
                if (k == vis_slot) {
                    /* Not an Article; see ft_x48_vis_lookup_slot above. */
                    uint32_t mn = in_data(c, x8, 4) ? rd32(c, x8 + 0x00) : 0;
                    if (mn != 0 && mn <= 12) {
                        conv_ft_vis_lookup(c, article, (int) mn);
                    }
                    continue;
                }
                attr = rd32(c, article + 0x00);
                if (attr == 0 || !in_data(c, attr, ITEMATTR_SIZE) ||
                    c->reloc[attr])
                {
                    break;
                }
                /* ItemAttr.x4_throw_speed_mul / x60_scale are sane floats in
                 * every retail article; a pointer offset misread as a float
                 * is a denormal or huge.  Skip the check for an already
                 * converted attr: several item kinds share one ItemAttr, and
                 * its bytes are then host order. */
                if (!c->seen[attr]) {
                    f4 = be_f32(c->data + attr + 0x04);
                    sc = be_f32(c->data + attr + 0x60);
                    if (!(f4 > 0.01f && f4 < 1000.0f) ||
                        !(sc > 0.01f && sc < 1000.0f))
                    {
                        break;
                    }
                }
                states = rd32(c, article + 0x0C);
                if (states != 0 && states >= article) {
                    break; /* state arrays precede their Article */
                }
                conv_article(c, article, -1);
            }
        }
    }
    /* ftData_x38: two { Fighter_Part x0; Vec3 x4; f32 x10 } entries (0x14);
     * ft_8007C630 indexes fp->x1614 and resolves each joint from x0. */
    {
        uint32_t x38 = rd32(c, off + 0x38);
        if (x38 != 0) {
            for (i = 0; i < 2; i++) {
                uint32_t e = x38 + (uint32_t) i * 0x14;
                int w;
                if (!in_data(c, e, 0x14)) {
                    break;
                }
                for (w = 0; w < 0x14; w += 4) {
                    conv_u32(c, e + (uint32_t) w);
                }
            }
        }
    }
    /* ftData_x3C: UnkFloat6_Camera { Vec3 x0; Vec3 xC } — the camera box
     * half-extents ftCamera_80076018 scales into fp->x890_cameraBox. */
    {
        uint32_t x3C = rd32(c, off + 0x3C);
        if (x3C != 0 && in_data(c, x3C, 0x18)) {
            for (i = 0; i < 6; i++) {
                conv_u32(c, x3C + (uint32_t) i * 4);
            }
        }
    }
    /* ftData_x58_t (types.h): the two-bone leg IK chain lengths ft_80089B08
     * feeds to lbBgFlash_80021410 — { u8 x0, x1; f32 x4; u8 x8, x9; f32 xC;
     * u8 x10, x11; pad; f32 x18 }.  The bone indices are bytes, but the
     * three f32 lengths are big-endian on disc; leaving them raw makes Link's
     * IK target explode and its leg matrices go NaN (P-627). */
    {
        uint32_t x58 = rd32(c, off + 0x58);
        if (x58 != 0 && in_data(c, x58, 0x1C)) {
            conv_u32(c, x58 + 0x04);
            conv_u32(c, x58 + 0x0C);
            conv_u32(c, x58 + 0x18);
        }
    }
    /* ftData->x24 is the WaitStruct array used by ftCo_Wait_Anim /
     * getAnimID: 8-byte {s32 x; s32 y} entries, 0xFFFFFFFF-terminated.  The
     * first word is the anim id (returned through the union's `p.x`). */
    {
        uint32_t x24 = rd32(c, off + 0x24);
        if (x24 != 0 && in_data(c, x24, 8)) {
            int n;
            for (n = 0; n < 256; n++) {
                uint32_t e = x24 + (uint32_t) n * 8;
                uint32_t first;
                if (!in_data(c, e, 8)) {
                    break;
                }
                first = be32(c->data + e);
                conv_u32(c, e);
                conv_u32(c, e + 4);
                if (first == 0xFFFFFFFFu) {
                    break;
                }
            }
        }
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

/* DWARF: ItemCommonData */
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
            conv_article_array(c, x4, 43, 0);
        }
        if (x8 != 0) {
            conv_article_array(c, x8, 118, 43);
        }
        if (xC != 0) {
            conv_article_array(c, xC, 47, 43 + 118);
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
    /* The EF_EffectDesc array follows the two bank pointers: efAsync sets
     * `efAsync_DatEntries[..].data = &symbol->data` (the address of +0x08),
     * and efLib_Create indexes that as EF_EffectDesc[].  Each entry is
     * { f32 lifetime; StaticModelDesc model_desc } (0x14); the effect models
     * (e.g. the entry/trophy platform) load through efLib_Create. */
    {
        uint32_t descs = off + 0x08;
        if (in_data(c, descs, 0x14)) {
            uint32_t end = (uint32_t) c->data_size;
            int n;
            int i;
            /* The desc array ends where the first bank blob starts, or at
             * the first entry with no relocation-backed model pointer when
             * neither bank is present (EfDk/EfPe/EfLk/EfNs: the rest of the
             * archive is other data, and walking it as descriptors made
             * conv_static_model_full treat unrelated words as joints). */
            if (cmd > descs && cmd < end) {
                end = cmd;
            }
            if (tex > descs && tex < end) {
                end = tex;
            }
            n = (int) ((end - descs) / 0x14);
            if (n > 1024) {
                n = 1024;
            }
            for (i = 0; i < n; i++) {
                uint32_t e = descs + (uint32_t) i * 0x14;
                if (!c->reloc[e + 0x04] && !c->reloc[e + 0x08] &&
                    !c->reloc[e + 0x0C] && !c->reloc[e + 0x10])
                {
                    break;
                }
                c->st.effect_descs++;
                conv_u32(c, e);
                conv_static_model_full(c, e + 4);
            }
        }
    }
}

/* DWARF: HSD_FogDesc */
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
        conv_dynamic_models(c, models);
    }
    /* P-701: these arrays are NUL-terminated, but a terminator is not the
     * only thing that can follow them -- the word after the last entry may be
     * unrelated archive data that still looks like a plausible offset, and
     * walking into it converts whatever it hits.  `SceneDesc.fogs` in
     * GmIntEz.dat overran into an HSD_PEDesc and byte-swapped its first word,
     * turning `flags = 0x29` into 0: HSD_SetupPEMode then called
     * GXSetColorUpdate(0) and the Classic splash screen's stage-marker chain
     * drew nothing at all (G-148).  Every real entry is a relocated pointer,
     * so ask the relocation table instead of only testing for zero. */
    if (cameras != 0) {
        uint32_t p = cameras;
        for (guard = 0; guard < 64; guard++) {
            uint32_t desc;
            if (!in_data(c, p, 4) || !c->reloc[p]) {
                break;
            }
            desc = rd32(c, p);
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
            uint32_t list;
            if (!in_data(c, p, 4) || !c->reloc[p]) {
                break;
            }
            list = rd32(c, p);
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
            uint32_t desc;
            if (!in_data(c, p, 4) || !c->reloc[p]) {
                break;
            }
            desc = rd32(c, p);
            if (desc == 0 || !in_data(c, desc, 0x14)) {
                break;
            }
            conv_fogdesc(c, desc);
            p += 8;
        }
    }
}

/* StaticModelDesc (sc/types.h): joint + animjoint + matanim_joint +
 * shapeanim_joint.  Used by the EF_EffectDesc model table. */
/* DWARF: StaticModelDesc */
static void conv_static_model_full(Conv* c, uint32_t off)
{
    uint32_t joint;
    uint32_t animjoint;
    uint32_t matanim;
    uint32_t shapeanim;

    if (!in_data(c, off, 0x10)) {
        return;
    }
    joint = rd32(c, off + 0x00);
    if (joint != 0) {
        conv_joint(c, joint);
    }
    animjoint = rd32(c, off + 0x04);
    if (animjoint != 0) {
        conv_anim_joint(c, animjoint);
    }
    matanim = rd32(c, off + 0x08);
    if (matanim != 0) {
        conv_matanim_joint(c, matanim);
    }
    shapeanim = rd32(c, off + 0x0C);
    if (shapeanim != 0) {
        conv_shapeanim_joint(c, shapeanim);
    }
}

/* MnSelectChrDataTable (mncharsel.c): the character-select camera, two lights,
 * fog and nine StaticModelDescs at +0x10.  The symbol name ends in
 * "DataTable", so the generic branch used to treat it as an effect bank; the
 * camera descriptor then stayed big-endian and HSD_CObjInit panicked on the
 * projection type when the CSS loaded. */
/* TyDataf `tyModelFileTbl`/`tyModelFileUsTbl`: 0x54-byte entries whose first
 * s32 is the trophy id `Toy_8030813C` matches against (the rest is pointers
 * and strings).  Unconverted ids make every character name resolve to the
 * same fallback entry. */
static void conv_toy_model_file_table(Conv* c, uint32_t off, int count)
{
    int i;

    if (!in_data(c, off, 0x54)) {
        return;
    }
    for (i = 0; i < count; i++) {
        uint32_t e = off + (uint32_t) i * 0x54;
        if (!in_data(c, e, 0x54)) {
            break;
        }
        conv_u32(c, e + 0x00);
    }
}

/* HSD_SObjDesc (sobjlib.h): { HSD_ImageDesc* image; HSD_Tlut* tlut; }.  These
 * are the 2D sprite backgrounds -- TyMnBg.dat's ToyFigureBg*_sobjdesc are the
 * trophy screen's, loaded by toy_sobj_loop and handed straight to
 * HSD_SObjLib_803A477C.  No branch claimed the root, so the ImageDescs behind
 * it stayed big-endian and the GX draw read 320x240 as 16385x61440 and format
 * 6 as 0x06000000, which the texture decoder rejects. */
/* DWARF: HSD_SObjDesc */
static void conv_sobjdesc(Conv* c, uint32_t off)
{
    uint32_t image;
    uint32_t tlut;

    if (!in_data(c, off, 8) || !mark(c, off)) {
        return;
    }
    image = rd32(c, off + 0x00);
    if (image != 0) {
        conv_imagedesc(c, image);
    }
    tlut = rd32(c, off + 0x04);
    if (tlut != 0) {
        conv_tlutdesc(c, tlut);
    }
}

/* TyDatai trophy tables (toy.c:6824, one lbArchive_LoadSymbols call pulling
 * seven symbols).  None of these are HSD descriptors, so no branch of the name
 * dispatch claimed them and they stayed big-endian -- silently, because every
 * field is a small integer and a byte-swapped small integer is another
 * plausible small integer.
 *
 * `tyModelSortTbl` is the one that bites.  It is ToyNameData[293]: six s16 per
 * entry, x0 being the trophy id (which equals the entry index in the retail
 * data).  `_Toy_803064B8` reads x0 and `Toy_8030813C` looks it up in TyDataf's
 * `tyModelFileTbl`, whose ids were already converted by P-645.  Trophy 268 is
 * 0x010C; read the wrong way round that is 0x0C01 = 3073, which is in no
 * table, so the Trophy Gallery panicked building its list:
 *
 *     **** Not Found Toy Model!(3073)
 *
 * Verified against the raw archive: entry 0 reads (0, 0, 224, 272, 224, 272)
 * big-endian and entry 1 reads (1, 60, 175, 229, 175, 229), i.e. x0 is the
 * index and x2 the sort key -- both nonsense byte-swapped. */

/* ToyNameData: six s16 (id, sort key, and per-language name indices). */
static void conv_toy_name_sort_table(Conv* c, uint32_t off, uint32_t limit)
{
    uint32_t e;

    for (e = off; e + 0x0C <= limit; e += 0x0C) {
        if (!in_data(c, e, 0x0C)) {
            break;
        }
        conv_u16_range(c, e, 6);
    }
}

/* TrophyData (0x24): two s32, six f32 for the stand transform, four s8. */
static void conv_toy_trophy_data_table(Conv* c, uint32_t off, uint32_t limit)
{
    uint32_t e;

    for (e = off; e + 0x24 <= limit; e += 0x24) {
        if (!in_data(c, e, 0x24)) {
            break;
        }
        conv_u32_range(c, e, 8);
    }
}

/* TyDspEntry (0x10): s32 id, two u8 and two pad bytes, then two f32.  Walked
 * until an id of -1, which reads the same in either byte order
 * (tyDisplay_8031B9DC scans for that terminator rather than a count). */
static void conv_toy_display_table(Conv* c, uint32_t off)
{
    uint32_t e = off;

    while (in_data(c, e, 0x10)) {
        uint32_t id = rd32(c, e + 0x00);

        conv_u32(c, e + 0x00);
        conv_u32(c, e + 0x08);
        conv_u32(c, e + 0x0C);
        if (id == 0xFFFFFFFFu) {
            break;
        }
        e += 0x10;
    }
}

/* Bare s16 lists, likewise terminated by -1 (0xFFFF either way round). */
static void conv_toy_s16_list(Conv* c, uint32_t off)
{
    uint32_t e = off;

    while (in_data(c, e, 2)) {
        uint16_t v = rd16(c, e);

        conv_u16(c, e);
        if (v == 0xFFFFu) {
            break;
        }
        e += 2;
    }
}

static void conv_mn_select_chr_table(Conv* c, uint32_t off)
{
    uint32_t cam;
    uint32_t light0;
    uint32_t light1;
    uint32_t fog;
    int i;

    if (!in_data(c, off, 0x10)) {
        return;
    }
    cam = rd32(c, off + 0x00);
    if (cam != 0) {
        conv_cobjdesc(c, cam);
    }
    light0 = rd32(c, off + 0x04);
    if (light0 != 0) {
        conv_lightdesc(c, light0);
    }
    light1 = rd32(c, off + 0x08);
    if (light1 != 0) {
        conv_lightdesc(c, light1);
    }
    fog = rd32(c, off + 0x0C);
    if (fog != 0) {
        conv_fogdesc(c, fog);
    }
    for (i = 0; i < 9; i++) {
        conv_static_model_full(c, off + 0x10 + (uint32_t) i * 0x10);
    }
}

/* MnSelectStageDataTable (mnstagesel.c): the stage-select camera, two lights,
 * fog, eleven StaticModelDescs at +0x10 and a joint/anim tail at +0xC0. */
static void conv_mn_stage_sel_table(Conv* c, uint32_t off)
{
    uint32_t p;
    int i;

    if (!in_data(c, off, 0x10)) {
        return;
    }
    p = rd32(c, off + 0x00);
    if (p != 0) {
        conv_cobjdesc(c, p);
    }
    p = rd32(c, off + 0x04);
    if (p != 0) {
        conv_lightdesc(c, p);
    }
    p = rd32(c, off + 0x08);
    if (p != 0) {
        conv_lightdesc(c, p);
    }
    p = rd32(c, off + 0x0C);
    if (p != 0) {
        conv_fogdesc(c, p);
    }
    for (i = 0; i < 11; i++) {
        conv_static_model_full(c, off + 0x10 + (uint32_t) i * 0x10);
    }
    p = rd32(c, off + 0xC0);
    if (p != 0) {
        conv_joint(c, p);
    }
    p = rd32(c, off + 0xC4);
    if (p != 0) {
        conv_anim_joint(c, p);
    }
    p = rd32(c, off + 0xC8);
    if (p != 0) {
        conv_matanim_joint(c, p);
    }
    p = rd32(c, off + 0xCC);
    if (p != 0) {
        conv_shapeanim_joint(c, p);
    }
}

#define STAGE_ANIM_ARRAY_MAX 256

/* UnkStageDat_x8_t's anim/matanim/shapeanim arrays (+4/+8/+C) are pointer
 * arrays indexed by joint; grAnime_801C7C1C / grAnime_801C6C0C pass the
 * matching entries to HSD_AObjLoadDesc at stage load, so all three chains have
 * to be host order before the game reads their AObjDesc end_frame/flags.  The
 * runtime indexes the arrays directly, so a NULL slot is a legal "no animation
 * for this joint" gap, not the end of the array; the first slot that is no
 * longer a relocation target ends it. */
static void conv_stage_anim_array(Conv* c, uint32_t arr, int kind)
{
    int i;

    for (i = 0; i < STAGE_ANIM_ARRAY_MAX && arr != 0; i++) {
        uint32_t slot = arr + (uint32_t) i * 4;
        uint32_t a;
        if (!in_data(c, slot, 4) || !c->reloc[slot]) {
            break;
        }
        a = rd32(c, slot);
        if (a == 0) {
            continue;
        }
        if (kind == 0) {
            conv_anim_joint(c, a);
        } else if (kind == 1) {
            conv_matanim_joint(c, a);
            c->st.stage_matanims++;
        } else {
            conv_shapeanim_joint(c, a);
            c->st.stage_shapeanims++;
        }
    }
}

/* Gr*.dat `map_head`: the stage's own descriptor table (src/melee/gr/types.h
 * UnkStageDat / UnkStageDat_x8_t).  The game loads item 0's `unk0` as the
 * stage JObj (Ground_GetStageGObj, ground.c:873), item 0's x10 through
 * lb_80013B14 (camera) and x18 through lb_80011AC4 (lights), and x1C through
 * HSD_FogLoadDesc (Ground_801C1E94).  Only the numeric fields of each
 * descriptor are converted here; pointers are relocation targets already in
 * host order. */
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
                /* `pairs` may legitimately point at data offset 0 (G-023);
                 * it is a relocation target either way. */
                if ((pairs != 0 || c->reloc[e + 4]) && pair_count > 0 &&
                    pair_count <= 2048) {
                    /* `pair_count` is the number of (joint,target) pairs;
                     * Ground_801C34AC advances `pair += 2` per pair. */
                    for (pj = 0; pj < pair_count * 2; pj++) {
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
        conv_stage_anim_array(c, rd32(c, e + 0x04), 0);
        conv_stage_anim_array(c, rd32(c, e + 0x08), 1);
        conv_stage_anim_array(c, rd32(c, e + 0x0C), 2);
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

/* MELEE_DUMP=<offset>[:<words>] prints the raw big-endian words at a data
 * offset, marking which are relocation fields.  The P-758 burn-down needs to
 * identify an unwalked descriptor's *type* before it can be given a walker,
 * and the type is usually obvious from the shape of a few words: pointers,
 * small counts, plausible floats.  Off unless the variable is set. */
static void dump_words(Conv* c, const char* spec)
{
    uint32_t off = (uint32_t) strtoul(spec, NULL, 0);
    const char* colon = strchr(spec, ':');
    uint32_t n = colon != NULL ? (uint32_t) strtoul(colon + 1, NULL, 0) : 16;
    uint32_t i;

    if (n > 512) {
        n = 512;
    }
    for (i = 0; i < n; i++) {
        uint32_t a = off + i * 4;
        uint32_t w;
        float f;
        if (!in_data(c, a, 4)) {
            break;
        }
        w = be32(c->data + a);
        memcpy(&f, &w, 4);
        fprintf(stderr, "[dump] +0x%06x %08x %-4s %12d %g\n", a, w,
                c->reloc[a] ? "PTR" : "", (int) w, (double) f);
    }
}

static void convert_relocs(Conv* c, uint32_t reloc_off, uint32_t nb_reloc)
{
    uint32_t i;
    const char* find = getenv("MELEE_FIND_PTR");
    c->st.reloc_total = nb_reloc;
    /* Traces every pointer field aimed at one data offset, which is how an
     * unwalked descriptor is traced back towards whatever should have reached
     * it (P-762).  Off unless the variable is set. */
    if (find != NULL) {
        uint32_t want = (uint32_t) strtoul(find, NULL, 0);
        for (i = 0; i < nb_reloc; i++) {
            uint32_t f = rd32_abs(c, reloc_off + i * 4);
            if (in_data(c, f, 4) && be32(c->data + f) == want) {
                fprintf(stderr, "[findptr] 0x%x <- field 0x%x\n", want, f);
            }
        }
    }
    for (i = 0; i < nb_reloc; i++) {
        uint32_t field = rd32_abs(c, reloc_off + i * 4);
        if (in_data(c, field, 4)) {
            c->reloc[field] = 1;
            wr32(c->data + field, be32(c->data + field));
            c->st.reloc_valid++;
        }
    }
    {
        const char* dump = getenv("MELEE_DUMP");
        if (dump != NULL) {
            dump_words(c, dump);
        }
    }
}

/* The end of a public symbol's data: the next public that starts after it, or
 * the end of the data section.  TyDatai's trophy tables are flat arrays with
 * no length anywhere in the file, and their real entry counts are not the ones
 * the game's own TY_TROPHY_COUNT would suggest -- tyInitModelDTbl holds six
 * entries, not 293 -- so converting a guessed count walks straight through the
 * neighbouring tables and byte-swaps them a second time at the wrong
 * granularity.  Clamp to the next symbol instead of trusting a count. */
static uint32_t next_public_after(Conv* c, uint32_t public_off,
                                  uint32_t nb_public, uint32_t off)
{
    uint32_t limit = c->data_size;
    uint32_t i;

    for (i = 0; i < nb_public; i++) {
        uint32_t o = rd32_abs(c, public_off + i * 8);
        if (o > off && o < limit) {
            limit = o;
        }
    }
    return limit;
}

/* The first data offset after `off` that something in the archive points at.
 *
 * An object can only extend up to the next object anyone holds a pointer to,
 * so this is an exact upper bound on its size and needs no per-type table.
 * It reproduces the decompilation's own struct sizes: for PlLk.dat it gives
 * 0x184 for `ftData->x0` and 0xDC for `ftData->x4`, which are exactly
 * `sizeof(ftCo_DatAttrs)` and `sizeof(ftLk_DatAttrs)`; for PlMr.dat 0x84,
 * which is `sizeof(ftMario_DatAttrs)`.
 *
 * `convert_relocs` runs first and byte-swaps each pointer word in place, so
 * the words read here are already host-order data offsets. */
static uint32_t next_pointed_at_after(Conv* c, uint32_t off)
{
    uint32_t limit = (uint32_t) c->data_size;
    uint32_t i;

    for (i = 0; i < c->nb_reloc; i++) {
        uint32_t field = rd32_abs(c, c->reloc_off + i * 4);
        uint32_t target;
        if (!in_data(c, field, 4)) {
            continue;
        }
        target = rd32(c, field);
        if (target > off && target < limit) {
            limit = target;
        }
    }
    return limit;
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

    /* Per-stage parameter layouts are selected by the archive's own
     * stage-named publics (GrYt.dat carries the GrdYorster* texture names).
     * Check the marker table in order and scan every public for each marker:
     * a stage can carry another stage's texture names (GrVe has both
     * GrdVenom* and GrdCorneria*), so the most specific marker must win. */
    {
        size_t m;
        for (m = 0; m < sizeof(stage_param_markers) /
                            sizeof(stage_param_markers[0]);
             m++)
        {
            int found = 0;
            for (i = 0; i < nb_public && !found; i++) {
                uint32_t so = rd32_abs(c, public_off + i * 8 + 4);
                if ((size_t) symbols_off + so < c->size &&
                    strstr((const char*) c->d + symbols_off + so,
                           stage_param_markers[m].marker) != NULL)
                {
                    found = 1;
                }
            }
            if (found) {
                c->stage_layout = stage_param_markers[m].layout;
                break;
            }
        }
    }
    for (i = 0; i < nb_public; i++) {
        uint32_t data_off = rd32_abs(c, public_off + i * 8);
        uint32_t symbol_off = rd32_abs(c, public_off + i * 8 + 4);
        const char* name;
        size_t remaining;
        size_t length;
        uint32_t limit;

        if (in_data(c, data_off, 4) && c->reloc[data_off]) {
            c->st.roots_struct++;
        }

        if ((size_t) symbols_off + symbol_off >= c->size) {
            continue;
        }
        name = (const char*) c->d + symbols_off + symbol_off;
        if (getenv("MELEE_ROOT_TRACE") != NULL) {
            /* Offsets make the trace usable for the P-758 burn-down: a
             * descriptor found unwalked traces back to the nearest root at or
             * below its offset. */
            fprintf(stderr, "[convert] root 0x%06x %.*s\n", data_off,
                    (int) strnlen(name, 128), name);
        }
        remaining = c->size - (size_t) symbols_off - symbol_off;
        length = 0;
        while (length < remaining && name[length] != '\0') {
            length++;
        }
        c->st.public_symbols++;
        /* Only the flat trophy tables below use this; computing it per symbol
         * keeps the cost proportional to nb_public^2 on one small archive. */
        limit = data_off;

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
        } else if (length == 14 && memcmp(name, "tyModelFileTbl", 14) == 0) {
            /* TyDataf: trophy name/model table (293 entries). */
            c->st.roots_unknown++;
            conv_toy_model_file_table(c, data_off, 293);
        } else if (length == 16 &&
                   memcmp(name, "tyModelFileUsTbl", 16) == 0) {
            /* TyDataf: US trophy name/model overrides (5 entries). */
            c->st.roots_unknown++;
            conv_toy_model_file_table(c, data_off, 5);
        } else if (name_ends_with(name, length, "_sobjdesc")) {
            c->st.roots_unknown++;
            conv_sobjdesc(c, data_off);
        } else if (length == 14 && memcmp(name, "tyModelSortTbl", 14) == 0) {
            /* TyDatai: ToyNameData[293], the trophy id/sort-key table. */
            c->st.roots_unknown++;
            limit = next_public_after(c, public_off, nb_public, data_off);
            conv_toy_name_sort_table(c, data_off, limit);
        } else if (length == 14 && memcmp(name, "tyInitModelTbl", 14) == 0) {
            /* TyDatai: TrophyData[293] (default stand transforms). */
            c->st.roots_unknown++;
            limit = next_public_after(c, public_off, nb_public, data_off);
            conv_toy_trophy_data_table(c, data_off, limit);
        } else if (length == 15 &&
                   memcmp(name, "tyInitModelDTbl", 15) == 0) {
            /* TyDatai: TrophyData[293] (JP variant). */
            c->st.roots_unknown++;
            limit = next_public_after(c, public_off, nb_public, data_off);
            conv_toy_trophy_data_table(c, data_off, limit);
        } else if (length == 17 &&
                   memcmp(name, "tyDisplayModelTbl", 17) == 0) {
            c->st.roots_unknown++;
            conv_toy_display_table(c, data_off);
        } else if (length == 19 &&
                   memcmp(name, "tyDisplayModelUsTbl", 19) == 0) {
            c->st.roots_unknown++;
            conv_toy_display_table(c, data_off);
        } else if (length == 17 &&
                   memcmp(name, "tyExpDifferentTbl", 17) == 0) {
            c->st.roots_unknown++;
            conv_toy_s16_list(c, data_off);
        } else if (length == 12 && memcmp(name, "tyNoGetUsTbl", 12) == 0) {
            c->st.roots_unknown++;
            conv_toy_s16_list(c, data_off);
        } else if (length == 6 &&
                   (memcmp(name, "pnlsce", 6) == 0 ||
                    memcmp(name, "flmsce", 6) == 0)) {
            /* GmRst: results panel/film SceneDesc (cameras/lights/models). */
            c->st.roots_unknown++;
            conv_scene_desc(c, data_off);
        } else if (name_ends_with(name, length, "_scene_data")) {
            c->st.roots_unknown++;
            conv_scene_desc(c, data_off);
        } else if (length > 11 && memcmp(name, "visual", 6) == 0 &&
                   name_ends_with(name, length, "Scene")) {
            /* Vi*.dat `visual<n>Scene` / `visual<n>InfoScene`: the cutscene
             * `SceneDesc` -- `un_804D6FB8->models[i]->joint`,
             * `->cameras->desc` and `->cameras->anims[0]` in vi0801.c:75-135
             * and its siblings.  Nine roots across the `Vi*` family, walked
             * by nothing until now, which is the "one missing root rule" the
             * P-758 row predicted for `Vi*`.  `standScene` and `cut*Scene`
             * below already take this path. */
            c->st.roots_unknown++;
            conv_scene_desc(c, data_off);
        } else if (length == 10 && memcmp(name, "standScene", 10) == 0) {
            /* GmRgStnd: trophy-stand scene desc (vi1201/v2, gmregtyfall). */
            c->st.roots_unknown++;
            conv_scene_desc(c, data_off);
        } else if (length > 7 && memcmp(name, "cut", 3) == 0 &&
                   name_ends_with(name, length, "Scene")) {
            /* GmRegEnd: cutscene camera/model descs. */
            c->st.roots_unknown++;
            conv_scene_desc(c, data_off);
        } else if (length == 23 &&
                   memcmp(name, "sqEventInitDataLevelTbl", 23) == 0) {
            /* GmEvent.dat event level table (gmevent.c:207). */
            conv_event_level_table(c, data_off);
        } else if (length == 16 &&
                   memcmp(name, "gmIntroEasyTable", 16) == 0) {
            /* GmIntEz.dat Classic-mode intro layout. */
            conv_intro_easy_table(c, data_off);
        } else if (length == 15 &&
                   memcmp(name, "quake_model_set", 15) == 0) {
            /* Gr*.dat: a single `DynamicModelDesc` -- the stage's
             * earthquake model plus its per-QuakeKind animation array,
             * `HSD_JObjLoadJoint(->joint)` and
             * `HSD_JObjAddAnimAll(jobj, ->anims[quake_idx])` in grlib.c:240
             * and :249.  Present in 99 archives and walked by nothing until
             * now, so every stage's quake model and its animations stayed
             * big-endian. */
            c->st.roots_unknown++;
            conv_dynamic_model_desc(c, data_off);
        } else if (length == 10 && memcmp(name, "ALDYakuAll", 10) == 0) {
            /* Gr*.dat: a NULL-terminated array of pointers, read from index
             * **1**, whose elements are assigned straight to
             * `ItemStateDesc.xC_script` (ground.c:494).  Those are `CMD_BE`
             * command scripts -- byte streams that must stay big-endian
             * (G-178) -- and the pointers themselves are relocation targets
             * the relocation pass already handled.  **There is nothing here
             * to convert**; it is named so it stops counting as an unhandled
             * root in 105 archives. */
            c->st.roots_unknown++;
        } else if (length == 16 &&
                   memcmp(name, "dbLoadCommonData", 16) == 0) {
            /* DbCo.dat: three char** name tables; the relocation pass and the
             * string data are already host-usable. */
            c->st.roots_unknown++;
        } else if (name_ends_with(name, length, "_scene_models")) {
            /* Scene/HUD sections are DynamicModelDesc** arrays
             * (lbArchive_LoadSections + x[0]->joint), not StaticModelDesc. */
            c->st.roots_unknown++;
            conv_dynamic_models(c, data_off);
        } else if (name_ends_with(name, length, "scemdls")) {
            /* IfAll/If* `Stc_scemdls`-style sections: DynamicModelDesc* array */
            c->st.roots_unknown++;
            conv_dynamic_models(c, data_off);
        } else if ((length == 4 && memcmp(name, "lupe", 4) == 0) ||
                   (length == 5 && memcmp(name, "tdsce", 5) == 0) ||
                   (length == 12 && memcmp(name, "Stc_rarwmdls", 12) == 0)) {
            /* IfAll HUD model sets whose names follow no pattern, all loaded
             * with lbArchive_LoadSections and dereferenced as
             * `(*desc)->joint`, exactly like Stc_scemdls:
             *   lupe          off-screen player magnifier (ifmagnify.c:468)
             *   tdsce         countdown timer digits     (iftime.c:35)
             *   Stc_rarwmdls  rotating HUD arrows        (if_2FD9.c:202)
             * Left unconverted, their HSD_ImageDesc kept big-endian fields:
             * the magnifier asked HSD_ImageDescCopyFromEFB for a
             * 0x4000 x 0x4000 copy (64 x 64 byte-swapped), which cost ~400 ms
             * a frame in the EFB encoder (G-146). */
            c->st.roots_unknown++;
            conv_dynamic_models(c, data_off);
        } else if (name_ends_with(name, length, "_modelset")) {
            /* GmStRoll: ScGamRegStaffrollNames_scene_modelset — the credits
             * name models are a DynamicModelDesc** (staffroll.c:84). */
            c->st.roots_unknown++;
            conv_dynamic_models(c, data_off);
        } else if (name_ends_with(name, length, "_camera")) {
            /* GmTtAll/Mn*: Sc*_cam_int1_camera */
            conv_cobjdesc(c, data_off);
        } else if (name_ends_with(name, length, "_scene_lights")) {
            /* Sc*_scene_lights: NUL-terminated LightList** array */
            conv_lightlist_array(c, data_off);
        } else if (name_ends_with(name, length, "_fog")) {
            conv_fogdesc(c, data_off);
        } else if (length == 22 &&
                   memcmp(name, "MnSelectStageDataTable", 22) == 0) {
            /* MnSlMap: stage-select camera/lights/fog/models table. */
            c->st.roots_unknown++;
            conv_mn_stage_sel_table(c, data_off);
        } else if (length == 20 &&
                   memcmp(name, "MnSelectChrDataTable", 20) == 0) {
            /* MnSlChr: character-select camera/lights/fog/models table. */
            c->st.roots_unknown++;
            conv_mn_select_chr_table(c, data_off);
        } else if (name_ends_with(name, length, "DataTable")) {
            /* Ef*.dat eff*DataTable: cmd/tex PS bank pair */
            conv_ef_dat(c, data_off);
        } else if (length >= 16 &&
                   memcmp(name, "ftLoadCommonData", 16) == 0) {
            conv_ft_common_data(c, data_off);
        } else if (length >= 15 &&
                   memcmp(name, "ftDataKirbyCopy", 15) == 0) {
            conv_kirby_hat(c, data_off);
        } else if (length >= 6 && memcmp(name, "ftData", 6) == 0) {
            conv_ft_data(c, data_off, name, length);
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
        } else if (length == 8 && memcmp(name, "map_plit", 8) == 0) {
            /* Stage LightList** used by Ground_801C49B4/lb_80011AC4. */
            conv_lightlist_array(c, data_off);
        } else if (name_ends_with(name, length, "grGroundParam")) {
            conv_ground_param(c, data_off);
        } else if (name_ends_with(name, length, "yakumono_param")) {
            conv_stage_yakumono(c, data_off);
        } else if (length > 13 && memcmp(name, "dynamicsdata_", 13) == 0) {
            /* GrCs.dat flag3/4/6 and GrRc.dat shipflag: source DynamicsDesc. */
            c->st.roots_unknown++;
            conv_dynamics_desc(c, data_off);
        } else if (length == 8 && memcmp(name, "itemdata", 8) == 0) {
            conv_itemdata(c, data_off);
        } else if (length > 19 &&
                   memcmp(name, "gmKumiteSystemTable", 19) == 0) {
            /* GmKumite.dat Stadium spawn tables (gm_181A.c). */
            c->st.roots_unknown++;
            conv_regclear_spawn_table(c, data_off);
        } else {
            /* MELEE_ROOT_TRACE lists the public symbols no rule claims.  A
             * root that should be walked but is not leaves its whole
             * sub-graph big-endian, which surfaces far away as absurd
             * dimensions or garbage pointers (G-146). */
            c->st.roots_unknown++;
            c->st.roots_unhandled++;
            if (in_data(c, data_off, 4) && c->reloc[data_off]) {
                c->st.roots_struct_unhandled++;
            }
            if (getenv("MELEE_ROOT_TRACE") != NULL) {
                fprintf(stderr, "[convert] unhandled root: %.*s\n",
                        (int) length, name);
            }
        }
    }
}


/* P-753: material-animation trees the descriptor walk cannot reach.
 *
 * ItCo.usd holds `Article` records that none of `itPublicData`'s three
 * pointer arrays name -- nothing in the archive points at them at all, so the
 * game reaches their `ItemStateArray` some other way -- and their state
 * descriptors' `HSD_MatAnimJoint` trees therefore stay big-endian.  The
 * damage lands in `HSD_TObjAddAnim`, which reads `n_tluttbl` and loops that
 * many times: a table of 3 read big-endian is 768, so it walks 765 entries
 * past the end and hands the garbage to `HSD_TlutLoadDesc`.
 *
 * Rather than guess the missing root, look for the shape.  Every relocation
 * target is a plausible struct start, so test the unwalked ones against a
 * three-level signature -- MatAnimJoint -> MatAnim -> TexAnim, each of whose
 * pointer words must be null or a relocation target, ending in counts that
 * are small when read big-endian and have a zero low byte.  Three chained
 * layouts agreeing by accident is not a real risk, and `conv_matanim_joint`
 * marks what it walks, so a tree the descriptor pass already reached is
 * skipped.  Same precedent as the article-joint scan (P-748/G-183): a
 * blanket "convert every relocation target" is not safe, a validated one is.
 */
static int null_or_pointer(const Conv* c, uint32_t off)
{
    if (!in_data(c, off, 4)) {
        return 0;
    }
    return c->reloc[off] || rd32(c, off) == 0;
}

static int looks_like_unconverted_texanim(const Conv* c, uint32_t off)
{
    uint16_t n_images;
    uint16_t n_tluts;

    if (!in_data(c, off, HSD_TEXANIM_SIZE) || c->seen[off]) {
        return 0;
    }
    if (!null_or_pointer(c, off + 0x00) || !null_or_pointer(c, off + 0x08) ||
        !null_or_pointer(c, off + 0x0C) || !null_or_pointer(c, off + 0x10))
    {
        return 0;
    }
    /* Still big-endian, so a real count has its low byte clear and its high
     * byte small.  Requiring one of the two to be non-zero keeps an all-zero
     * run of padding from matching. */
    n_images = rd16(c, off + 0x14);
    n_tluts = rd16(c, off + 0x16);
    if ((n_images & 0x00FFu) != 0 || (n_tluts & 0x00FFu) != 0) {
        return 0;
    }
    if (n_images == 0 && n_tluts == 0) {
        return 0;
    }
    return (n_images >> 8) <= HSD_MAX_LIST && (n_tluts >> 8) <= HSD_MAX_LIST;
}

static int looks_like_unconverted_matanim(const Conv* c, uint32_t off)
{
    uint32_t texanim;

    if (!in_data(c, off, HSD_MATANIM_SIZE) || c->seen[off]) {
        return 0;
    }
    if (!null_or_pointer(c, off + 0x00) || !null_or_pointer(c, off + 0x04) ||
        !null_or_pointer(c, off + 0x08) || !null_or_pointer(c, off + 0x0C))
    {
        return 0;
    }
    texanim = rd32(c, off + 0x08);
    return texanim != 0 && looks_like_unconverted_texanim(c, texanim);
}

static int looks_like_unconverted_matanim_joint(const Conv* c, uint32_t off)
{
    uint32_t matanim;

    if (!in_data(c, off, HSD_MATANIMJOINT_SIZE) || c->seen[off]) {
        return 0;
    }
    if (!null_or_pointer(c, off + 0x00) || !null_or_pointer(c, off + 0x04) ||
        !null_or_pointer(c, off + 0x08))
    {
        return 0;
    }
    matanim = rd32(c, off + 0x08);
    return matanim != 0 && looks_like_unconverted_matanim(c, matanim);
}

static void conv_orphan_matanim_trees(Conv* c)
{
    uint32_t i;

    for (i = 0; i < c->nb_reloc; i++) {
        uint32_t field = rd32_abs(c, c->reloc_off + i * 4);
        uint32_t target;

        if (!in_data(c, field, 4)) {
            continue;
        }
        target = rd32(c, field);
        if (target != 0 && looks_like_unconverted_matanim_joint(c, target)) {
            c->st.orphan_matanims++;
            conv_matanim_joint(c, target);
        }
    }
}

/* Orphan `HSD_AnimJoint` trees, the animation counterpart of
 * `conv_orphan_matanim_trees` above and the same standard of evidence.
 *
 * After the `ftData_x1C` chain landed (P-758's head item), the largest
 * remaining shape on the disc was still one 0x14 struct, and outside the
 * `PlXx.dat` files it is not the unreachable prefix case: in `ItCo.dat` the
 * chain at 0x2050f8 is a real `HSD_FObjDesc` run under an `HSD_AObjDesc` at
 * 0x205170, hanging off an `HSD_AnimJoint` at 0x205d40 whose tree root
 * nothing in the archive points at.  That is the same orphan-`Article`
 * situation the comment above describes -- ItCo carries `Article` records
 * that none of `itPublicData`'s three tables name -- reaching the animation
 * joints instead of the material ones.
 *
 * Rather than guess the missing root, test the shape, and require **three
 * chained layouts** to agree before converting anything:
 *
 *   HSD_AnimJoint -> HSD_AObjDesc -> HSD_FObjDesc
 *
 * Every pointer word of each must be null or a relocation target; the
 * `AObjDesc.end_frame` and `FObjDesc.startframe` must be plausible finite
 * frame counts when read big-endian; `FObjDesc.length` must be a sane byte
 * count; and the `AnimJoint.flags` must be small.  `conv_anim_joint` marks
 * what it walks, so a tree the descriptor pass already reached is skipped --
 * the scan only ever adds.
 *
 * The values are read **big-endian on purpose**: these words are by
 * definition unconverted, so `be32` is their true value, and the `c->num[]`
 * checks make sure no walker has already swapped them underneath us. */
static int plausible_be_frame(const Conv* c, uint32_t off)
{
    uint32_t w;
    float f;

    if (!in_data(c, off, 4) || c->reloc[off] || c->num[off]) {
        return 0;
    }
    w = be32(c->data + off);
    if (w == 0) {
        return 1;
    }
    memcpy(&f, &w, sizeof(f));
    /* Finite, non-denormal, and within the frame counts animation data
     * actually uses.  A byte-reversed float lands outside this almost
     * always -- 1.0f reversed is 4.6e-41. */
    return f > 0.0009765625f && f < 1000000.0f;
}

static int looks_like_unconverted_fobjdesc(const Conv* c, uint32_t off)
{
    uint32_t length;

    if (!in_data(c, off, HSD_FOBJDESC_SIZE) || c->seen[off]) {
        return 0;
    }
    if (!null_or_pointer(c, off + 0x00) || !null_or_pointer(c, off + 0x10)) {
        return 0;
    }
    /* `ad` is the byte stream the track plays; a real FObjDesc always has
     * one, and that is what separates this from a run of zeros. */
    if (rd32(c, off + 0x10) == 0) {
        return 0;
    }
    if (!in_data(c, off + 0x04, 4) || c->reloc[off + 0x04] ||
        c->num[off + 0x04])
    {
        return 0;
    }
    length = be32(c->data + off + 0x04);
    if (length == 0 || length > 0x10000u) {
        return 0;
    }
    return plausible_be_frame(c, off + 0x08);
}

static int looks_like_unconverted_aobjdesc(const Conv* c, uint32_t off)
{
    uint32_t fobj;

    if (!in_data(c, off, HSD_AOBJDESC_SIZE) || c->seen[off]) {
        return 0;
    }
    if (!null_or_pointer(c, off + 0x08) || !null_or_pointer(c, off + 0x0C)) {
        return 0;
    }
    if (!plausible_be_frame(c, off + 0x04)) { /* end_frame */
        return 0;
    }
    fobj = rd32(c, off + 0x08);
    return fobj != 0 && looks_like_unconverted_fobjdesc(c, fobj);
}

static int looks_like_unconverted_anim_joint(const Conv* c, uint32_t off)
{
    uint32_t aobj;

    if (!in_data(c, off, HSD_ANIMJOINT_SIZE) || c->seen[off]) {
        return 0;
    }
    if (!null_or_pointer(c, off + 0x00) || !null_or_pointer(c, off + 0x04) ||
        !null_or_pointer(c, off + 0x08) || !null_or_pointer(c, off + 0x0C))
    {
        return 0;
    }
    if (!in_data(c, off + 0x10, 4) || c->reloc[off + 0x10] ||
        c->num[off + 0x10])
    {
        return 0;
    }
    if (be32(c->data + off + 0x10) > 0xFFu) { /* flags */
        return 0;
    }
    aobj = rd32(c, off + 0x08);
    return aobj != 0 && looks_like_unconverted_aobjdesc(c, aobj);
}

static void conv_orphan_anim_trees(Conv* c)
{
    uint32_t i;

    for (i = 0; i < c->nb_reloc; i++) {
        uint32_t field = rd32_abs(c, c->reloc_off + i * 4);
        uint32_t target;

        if (!in_data(c, field, 4)) {
            continue;
        }
        target = rd32(c, field);
        if (target != 0 && looks_like_unconverted_anim_joint(c, target)) {
            c->st.orphan_animjoints++;
            conv_anim_joint(c, target);
        }
    }
}

/* P-756: measure how much of the archive the descriptor walk actually
 * reached.  Every pointer in the file is named by the relocation table, so
 * its targets enumerate every object the game can reach -- an exact
 * denominator for "structures whose layout the converter knows".  A target
 * that no walker visited still has big-endian scalars; it is a P-753 waiting
 * to happen, and this counts them before a player does. */
static void measure_coverage(Conv* c)
{
    unsigned char* counted;
    const char* unwalked = getenv("MELEE_UNWALKED");
    uint32_t i;

    c->st.data_size = (unsigned) c->data_size;
    counted = calloc(c->data_size ? c->data_size : 1, 1);
    if (counted == NULL) {
        return;
    }
    for (i = 0; i < c->nb_reloc; i++) {
        uint32_t field = rd32_abs(c, c->reloc_off + i * 4);
        uint32_t target;

        if (!in_data(c, field, 4)) {
            continue;
        }
        target = rd32(c, field);
        if (!in_data(c, target, 1) || counted[target]) {
            continue;
        }
        counted[target] = 1;
        c->st.reloc_targets++;
        if (c->seen[target] || c->num[target]) {
            c->st.reloc_targets_walked++;
        }
        /* A target whose own first word is a relocation points at something
         * else, so it is a descriptor rather than payload. */
        if (c->reloc[target]) {
            c->st.struct_targets++;
            if (c->seen[target] || c->num[target]) {
                c->st.struct_targets_walked++;
                if (unwalked != NULL && unwalked[0] == '2') {
                    fprintf(stderr, "[walked] 0x%06x <- field 0x%06x\n",
                            target, field);
                }
            } else if (unwalked) {
                /* MELEE_UNWALKED: name the descriptors no walker reached, the
                 * field that points at each, and -- decisively -- how much of
                 * each one actually needs converting.
                 *
                 * `words` is the descriptor's extent (to the next thing
                 * anything points at, which is how the rest of the converter
                 * bounds an untyped object).  `ptr` counts the words the
                 * relocation table already byte-swapped.  A descriptor where
                 * ptr == words is a pure pointer table: it has nothing left
                 * to convert, so it is unwalked only in the bookkeeping sense
                 * and adding a walker for it would raise coverage while
                 * changing no byte.  `raw` is the number that could still be
                 * wrong, and that is the real P-758 worklist. */
                /* The extent stops at the next thing anything points at --
                 * and also at the next **public symbol**, which nothing
                 * points at because the game reaches it by name.  Without
                 * that second clamp a descriptor sitting just before a
                 * symbol swallows everything after it: in `Vi1201v2.dat` the
                 * object at 0x4fae8 ran over `visual1201v2Scene` (0x4faf0)
                 * and `ftDemoVi1201V2MotionFileGkoopa` (0x4fb00) and
                 * reported 4096 words, almost all of them the motion file's
                 * byte stream -- which must never be swapped.  That one
                 * miscount is what made `Vi*` look like the densest family
                 * on the disc. */
                uint32_t end = next_pointed_at_after(c, target);
                uint32_t words;
                uint32_t pub = next_public_after(c, c->public_off,
                                                 c->nb_public, target);
                if (pub < end) {
                    end = pub;
                }
                words = end > target ? (end - target) / 4 : 0;
                uint32_t w;
                uint32_t ptr = 0;
                uint32_t conv = 0;
                uint32_t chg = 0;
                if (words > 4096) {
                    words = 4096;
                }
                for (w = 0; w < words; w++) {
                    uint32_t a = target + w * 4;
                    if (c->reloc[a]) {
                        ptr++;          /* the reloc pass already swapped it */
                    } else if (c->num[a] || c->num[a + 2]) {
                        conv++;         /* a walker converted it in place */
                    }
                }
                /* `cold` is the number that matters: words that are neither
                 * relocation targets nor touched by any walker, so they are
                 * still big-endian in the converted image.  A descriptor with
                 * cold == 0 is unwalked only in the bookkeeping sense -- its
                 * bytes are all correct -- and giving it a walker would raise
                 * coverage without changing a byte.
                 *
                 * `chg` narrows that further, and it is the column to sort
                 * on.  A cold word whose bytes read the same both ways --
                 * every zero word, and every `0x01010101` -- is *already*
                 * correct however it was stored, so converting it changes
                 * nothing either.  Zeros are not rare in this data: an
                 * unbound `HSD_AnimJoint` carries a null `aobjdesc` and a
                 * null `robj_anim` beside its one live `flags` word, so
                 * `cold` counts three where only one can be wrong. Sorting
                 * by `cold` sends the next agent after descriptor families
                 * that are two thirds padding (P-758). */
                for (w = 0; w < words; w++) {
                    uint32_t a2 = target + w * 4;
                    const unsigned char* q;
                    if (c->reloc[a2] || c->num[a2] || c->num[a2 + 2]) {
                        continue;
                    }
                    if (a2 + 4 > c->data_size) {
                        break;
                    }
                    q = c->data + a2;
                    if (q[0] != q[3] || q[1] != q[2]) {
                        chg++;
                    }
                }
                fprintf(stderr,
                        "[unwalked] 0x%06x <- field 0x%06x words=%u ptr=%u "
                        "conv=%u cold=%u chg=%u\n",
                        target, field, words, ptr, conv,
                        words - ptr - conv, chg);
            }
        }
    }
    free(counted);
}

/* Archive *extern* references, and the reason they are not relocations.
 *
 * `HSD_ArchiveLocateExtern` (archive.c:96) does not patch one field per
 * symbol -- it patches a **linked list of sites**.  Each site holds the data
 * offset of the next site to patch, terminated by -1:
 *
 *     while (offset != -1U && offset < data_size) {
 *         next = *(u32*) (data + offset);
 *         *(u32*) (data + offset) = addr;
 *         offset = next;
 *     }
 *
 * `lbArchive_InitializeDAT` (lbarchive.c:27) runs it for every extern symbol
 * with `addr = NULL`, so on console every site ends up NULL and the
 * `if (matanim_joint != NULL)` guards downstream do their job.
 *
 * Those chain links are **plain numeric data**: the relocation table does not
 * list them -- that is the whole point of an extern -- and no walker reaches
 * them, so nothing byte-swapped them.  The port's loop then reads `next` as
 * `0x18070600` instead of `0x60718`, fails `offset < data_size`, and **stops
 * after the very first site**.  Every later site keeps its raw link, which
 * the game dereferences as a pointer: in `GrCn.dat` the Corneria arwing-beam
 * chain is 0x606e8 -> 0x606f8 -> 0x60708 -> 0x60718 -> 0x60728 -> 0x60738,
 * six sites of which the port patched one, and `HSD_JObjAddAnim` was handed
 * `mat_joint = 0x18070600` (P-771).
 *
 * Swap the links so the engine's own loop can follow the chain.  Disc-wide
 * this is 25 archives, 556 symbols and 630 sites, of which **74 are sites the
 * port never reached**.  Run after `convert_relocs`, so `c->reloc[]` is
 * populated and a link that is somehow also a relocation field stops the
 * walk instead of being read from the wrong end.  `c->num[]` doubles as the
 * cycle guard -- a revisited site is already converted -- and the explicit
 * cap is belt and braces. */
#define HSD_MAX_EXTERN_SITES 65536u

static void convert_extern_chains(Conv* c, uint32_t extern_off,
                                  uint32_t nb_extern)
{
    uint32_t i;

    for (i = 0; i < nb_extern; i++) {
        uint32_t off = rd32_abs(c, extern_off + i * 8);
        uint32_t guard;

        for (guard = 0; guard < HSD_MAX_EXTERN_SITES; guard++) {
            uint32_t next;
            if (off == 0xFFFFFFFFu || !in_data(c, off, 4) || (off & 3u) ||
                c->reloc[off] || c->num[off])
            {
                break;
            }
            next = be32(c->data + off);
            conv_u32(c, off);
            c->st.extern_sites++;
            off = next;
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
    c->stage_layout = STAGE_PARAM_NONE;
    c->reloc = calloc(data_size ? data_size : 1, 1);
    c->seen = calloc(data_size ? data_size : 1, 1);
    c->num = calloc(data_size ? data_size : 1, 1);
    if (c->reloc == NULL || c->seen == NULL || c->num == NULL) {
        free(c->reloc);
        free(c->seen);
        free(c->num);
        c->reloc = NULL;
        c->seen = NULL;
        c->num = NULL;
        return 0;
    }

    swap_header_and_tables(c, nb_reloc, nb_public, nb_extern);
    reloc_off = HSD_PREFIX_SIZE + data_size;
    public_off = reloc_off + nb_reloc * 4;
    symbols_off = public_off + nb_public * 8 + nb_extern * 8;

    c->reloc_off = reloc_off;
    c->nb_reloc = nb_reloc;
    c->public_off = public_off;
    c->nb_public = nb_public;
    convert_relocs(c, reloc_off, nb_reloc);
    convert_extern_chains(c, public_off + nb_public * 8, nb_extern);
    c->st.roots_total = nb_public;
    convert_roots(c, public_off, nb_public, symbols_off);
    conv_orphan_matanim_trees(c);
    conv_orphan_anim_trees(c);
    measure_coverage(c);

    c->st.ok = c->st.reloc_valid == c->st.reloc_total;
    free(c->reloc);
    free(c->seen);
    free(c->num);
    c->reloc = NULL;
    c->seen = NULL;
    c->num = NULL;
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

    /*
     * The temporary name carries the pid: the soak harness (P-759) runs many
     * boots in parallel against one shared cache directory, and a fixed
     * "<path>.tmp" let two converters interleave writes into the same file
     * before both renamed it into place.  The rename is atomic; the bytes
     * going into it were not.
     */
    n = snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long) getpid());
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
static void (*asset_register_hook)(const void*, size_t);

void hsd_asset_set_register_hook(void (*fn)(const void*, size_t))
{
    asset_register_hook = fn;
}

s32 melee_port_HSD_ArchiveParse(HSD_Archive* archive, u8* src,
                                size_t file_size)
{
    if (src != NULL && file_size >= HSD_PREFIX_SIZE) {
        hsd_asset_convert((unsigned char*) src, file_size, NULL);
        if (asset_register_hook != NULL) {
            asset_register_hook(src, file_size);
        }
    }
    return HSD_ArchiveParse(archive, src, file_size);
}

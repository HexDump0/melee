/*
 * S2 compiled-HSD asset bridge + bootstrap.  See hsd_scene.h.
 *
 * Byte-order strategy (the P-605 spec, learnings/decomp_assets.md §7): the
 * data section is not blanket-swapped.  Only the descriptor graph and the
 * archive header/tables are converted, field by field, so every byte-defined
 * range (GX display lists, vertex arrays, textures/TLUTs, FObj streams,
 * strings) keeps its original big-endian bytes.  The compiled loaders then see
 * host-order descriptors, while the GX HLE reads the byte data as BE exactly
 * like the prototype parsers (native/hsd/model.c, native/gx/texture.c).
 *
 * Verified against the S0 probe (native/tests/test_decomp_hsd.c) and the
 * archive census in native/AI/learnings/hsd_archive_format.md.
 */
#include "decomp/hsd/hsd_scene.h"

#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/tev.h>

#include "platform/disc.h"

void JObjInfoInit(void);

/* Descriptor sizes and field offsets (learnings/decomp_assets.md §2). */
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
#define HSD_PEDESC_SIZE 0x0C

#define JOBJ_INSTANCE (1 << 12)
#define JOBJ_PTCL (1 << 5)
#define JOBJ_SPLINE (1 << 14)

#define POBJ_TYPE_MASK 0x3000
#define POBJ_ENVELOPE (2 << 12)

#define MAX_WALK_DEPTH 256
#define MAX_VISITED 4096

typedef struct {
    const unsigned char* orig;
    size_t size;
    size_t data_size;
    unsigned char* work;
    int visited[MAX_VISITED];
    int visited_count;
    int depth;
} Conv;

static unsigned int rd32(const unsigned char* d, size_t o)
{
    return ((unsigned int) d[o] << 24) | ((unsigned int) d[o + 1] << 16) |
           ((unsigned int) d[o + 2] << 8) | d[o + 3];
}

static unsigned int rd16(const unsigned char* d, size_t o)
{
    return ((unsigned int) d[o] << 8) | d[o + 1];
}

/* Writes a host-order u32 (the target is little-endian x86, ADR-0012). */
static void wr32(unsigned char* d, size_t o, unsigned int v)
{
    memcpy(d + o, &v, 4);
}

static void wr16(unsigned char* d, size_t o, unsigned int v)
{
    unsigned short s = (unsigned short) v;
    memcpy(d + o, &s, 2);
}

static int in_data(const Conv* c, unsigned int off, size_t need)
{
    return (size_t) off + need <= c->data_size;
}

static int mark_visited(Conv* c, unsigned int off)
{
    int i;
    for (i = 0; i < c->visited_count; ++i) {
        if (c->visited[i] == (int) off) {
            return 0;
        }
    }
    if (c->visited_count < MAX_VISITED) {
        c->visited[c->visited_count++] = (int) off;
    }
    return 1;
}

static void conv_vtxdesc(Conv* c, unsigned int off);
static void conv_pobjdesc(Conv* c, unsigned int off);
static void conv_tobjdesc(Conv* c, unsigned int off);
static void conv_mobjdesc(Conv* c, unsigned int off);
static void conv_dobjdesc(Conv* c, unsigned int off);
static void conv_joint(Conv* c, unsigned int off);

static void conv_imagedesc(Conv* c, unsigned int off)
{
    if (!in_data(c, off, HSD_IMAGEDESC_SIZE)) {
        return;
    }
    wr32(c->work, off + 0x00, rd32(c->orig, off + 0x00));
    wr16(c->work, off + 0x04, rd16(c->orig, off + 0x04));
    wr16(c->work, off + 0x06, rd16(c->orig, off + 0x06));
    wr32(c->work, off + 0x08, rd32(c->orig, off + 0x08));
    wr32(c->work, off + 0x0C, rd32(c->orig, off + 0x0C));
    wr32(c->work, off + 0x10, rd32(c->orig, off + 0x10));
    wr32(c->work, off + 0x14, rd32(c->orig, off + 0x14));
}

static void conv_tlutdesc(Conv* c, unsigned int off)
{
    if (!in_data(c, off, HSD_TLUTDESC_SIZE)) {
        return;
    }
    wr32(c->work, off + 0x00, rd32(c->orig, off + 0x00));
    wr32(c->work, off + 0x04, rd32(c->orig, off + 0x04));
    wr32(c->work, off + 0x08, rd32(c->orig, off + 0x08));
    wr16(c->work, off + 0x0C, rd16(c->orig, off + 0x0C));
}

static void conv_texloddesc(Conv* c, unsigned int off)
{
    if (!in_data(c, off, HSD_TEXLODDESC_SIZE)) {
        return;
    }
    wr32(c->work, off + 0x00, rd32(c->orig, off + 0x00));
    wr32(c->work, off + 0x04, rd32(c->orig, off + 0x04));
    /* bias_clamp/edgeLODEnable are bytes (kept). */
    wr32(c->work, off + 0x0C, rd32(c->orig, off + 0x0C));
}

static void conv_tobjtevdesc(Conv* c, unsigned int off)
{
    if (!in_data(c, off, HSD_TOBJTEVDESC_SIZE)) {
        return;
    }
    /* 16 u8 fields + three GXColor word-runs are bytes; only `active`. */
    wr32(c->work, off + 0x1C, rd32(c->orig, off + 0x1C));
}

static void conv_tobjdesc(Conv* c, unsigned int off)
{
    static const size_t words[] = {
        0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24,
        0x28, 0x2C, 0x30, 0x34, 0x38, 0x40, 0x44, 0x48, 0x4C, 0x50,
        0x54, 0x58
    };
    unsigned int next;
    unsigned int image;
    unsigned int tlut;
    unsigned int lod;
    unsigned int tev;
    size_t i;

    if (!in_data(c, off, HSD_TOBJDESC_SIZE) || !mark_visited(c, off)) {
        return;
    }
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        wr32(c->work, off + words[i], rd32(c->orig, off + words[i]));
    }
    next = rd32(c->orig, off + 0x04);
    image = rd32(c->orig, off + 0x4C);
    tlut = rd32(c->orig, off + 0x50);
    lod = rd32(c->orig, off + 0x54);
    tev = rd32(c->orig, off + 0x58);
    if (next != 0) {
        conv_tobjdesc(c, next);
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

static void conv_mobjdesc(Conv* c, unsigned int off)
{
    unsigned int texdesc;
    unsigned int mat;

    if (!in_data(c, off, HSD_MOBJDESC_SIZE)) {
        return;
    }
    wr32(c->work, off + 0x00, rd32(c->orig, off + 0x00));
    wr32(c->work, off + 0x04, rd32(c->orig, off + 0x04));
    wr32(c->work, off + 0x08, rd32(c->orig, off + 0x08));
    wr32(c->work, off + 0x0C, rd32(c->orig, off + 0x0C));
    wr32(c->work, off + 0x10, rd32(c->orig, off + 0x10));
    wr32(c->work, off + 0x14, rd32(c->orig, off + 0x14));
    texdesc = rd32(c->orig, off + 0x08);
    mat = rd32(c->orig, off + 0x0C);
    if (texdesc != 0) {
        conv_tobjdesc(c, texdesc);
    }
    if (mat != 0 && in_data(c, mat, HSD_MATERIAL_SIZE)) {
        /* ambient/diffuse/specular are GXColor byte runs; alpha/shininess
         * are f32 words. */
        wr32(c->work, mat + 0x0C, rd32(c->orig, mat + 0x0C));
        wr32(c->work, mat + 0x10, rd32(c->orig, mat + 0x10));
    }
}

static void conv_envelopes(Conv* c, unsigned int off)
{
    int i;
    for (i = 0; i < 10; ++i) {
        unsigned int list_off;
        unsigned int desc_off;
        if (!in_data(c, off + (size_t) i * 4, 4)) {
            break;
        }
        list_off = rd32(c->orig, off + (size_t) i * 4);
        if (list_off == 0) {
            break;
        }
        wr32(c->work, off + (size_t) i * 4, list_off);
        desc_off = list_off;
        while (in_data(c, desc_off, 8)) {
            unsigned int joint = rd32(c->orig, desc_off);
            if (joint == 0) {
                break;
            }
            wr32(c->work, desc_off + 0x00, joint);
            wr32(c->work, desc_off + 0x04, rd32(c->orig, desc_off + 0x04));
            desc_off += 8;
        }
    }
}

static void conv_vtxdesc(Conv* c, unsigned int off)
{
    int i;
    for (i = 0; i < 32; ++i) {
        unsigned int attr;
        if (!in_data(c, off, HSD_VTXDESCLIST_SIZE)) {
            break;
        }
        attr = rd32(c->orig, off + 0x00);
        if (attr == GX_VA_NULL) {
            wr32(c->work, off + 0x00, attr);
            break;
        }
        wr32(c->work, off + 0x00, attr);
        wr32(c->work, off + 0x04, rd32(c->orig, off + 0x04));
        wr32(c->work, off + 0x08, rd32(c->orig, off + 0x08));
        wr32(c->work, off + 0x0C, rd32(c->orig, off + 0x0C));
        /* frac u8 kept. */
        wr16(c->work, off + 0x12, rd16(c->orig, off + 0x12));
        wr32(c->work, off + 0x14, rd32(c->orig, off + 0x14));
        off += HSD_VTXDESCLIST_SIZE;
    }
}

static void conv_pobjdesc(Conv* c, unsigned int off)
{
    unsigned int next;
    unsigned int verts;
    unsigned int upt;
    unsigned int flags;

    if (!in_data(c, off, HSD_POBJDESC_SIZE)) {
        return;
    }
    wr32(c->work, off + 0x00, rd32(c->orig, off + 0x00));
    wr32(c->work, off + 0x04, rd32(c->orig, off + 0x04));
    wr32(c->work, off + 0x08, rd32(c->orig, off + 0x08));
    wr16(c->work, off + 0x0C, rd16(c->orig, off + 0x0C));
    wr16(c->work, off + 0x0E, rd16(c->orig, off + 0x0E));
    wr32(c->work, off + 0x10, rd32(c->orig, off + 0x10));
    wr32(c->work, off + 0x14, rd32(c->orig, off + 0x14));

    next = rd32(c->orig, off + 0x04);
    verts = rd32(c->orig, off + 0x08);
    upt = rd32(c->orig, off + 0x14);
    flags = rd16(c->orig, off + 0x0C);
    if (next != 0) {
        conv_pobjdesc(c, next);
    }
    if (verts != 0) {
        conv_vtxdesc(c, verts);
    }
    if ((flags & POBJ_TYPE_MASK) == POBJ_ENVELOPE && upt != 0) {
        conv_envelopes(c, upt);
    }
}

static void conv_dobjdesc(Conv* c, unsigned int off)
{
    unsigned int next;
    unsigned int mobj;
    unsigned int pobj;

    if (!in_data(c, off, HSD_DOBJDESC_SIZE)) {
        return;
    }
    wr32(c->work, off + 0x00, rd32(c->orig, off + 0x00));
    wr32(c->work, off + 0x04, rd32(c->orig, off + 0x04));
    wr32(c->work, off + 0x08, rd32(c->orig, off + 0x08));
    wr32(c->work, off + 0x0C, rd32(c->orig, off + 0x0C));
    next = rd32(c->orig, off + 0x04);
    mobj = rd32(c->orig, off + 0x08);
    pobj = rd32(c->orig, off + 0x0C);
    if (next != 0) {
        conv_dobjdesc(c, next);
    }
    if (mobj != 0) {
        conv_mobjdesc(c, mobj);
    }
    if (pobj != 0) {
        conv_pobjdesc(c, pobj);
    }
}

static void conv_joint(Conv* c, unsigned int off)
{
    unsigned int flags;
    unsigned int child;
    unsigned int next;
    unsigned int u;
    unsigned int mtx;
    int i;

    if (!in_data(c, off, HSD_JOINT_SIZE) || c->depth > MAX_WALK_DEPTH ||
        !mark_visited(c, off)) {
        return;
    }
    c->depth++;
    flags = rd32(c->orig, off + 0x04);
    wr32(c->work, off + 0x00, rd32(c->orig, off + 0x00));
    wr32(c->work, off + 0x04, flags);
    wr32(c->work, off + 0x08, rd32(c->orig, off + 0x08));
    wr32(c->work, off + 0x0C, rd32(c->orig, off + 0x0C));
    wr32(c->work, off + 0x10, rd32(c->orig, off + 0x10));
    for (i = 0; i < 9; ++i) {
        wr32(c->work, off + 0x14 + (size_t) i * 4,
             rd32(c->orig, off + 0x14 + (size_t) i * 4));
    }
    mtx = rd32(c->orig, off + 0x38);
    wr32(c->work, off + 0x38, mtx);
    wr32(c->work, off + 0x3C, rd32(c->orig, off + 0x3C));
    if (mtx != 0 && in_data(c, mtx, 0x30)) {
        for (i = 0; i < 12; ++i) {
            wr32(c->work, mtx + (size_t) i * 4,
                 rd32(c->orig, mtx + (size_t) i * 4));
        }
    }

    child = rd32(c->orig, off + 0x08);
    next = rd32(c->orig, off + 0x0C);
    u = rd32(c->orig, off + 0x10);

    if (!(flags & (JOBJ_PTCL | JOBJ_SPLINE))) {
        if (u != 0) {
            conv_dobjdesc(c, u);
        }
    }
    if (!(flags & JOBJ_INSTANCE) && child != 0) {
        conv_joint(c, child);
    }
    if (next != 0) {
        conv_joint(c, next);
    }
    c->depth--;
}

/*
 * Clears joint->robj descriptors after HSD_ArchiveParse; the S0 probe proved
 * the bind pose does not need them and their descriptor layout is not part of
 * the S2 conversion set (learnings/decomp_assets.md open question 1).  This
 * runs on the parsed tree (host pointers).
 */
static void scrub_robj(HSD_JObj* jobj, int depth)
{
    if (jobj == NULL || depth > MAX_WALK_DEPTH) {
        return;
    }
    jobj->robj = NULL;
    if (!(jobj->flags & JOBJ_INSTANCE)) {
        scrub_robj(jobj->child, depth + 1);
    }
    scrub_robj(jobj->next, depth + 1);
}

/* Also clears the descriptor's robjdesc so a later reload cannot use it. */
static void scrub_joint_robjdesc(Conv* c, unsigned int off, int depth)
{
    unsigned int flags;
    unsigned int child;
    unsigned int next;
    if (depth > MAX_WALK_DEPTH || !in_data(c, off, HSD_JOINT_SIZE)) {
        return;
    }
    wr32(c->work, off + 0x3C, 0);
    flags = rd32(c->orig, off + 0x04);
    child = rd32(c->orig, off + 0x08);
    next = rd32(c->orig, off + 0x0C);
    if (!(flags & JOBJ_INSTANCE) && child != 0) {
        scrub_joint_robjdesc(c, child, depth + 1);
    }
    if (next != 0) {
        scrub_joint_robjdesc(c, next, depth + 1);
    }
}

/* --------------------------------------------------------------- boot */

int hsd_scene_boot(void)
{
    OSHeapHandle heap;
    void* lo;
    void* hi;
    void* new_lo;

    OSInit();
    /* Mirrors HSD_OSInit (initialize.c:161): reserve the heap descriptors and
     * create the main heap the HSD_ObjAlloc pools allocate from. */
    lo = (void*) OSRoundUp32B(OSGetArenaLo());
    hi = (void*) OSRoundDown32B(OSGetArenaHi());
    lo = OSInitAlloc(lo, hi, 4);
    OSSetArenaLo(lo);
    new_lo = (void*) OSRoundUp32B(lo);
    hi = (void*) OSRoundDown32B(hi);
    heap = OSCreateHeap(new_lo, hi);
    if (heap < 0) {
        return 0;
    }
    OSSetCurrentHeap(heap);
    HSD_SetHeap(heap);
    HSD_ObjSetHeap((u32) ((unsigned char*) hi - (unsigned char*) new_lo),
                   NULL);
    /* HSD_ObjInit is static in initialize.c; call the pools directly. */
    HSD_ListInitAllocData();
    HSD_AObjInitAllocData();
    HSD_FObjInitAllocData();
    HSD_IDInitAllocData();
    HSD_VecInitAllocData();
    HSD_MtxInitAllocData();
    HSD_RObjInitAllocData();
    HSD_RenderInitAllocData();
    HSD_ShadowInitAllocData();
    HSD_ZListInitAllocData();
    HSD_IDSetup();
    JObjInfoInit();
    return 1;
}

/* --------------------------------------------------------------- load */

static size_t strings_offset(const unsigned char* d)
{
    size_t data_size = rd32(d, 0x04);
    size_t nb_reloc = rd32(d, 0x08);
    size_t nb_public = rd32(d, 0x0C);
    size_t nb_extern = rd32(d, 0x10);
    return 0x20 + data_size + nb_reloc * 4 + nb_public * 8 + nb_extern * 8;
}

static int find_root_joint(const unsigned char* d, size_t size,
                           unsigned int* out_offset)
{
    size_t data_size = rd32(d, 0x04);
    size_t nb_reloc = rd32(d, 0x08);
    size_t nb_public = rd32(d, 0x0C);
    size_t nb_extern = rd32(d, 0x10);
    size_t public_off = 0x20 + data_size + nb_reloc * 4;
    size_t symbols_off = public_off + nb_public * 8 + nb_extern * 8;
    size_t i;

    if (symbols_off > size) {
        return 0;
    }
    for (i = 0; i < nb_public; ++i) {
        unsigned int data_off = rd32(d, public_off + i * 8);
        unsigned int name_off = rd32(d, public_off + i * 8 + 4);
        const char* name;
        if (symbols_off + name_off >= size) {
            continue;
        }
        name = (const char*) d + symbols_off + name_off;
        if (strstr(name, "_joint") != NULL &&
            strstr(name, "matanim") == NULL) {
            *out_offset = data_off;
            return 1;
        }
    }
    return 0;
}

int hsd_scene_load(HsdScene* scene, const char* disc, const char* model,
                   char* error, size_t error_size)
{
    DiscFile file;
    Conv conv;
    unsigned int root_off;
    size_t i;
    size_t symbols_off;

    memset(scene, 0, sizeof(*scene));
    if (disc_load(disc, model, &file, error, error_size) != DISC_OK) {
        return 0; /* caller SKIPs */
    }
    scene->work = (unsigned char*) malloc(file.size);
    if (scene->work == NULL) {
        disc_free(&file);
        snprintf(error, error_size, "out of memory");
        return 0;
    }
    memcpy(scene->work, file.data, file.size);
    scene->work_size = file.size;
    snprintf(scene->model, sizeof(scene->model), "%s", model);

    memset(&conv, 0, sizeof(conv));
    /* Descriptor walks use data-relative offsets: orig/work point at the
     * data section (file offset 0x20).  Header/tables are handled absolutely
     * below. */
    conv.orig = (const unsigned char*) file.data + 0x20;
    conv.work = scene->work + 0x20;
    conv.size = file.size;
    conv.data_size = rd32((const unsigned char*) file.data, 0x04);
    if (conv.data_size + 0x20 > file.size) {
        snprintf(error, error_size, "bad data size");
        goto fail;
    }

    /* Header: host order (HSD_ArchiveParse copies it and checks file_size). */
    wr32(scene->work, 0x00, rd32((const unsigned char*) file.data, 0x00));
    wr32(scene->work, 0x04, rd32((const unsigned char*) file.data, 0x04));
    wr32(scene->work, 0x08, rd32((const unsigned char*) file.data, 0x08));
    wr32(scene->work, 0x0C, rd32((const unsigned char*) file.data, 0x0C));
    wr32(scene->work, 0x10, rd32((const unsigned char*) file.data, 0x10));

    /* Relocation/public/extern tables: host order u32s. */
    {
        const unsigned char* src = (const unsigned char*) file.data;
        symbols_off = strings_offset(src);
        for (i = 0x20 + conv.data_size; i + 4 <= symbols_off; i += 4) {
            wr32(scene->work, i, rd32(src, i));
        }
    }

    if (!find_root_joint((const unsigned char*) file.data, file.size,
                         &root_off)) {
        snprintf(error, error_size, "no *_joint public symbol");
        goto fail;
    }

    scrub_joint_robjdesc(&conv, root_off, 0);
    conv.depth = 0;
    conv.visited_count = 0;
    conv_joint(&conv, root_off);

    if (HSD_ArchiveParse(&scene->archive, scene->work, file.size) != 0) {
        snprintf(error, error_size, "HSD_ArchiveParse failed");
        goto fail;
    }
    {
        char* name = NULL;
        size_t j;
        for (j = 0; j < scene->archive.header.nb_public; ++j) {
            const char* sym =
                scene->archive.symbols + scene->archive.public_info[j].symbol;
            if (strstr(sym, "_joint") != NULL &&
                strstr(sym, "matanim") == NULL) {
                name = (char*) sym;
                break;
            }
        }
        if (name == NULL) {
            snprintf(error, error_size, "root joint symbol missing");
            goto fail;
        }
        scene->root = HSD_JObjLoadJoint(
            (HSD_Joint*) HSD_ArchiveGetPublicAddress(&scene->archive, name));
    }
    if (scene->root == NULL) {
        snprintf(error, error_size, "HSD_JObjLoadJoint failed");
        goto fail;
    }
    HSD_JObjSetupMatrix(scene->root);
    scrub_robj(scene->root, 0);
    disc_free(&file);
    return 1;

fail:
    disc_free(&file);
    free(scene->work);
    scene->work = NULL;
    return -1;
}

void hsd_scene_free(HsdScene* scene)
{
    if (scene->root != NULL) {
        HSD_JObjUnrefThis(scene->root);
        scene->root = NULL;
    }
    free(scene->work);
    scene->work = NULL;
}

/* ------------------------------------------------------- part visibility */

#define FT_DATA_BASE 0x20u
#define FT_MAX_HIDDEN 256

typedef struct {
    const unsigned char* data;
    size_t size;
    unsigned int model_num;
    unsigned char hidden[FT_MAX_HIDDEN];
} VisTable;

static int vis_range(const VisTable* v, size_t off, size_t need)
{
    return off + need <= v->size;
}

/* native/hsd/parts.c: apply_variant -> mark the listed DObj indices. */
static void vis_apply_variant(VisTable* v, size_t variant_off, int hidden)
{
    unsigned int count;
    unsigned int indices;
    size_t list;
    unsigned int k;

    if (!vis_range(v, variant_off, 8)) {
        return;
    }
    count = rd32(v->data, variant_off);
    indices = rd32(v->data, variant_off + 4);
    if (indices == 0 || indices > v->size - FT_DATA_BASE) {
        return;
    }
    list = FT_DATA_BASE + indices;
    for (k = 0; k < count; ++k) {
        unsigned int idx;
        if (!vis_range(v, list + k, 1)) {
            break;
        }
        idx = v->data[list + k];
        if (idx < FT_MAX_HIDDEN) {
            v->hidden[idx] = (unsigned char) hidden;
        }
    }
}

static void vis_hide_slot(VisTable* v, size_t slot_off)
{
    size_t lookup;
    size_t i;
    if (slot_off == 0 || slot_off > v->size - FT_DATA_BASE) {
        return;
    }
    lookup = FT_DATA_BASE + slot_off;
    for (i = 0; i < v->model_num; ++i) {
        unsigned int variants;
        size_t variants_off;
        unsigned int j;
        if (!vis_range(v, lookup + i * 8, 8)) {
            break;
        }
        variants = rd32(v->data, lookup + i * 8);
        {
            unsigned int p = rd32(v->data, lookup + i * 8 + 4);
            if (p == 0 || p > v->size - FT_DATA_BASE) {
                continue;
            }
            variants_off = FT_DATA_BASE + p;
        }
        for (j = 0; j < variants; ++j) {
            vis_apply_variant(v, variants_off + (size_t) j * 8, 1);
        }
    }
}

static void vis_show_variant(VisTable* v, size_t slot_off, size_t variant)
{
    size_t lookup;
    size_t i;
    if (slot_off == 0 || slot_off > v->size - FT_DATA_BASE) {
        return;
    }
    lookup = FT_DATA_BASE + slot_off;
    for (i = 0; i < v->model_num; ++i) {
        unsigned int count;
        unsigned int p;
        if (!vis_range(v, lookup + i * 8, 8)) {
            break;
        }
        count = rd32(v->data, lookup + i * 8);
        if (variant >= count) {
            continue;
        }
        p = rd32(v->data, lookup + i * 8 + 4);
        if (p == 0 || p > v->size - FT_DATA_BASE) {
            continue;
        }
        vis_apply_variant(v, FT_DATA_BASE + p + variant * 8, 0);
    }
}

/* JObjDisp order: DObjs of this joint, child subtree, then next. */
static void vis_walk_jobjs(HSD_JObj* jobj, const VisTable* v, int* index,
                           int depth, int* hidden_count)
{
    HSD_DObj* d;
    if (jobj == NULL || depth > 256) {
        return;
    }
    if (!(jobj->flags & JOBJ_HIDDEN)) {
        for (d = jobj->u.dobj; d != NULL; d = d->next) {
            if (*index < FT_MAX_HIDDEN && v->hidden[*index]) {
                HSD_DObjSetFlags(d, DOBJ_HIDDEN);
                (*hidden_count)++;
            }
            (*index)++;
        }
    }
    if (!(jobj->flags & JOBJ_INSTANCE)) {
        vis_walk_jobjs(jobj->child, v, index, depth + 1, hidden_count);
    }
    vis_walk_jobjs(jobj->next, v, index, depth, hidden_count);
}

/* Reads the Pl<Char>.dat ftData visibility tables (raw big-endian, not the
 * HSD archive) exactly like native/hsd/parts.c. */
int hsd_scene_apply_visibility(HsdScene* scene, const char* disc,
                               const char* model, int slot, int variant,
                               char* error, size_t error_size)
{
    char ft_name[32];
    DiscFile asset;
    VisTable v;
    unsigned int ft = 0;
    size_t i;
    int hidden_count = 0;

    if (strlen(model) < 6 || strncmp(model, "Pl", 2) != 0) {
        return -1;
    }
    snprintf(ft_name, sizeof(ft_name), "%.4s.dat", model);
    if (disc_load(disc, ft_name, &asset, error, error_size) != DISC_OK) {
        return -1;
    }
    v.data = (const unsigned char*) asset.data;
    v.size = asset.size;
    v.model_num = 0;
    memset(v.hidden, 0, sizeof(v.hidden));

    {
        const unsigned char* d = v.data;
        unsigned int data_size = rd32(d, 0x04);
        unsigned int nb_reloc = rd32(d, 0x08);
        unsigned int nb_public = rd32(d, 0x0C);
        unsigned int nb_extern = rd32(d, 0x10);
        size_t public_off = 0x20 + data_size + (size_t) nb_reloc * 4;
        size_t symbols_off =
            public_off + (size_t) nb_public * 8 + (size_t) nb_extern * 8;
        for (i = 0; i < nb_public && ft == 0; ++i) {
            unsigned int data_off = rd32(d, public_off + i * 8);
            unsigned int name_off = rd32(d, public_off + i * 8 + 4);
            if (symbols_off + name_off < v.size &&
                strncmp((const char*) d + symbols_off + name_off, "ftData",
                        6) == 0) {
                ft = data_off;
            }
        }
    }
    if (ft == 0 || ft + 0x20 > v.size) {
        disc_free(&asset);
        return -1;
    }
    {
        unsigned int desc_ptr = rd32(v.data, 0x20 + ft + 8);
        unsigned int model_num;
        unsigned int vis_table;
        size_t desc;
        size_t vis;
        int s;
        int index = 0;

        if (desc_ptr > v.size - FT_DATA_BASE) {
            disc_free(&asset);
            return -1;
        }
        desc = FT_DATA_BASE + desc_ptr;
        if (!vis_range(&v, desc, 8)) {
            disc_free(&asset);
            return -1;
        }
        model_num = rd32(v.data, desc);
        vis_table = rd32(v.data, desc + 4);
        if (model_num == 0 || model_num > 64 ||
            vis_table > v.size - FT_DATA_BASE) {
            disc_free(&asset);
            return -1;
        }
        v.model_num = model_num;
        vis = FT_DATA_BASE + vis_table;
        for (s = 0; s < 4; ++s) {
            unsigned int slot_ptr;
            if (!vis_range(&v, vis + (size_t) s * 4, 4)) {
                break;
            }
            slot_ptr = rd32(v.data, vis + (size_t) s * 4);
            vis_hide_slot(&v, slot_ptr);
        }
        if (slot >= 0 && slot < 4) {
            unsigned int slot_ptr = rd32(v.data, vis + (size_t) slot * 4);
            vis_show_variant(&v, slot_ptr, (size_t) variant);
        }
        vis_walk_jobjs(scene->root, &v, &index, 0, &hidden_count);
    }
    disc_free(&asset);
    return hidden_count;
}

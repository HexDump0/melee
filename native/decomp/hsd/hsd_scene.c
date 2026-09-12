/*
 * Compiled-HSD asset bridge + bootstrap for the headless render harness and
 * the interactive viewer.  See hsd_scene.h.
 *
 * Archive byte order is handled by native/decomp/assets/hsd_convert.c through
 * the decomp shim: the compiled `HSD_ArchiveParse` symbol is renamed, so this
 * file's call converts the raw big-endian archive in place and then runs the
 * real parser.  The GX HLE still reads the byte-defined ranges (display
 * lists, vertex arrays, textures/TLUTs, FObj streams) as big-endian exactly
 * like the prototype parsers (native/hsd/model.c, native/gx/texture.c).
 *
 * Verified against the S0 probe (native/tests/test_decomp_hsd.c) and the
 * archive census in native/AI/learnings/hsd_archive_format.md.
 */
#include "decomp/hsd/hsd_scene.h"

#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <melee/lb/lbspdisplay.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/video.h>

#include "platform/disc.h"

void JObjInfoInit(void);

/* Archive conversion lives in native/decomp/assets/hsd_convert.c; the decomp
 * shim routes every compiled HSD_ArchiveParse call through it, and it
 * converts the caller's buffer in place (header/tables, every relocation
 * target, and the numeric fields of the known descriptor classes).  The
 * byte-defined ranges (display lists, vertex arrays, textures, FObj streams,
 * strings) are left big-endian for the GX HLE and the FObj player. */

static unsigned int rd32(const unsigned char* d, size_t o)
{
    return ((unsigned int) d[o] << 24) | ((unsigned int) d[o + 1] << 16) |
           ((unsigned int) d[o + 2] << 8) | d[o + 3];
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
    /* HSD_InitComponent normally fills HSD_VIData via HSD_VIInit; this harness
     * skips it.  HSD_CObjSetCurrent reads the render mode for VIEWPORT/scissor
     * scaling, so without it HSD_VIGetRenderMode returns a zeroed object and
     * the scissor becomes 0x0 (P-612). */
    HSD_VIData.current.vi.rmode = GXNtsc480IntDf;
    return 1;
}

/* --------------------------------------------------------------- load */

static int scene_open_archive(HsdScene* scene, const char* disc,
                              const char* path, char* error,
                              size_t error_size)
{
    DiscFile file;

    memset(scene, 0, sizeof(*scene));
    if (disc_load(disc, path, &file, error, error_size) != DISC_OK) {
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
    snprintf(scene->model, sizeof(scene->model), "%s", path);
    disc_free(&file);

    /* HSD_ArchiveParse is the platform wrapper: it converts the buffer in
     * place and then runs the compiled parser. */
    if (HSD_ArchiveParse(&scene->archive, scene->work, scene->work_size) !=
        0) {
        snprintf(error, error_size, "HSD_ArchiveParse failed");
        free(scene->work);
        scene->work = NULL;
        return -1;
    }
    return 1;
}

int hsd_scene_load(HsdScene* scene, const char* disc, const char* model,
                   char* error, size_t error_size)
{
    int opened = scene_open_archive(scene, disc, model, error, error_size);
    char* name = NULL;
    size_t j;

    if (opened <= 0) {
        return opened;
    }
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
    if (scene->root == NULL) {
        snprintf(error, error_size, "HSD_JObjLoadJoint failed");
        goto fail;
    }
    HSD_JObjSetupMatrix(scene->root);
    return 1;

fail:
    free(scene->work);
    scene->work = NULL;
    return -1;
}

static int scene_ptr_valid(const HsdScene* scene, const void* p)
{
    const unsigned char* base = scene->work;
    const unsigned char* end = scene->work + scene->work_size;
    const unsigned char* q = (const unsigned char*) p;
    return q >= base && q < end;
}

int hsd_scene_load_stage(HsdScene* scene, const char* disc, const char* stage,
                         int map_id, char* error, size_t error_size)
{
    HsdStageData* data;
    HsdStageMap* map;
    int opened = scene_open_archive(scene, disc, stage, error, error_size);

    if (opened <= 0) {
        return opened;
    }
    scene->stage_mode = 1;
    /* The map_head symbol is the UnkStageDat header (src/melee/gr/types.h);
     * item `map_id` holds the stage joint tree and the map's own
     * camera/lights/fog, exactly what Ground_GetStageGObj uses. */
    data = (HsdStageData*) HSD_ArchiveGetPublicAddress(&scene->archive,
                                                       "map_head");
    if (data == NULL || data->maps == NULL || map_id < 0 ||
        map_id >= data->map_count) {
        snprintf(error, error_size, "map_head/map %d missing", map_id);
        goto fail;
    }
    map = &data->maps[map_id];
    scene->stage_map = map;
    scene->stage_map_count = data->map_count;
    if (!scene_ptr_valid(scene, map->joint)) {
        snprintf(error, error_size, "stage map %d has no joint", map_id);
        goto fail;
    }
    scene->root = HSD_JObjLoadJoint(map->joint);
    if (scene->root == NULL) {
        snprintf(error, error_size, "stage HSD_JObjLoadJoint failed");
        goto fail;
    }
    HSD_JObjSetupMatrix(scene->root);
    scene->stage_roots[0] = scene->root;
    scene->stage_root_count = 1;
    if (scene_ptr_valid(scene, map->camera)) {
        scene->stage_cobj =
            lb_80013B14((HSD_CameraDescPerspective*) map->camera);
    }
    if (scene_ptr_valid(scene, map->lights)) {
        scene->stage_lobj = lb_80011AC4((LightList**) map->lights);
    }
    if (scene_ptr_valid(scene, map->fog)) {
        scene->stage_fog = HSD_FogLoadDesc((HSD_FogDesc*) map->fog);
    }
    return 1;

fail:
    free(scene->work);
    scene->work = NULL;
    return -1;
}

int hsd_scene_load_stage_all(HsdScene* scene, const char* disc,
                             const char* stage, int camera_map_id,
                             char* error, size_t error_size)
{
    HsdStageData* data;
    HsdStageMap* camera_map;
    int i;
    int opened = scene_open_archive(scene, disc, stage, error, error_size);

    if (opened <= 0) {
        return opened;
    }
    scene->stage_mode = 1;
    data = (HsdStageData*) HSD_ArchiveGetPublicAddress(&scene->archive,
                                                       "map_head");
    if (data == NULL || data->maps == NULL || camera_map_id < 0 ||
        camera_map_id >= data->map_count) {
        snprintf(error, error_size, "map_head/map %d missing", camera_map_id);
        goto fail;
    }
    scene->stage_map_count = data->map_count;
    for (i = 0; i < data->map_count; i++) {
        HSD_JObj* root;
        if (!scene_ptr_valid(scene, data->maps[i].joint) ||
            scene->stage_root_count >=
                (int) (sizeof(scene->stage_roots) /
                       sizeof(scene->stage_roots[0]))) {
            continue;
        }
        root = HSD_JObjLoadJoint(data->maps[i].joint);
        if (root == NULL) {
            continue;
        }
        HSD_JObjSetupMatrix(root);
        scene->stage_roots[scene->stage_root_count++] = root;
    }
    if (scene->stage_root_count == 0) {
        snprintf(error, error_size, "stage has no joints");
        goto fail;
    }
    scene->root = scene->stage_roots[0];
    camera_map = &data->maps[camera_map_id];
    scene->stage_map = camera_map;
    if (scene_ptr_valid(scene, camera_map->camera)) {
        scene->stage_cobj =
            lb_80013B14((HSD_CameraDescPerspective*) camera_map->camera);
    }
    if (scene_ptr_valid(scene, camera_map->lights)) {
        scene->stage_lobj = lb_80011AC4((LightList**) camera_map->lights);
    }
    if (scene_ptr_valid(scene, camera_map->fog)) {
        scene->stage_fog = HSD_FogLoadDesc((HSD_FogDesc*) camera_map->fog);
    }
    return 1;

fail:
    free(scene->work);
    scene->work = NULL;
    return -1;
}

void hsd_scene_free(HsdScene* scene)
{
    if (scene->stage_root_count > 0) {
        int i;
        for (i = 0; i < scene->stage_root_count; i++) {
            if (scene->stage_roots[i] != NULL) {
                HSD_JObjUnrefThis(scene->stage_roots[i]);
            }
            scene->stage_roots[i] = NULL;
        }
        scene->stage_root_count = 0;
        scene->root = NULL;
    } else if (scene->root != NULL) {
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

static void vis_clear_jobjs(HSD_JObj* jobj, int depth)
{
    HSD_DObj* d;
    if (jobj == NULL || depth > 256) {
        return;
    }
    if (!(jobj->flags & JOBJ_HIDDEN)) {
        for (d = jobj->u.dobj; d != NULL; d = d->next) {
            HSD_DObjClearFlags(d, DOBJ_HIDDEN);
        }
    }
    if (!(jobj->flags & JOBJ_INSTANCE)) {
        vis_clear_jobjs(jobj->child, depth + 1);
    }
    vis_clear_jobjs(jobj->next, depth);
}

void hsd_scene_clear_visibility(HsdScene* scene)
{
    if (scene != NULL) {
        vis_clear_jobjs(scene->root, 0);
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

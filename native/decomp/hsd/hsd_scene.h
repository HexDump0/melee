#ifndef MELEE_DECOMP_HSD_SCENE_H
#define MELEE_DECOMP_HSD_SCENE_H

/*
 * S2 compiled-HSD scene bootstrap.
 *
 * Loads a real HSD archive (PlMrNr.dat...) into the compiled decompilation:
 * converts the descriptor graph to host byte order while leaving byte-defined
 * GX data (display lists, vertex arrays, textures, FObj streams) untouched,
 * runs HSD_ArchiveParse + HSD_JObjLoadJoint, and exposes the root JObj.
 *
 * This is the minimal bridge S2 needs; the full per-format asset pipeline is
 * S3 (see learnings/decomp_assets.md).
 */
#include <stddef.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/forward.h>

typedef struct HsdScene {
    unsigned char* work; /* converted archive image (owned) */
    size_t work_size;
    HSD_Archive archive;
    HSD_JObj* root;
    char model[64];
    /* Stage mode (Gr*.dat `map_head`): the map's own camera/lights/fog. */
    int stage_mode;
    void* stage_map;      /* HsdStageMap* into `work` */
    int stage_map_count;  /* maps available in the archive */
    HSD_CObj* stage_cobj; /* lb_80013B14(map->camera) */
    HSD_LObj* stage_lobj; /* lb_80011AC4(map->lights) */
    HSD_Fog* stage_fog;   /* HSD_FogLoadDesc(map->fog) */
    /* Stage geometry roots (map 0..N, one Ground GObj per map in the game).
     * `root` aliases index 0 when stage_mode. */
    HSD_JObj* stage_roots[64];
    int stage_root_count;
} HsdScene;

/* Gr*.dat `map_head` map entry (src/melee/gr/types.h UnkStageDat_x8_t).
 * Pointers are relocated by the platform converter, so this struct is read
 * directly from the converted archive image. */
typedef struct HsdStageMap {
    HSD_Joint* joint;      /* +0x00 */
    void* anims;           /* +0x04 HSD_AnimJoint** */
    void* matanims;        /* +0x08 */
    void* shapeanims;      /* +0x0C */
    void* camera;          /* +0x10 HSD_CameraDescPerspective* */
    void* x14;             /* +0x14 */
    void* lights;          /* +0x18 LightList** */
    void* fog;             /* +0x1C HSD_FogDesc* */
    void* grjoints;        /* +0x20 */
    int grjoint_count;     /* +0x24 */
    void* x28;             /* +0x28 */
    void* x2C;             /* +0x2C */
    int x30;               /* +0x30 */
} HsdStageMap;

/* UnkStageDat header; `maps`/`map_count` are the x8 array. */
typedef struct HsdStageData {
    void* unk0;
    int unk4;
    HsdStageMap* maps;
    int map_count;
    void* x10;
    int x14;
    void* x18;
    int x1C;
    void* x20;
    int x24;
    void* x28;
    int x2C;
} HsdStageData;

int hsd_scene_boot(void);

/* Returns 1 on success, 0 when the disc image/asset is unavailable (caller
 * SKIPs), -1 on a conversion/parse failure. */
int hsd_scene_load(HsdScene* scene, const char* disc, const char* model,
                   char* error, size_t error_size);

/* Loads a stage archive (Gr*.dat) map `map_id` (0 = the stage's default
 * background) through the same converter/parser path.  The geometry is
 * `map->joint`; the map's own camera/lights/fog are loaded too when present. */
int hsd_scene_load_stage(HsdScene* scene, const char* disc, const char* stage,
                         int map_id, char* error, size_t error_size);

/* Loads every `map_head` joint into one sibling chain (a Melee stage is one
 * Ground GObj per map id, all drawn together: platform + background layers).
 * `camera_map_id` selects which map supplies the camera/lights/fog. */
int hsd_scene_load_stage_all(HsdScene* scene, const char* disc,
                             const char* stage, int camera_map_id,
                             char* error, size_t error_size);
void hsd_scene_free(HsdScene* scene);

/*
 * Applies the character's ftData part-visibility tables to the loaded JObj
 * tree, exactly like the game's ftParts_800749CC/ftParts_80074A4C: mark all
 * variants hidden, then show `slot`/`variant`.  Compiled equivalent of
 * native/hsd/parts.c's parts_apply using DOBJ_HIDDEN.
 * Returns the number of hidden DObjs, or -1 when the table is unavailable.
 */
int hsd_scene_apply_visibility(HsdScene* scene, const char* disc,
                               const char* model, int slot, int variant,
                               char* error, size_t error_size);

/* Clears DOBJ_HIDDEN on every DObj in display order (the viewer's
 * show-hidden toggle); hsd_scene_apply_visibility re-hides after this. */
void hsd_scene_clear_visibility(HsdScene* scene);

#endif

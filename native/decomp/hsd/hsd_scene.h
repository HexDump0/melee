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
} HsdScene;

int hsd_scene_boot(void);

/* Returns 1 on success, 0 when the disc image/asset is unavailable (caller
 * SKIPs), -1 on a conversion/parse failure. */
int hsd_scene_load(HsdScene* scene, const char* disc, const char* model,
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

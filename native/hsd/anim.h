#ifndef MELEE_NATIVE_DEMO_ANIM_H
#define MELEE_NATIVE_DEMO_ANIM_H

/*
 * Melee fighter animation: the FigaTree clip table from `Pl<Char>AJ.dat` and
 * its binding to the HSD joint tree, following lbAnim_* (src/melee/lb/lbanim.c)
 * and the ftParts traversal rules (src/melee/ft/ftparts.c, ftanim.c).
 *
 * The `ftPartsTable` and joint-skip tables come from `PlCo.dat`'s
 * `ftLoadCommonData` symbol, exactly like Fighter_LoadCommonData.
 */
#include <stddef.h>
#include <stdint.h>

#include "hsd/aobj.h"
#include "hsd/model.h"

#define DEMO_MAX_CLIPS 512
#define DEMO_CLIP_NAME 64

typedef struct DemoClipInfo {
    char name[DEMO_CLIP_NAME];     /* display name, e.g. "Wait1" */
    char symbol[DEMO_CLIP_NAME * 2]; /* full archive symbol */
    size_t container_offset;       /* file offset of the sub-archive header */
    size_t figa_offset;            /* file offset of the HSD_FigaTree */
} DemoClipInfo;

typedef struct DemoAnim {
    uint8_t *data; /* owned copy of the animation archive */
    size_t size;
    DemoClipInfo clips[DEMO_MAX_CLIPS];
    size_t clip_count;

    int kind;      /* FighterKind, from the model archive name */
    int parts_num[64];
    uint8_t skip_count[64];
    uint8_t skip_ids[64][64];

    int active;
    float frames;
    uint32_t flags;
    uint16_t joint_first[DEMO_MAX_JOINTS];
    uint16_t joint_tracks[DEMO_MAX_JOINTS];
    DemoFobj *fobjs;
    size_t fobj_count;
} DemoAnim;

/*
 * Loads the animation archive for `model_file` (e.g. "PlMrNr.dat" ->
 * "PlMrAJ.dat"; pass anim_file to override) and the ftParts tables from
 * PlCo.dat.  Returns 0 on success.
 */
int demo_anim_load(DemoAnim *anim, const char *disc, const char *model_file,
                   const char *anim_file, char *err, size_t errn);

void demo_anim_free(DemoAnim *anim);

size_t demo_anim_clip_count(const DemoAnim *anim);
const char *demo_anim_clip_name(const DemoAnim *anim, size_t index);
float demo_anim_clip_frames(const DemoAnim *anim, size_t index);

/* Case-insensitive name match; accepts a numeric index as "3". -1 if absent. */
int demo_anim_clip_find(const DemoAnim *anim, const char *name);

/* Parses the clip's tracks and binds them to `model`'s joints. */
int demo_anim_set_clip(DemoAnim *anim, size_t index, DemoModel *model,
                       char *err, size_t errn);

/* Length of the active clip in frames. */
float demo_anim_end_frame(const DemoAnim *anim);

/* Evaluates the active clip at `frame` and re-skins the model. */
void demo_anim_apply(DemoAnim *anim, DemoModel *model, float frame);

#endif

#ifndef MELEE_NATIVE_HSD_ANIM_H
#define MELEE_NATIVE_HSD_ANIM_H

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

#define HSD_MAX_CLIPS 512
#define CLIP_NAME_MAX 64

typedef struct ClipInfo {
    char name[CLIP_NAME_MAX];     /* display name, e.g. "Wait1" */
    char symbol[CLIP_NAME_MAX * 2]; /* full archive symbol */
    size_t container_offset;       /* file offset of the sub-archive header */
    size_t figa_offset;            /* file offset of the HSD_FigaTree */
} ClipInfo;

typedef struct Anim {
    uint8_t *data; /* owned copy of the animation archive */
    size_t size;
    ClipInfo clips[HSD_MAX_CLIPS];
    size_t clip_count;

    int kind;      /* FighterKind, from the model archive name */
    int parts_num[64];
    uint8_t skip_count[64];
    uint8_t skip_ids[64][64];

    int active;
    float frames;
    uint32_t flags;
    uint16_t joint_first[HSD_MAX_JOINTS];
    uint16_t joint_tracks[HSD_MAX_JOINTS];
    Fobj *fobjs;
    size_t fobj_count;
} Anim;

/*
 * Loads the animation archive for `model_file` (e.g. "PlMrNr.dat" ->
 * "PlMrAJ.dat"; pass anim_file to override) and the ftParts tables from
 * PlCo.dat.  Returns 0 on success.
 */
int anim_load(Anim *anim, const char *disc, const char *model_file,
                   const char *anim_file, char *err, size_t errn);

void anim_free(Anim *anim);

size_t anim_clip_count(const Anim *anim);
const char *anim_clip_name(const Anim *anim, size_t index);
float anim_clip_frames(const Anim *anim, size_t index);

/* Case-insensitive name match; accepts a numeric index as "3". -1 if absent. */
int anim_clip_find(const Anim *anim, const char *name);

/* Parses the clip's tracks and binds them to `model`'s joints. */
int anim_set_clip(Anim *anim, size_t index, HsdModel *model,
                       char *err, size_t errn);

/* Length of the active clip in frames. */
float anim_end_frame(const Anim *anim);

/* Evaluates the active clip at `frame` and re-skins the model. */
void anim_apply(Anim *anim, HsdModel *model, float frame);

#endif

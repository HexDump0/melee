#ifndef MELEE_NATIVE_EXTRAS_VIEWER_H
#define MELEE_NATIVE_EXTRAS_VIEWER_H

/* Port-only interactive model viewer (orbit, parts, animation playback). */
#include <SDL.h>
#include "gx/render.h"
#include "platform/disc.h"

typedef struct ViewerOptions {
    float angle;
    float elevation;
    float zoom;
    int animate;
    int show_hidden;
    int no_grid;
    int force_no_cull;
    int frames;
    const char *capture;
    const char *clip;
    const char *anim_file;
    float anim_frame;
    float anim_speed;
    int part;
    int part_mode;
    int vis_slot;
    int vis_variant;
} ViewerOptions;

/* `visuals` points at the already-loaded/compiled character and `models` at
 * the disc model list used for cycling.  Returns 0. */
int viewer_run(SDL_Window *window, const char *disc, const char *model_file,
               DiscFileList *models, Visual *visuals,
               const ViewerOptions *opts);

#endif

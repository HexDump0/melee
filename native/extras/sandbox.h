#ifndef MELEE_NATIVE_EXTRAS_SANDBOX_H
#define MELEE_NATIVE_EXTRAS_SANDBOX_H

/* Port-only movement sandbox: two placeholder fighters, one stage slab.
 * This is the temporary harness, not the real game loop. */
#include <SDL.h>
#include "gx/render.h"

typedef struct SandboxOptions {
    int frames;
    int scripted;
    int show_hidden;
    int no_visibility;
    int no_controller;
    int vis_slot;
    int vis_variant;
    const char *capture;
    const char *anim_file;
} SandboxOptions;

/* `visuals[0]` is the already-loaded character; `visuals[1]` is decoded here
 * so both fighters can pose independently.  Returns 0. */
int sandbox_run(SDL_Window *window, const char *disc, const char *model_file,
                Visual *visuals, const SandboxOptions *opts);

#endif

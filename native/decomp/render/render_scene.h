#ifndef MELEE_DECOMP_RENDER_SCENE_H
#define MELEE_DECOMP_RENDER_SCENE_H

/*
 * Shared setup for the compiled-HSD render targets (P-611).
 *
 * Owns the asset bridge + bootstrap, the model list, model scaling, ftData
 * visibility, the prototype-viewer camera framing and the compiled HSD_LObj
 * light chain, so the headless test (tests/test_decomp_render.c) and the SDL3
 * viewer (decomp/render/viewer_main.c) drive the same code.
 */
#include <stddef.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/forward.h>

#include "decomp/hsd/hsd_scene.h"
#include "hsd/light.h"
#include "platform/disc.h"

#define RENDER_SCENE_DISC_DEFAULT \
    "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"
#define RENDER_SCENE_MODEL_DEFAULT "PlMrNr.dat"
#define RENDER_SCENE_MAX_PATH 512

typedef struct RenderSceneOptions {
    const char* disc;
    const char* model;      /* NULL = first in the disc list */
    int width, height;      /* camera aspect + scissor */
    float angle;            /* yaw degrees, prototype default 25 */
    float elevation;        /* pitch degrees, prototype default -12 */
    float zoom;             /* distance multiplier, 1.0 default */
    float scale_override;   /* > 0 overrides ftData.model_scaling */
    int no_scale;
    int no_lights;
} RenderSceneOptions;

typedef struct RenderScene {
    HsdScene hsd;
    char disc[RENDER_SCENE_MAX_PATH];
    char model[64];
    DiscFileList models;
    int model_index;
    float model_scale;
    int hidden_dobjs;
    float bounds_min[3];
    float bounds_max[3];
    float angle, elevation, zoom;
    int vis_slot;
    int vis_variant;
    int show_hidden;
    int need_view_update;
    int width, height;
    HSD_CObj* cobj;
    SceneLights lights;
    int have_lights;
    HSD_LObj* lobj;
} RenderScene;

/* One-time HSD bootstrap (arena, pools, ID table, class info). */
int render_scene_boot(void);

/* Opens the model (and its disc list).  Returns 1 on success, 0 when the
 * disc/asset is unavailable (caller SKIPs), -1 on failure. */
int render_scene_open(RenderScene* scene, const RenderSceneOptions* options,
                      char* error, size_t error_size);

/* Switches to the next/previous Pl*Nr.dat in the disc list (dir = +1/-1),
 * skipping models that fail to load.  Keeps angle/elevation/zoom/slot. */
int render_scene_cycle(RenderScene* scene, int dir, char* error,
                       size_t error_size);

void render_scene_close(RenderScene* scene);

/* Recomputes the camera from the cached bounds + angle/elevation/zoom. */
void render_scene_update_view(RenderScene* scene);

/* Re-applies ftData visibility (slot/variant, show_hidden) and reframes. */
void render_scene_update_visibility(RenderScene* scene);

/* Runs the compiled display path for the current camera.  The caller then
 * consumes the frame with gx_hle_get_frame()/gx_gl_render_frame(). */
void render_scene_draw(RenderScene* scene);

#endif

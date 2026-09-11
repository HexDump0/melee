#ifndef MELEE_NATIVE_GX_RENDER_H
#define MELEE_NATIVE_GX_RENDER_H

/*
 * The port's OpenGL renderer: shaders, per-batch buffers and the GX material
 * state.  Owns one `Visual` per rendered character (model + animation + GL
 * objects).  Extras (viewer, sandbox) drive it through this interface.
 */
#include "gx/gl.h"
#include "gx/math.h"
#include "hsd/model.h"
#include "hsd/anim.h"
#include "hsd/light.h"

typedef struct Visual {
    HsdModel model;
    Anim anim;
    int anim_loaded;
    int shared; /* model/GL resources are owned by another Visual */
    GLuint batch_vao[HSD_MAX_BATCHES]; /* immutable bind-pose geometry */
    GLuint batch_vbo[HSD_MAX_BATCHES];
    GLuint pose_vao[HSD_MAX_BATCHES];  /* per-frame animated geometry */
    GLuint pose_vbo[HSD_MAX_BATCHES];
    GLuint textures[HSD_MAX_TEXTURES];
    float scale;
    const char *label;
} Visual;

int renderer_init(void);

/* Scene lights/fog for the next draw; NULL disables them (stand-in light). */
void render_set_lights(const SceneLights *lights);

/* Builds the GX channel inputs from the loaded HSD_LObj set and transforms
 * them into the light matrix's space. */
void render_set_view(const Mat4 mvp, const Mat4 mv, const Mat4 light_mv,
                     int lighting);

/* Restores the default blend/depth state overlays expect. */
void render_reset_state(void);

int visual_load_model(Visual *v, const char *disc, const char *file,
                      int vis_slot, int vis_variant);
int visual_load_anim(Visual *v, const char *disc, const char *file,
                     const char *anim_file);
void visual_compile(Visual *v);
void render_apply_cull(int cull);
void render_batch(const Visual *v, size_t batch, int pose, int textured);
void visual_destroy(Visual *v);

#endif

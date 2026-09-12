/*
 * Shared compiled-HSD render scene: see render_scene.h.
 *
 * The camera framing and light setup mirror the prototype viewer
 * (native/extras/viewer.c + native/hsd/light.c) so screenshots stay
 * comparable; the display pass is the compiled decompilation's own
 * HSD_StartRender/HSD_CObjSetCurrent/HSD_LObjSetupInit/HSD_JObjDispAll.
 */
#include "decomp/render/render_scene.h"

#include <dolphin/gx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/wobj.h>

#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"

int render_scene_boot(void)
{
    return hsd_scene_boot();
}

/* PlMrNr.dat -> PlMr.dat, then ftData + 0 -> attrs, +0x8C model_scaling.
 * Offsets from src/melee/ft/types.h and native/hsd/parts.c. */
static float read_model_scaling(const char* disc, const char* model)
{
    char ft_name[32];
    DiscFile asset;
    char error[256];
    unsigned int ft = 0;
    float scale = 1.0f;

    if (strlen(model) < 6 || strncmp(model, "Pl", 2) != 0) {
        return 1.0f;
    }
    snprintf(ft_name, sizeof(ft_name), "%.4s.dat", model);
    if (disc_load(disc, ft_name, &asset, error, sizeof(error)) != DISC_OK) {
        return 1.0f;
    }
    {
        const unsigned char* d = (const unsigned char*) asset.data;
        size_t n = asset.size;
        unsigned int data_size = ((unsigned int) d[4] << 24) |
                                 ((unsigned int) d[5] << 16) |
                                 ((unsigned int) d[6] << 8) | d[7];
        unsigned int nb_reloc = ((unsigned int) d[8] << 24) |
                                ((unsigned int) d[9] << 16) |
                                ((unsigned int) d[10] << 8) | d[11];
        unsigned int nb_public = ((unsigned int) d[12] << 24) |
                                 ((unsigned int) d[13] << 16) |
                                 ((unsigned int) d[14] << 8) | d[15];
        size_t public_off = 0x20 + data_size + (size_t) nb_reloc * 4;
        size_t i;
        for (i = 0; i < nb_public && ft == 0; ++i) {
            unsigned int data_off = ((unsigned int) d[public_off + i * 8] << 24) |
                                    ((unsigned int) d[public_off + i * 8 + 1] << 16) |
                                    ((unsigned int) d[public_off + i * 8 + 2] << 8) |
                                    d[public_off + i * 8 + 3];
            unsigned int name_off = ((unsigned int) d[public_off + i * 8 + 4] << 24) |
                                    ((unsigned int) d[public_off + i * 8 + 5] << 16) |
                                    ((unsigned int) d[public_off + i * 8 + 6] << 8) |
                                    d[public_off + i * 8 + 7];
            unsigned int nb_extern = ((unsigned int) d[16] << 24) |
                                     ((unsigned int) d[17] << 16) |
                                     ((unsigned int) d[18] << 8) | d[19];
            size_t symbols_off = public_off + (size_t) nb_public * 8 +
                                 (size_t) nb_extern * 8;
            if (symbols_off + name_off < n &&
                strncmp((const char*) d + symbols_off + name_off, "ftData",
                        6) == 0) {
                ft = data_off;
            }
        }
        if (ft != 0 && 0x20 + ft + 4 <= n) {
            unsigned int attrs = ((unsigned int) d[0x20 + ft] << 24) |
                                 ((unsigned int) d[0x20 + ft + 1] << 16) |
                                 ((unsigned int) d[0x20 + ft + 2] << 8) |
                                 d[0x20 + ft + 3];
            if (0x20 + attrs + 0x90 <= n) {
                unsigned int bits = ((unsigned int) d[0x20 + attrs + 0x8C] << 24) |
                                    ((unsigned int) d[0x20 + attrs + 0x8D] << 16) |
                                    ((unsigned int) d[0x20 + attrs + 0x8E] << 8) |
                                    d[0x20 + attrs + 0x8F];
                memcpy(&scale, &bits, sizeof(scale));
                if (!(scale > 0.02f && scale < 8.0f) || !isfinite(scale)) {
                    scale = 1.0f;
                }
            }
        }
    }
    disc_free(&asset);
    return scale;
}

/* native/extras/viewer.c: viewer_frame_bounds + render_viewer. */
static void frame_camera(const RenderScene* scene, float* eye, float* target,
                         float* znear, float* zfar)
{
    const float* mn = scene->bounds_min;
    const float* mx = scene->bounds_max;
    float size[3];
    float radius;
    float dist;
    float cp;
    float sp;
    float yaw = scene->angle * (float) M_PI / 180.0f;
    float pitch = -scene->elevation * (float) M_PI / 180.0f;
    int i;

    /* In stage mode frame the posed fighter so it reads at a useful size;
     * the stage stays visible around it.  Without one, frame the main map
     * (the camera/lights source), not the whole background union. */
    if (scene->stage_mode && scene->have_fighter && scene->show_fighter) {
        mn = scene->fighter_bounds_min;
        mx = scene->fighter_bounds_max;
    } else if (scene->stage_mode && scene->have_stage_main) {
        mn = scene->stage_main_min;
        mx = scene->stage_main_max;
    }
    for (i = 0; i < 3; ++i) {
        size[i] = mx[i] - mn[i];
        target[i] = (mn[i] + mx[i]) * 0.5f;
    }
    radius = 0.5f * sqrtf(size[0] * size[0] + size[1] * size[1] +
                          size[2] * size[2]);
    if (radius < 1e-3f) {
        radius = 1.0f;
    }
    dist = radius / tanf(0.35f) * 1.15f * scene->zoom;
    *zfar = dist * 8.0f + 100.0f;
    *znear = *zfar / 1200.0f;
    if (*znear < 0.05f) {
        *znear = 0.05f;
    }
    cp = cosf(pitch);
    sp = sinf(pitch);
    eye[0] = target[0] + dist * cp * sinf(yaw);
    eye[1] = target[1] + dist * sp;
    eye[2] = target[2] + dist * cp * cosf(yaw);
}

static HSD_LObj* build_lights(const SceneLights* set)
{
    static HSD_LightDesc descs[MAX_LOBS];
    static HSD_WObjDesc positions[MAX_LOBS];
    size_t i;

    memset(descs, 0, sizeof(descs));
    memset(positions, 0, sizeof(positions));
    for (i = 0; i < set->count && i < MAX_LOBS; ++i) {
        const SceneLight* l = &set->lights[i];
        descs[i].class_name = NULL;
        descs[i].next = (i + 1 < set->count) ? &descs[i + 1] : NULL;
        descs[i].flags = l->flags;
        descs[i].color.r = l->color[0];
        descs[i].color.g = l->color[1];
        descs[i].color.b = l->color[2];
        descs[i].color.a = l->color[3];
        positions[i].pos.x = l->position[0];
        positions[i].pos.y = l->position[1];
        positions[i].pos.z = l->position[2];
        descs[i].position = &positions[i];
        descs[i].u.shininess = NULL;
    }
    return HSD_LObjLoadDesc(descs);
}

/* Identity-projection pass: the GX position matrices are the world matrices,
 * so the captured view positions are world-space bounds.  Runs one root at a
 * time so the caller can attribute the vertices (stage vs fighter). */
static int bounds_pass(HSD_JObj* root, float* mn, float* mx)
{
    static const float identity[4][4] = { { 1, 0, 0, 0 },
                                          { 0, 1, 0, 0 },
                                          { 0, 0, 1, 0 },
                                          { 0, 0, 0, 1 } };
    const GxHleVertex* vertices = NULL;
    size_t vertex_count = 0;
    size_t i;

    mn[0] = mn[1] = mn[2] = 1e30f;
    mx[0] = mx[1] = mx[2] = -1e30f;
    gx_hle_begin_frame();
    /* The backend state was just reset; the compiled engine caches GX state
     * (channel registers, TEV stages, vtx descs) and would otherwise skip
     * re-emitting it for the next model, leaving the raster black. */
    HSD_StateInvalidate(-1);
    GXSetProjection((f32 (*)[4]) identity, GX_PERSPECTIVE);
    HSD_JObjDispAll(root, (f32 (*)[4]) identity, HSD_TRSP_ALL, 0);
    gx_hle_get_frame(&vertices, &vertex_count, NULL, NULL, NULL, NULL);
    for (i = 0; i < vertex_count; ++i) {
        int k;
        for (k = 0; k < 3; ++k) {
            if (vertices[i].view[k] < mn[k]) {
                mn[k] = vertices[i].view[k];
            }
            if (vertices[i].view[k] > mx[k]) {
                mx[k] = vertices[i].view[k];
            }
        }
    }
    return mx[0] >= mn[0];
}

static int compute_bounds(RenderScene* scene)
{
    int have = bounds_pass(scene->hsd.root, scene->bounds_min,
                           scene->bounds_max);
    int i;

    /* Stage mode: every map root is geometry (platform + background layers). */
    for (i = 1; i < scene->hsd.stage_root_count; i++) {
        float mn[3];
        float mx[3];
        int k;
        if (!bounds_pass(scene->hsd.stage_roots[i], mn, mx)) {
            continue;
        }
        have = 1;
        for (k = 0; k < 3; ++k) {
            if (mn[k] < scene->bounds_min[k]) {
                scene->bounds_min[k] = mn[k];
            }
            if (mx[k] > scene->bounds_max[k]) {
                scene->bounds_max[k] = mx[k];
            }
        }
    }

    scene->fighter_bounds_min[0] = scene->fighter_bounds_min[1] =
        scene->fighter_bounds_min[2] = 1e30f;
    scene->fighter_bounds_max[0] = scene->fighter_bounds_max[1] =
        scene->fighter_bounds_max[2] = -1e30f;
    if (scene->fighter.root != NULL && scene->stage_mode) {
        int k;
        if (bounds_pass(scene->fighter.root, scene->fighter_bounds_min,
                        scene->fighter_bounds_max)) {
            have = 1;
            /* Keep the combined bounds so the orbit camera never clips the
             * stage while the fighter is hidden. */
            for (k = 0; k < 3; ++k) {
                if (scene->fighter_bounds_min[k] < scene->bounds_min[k]) {
                    scene->bounds_min[k] = scene->fighter_bounds_min[k];
                }
                if (scene->fighter_bounds_max[k] > scene->bounds_max[k]) {
                    scene->bounds_max[k] = scene->fighter_bounds_max[k];
                }
            }
        }
    }
    return have;
}

static void scene_unload_stage(RenderScene* scene)
{
    /* HSD keeps a global "current lights" list; the previous frame's draw
     * may have installed this stage's (or the model's) light objects.
     * Clear it before freeing anything, or the next HSD_JObjDispAll (e.g.
     * compute_bounds) dereferences freed lights (HSD_LObjSetupSpecularInit). */
    HSD_LObjSetCurrentAll(NULL);
    if (scene->hsd.stage_cobj != NULL) {
        hsdDelete((HSD_Class*) scene->hsd.stage_cobj);
        scene->hsd.stage_cobj = NULL;
    }
    if (scene->hsd.stage_lobj != NULL) {
        HSD_LObjRemoveAll(scene->hsd.stage_lobj);
        scene->hsd.stage_lobj = NULL;
    }
    scene->hsd.stage_fog = NULL;
    if (scene->hsd.root != NULL) {
        gx_hle_reset_assets();
        gx_gl_clear_textures();
        hsd_scene_free(&scene->hsd);
    }
}

static void scene_unload_roots(RenderScene* scene)
{
    scene_unload_stage(scene);
    if (scene->fighter.root != NULL) {
        gx_hle_reset_assets();
        gx_gl_clear_textures();
        hsd_scene_free(&scene->fighter);
    }
}

/* The GX HLE asset registry is global; after a reset re-register every live
 * archive image so texture/decode lookups still resolve. */
static void scene_register_assets(RenderScene* scene)
{
    if (scene->hsd.work != NULL) {
        gx_hle_register_asset(scene->hsd.work, scene->hsd.work_size);
    }
    if (scene->fighter.work != NULL) {
        gx_hle_register_asset(scene->fighter.work, scene->fighter.work_size);
    }
}

static int scene_load_model(RenderScene* scene, const RenderSceneOptions* opt,
                            const char* model, char* error,
                            size_t error_size)
{
    int loaded;

    scene_unload_roots(scene);
    loaded = hsd_scene_load(&scene->hsd, scene->disc, model, error,
                            error_size);
    if (loaded <= 0) {
        return loaded;
    }
    snprintf(scene->model, sizeof(scene->model), "%s", model);
    gx_hle_register_asset(scene->hsd.work, scene->hsd.work_size);
    scene->have_stage_main = 0;

    scene->model_scale = opt->scale_override > 0.0f
                             ? opt->scale_override
                             : read_model_scaling(scene->disc, model);
    if (opt->no_scale) {
        scene->model_scale = 1.0f;
    }
    {
        Vec3 scale;
        scale.x = scale.y = scale.z = scene->model_scale;
        HSD_JObjSetScale(scene->hsd.root, &scale);
    }
    HSD_JObjSetupMatrix(scene->hsd.root);

    scene->hidden_dobjs =
        hsd_scene_apply_visibility(&scene->hsd, scene->disc, model,
                                   scene->vis_slot, scene->vis_variant,
                                   error, error_size);

    if (!compute_bounds(scene)) {
        snprintf(error, error_size, "no geometry captured");
        return -1;
    }
    scene->need_view_update = 1;
    return 1;
}

/* Loads the posed fighter for stage mode: the same compiled HSD path with the
 * character's ftData scale and neutral-pose visibility, placed at the origin
 * (Melee model origins are at the feet). */
static int scene_load_fighter(RenderScene* scene, const RenderSceneOptions* opt,
                              const char* model, char* error,
                              size_t error_size)
{
    int loaded;
    float scale;

    if (scene->fighter.root != NULL) {
        gx_hle_reset_assets();
        gx_gl_clear_textures();
        hsd_scene_free(&scene->fighter);
    }
    loaded = hsd_scene_load(&scene->fighter, scene->disc, model, error,
                            error_size);
    if (loaded <= 0) {
        return loaded;
    }
    snprintf(scene->fighter_name, sizeof(scene->fighter_name), "%s", model);
    gx_hle_register_asset(scene->fighter.work, scene->fighter.work_size);
    scale = opt->scale_override > 0.0f ? opt->scale_override
                                       : read_model_scaling(scene->disc, model);
    if (opt->no_scale) {
        scale = 1.0f;
    }
    scene->fighter_scale = scale;
    {
        Vec3 s;
        Vec3 t = { 0.0f, 0.0f, 0.0f };
        s.x = s.y = s.z = scale;
        HSD_JObjSetScale(scene->fighter.root, &s);
        HSD_JObjSetTranslate(scene->fighter.root, &t);
    }
    HSD_JObjSetupMatrix(scene->fighter.root);
    scene->fighter_hidden_dobjs = hsd_scene_apply_visibility(
        &scene->fighter, scene->disc, model, scene->vis_slot,
        scene->vis_variant, error, error_size);
    if (!compute_bounds(scene)) {
        snprintf(error, error_size, "fighter geometry missing");
        return -1;
    }
    return 1;
}

static int scene_load_stage(RenderScene* scene, const RenderSceneOptions* opt,
                            const char* stage, char* error,
                            size_t error_size)
{
    int isolate = scene->stage_map;
    int loaded;

    if (isolate >= 0) {
        /* Explicit --stage-map / map cycling: one layer only. */
        scene_unload_stage(scene);
        loaded = hsd_scene_load_stage(&scene->hsd, scene->disc, stage,
                                      isolate, error, error_size);
        if (loaded <= 0) {
            return loaded;
        }
        scene->stage_camera_map = isolate;
        scene->have_stage_main = bounds_pass(scene->hsd.root,
                                             scene->stage_main_min,
                                             scene->stage_main_max);
        compute_bounds(scene);
    } else {
        /* All maps (the game draws one Ground GObj per map id).  Pick the
         * drawing map with the smallest extent as the camera/lights/fog
         * source: background layers are far larger than the playable map. */
        int map;
        int count = 0;
        int best = -1;
        float best_extent = 0.0f;
        float best_min[3] = { 0.0f, 0.0f, 0.0f };
        float best_max[3] = { 0.0f, 0.0f, 0.0f };

        scene_unload_stage(scene);
        loaded = hsd_scene_load_stage(&scene->hsd, scene->disc, stage, 0,
                                      error, error_size);
        if (loaded <= 0) {
            return loaded;
        }
        count = scene->hsd.stage_map_count;
        if (count <= 0) {
            count = 1;
        }
        for (map = 0; map < count; map++) {
            float mn[3];
            float mx[3];
            if (map != 0) {
                scene_unload_stage(scene);
                loaded = hsd_scene_load_stage(&scene->hsd, scene->disc, stage,
                                              map, error, error_size);
                if (loaded < 0) {
                    continue; /* placeholder map with no usable joint */
                }
                if (loaded == 0) {
                    return 0;
                }
            }
            if (bounds_pass(scene->hsd.root, mn, mx)) {
                float dx = mx[0] - mn[0];
                float dy = mx[1] - mn[1];
                float dz = mx[2] - mn[2];
                float extent = dx * dx + dy * dy + dz * dz;
                if (best < 0 || extent < best_extent) {
                    int k;
                    best = map;
                    best_extent = extent;
                    for (k = 0; k < 3; k++) {
                        best_min[k] = mn[k];
                        best_max[k] = mx[k];
                    }
                }
            }
        }
        if (best < 0) {
            snprintf(error, error_size, "stage %s has no geometry", stage);
            scene_unload_stage(scene);
            return -1;
        }
        scene_unload_stage(scene);
        loaded = hsd_scene_load_stage_all(&scene->hsd, scene->disc, stage,
                                          best, error, error_size);
        if (loaded <= 0) {
            return loaded;
        }
        scene->stage_camera_map = best;
        {
            int k;
            for (k = 0; k < 3; k++) {
                scene->stage_main_min[k] = best_min[k];
                scene->stage_main_max[k] = best_max[k];
            }
        }
        scene->have_stage_main = 1;
        compute_bounds(scene);
    }
    snprintf(scene->stage, sizeof(scene->stage), "%s", stage);
    scene->stage_map = isolate;
    scene_register_assets(scene);
    scene->stage_mode = 1;
    scene->model_scale = 1.0f;
    scene->hidden_dobjs = 0;
    scene->need_view_update = 1;
    (void) opt;
    return 1;
}

void render_scene_update_view(RenderScene* scene)
{
    float eye[3];
    float target[3];
    float znear;
    float zfar;

    if (scene->cobj == NULL || scene->width <= 0 || scene->height <= 0) {
        return;
    }
    frame_camera(scene, eye, target, &znear, &zfar);
    HSD_CObjSetEyePosition(scene->cobj, (Vec3*) eye);
    HSD_CObjSetInterest(scene->cobj, (Vec3*) target);
    {
        Vec3 up = { 0.0f, 1.0f, 0.0f };
        HSD_CObjSetUpVector(scene->cobj, &up);
    }
    HSD_CObjSetNear(scene->cobj, znear);
    HSD_CObjSetFar(scene->cobj, zfar);
    HSD_CObjSetPerspective(scene->cobj, 0.7f * 180.0f / (float) M_PI,
                           (float) scene->width / (float) scene->height);
    {
        HSD_RectS16 vp = { 0, (s16) scene->width, 0, (s16) scene->height };
        HSD_CObjSetViewport(scene->cobj, &vp);
        HSD_CObjSetScissorx4(scene->cobj, 0, (u16) scene->width, 0,
                             (u16) scene->height);
    }
    HSD_CObjSetMtxDirty(scene->cobj);
    /* The stage's own camera keeps its desc eye/interest, but its viewport and
     * scissor come from the desc's 640x480 authoring size; map them to the
     * render target so the viewer does not crop. */
    if (scene->hsd.stage_cobj != NULL) {
        HSD_RectS16 vp = { 0, (s16) scene->width, 0, (s16) scene->height };
        HSD_CObjSetViewport(scene->hsd.stage_cobj, &vp);
        HSD_CObjSetScissorx4(scene->hsd.stage_cobj, 0, (u16) scene->width, 0,
                             (u16) scene->height);
    }
    scene->need_view_update = 0;
}

void render_scene_update_visibility(RenderScene* scene)
{
    char error[128];
    HsdScene* target = scene->stage_mode ? &scene->fighter : &scene->hsd;
    const char* name =
        scene->stage_mode ? scene->fighter_name : scene->model;

    if (target->root == NULL) {
        return;
    }
    hsd_scene_clear_visibility(target);
    if (!scene->show_hidden) {
        int hidden = hsd_scene_apply_visibility(
            target, scene->disc, name, scene->vis_slot, scene->vis_variant,
            error, sizeof(error));
        if (scene->stage_mode) {
            scene->fighter_hidden_dobjs = hidden;
        } else {
            scene->hidden_dobjs = hidden;
        }
    } else if (scene->stage_mode) {
        scene->fighter_hidden_dobjs = 0;
    } else {
        scene->hidden_dobjs = 0;
    }
    if (compute_bounds(scene)) {
        scene->need_view_update = 1;
    }
}

void render_scene_draw(RenderScene* scene)
{
    HSD_CObj* active;
    HSD_LObj* lights;

    if (scene->need_view_update) {
        render_scene_update_view(scene);
    }
    active = (scene->stage_mode && scene->stage_camera &&
              scene->hsd.stage_cobj != NULL)
                 ? scene->hsd.stage_cobj
                 : scene->cobj;
    gx_hle_discard_geometry();
    HSD_StartRender(HSD_RP_SCREEN);
    if (!HSD_CObjSetCurrent(active)) {
        return;
    }
    /* Stage mode uses the map's own fog and light list (Ground_801C1E94 /
     * Ground_GetStageGObj); otherwise the prototype's MnSlChr lights. */
    if (scene->hsd.stage_fog != NULL) {
        HSD_FogSet(scene->hsd.stage_fog);
    }
    lights = scene->hsd.stage_lobj != NULL ? scene->hsd.stage_lobj
                                           : scene->lobj;
    if (lights != NULL) {
        HSD_LObj* l;
        HSD_LObjSetCurrentAll(lights);
        for (l = lights->next; l != NULL; l = l->next) {
            HSD_LObjAddCurrent(l);
        }
        HSD_LObjSetupInit(active);
    }
    HSD_JObjDispAll(scene->hsd.root, NULL, HSD_TRSP_ALL, 0);
    if (scene->hsd.stage_root_count > 1) {
        int i;
        for (i = 1; i < scene->hsd.stage_root_count; i++) {
            HSD_JObjDispAll(scene->hsd.stage_roots[i], NULL, HSD_TRSP_ALL, 0);
        }
    }
    if (scene->show_fighter && scene->fighter.root != NULL) {
        HSD_JObjDispAll(scene->fighter.root, NULL, HSD_TRSP_ALL, 0);
    }
    /* Flush HSD's XLU z-sort list: joints whose draws were queued by
     * HSD_JObjDispDObj are only submitted by HSD_CObjEndCurrent. */
    HSD_CObjEndCurrent();
}

int render_scene_open(RenderScene* scene, const RenderSceneOptions* opt,
                      char* error, size_t error_size)
{
    int loaded;

    memset(scene, 0, sizeof(*scene));
    scene->angle = opt->angle;
    scene->elevation = opt->elevation;
    scene->zoom = opt->zoom > 0.0f ? opt->zoom : 1.0f;
    scene->width = opt->width > 0 ? opt->width : 640;
    scene->height = opt->height > 0 ? opt->height : 480;
    scene->vis_slot = 0;
    scene->vis_variant = 0;
    scene->stage_map = opt->stage_map;
    scene->stage_camera_map = -1;
    scene->scale_override = opt->scale_override;
    scene->no_scale = opt->no_scale;
    scene->stage_camera = opt->stage_camera;
    scene->no_fighter = opt->no_fighter;
    scene->show_fighter = 1;
    snprintf(scene->disc, sizeof(scene->disc), "%s",
             opt->disc != NULL ? opt->disc : RENDER_SCENE_DISC_DEFAULT);

    if (disc_list(scene->disc, "Pl", "Nr.dat", &scene->models, error,
                  error_size) != DISC_OK) {
        scene->models.count = 0;
    }
    if (disc_list(scene->disc, "Gr", ".dat", &scene->stages, error,
                  error_size) != DISC_OK) {
        scene->stages.count = 0;
    }

    if (opt->stage != NULL) {
        scene->stage_mode = 1;
        loaded = scene_load_stage(scene, opt, opt->stage, error, error_size);
        if (loaded <= 0) {
            return loaded;
        }
        /* Remember which fighter/model to come back to when toggling out of
         * stage mode (scene_load_model uses scene->model_index). */
        snprintf(scene->model, sizeof(scene->model), "%s",
                 opt->fighter != NULL ? opt->fighter
                                      : RENDER_SCENE_FIGHTER_DEFAULT);
        if (!opt->no_fighter) {
            const char* fighter = opt->fighter != NULL
                                      ? opt->fighter
                                      : RENDER_SCENE_FIGHTER_DEFAULT;
            int fighter_loaded =
                scene_load_fighter(scene, opt, fighter, error, error_size);
            scene->have_fighter = fighter_loaded > 0;
            if (fighter_loaded < 0) {
                fprintf(stderr, "render_scene: fighter %s failed: %s\n",
                        fighter, error);
            }
        }
    } else {
        loaded = scene_load_model(scene, opt,
                                  opt->model != NULL
                                      ? opt->model
                                      : RENDER_SCENE_MODEL_DEFAULT,
                                  error, error_size);
        if (loaded <= 0) {
            return loaded;
        }
    }

    scene->cobj = HSD_CObjAlloc();
    if (scene->cobj == NULL) {
        snprintf(error, error_size, "HSD_CObjAlloc failed");
        return -1;
    }

    if (!opt->no_lights) {
        if (lights_load(scene->disc, &scene->lights, error, error_size) ==
            0) {
            scene->have_lights = 1;
            scene->lobj = build_lights(&scene->lights);
        }
    }
    if (scene->stages.count > 0 && scene->stage[0] != '\0') {
        size_t i;
        for (i = 0; i < scene->stages.count; ++i) {
            if (strcmp(scene->stages.names[i], scene->stage) == 0) {
                scene->stage_index = (int) i;
                break;
            }
        }
    }
    {
        size_t i;
        for (i = 0; i < scene->models.count; ++i) {
            if (strcmp(scene->models.names[i], scene->model) == 0) {
                scene->model_index = (int) i;
                break;
            }
        }
    }
    render_scene_update_view(scene);
    return 1;
}

static void scene_remember_options(const RenderScene* scene,
                                   RenderSceneOptions* opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->disc = scene->disc;
    opt->width = scene->width;
    opt->height = scene->height;
    opt->angle = scene->angle;
    opt->elevation = scene->elevation;
    opt->zoom = scene->zoom;
    opt->scale_override =
        scene->scale_override > 0.0f ? scene->scale_override : -1.0f;
    opt->no_scale = scene->no_scale;
    opt->stage_map = scene->stage_map;
}

int render_scene_cycle(RenderScene* scene, int dir, char* error,
                       size_t error_size)
{
    RenderSceneOptions opt;
    int attempts;

    scene_remember_options(scene, &opt);
    if (scene->stage_mode) {
        int attempts;
        if (scene->stages.count == 0) {
            return 0;
        }
        /* Re-select the map per stage: map ids differ between stages. */
        scene->stage_map = -1;
        opt.stage_map = -1;
        attempts = (int) scene->stages.count;
        while (attempts-- > 0) {
            scene->stage_index += dir;
            if (scene->stage_index < 0) {
                scene->stage_index = (int) scene->stages.count - 1;
            }
            if (scene->stage_index >= (int) scene->stages.count) {
                scene->stage_index = 0;
            }
            opt.stage = scene->stages.names[scene->stage_index];
            if (scene_load_stage(scene, &opt, opt.stage, error,
                                 error_size) == 1) {
                return 1;
            }
        }
        return 0;
    }
    if (scene->models.count == 0) {
        return 0;
    }
    attempts = (int) scene->models.count;
    while (attempts-- > 0) {
        scene->model_index += dir;
        if (scene->model_index < 0) {
            scene->model_index = (int) scene->models.count - 1;
        }
        if (scene->model_index >= (int) scene->models.count) {
            scene->model_index = 0;
        }
        opt.model = scene->models.names[scene->model_index];
        if (scene_load_model(scene, &opt, opt.model, error, error_size) == 1) {
            return 1;
        }
    }
    return 0;
}

int render_scene_toggle_mode(RenderScene* scene, char* error,
                             size_t error_size)
{
    RenderSceneOptions opt;
    int loaded = 0;

    scene_remember_options(scene, &opt);
    if (!scene->stage_mode) {
        const char* stage = scene->stage[0] != '\0'
                                ? scene->stage
                                : RENDER_SCENE_STAGE_DEFAULT;
        opt.stage = stage;
        scene->stage_mode = 1;
        loaded = scene_load_stage(scene, &opt, stage, error, error_size);
        if (loaded == 1 && !scene->no_fighter && scene->fighter.root == NULL) {
            int fighter_loaded =
                scene_load_fighter(scene, &opt,
                                   RENDER_SCENE_FIGHTER_DEFAULT, error,
                                   error_size);
            scene->have_fighter = fighter_loaded > 0;
        }
    } else {
        const char* model = scene->models.count > 0
                                ? scene->models.names[scene->model_index]
                                : RENDER_SCENE_MODEL_DEFAULT;
        opt.model = model;
        scene->stage_mode = 0;
        loaded = scene_load_model(scene, &opt, model, error, error_size);
    }
    if (loaded != 1) {
        scene->stage_mode = !scene->stage_mode;
        return 0;
    }
    scene->need_view_update = 1;
    return 1;
}

void render_scene_toggle_fighter(RenderScene* scene)
{
    scene->show_fighter = !scene->show_fighter;
    scene->need_view_update = 1;
}

int render_scene_cycle_map(RenderScene* scene, int dir, char* error,
                           size_t error_size)
{
    RenderSceneOptions opt;
    char stage[64];
    int count = scene->hsd.stage_map_count;

    if (!scene->stage_mode || count <= 0) {
        return 0;
    }
    scene_remember_options(scene, &opt);
    snprintf(stage, sizeof(stage), "%s", scene->stage);
    if (dir > 0) {
        scene->stage_map++;
        if (scene->stage_map >= count) {
            scene->stage_map = -1;
        }
    } else {
        scene->stage_map--;
        if (scene->stage_map < -1) {
            scene->stage_map = count - 1;
        }
    }
    opt.stage_map = scene->stage_map;
    return scene_load_stage(scene, &opt, stage, error, error_size) == 1;
}

void render_scene_close(RenderScene* scene)
{
    HSD_LObjSetCurrentAll(NULL);
    if (scene->hsd.root != NULL || scene->fighter.root != NULL) {
        gx_hle_reset_assets();
        gx_gl_clear_textures();
        hsd_scene_free(&scene->hsd);
        hsd_scene_free(&scene->fighter);
    }
    if (scene->cobj != NULL) {
        hsdDelete((HSD_Class*) scene->cobj);
        scene->cobj = NULL;
    }
    if (scene->lobj != NULL) {
        HSD_LObjRemoveAll(scene->lobj);
        scene->lobj = NULL;
    }
    if (scene->hsd.stage_cobj != NULL) {
        hsdDelete((HSD_Class*) scene->hsd.stage_cobj);
        scene->hsd.stage_cobj = NULL;
    }
    if (scene->hsd.stage_lobj != NULL) {
        HSD_LObjRemoveAll(scene->hsd.stage_lobj);
        scene->hsd.stage_lobj = NULL;
    }
    /* HSD_Fog has no public free; one fog object per loaded stage is left to
     * the process exit (viewer tool). */
    scene->hsd.stage_fog = NULL;
    disc_list_free(&scene->models);
    disc_list_free(&scene->stages);
    memset(scene, 0, sizeof(*scene));
}

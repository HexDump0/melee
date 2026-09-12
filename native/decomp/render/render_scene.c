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
 * so the captured view positions are world-space bounds. */
static int compute_bounds(RenderScene* scene)
{
    static const float identity[4][4] = { { 1, 0, 0, 0 },
                                          { 0, 1, 0, 0 },
                                          { 0, 0, 1, 0 },
                                          { 0, 0, 0, 1 } };
    const GxHleVertex* vertices = NULL;
    size_t vertex_count = 0;
    size_t i;

    scene->bounds_min[0] = scene->bounds_min[1] = scene->bounds_min[2] =
        1e30f;
    scene->bounds_max[0] = scene->bounds_max[1] = scene->bounds_max[2] =
        -1e30f;
    gx_hle_begin_frame();
    /* The backend state was just reset; the compiled engine caches GX state
     * (channel registers, TEV stages, vtx descs) and would otherwise skip
     * re-emitting it for the next model, leaving the raster black. */
    HSD_StateInvalidate(-1);
    GXSetProjection((f32 (*)[4]) identity, GX_PERSPECTIVE);
    HSD_JObjDispAll(scene->hsd.root, (f32 (*)[4]) identity, HSD_TRSP_ALL, 0);
    gx_hle_get_frame(&vertices, &vertex_count, NULL, NULL, NULL, NULL);
    for (i = 0; i < vertex_count; ++i) {
        int k;
        for (k = 0; k < 3; ++k) {
            if (vertices[i].view[k] < scene->bounds_min[k]) {
                scene->bounds_min[k] = vertices[i].view[k];
            }
            if (vertices[i].view[k] > scene->bounds_max[k]) {
                scene->bounds_max[k] = vertices[i].view[k];
            }
        }
    }
    return scene->bounds_max[0] >= scene->bounds_min[0];
}

static int scene_load_model(RenderScene* scene, const RenderSceneOptions* opt,
                            const char* model, char* error,
                            size_t error_size)
{
    int loaded;

    if (scene->hsd.root != NULL) {
        gx_hle_reset_assets();
        gx_gl_clear_textures();
        hsd_scene_free(&scene->hsd);
    }

    loaded = hsd_scene_load(&scene->hsd, scene->disc, model, error,
                            error_size);
    if (loaded <= 0) {
        return loaded;
    }
    snprintf(scene->model, sizeof(scene->model), "%s", model);
    gx_hle_register_asset(scene->hsd.work, scene->hsd.work_size);

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
    scene->need_view_update = 0;
}

void render_scene_update_visibility(RenderScene* scene)
{
    char error[128];
    hsd_scene_clear_visibility(&scene->hsd);
    if (!scene->show_hidden) {
        scene->hidden_dobjs = hsd_scene_apply_visibility(
            &scene->hsd, scene->disc, scene->model, scene->vis_slot,
            scene->vis_variant, error, sizeof(error));
    } else {
        scene->hidden_dobjs = 0;
    }
    if (compute_bounds(scene)) {
        scene->need_view_update = 1;
    }
}

void render_scene_draw(RenderScene* scene)
{
    if (scene->need_view_update) {
        render_scene_update_view(scene);
    }
    gx_hle_discard_geometry();
    HSD_StartRender(HSD_RP_SCREEN);
    if (!HSD_CObjSetCurrent(scene->cobj)) {
        return;
    }
    if (scene->have_lights) {
        HSD_LObjSetupInit(scene->cobj);
    }
    HSD_JObjDispAll(scene->hsd.root, NULL, HSD_TRSP_ALL, 0);
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
    snprintf(scene->disc, sizeof(scene->disc), "%s",
             opt->disc != NULL ? opt->disc : RENDER_SCENE_DISC_DEFAULT);

    if (disc_list(scene->disc, "Pl", "Nr.dat", &scene->models, error,
                  error_size) != DISC_OK) {
        scene->models.count = 0;
    }

    loaded = scene_load_model(scene, opt,
                              opt->model != NULL ? opt->model
                                                 : RENDER_SCENE_MODEL_DEFAULT,
                              error, error_size);
    if (loaded <= 0) {
        return loaded;
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
            if (scene->lobj != NULL) {
                HSD_LObj* l;
                HSD_LObjSetCurrentAll(scene->lobj);
                for (l = scene->lobj->next; l != NULL; l = l->next) {
                    HSD_LObjAddCurrent(l);
                }
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

int render_scene_cycle(RenderScene* scene, int dir, char* error,
                       size_t error_size)
{
    RenderSceneOptions opt;
    int attempts;

    if (scene->models.count == 0) {
        return 0;
    }
    memset(&opt, 0, sizeof(opt));
    opt.disc = scene->disc;
    opt.width = scene->width;
    opt.height = scene->height;
    opt.angle = scene->angle;
    opt.elevation = scene->elevation;
    opt.zoom = scene->zoom;
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

void render_scene_close(RenderScene* scene)
{
    if (scene->hsd.root != NULL) {
        gx_hle_reset_assets();
        gx_gl_clear_textures();
        hsd_scene_free(&scene->hsd);
    }
    if (scene->cobj != NULL) {
        hsdDelete((HSD_Class*) scene->cobj);
        scene->cobj = NULL;
    }
    if (scene->lobj != NULL) {
        HSD_LObjRemoveAll(scene->lobj);
        scene->lobj = NULL;
    }
    disc_list_free(&scene->models);
    memset(scene, 0, sizeof(*scene));
}

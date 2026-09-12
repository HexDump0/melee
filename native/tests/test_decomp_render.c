/*
 * S2 exit harness: render a real character through the compiled HSD runtime
 * and the GX HLE backend.
 *
 * Flow:
 *   1. bootstrap HSD (arena, object pools, ID table, class info) on the S1
 *      platform OS layer,
 *   2. load a retail Pl<Char>Nr.dat through the S2 asset bridge
 *      (hsd_scene.c), which runs the decompilation's own
 *      HSD_ArchiveParse + HSD_JObjLoadJoint,
 *   3. apply the same per-character model scaling as
 *      Fighter_UpdateModelScale (src/melee/ft/fighter.c:213),
 *   4. frame the camera exactly like the prototype viewer
 *      (native/extras/viewer.c:viewer_frame_bounds + render_viewer),
 *   5. build the character-select HSD_LObj set with the compiled lobj code
 *      (same data the prototype's native/hsd/light.c reads),
 *   6. run the compiled display path (HSD_JObjDispAll) and capture the GX
 *      command stream,
 *   7. render through GLES3 (gx_gl.c) and save a BMP.
 *
 * Exit code 0 = rendered (SKIP without a disc image); non-zero = failure.
 */
#include <dolphin/gx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/forward.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>

#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"
#include "decomp/hsd/hsd_scene.h"
#include "hsd/light.h"
#include "platform/disc.h"
#include "platform/platform.h"

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"
#define DEFAULT_MODEL "PlMrNr.dat"

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--disc PATH] [--model NAME] [--shot FILE]\n"
            "          [--width N] [--height N] [--scale F]\n"
            "          [--angle DEG] [--elevation DEG] [--no-lights]\n"
            "          [--dump] [--no-scale]\n",
            argv0);
}

/* PlMrNr.dat -> PlMr.dat, then ftData + 0x00 -> attrs, +0x8C model_scaling.
 * Offsets from src/melee/ft/types.h and native/hsd/parts.c. */
static float read_model_scaling(const char* disc, const char* model)
{
    char ft_name[32];
    DiscFile asset;
    char error[256];
    unsigned int ft = 0;
    unsigned int attrs;
    size_t i;
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
        unsigned int data_size =
            ((unsigned int) d[4] << 24) | ((unsigned int) d[5] << 16) |
            ((unsigned int) d[6] << 8) | d[7];
        unsigned int nb_reloc =
            ((unsigned int) d[8] << 24) | ((unsigned int) d[9] << 16) |
            ((unsigned int) d[10] << 8) | d[11];
        unsigned int nb_public =
            ((unsigned int) d[12] << 24) | ((unsigned int) d[13] << 16) |
            ((unsigned int) d[14] << 8) | d[15];
        unsigned int nb_extern =
            ((unsigned int) d[16] << 24) | ((unsigned int) d[17] << 16) |
            ((unsigned int) d[18] << 8) | d[19];
        size_t public_off = 0x20 + data_size + (size_t) nb_reloc * 4;
        size_t symbols_off =
            public_off + (size_t) nb_public * 8 + (size_t) nb_extern * 8;
        for (i = 0; i < nb_public && ft == 0; ++i) {
            unsigned int data_off =
                ((unsigned int) d[public_off + i * 8] << 24) |
                ((unsigned int) d[public_off + i * 8 + 1] << 16) |
                ((unsigned int) d[public_off + i * 8 + 2] << 8) |
                d[public_off + i * 8 + 3];
            unsigned int name_off =
                ((unsigned int) d[public_off + i * 8 + 4] << 24) |
                ((unsigned int) d[public_off + i * 8 + 5] << 16) |
                ((unsigned int) d[public_off + i * 8 + 6] << 8) |
                d[public_off + i * 8 + 7];
            if (symbols_off + name_off < n &&
                strncmp((const char*) d + symbols_off + name_off, "ftData",
                        6) == 0) {
                ft = data_off;
            }
        }
        if (ft != 0 && 0x20 + ft + 4 <= n) {
            attrs = ((unsigned int) d[0x20 + ft] << 24) |
                    ((unsigned int) d[0x20 + ft + 1] << 16) |
                    ((unsigned int) d[0x20 + ft + 2] << 8) |
                    d[0x20 + ft + 3];
            if (0x20 + attrs + 0x90 <= n) {
                unsigned int bits =
                    ((unsigned int) d[0x20 + attrs + 0x8C] << 24) |
                    ((unsigned int) d[0x20 + attrs + 0x8D] << 16) |
                    ((unsigned int) d[0x20 + attrs + 0x8E] << 8) |
                    d[0x20 + attrs + 0x8F];
                memcpy(&scale, &bits, sizeof(scale));
                if (!(scale > 0.02f && scale < 8.0f) ||
                    !isfinite(scale)) {
                    scale = 1.0f;
                }
            }
        }
    }
    disc_free(&asset);
    return scale;
}

/* The viewer's framing math: native/extras/viewer.c viewer_frame_bounds +
 * render_viewer (fovy 0.7 rad, zfar = distance*8+100, znear = zfar/1200). */
static void frame_bounds(const float mn[3], const float mx[3], float yaw_deg,
                         float elev_deg, float zoom, float* eye,
                         float* target, float* distance, float* znear,
                         float* zfar)
{
    float size[3];
    float radius;
    float dist;
    float cp;
    float sp;
    float yaw = yaw_deg * (float) M_PI / 180.0f;
    float pitch = -elev_deg * (float) M_PI / 180.0f;
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
    dist = radius / tanf(0.35f) * 1.15f * zoom;
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
    *distance = dist;
}

/* Builds the compiled HSD_LObj chain from the same character-select lights
 * the prototype viewer uses (native/hsd/light.c). */
static HSD_LObj* build_scene_lights(const SceneLights* set)
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

int main(int argc, char** argv)
{
    const char* disc = DEFAULT_DISC;
    const char* model = DEFAULT_MODEL;
    const char* shot = NULL;
    int width = 640;
    int height = 480;
    int no_lights = 0;
    int dump = 0;
    int no_scale = 0;
    int no_gl = 0;
    float scale_arg = -1.0f;
    float angle = 25.0f;
    float elevation = -12.0f;
    HsdScene scene;
    char error[256];
    float model_scale;
    float identity[4][4] = { { 1, 0, 0, 0 },
                             { 0, 1, 0, 0 },
                             { 0, 0, 1, 0 },
                             { 0, 0, 0, 1 } };
    float bounds_min[3] = { 1e30f, 1e30f, 1e30f };
    float bounds_max[3] = { -1e30f, -1e30f, -1e30f };
    float eye[3];
    float target[3];
    float distance;
    float znear;
    float zfar;
    const GxHleVertex* vertices = NULL;
    const GxHleDraw* draws = NULL;
    GxHleTexture* textures = NULL;
    size_t vertex_count = 0;
    size_t draw_count = 0;
    size_t texture_count = 0;
    size_t i;
    HSD_CObj* cobj;
    HSD_LObj* lights = NULL;
    SceneLights scene_lights;
    int have_lights = 0;
    int rendered;

    for (i = 1; (int) i < argc; ++i) {
        if (strcmp(argv[i], "--disc") == 0 && (int) i + 1 < argc) {
            disc = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && (int) i + 1 < argc) {
            model = argv[++i];
        } else if (strcmp(argv[i], "--shot") == 0 && (int) i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && (int) i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && (int) i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && (int) i + 1 < argc) {
            scale_arg = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--angle") == 0 && (int) i + 1 < argc) {
            angle = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--elevation") == 0 &&
                   (int) i + 1 < argc) {
            elevation = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--no-lights") == 0) {
            no_lights = 1;
        } else if (strcmp(argv[i], "--no-scale") == 0) {
            no_scale = 1;
        } else if (strcmp(argv[i], "--dump") == 0) {
            dump = 1;
        } else if (strcmp(argv[i], "--no-gl") == 0) {
            no_gl = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: unknown option %s\n", argv[0], argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (!hsd_scene_boot()) {
        fprintf(stderr, "decomp_render: HSD bootstrap failed\n");
        return 1;
    }
    {
        int loaded = hsd_scene_load(&scene, disc, model, error, sizeof(error));
        if (loaded == 0) {
            printf("decomp_render: SKIP (%s: %s)\n", disc, error);
            return 0;
        }
        if (loaded < 0) {
            fprintf(stderr, "decomp_render: load failed: %s\n", error);
            return 1;
        }
    }
    gx_hle_register_asset(scene.work, scene.work_size);
    model_scale = scale_arg > 0.0f ? scale_arg
                                   : read_model_scaling(disc, model);
    if (no_scale) {
        model_scale = 1.0f;
    }
    printf("decomp_render: %s root=%p scale=%.4f\n", model, (void*) scene.root,
           (double) model_scale);

    /* Fighter_UpdateModelScale (src/melee/ft/fighter.c:213). */
    {
        Vec3 scale;
        scale.x = model_scale;
        scale.y = model_scale;
        scale.z = model_scale;
        HSD_JObjSetScale(scene.root, &scale);
    }
    HSD_JObjSetupMatrix(scene.root);

    /* ftParts slot 0 / variant 0 part visibility (native/hsd/parts.c). */
    {
        int hidden = hsd_scene_apply_visibility(&scene, disc, model, 0, 0,
                                                error, sizeof(error));
        if (hidden < 0) {
            printf("decomp_render: part visibility unavailable (%s)\n",
                   error);
        } else {
            printf("decomp_render: hidden DObjs=%d\n", hidden);
        }
    }

    /* Pass 1: identity projection; GX pos matrices are the world matrices, so
     * the captured view positions give the world-space bounds. */
    gx_hle_begin_frame();
    GXSetProjection(identity, GX_PERSPECTIVE);
    HSD_JObjDispAll(scene.root, identity, HSD_TRSP_ALL, 0);
    gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count, NULL,
                     NULL);
    for (i = 0; i < vertex_count; ++i) {
        int k;
        for (k = 0; k < 3; ++k) {
            if (vertices[i].view[k] < bounds_min[k]) {
                bounds_min[k] = vertices[i].view[k];
            }
            if (vertices[i].view[k] > bounds_max[k]) {
                bounds_max[k] = vertices[i].view[k];
            }
        }
    }
    printf("decomp_render: bounds [%.2f %.2f %.2f] to [%.2f %.2f %.2f]\n",
           bounds_min[0], bounds_min[1], bounds_min[2], bounds_max[0],
           bounds_max[1], bounds_max[2]);
    if (bounds_max[0] < bounds_min[0]) {
        fprintf(stderr, "decomp_render: no geometry captured\n");
        return 1;
    }

    /* Camera, matching the prototype viewer. */
    frame_bounds(bounds_min, bounds_max, angle, elevation, 1.0f, eye, target,
                 &distance, &znear, &zfar);
    cobj = HSD_CObjAlloc();
    if (cobj == NULL) {
        fprintf(stderr, "decomp_render: HSD_CObjAlloc failed\n");
        return 1;
    }
    HSD_CObjSetEyePosition(cobj, (Vec3*) eye);
    HSD_CObjSetInterest(cobj, (Vec3*) target);
    {
        Vec3 up = { 0.0f, 1.0f, 0.0f };
        HSD_CObjSetUpVector(cobj, &up);
    }
    HSD_CObjSetNear(cobj, znear);
    HSD_CObjSetFar(cobj, zfar);
    HSD_CObjSetPerspective(
        cobj, 0.7f * 180.0f / (float) M_PI,
        (float) width / (float) height);
    {
        HSD_RectS16 vp = { 0, (s16) width, 0, (s16) height };
        HSD_CObjSetViewport(cobj, &vp);
        HSD_CObjSetScissorx4(cobj, 0, (u16) width, 0, (u16) height);
    }

    /* Same light data as the prototype viewer (MnSlChr). */
    if (!no_lights &&
        lights_load(disc, &scene_lights, error, sizeof(error)) == 0) {
        have_lights = 1;
    }

    /* Pass 2: real camera.  The GX state from pass 1 is still current in the
     * backend, so the compiled engine's state caches stay coherent. */
    gx_hle_discard_geometry();
    HSD_StartRender(HSD_RP_SCREEN);
    if (!HSD_CObjSetCurrent(cobj)) {
        fprintf(stderr, "decomp_render: HSD_CObjSetCurrent failed\n");
        return 1;
    }
    if (have_lights) {
        HSD_LObj* l;
        lights = build_scene_lights(&scene_lights);
        /* HSD_LObjSetCurrentAll registers only the head; the game adds each
         * additional light with HSD_LObjAddCurrent (lobj.c). */
        HSD_LObjSetCurrentAll(lights);
        for (l = lights != NULL ? lights->next : NULL; l != NULL;
             l = l->next) {
            HSD_LObjAddCurrent(l);
        }
        HSD_LObjSetupInit(cobj);
        printf("decomp_render: lights=%zu\n", scene_lights.count);
    }
    HSD_JObjDispAll(scene.root, NULL, HSD_TRSP_ALL, 0);

    if (no_gl) {
        rendered = (int) draw_count;
    } else {
        if (!gx_gl_init(width, height, error, sizeof(error))) {
            fprintf(stderr, "decomp_render: GL init failed: %s\n", error);
            return 1;
        }
        gx_gl_set_clear(0.05f, 0.06f, 0.09f, 1.0f);
        rendered = gx_gl_render_frame();
    }
    gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count,
                     &textures, &texture_count);
    printf("decomp_render: draws=%d vertices=%u textures=%u display_lists=%u "
           "primitives=%u skipped=%u degenerate=%u\n",
           rendered, (unsigned) vertex_count, (unsigned) texture_count,
           (unsigned) gx_hle_display_list_count(),
           (unsigned) gx_hle_primitive_count(),
           (unsigned) gx_hle_skipped_count(),
           (unsigned) gx_hle_degenerate_count());
    if (dump) {
        size_t d;
        for (d = 0; d < draw_count; ++d) {
            const GxHleDraw* dr = &draws[d];
            float vmn[3] = { 1e30f, 1e30f, 1e30f };
            float vmx[3] = { -1e30f, -1e30f, -1e30f };
            float wmn = 1e30f;
            float wmx = -1e30f;
            size_t k;
            int a;
            for (k = 0; k < dr->vertex_count; ++k) {
                const GxHleVertex* vv = &vertices[dr->first_vertex + k];
                for (a = 0; a < 3; ++a) {
                    if (vv->view[a] < vmn[a]) {
                        vmn[a] = vv->view[a];
                    }
                    if (vv->view[a] > vmx[a]) {
                        vmx[a] = vv->view[a];
                    }
                }
                if (vv->clip[3] < wmn) {
                    wmn = vv->clip[3];
                }
                if (vv->clip[3] > wmx) {
                    wmx = vv->clip[3];
                }
            }
            {
                long off0 = -1, off1 = -1;
                if (dr->state.texmap[0] >= 0 &&
                    (size_t) dr->state.texmap[0] < texture_count) {
                    off0 = (long) ((const unsigned char*)
                                       textures[dr->state.texmap[0]].image -
                                   scene.work);
                }
                if (dr->state.texmap[1] >= 0 &&
                    (size_t) dr->state.texmap[1] < texture_count) {
                    off1 = (long) ((const unsigned char*)
                                       textures[dr->state.texmap[1]].image -
                                   scene.work);
                }
                printf("  draw %u texoff=%ld/%ld ", (unsigned) d, off0, off1);
            }
            printf("  draw %u: verts=%u cull=%u blend=%u z=%u zf=%u stages=%u "
                   "map0=%d map1=%d view=[%.2f %.2f %.2f]..[%.2f %.2f %.2f] "
                   "w=[%.2f %.2f]\n",
                   (unsigned) d, (unsigned) dr->vertex_count,
                   dr->state.cull_mode, dr->state.blend_type,
                   dr->state.z_enable, dr->state.z_func,
                   dr->state.num_stages, dr->state.texmap[0],
                   dr->state.texmap[1], vmn[0], vmn[1], vmn[2], vmx[0],
                   vmx[1], vmx[2], wmn, wmx);
        }
    }
    if (shot != NULL && !no_gl) {
        if (!gx_gl_save_bmp(shot)) {
            fprintf(stderr, "decomp_render: screenshot failed\n");
            return 1;
        }
        printf("decomp_render: wrote %s\n", shot);
    }
    if (rendered <= 0 || vertex_count == 0) {
        fprintf(stderr, "decomp_render: nothing rendered\n");
        return 1;
    }
    if (!no_gl) {
        gx_gl_shutdown();
    }
    hsd_scene_free(&scene);
    printf("decomp_render: PASS\n");
    return 0;
}

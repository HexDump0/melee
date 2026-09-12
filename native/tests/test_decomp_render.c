/*
 * S2 exit harness: render a real character through the compiled HSD runtime
 * and the GX HLE backend (headless EGL/GLES3).
 *
 * The scene setup (bootstrap, asset bridge, model scaling, ftData
 * visibility, prototype camera/lights) lives in decomp/render/render_scene.c,
 * shared with the interactive SDL3 viewer (decomp/render/viewer_main.c).
 *
 * Exit code 0 = rendered (SKIP without a disc image); non-zero = failure.
 */
#include <math.h>
#include <sysdolphin/baselib/state.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"
#include "decomp/render/render_scene.h"
#include "platform/disc.h"

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--disc PATH] [--model NAME] [--shot FILE]\n"
            "          [--width N] [--height N] [--scale F]\n"
            "          [--angle DEG] [--elevation DEG] [--no-lights]\n"
            "          [--dump] [--no-scale] [--no-gl]\n",
            argv0);
}

int main(int argc, char** argv)
{
    RenderSceneOptions opt;
    RenderScene scene;
    char error[256];
    const char* shot = NULL;
    int dump = 0;
    int no_gl = 0;
    const char* dump_world = NULL;
    int rendered;
    int loaded;
    size_t i;
    const GxHleVertex* vertices = NULL;
    const GxHleDraw* draws = NULL;
    GxHleTexture* textures = NULL;
    size_t vertex_count = 0;
    size_t draw_count = 0;
    size_t texture_count = 0;

    memset(&opt, 0, sizeof(opt));
    opt.disc = RENDER_SCENE_DISC_DEFAULT;
    opt.model = RENDER_SCENE_MODEL_DEFAULT;
    opt.width = 640;
    opt.height = 480;
    opt.angle = 25.0f;
    opt.elevation = -12.0f;
    opt.zoom = 1.0f;
    opt.scale_override = -1.0f;

    for (i = 1; (int) i < argc; ++i) {
        if (strcmp(argv[i], "--disc") == 0 && (int) i + 1 < argc) {
            opt.disc = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && (int) i + 1 < argc) {
            opt.model = argv[++i];
        } else if (strcmp(argv[i], "--shot") == 0 && (int) i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && (int) i + 1 < argc) {
            opt.width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && (int) i + 1 < argc) {
            opt.height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && (int) i + 1 < argc) {
            opt.scale_override = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--angle") == 0 && (int) i + 1 < argc) {
            opt.angle = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--elevation") == 0 &&
                   (int) i + 1 < argc) {
            opt.elevation = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--no-lights") == 0) {
            opt.no_lights = 1;
        } else if (strcmp(argv[i], "--no-scale") == 0) {
            opt.no_scale = 1;
        } else if (strcmp(argv[i], "--dump") == 0) {
            dump = 1;
        } else if (strcmp(argv[i], "--dump-world") == 0 &&
                   (int) i + 1 < argc) {
            dump_world = argv[++i];
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

    if (!render_scene_boot()) {
        fprintf(stderr, "decomp_render: HSD bootstrap failed\n");
        return 1;
    }
    loaded = render_scene_open(&scene, &opt, error, sizeof(error));
    if (loaded == 0) {
        printf("decomp_render: SKIP (%s: %s)\n", opt.disc, error);
        return 0;
    }
    if (loaded < 0) {
        fprintf(stderr, "decomp_render: load failed: %s\n", error);
        return 1;
    }
    printf("decomp_render: %s root=%p scale=%.4f hidden_dobjs=%d\n",
           scene.model, (void*) scene.hsd.root, (double) scene.model_scale,
           scene.hidden_dobjs);
    printf("decomp_render: bounds [%.2f %.2f %.2f] to [%.2f %.2f %.2f]\n",
           scene.bounds_min[0], scene.bounds_min[1], scene.bounds_min[2],
           scene.bounds_max[0], scene.bounds_max[1], scene.bounds_max[2]);
    if (scene.have_lights) {
        printf("decomp_render: lights=%zu\n", scene.lights.count);
    }
    if (dump_world != NULL) {
        /* Dev diagnostic: world-space vertices per draw from an identity pass
         * (compare with the prototype's --dump-verts to catch GX decode or
         * primitive-assembly bugs; see learnings/decomp_viewer.md). */
        static const float identity[4][4] = { { 1, 0, 0, 0 },
                                              { 0, 1, 0, 0 },
                                              { 0, 0, 1, 0 },
                                              { 0, 0, 0, 1 } };
        FILE* f;
        gx_hle_begin_frame();
        HSD_StateInvalidate(-1);
        GXSetProjection((f32 (*)[4]) identity, GX_PERSPECTIVE);
        HSD_JObjDispAll(scene.hsd.root, (f32 (*)[4]) identity, HSD_TRSP_ALL,
                        0);
        gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count, NULL,
                         NULL);
        f = fopen(dump_world, "wb");
        for (i = 0; i < draw_count && f != NULL; ++i) {
            unsigned int count = (unsigned int) draws[i].vertex_count;
            size_t k;
            fwrite(&count, 4, 1, f);
            for (k = 0; k < draws[i].vertex_count; ++k) {
                fwrite(vertices[draws[i].first_vertex + k].view, 4, 3, f);
            }
        }
        if (f != NULL) {
            fclose(f);
            printf("decomp_render: dumped %zu draws to %s\n", draw_count,
                   dump_world);
        } else {
            fprintf(stderr, "decomp_render: cannot write %s\n", dump_world);
            return 1;
        }
    }

    render_scene_draw(&scene);

    if (no_gl) {
        rendered = 0;
    } else {
        if (!gx_gl_init(opt.width, opt.height, error, sizeof(error))) {
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
            size_t k;
            int a;
            long off0 = -1;
            long off1 = -1;
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
            }
            if (dr->state.texmap[0] >= 0 &&
                (size_t) dr->state.texmap[0] < texture_count) {
                off0 = (long) ((const unsigned char*)
                                   textures[dr->state.texmap[0]].image -
                               scene.hsd.work);
            }
            if (dr->state.texmap[1] >= 0 &&
                (size_t) dr->state.texmap[1] < texture_count) {
                off1 = (long) ((const unsigned char*)
                                   textures[dr->state.texmap[1]].image -
                               scene.hsd.work);
            }
            printf("  draw %u: verts=%u cull=%u blend=%u z=%u zf=%u stages=%u "
                   "texoff=%ld/%ld view=[%.2f %.2f %.2f]..[%.2f %.2f %.2f]\n",
                   (unsigned) d, (unsigned) dr->vertex_count,
                   dr->state.cull_mode, dr->state.blend_type,
                   dr->state.z_enable, dr->state.z_func,
                   dr->state.num_stages, off0, off1, vmn[0], vmn[1], vmn[2],
                   vmx[0], vmx[1], vmx[2]);
        }
    }
    if (shot != NULL && !no_gl) {
        if (!gx_gl_save_bmp(shot)) {
            fprintf(stderr, "decomp_render: screenshot failed\n");
            return 1;
        }
        printf("decomp_render: wrote %s\n", shot);
    }
    if (!no_gl && (rendered <= 0 || vertex_count == 0)) {
        fprintf(stderr, "decomp_render: nothing rendered\n");
        return 1;
    }
    if (!no_gl) {
        gx_gl_shutdown();
    }
    render_scene_close(&scene);
    printf("decomp_render: PASS\n");
    return 0;
}

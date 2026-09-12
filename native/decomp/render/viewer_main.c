/*
 * Interactive compiled-path viewer (P-611).
 *
 * Runs the same scene/capture as tests/test_decomp_render.c but presents it
 * in an SDL3 window with orbit, zoom, model cycling, part isolation and
 * display toggles.  SDL owns the window and the EGL/GLES3 context (ADR-0014);
 * gx_gl attaches to it and renders the captured GX frame.
 *
 * Usage:
 *   melee_decomp_viewer [--disc PATH] [--model NAME] [--width N] [--height N]
 *                       [--angle DEG] [--elevation DEG] [--zoom F]
 *                       [--frames N] [--shot FILE] [--hidden] [--no-lights]
 *
 * Keys: drag orbit, wheel zoom, N/P model, [ ] part, V part mode, shift+V
 *       variant, B slot, Y hidden, L lights, T textures, W wireframe, C cull,
 *       H HUD, F12 screenshot, R reset, ESC quit.
 */
#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"
#include "decomp/render/hud.h"
#include "decomp/render/render_scene.h"

typedef struct Viewer {
    RenderScene scene;
    GxGlOptions gl;
    int part;      /* selected draw index */
    int part_mode; /* 0 all, 1 only, 2 hide */
    int hud;
    int draw_total;
} Viewer;

static const char* part_mode_name(int mode)
{
    static const char* names[3] = { "ALL", "ONLY", "HIDE" };
    return names[mode];
}

static void update_part_filter(Viewer* v)
{
    v->gl.only_draw = -1;
    v->gl.hide_draw = -1;
    if (v->draw_total > 0) {
        if (v->part >= v->draw_total) {
            v->part = v->draw_total - 1;
        }
        if (v->part < 0) {
            v->part = 0;
        }
    } else {
        v->part = 0;
    }
    if (v->part_mode == 1) {
        v->gl.only_draw = v->part;
    } else if (v->part_mode == 2) {
        v->gl.hide_draw = v->part;
    }
    gx_gl_set_options(&v->gl);
}

static void print_status(const Viewer* v)
{
    printf("[viewer] %s  angle=%.0f elev=%.0f zoom=%.2f  part=%d/%d %s  "
           "slot=%d variant=%d hidden=%s  textures=%s lights=%s wire=%s  "
           "%d draws, %d hidden DObjs\n",
           v->scene.model, (double) v->scene.angle, (double) v->scene.elevation,
           (double) v->scene.zoom, v->part + 1, v->draw_total,
           part_mode_name(v->part_mode), v->scene.vis_slot,
           v->scene.vis_variant, v->scene.show_hidden ? "shown" : "game",
           v->gl.textures ? "on" : "off", v->gl.lighting ? "on" : "off",
           v->gl.wireframe ? "on" : "off", (int) v->draw_total,
           v->scene.hidden_dobjs);
}

static void draw_hud(const Viewer* v)
{
    if (!v->hud) {
        return;
    }
    hud_begin(v->scene.width, v->scene.height);
    hud_set_color(0.95f, 0.87f, 0.55f, 1.0f);
    hud_printf(10.0f, 8.0f, 2.0f, "MELEE COMPILED VIEWER - HSD + GX HLE");
    hud_set_color(0.75f, 0.92f, 0.95f, 1.0f);
    hud_printf(10.0f, 30.0f, 1.5f, "%s", v->scene.model);
    hud_set_color(0.92f, 0.92f, 0.95f, 1.0f);
    hud_printf(10.0f, 50.0f, 1.4f, "PART %d/%d %s", v->part + 1,
               v->draw_total, part_mode_name(v->part_mode));
    hud_printf(10.0f, 68.0f, 1.4f, "SLOT %d VARIANT %d HIDDEN %s",
               v->scene.vis_slot, v->scene.vis_variant,
               v->scene.show_hidden ? "ON" : "GAME");
    hud_printf(10.0f, 86.0f, 1.4f, "TEX %s LIGHT %s WIRE %s CULL %s",
               v->gl.textures ? "ON" : "OFF",
               v->gl.lighting ? "ON" : "OFF",
               v->gl.wireframe ? "ON" : "OFF",
               v->gl.no_cull ? "OFF" : "ON");
    hud_printf(10.0f, 104.0f, 1.4f, "ANGLE %.0f ELEV %.0f ZOOM %.2f",
               (double) v->scene.angle, (double) v->scene.elevation,
               (double) v->scene.zoom);
    hud_set_color(0.65f, 0.72f, 0.82f, 1.0f);
    hud_printf(10.0f, (float) v->scene.height - 22.0f, 1.2f,
               "DRAG ORBIT WHEEL ZOOM N/P MODEL [ ] PART V MODE Y HIDDEN "
               "L LIGHT T TEX W WIRE C CULL H HUD F12 SHOT R RESET ESC QUIT");
    hud_end();
}

static int handle_key(Viewer* v, const SDL_KeyboardEvent* key,
                      int* want_shot)
{
    SDL_Keycode code = key->key;
    int shift = (key->mod & SDL_KMOD_SHIFT) != 0;

    switch (code) {
    case SDLK_ESCAPE:
        return 0;
    case SDLK_N:
    case SDLK_P: {
        char error[256];
        if (!render_scene_cycle(&v->scene, code == SDLK_N ? 1 : -1, error,
                                sizeof(error))) {
            fprintf(stderr, "[viewer] no other model loads: %s\n", error);
        }
        v->part = 0;
        update_part_filter(v);
        break;
    }
    case SDLK_LEFTBRACKET:
        v->part--;
        update_part_filter(v);
        break;
    case SDLK_RIGHTBRACKET:
        v->part++;
        update_part_filter(v);
        break;
    case SDLK_V:
        if (shift) {
            v->scene.vis_variant = (v->scene.vis_variant + 1) % 4;
            render_scene_update_visibility(&v->scene);
        } else {
            v->part_mode = (v->part_mode + 1) % 3;
            update_part_filter(v);
        }
        break;
    case SDLK_B:
        v->scene.vis_slot = (v->scene.vis_slot + 1) % 4;
        render_scene_update_visibility(&v->scene);
        break;
    case SDLK_Y:
        v->scene.show_hidden = !v->scene.show_hidden;
        render_scene_update_visibility(&v->scene);
        break;
    case SDLK_L:
        v->gl.lighting = !v->gl.lighting;
        gx_gl_set_options(&v->gl);
        break;
    case SDLK_T:
        v->gl.textures = !v->gl.textures;
        gx_gl_set_options(&v->gl);
        break;
    case SDLK_W:
        v->gl.wireframe = !v->gl.wireframe;
        gx_gl_set_options(&v->gl);
        break;
    case SDLK_C:
        v->gl.no_cull = !v->gl.no_cull;
        gx_gl_set_options(&v->gl);
        break;
    case SDLK_H:
        v->hud = !v->hud;
        break;
    case SDLK_R:
        v->scene.angle = 25.0f;
        v->scene.elevation = -12.0f;
        v->scene.zoom = 1.0f;
        v->scene.need_view_update = 1;
        break;
    case SDLK_F12:
        *want_shot = 1;
        break;
    default:
        break;
    }
    return 1;
}

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--disc PATH] [--model NAME] [--width N] [--height N]\n"
            "          [--angle DEG] [--elevation DEG] [--zoom F]\n"
            "          [--frames N] [--shot FILE] [--hidden] [--no-lights]\n"
            "          [--unlit] [--wire] [--no-hud] [--cycle N] [--spin DEG]\n"
            "          [--no-cull] [--no-alpha-test] [--part N] [--part-mode "
            "all|only|hide]\n",
            argv0);
}

int main(int argc, char** argv)
{
    RenderSceneOptions opt;
    Viewer viewer;
    Viewer* v = &viewer;
    const char* shot = NULL;
    int width = 1280;
    int height = 800;
    int frames = 0;
    int cycle = 0;
    float spin = 0.0f;
    int hidden = 0;
    int want_shot = 0;
    int quit = 0;
    int frame_count = 0;
    int draws = 0;
    int loaded;
    char error[256];
    SDL_Window* window;
    SDL_GLContext context;
    size_t i;

    memset(&opt, 0, sizeof(opt));
    memset(v, 0, sizeof(*v));
    opt.disc = RENDER_SCENE_DISC_DEFAULT;
    opt.angle = 25.0f;
    opt.elevation = -12.0f;
    opt.zoom = 1.0f;
    opt.scale_override = -1.0f;
    v->gl.textures = 1;
    v->gl.lighting = 1;
    v->gl.only_draw = -1;
    v->gl.hide_draw = -1;
    v->hud = 1;

    for (i = 1; (int) i < argc; ++i) {
        if (strcmp(argv[i], "--disc") == 0 && (int) i + 1 < argc) {
            opt.disc = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0 && (int) i + 1 < argc) {
            opt.model = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && (int) i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && (int) i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--angle") == 0 && (int) i + 1 < argc) {
            opt.angle = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--elevation") == 0 &&
                   (int) i + 1 < argc) {
            opt.elevation = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--zoom") == 0 && (int) i + 1 < argc) {
            opt.zoom = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && (int) i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--shot") == 0 && (int) i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(argv[i], "--hidden") == 0) {
            hidden = 1;
        } else if (strcmp(argv[i], "--no-lights") == 0) {
            opt.no_lights = 1;
        } else if (strcmp(argv[i], "--unlit") == 0) {
            v->gl.lighting = 0;
        } else if (strcmp(argv[i], "--spin") == 0 && (int) i + 1 < argc) {
            spin = (float) atof(argv[++i]);
        } else if (strcmp(argv[i], "--wire") == 0) {
            v->gl.wireframe = 1;
        } else if (strcmp(argv[i], "--no-cull") == 0) {
            v->gl.no_cull = 1;
        } else if (strcmp(argv[i], "--no-alpha-test") == 0) {
            v->gl.no_alpha_test = 1;
        } else if (strcmp(argv[i], "--no-hud") == 0) {
            v->hud = 0;
        } else if (strcmp(argv[i], "--cycle") == 0 && (int) i + 1 < argc) {
            cycle = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--part") == 0 && (int) i + 1 < argc) {
            v->part = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--part-mode") == 0 &&
                   (int) i + 1 < argc) {
            const char* mode = argv[++i];
            if (strcmp(mode, "only") == 0) {
                v->part_mode = 1;
            } else if (strcmp(mode, "hide") == 0) {
                v->part_mode = 2;
            } else {
                v->part_mode = 0;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: unknown option %s\n", argv[0], argv[i]);
            usage(argv[0]);
            return 2;
        }
    }
    opt.width = width;
    opt.height = height;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "viewer: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    window = SDL_CreateWindow(
        "Melee native - compiled HSD / GX HLE viewer", width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
            (hidden ? SDL_WINDOW_HIDDEN : 0));
    if (window == NULL) {
        fprintf(stderr, "viewer: SDL_CreateWindow failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        return 1;
    }
    context = SDL_GL_CreateContext(window);
    if (context == NULL) {
        fprintf(stderr, "viewer: SDL_GL_CreateContext failed: %s\n",
                SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    {
        int dw = 0;
        int dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        printf("viewer: requested %dx%d, drawable %dx%d\n", width, height, dw,
               dh);
        if (dw > 0 && dh > 0) {
            width = dw;
            height = dh;
            opt.width = dw;
            opt.height = dh;
        }
    }
    printf("viewer: GL_VERSION=%s\n", (const char*) glGetString(GL_VERSION));
    printf("viewer: GL_RENDERER=%s\n", (const char*) glGetString(GL_RENDERER));

    if (!gx_gl_attach(width, height, error, sizeof(error))) {
        fprintf(stderr, "viewer: gx_gl_attach failed: %s\n", error);
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    gx_gl_set_clear(0.05f, 0.06f, 0.09f, 1.0f);
    gx_gl_set_options(&v->gl);
    if (!hud_init(error, sizeof(error))) {
        fprintf(stderr, "viewer: hud init failed: %s\n", error);
        return 1;
    }

    if (!render_scene_boot()) {
        fprintf(stderr, "viewer: HSD bootstrap failed\n");
        return 1;
    }
    loaded = render_scene_open(&v->scene, &opt, error, sizeof(error));
    if (loaded == 0) {
        printf("viewer: SKIP (%s: %s)\n", opt.disc, error);
        return 0;
    }
    if (loaded < 0) {
        fprintf(stderr, "viewer: load failed: %s\n", error);
        return 1;
    }
    while (cycle-- > 0) {
        char cycle_error[256];
        if (!render_scene_cycle(&v->scene, 1, cycle_error,
                                sizeof(cycle_error))) {
            fprintf(stderr, "viewer: cycle failed: %s\n", cycle_error);
            break;
        }
    }
    printf("viewer: drag=orbit wheel=zoom N/P=model [ ]=part V=mode "
           "shift+V=variant B=slot Y=hidden L=lights T=textures W=wire "
           "H=hud F12=shot R=reset ESC=quit\n");
    update_part_filter(v);

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                quit = 1;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!e.key.repeat && !handle_key(v, &e.key, &want_shot)) {
                    quit = 1;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    v->scene.angle -= e.motion.xrel * 0.5f;
                    v->scene.elevation -= e.motion.yrel * 0.5f;
                    if (v->scene.elevation > 85.0f) {
                        v->scene.elevation = 85.0f;
                    }
                    if (v->scene.elevation < -85.0f) {
                        v->scene.elevation = -85.0f;
                    }
                    v->scene.need_view_update = 1;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                v->scene.zoom *= 1.0f - e.wheel.y * 0.1f;
                if (v->scene.zoom < 0.2f) {
                    v->scene.zoom = 0.2f;
                }
                if (v->scene.zoom > 6.0f) {
                    v->scene.zoom = 6.0f;
                }
                v->scene.need_view_update = 1;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                int w = 0;
                int h = 0;
                SDL_GetWindowSizeInPixels(window, &w, &h);
                if (w > 0 && h > 0) {
                    v->scene.width = w;
                    v->scene.height = h;
                    gx_gl_set_size(w, h);
                    v->scene.need_view_update = 1;
                }
                break;
            }
            default:
                break;
            }
        }

        if (spin != 0.0f) {
            v->scene.angle += spin;
            v->scene.need_view_update = 1;
        }
        render_scene_draw(&v->scene);
        draws = gx_gl_render_frame();
        {
            const GxHleDraw* list = NULL;
            size_t count = 0;
            gx_hle_get_frame(NULL, NULL, &list, &count, NULL, NULL);
            v->draw_total = (int) count;
        }
        draw_hud(v);
        ++frame_count;
        if (want_shot || (shot != NULL && quit) ||
            (shot != NULL && frames > 0 && frame_count >= frames)) {
            const char* path = shot != NULL ? shot : "viewer.bmp";
            if (gx_gl_save_bmp(path)) {
                printf("viewer: wrote %s\n", path);
            } else {
                fprintf(stderr, "viewer: screenshot failed\n");
            }
            want_shot = 0;
        }
        SDL_GL_SwapWindow(window);

        if (frames > 0 && frame_count >= frames) {
            print_status(v);
            break;
        }
        if ((frame_count % 60) == 0) {
            print_status(v);
        }
    }
    printf("viewer: %d frames, %d draws\n", frame_count, draws);

    hud_shutdown();
    render_scene_close(&v->scene);
    gx_gl_shutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

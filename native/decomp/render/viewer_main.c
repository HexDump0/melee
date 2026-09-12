/*
 * Interactive compiled-path viewer (P-611).
 *
 * Runs the same scene/capture as tests/test_decomp_render.c but presents it
 * in an SDL3 window with orbit, zoom, model cycling and display toggles.
 * SDL owns the window and the EGL/GLES3 context (ADR-0014); gx_gl attaches
 * to it and renders the captured GX frame.
 *
 * Usage:
 *   melee_decomp_viewer [--disc PATH] [--model NAME] [--width N] [--height N]
 *                       [--angle DEG] [--elevation DEG] [--zoom F]
 *                       [--frames N] [--shot FILE] [--hidden] [--no-lights]
 *
 * Keys: drag orbit, wheel zoom, N/P model, B slot, V variant, Y hidden,
 *       L lights, T textures, F12 screenshot, R reset, ESC quit.
 */
#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"
#include "decomp/render/render_scene.h"

#define MAX_MODEL_ATTEMPTS 64

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--disc PATH] [--model NAME] [--width N] [--height N]\n"
            "          [--angle DEG] [--elevation DEG] [--zoom F]\n"
            "          [--frames N] [--shot FILE] [--hidden] [--no-lights]\n",
            argv0);
}

static void print_status(const RenderScene* scene, const GxGlOptions* gl)
{
    printf("[viewer] %s  angle=%.0f elev=%.0f zoom=%.2f  slot=%d variant=%d "
           "hidden=%s  textures=%s lights=%s  %d draws, %d hidden DObjs\n",
           scene->model, (double) scene->angle, (double) scene->elevation,
           (double) scene->zoom, scene->vis_slot, scene->vis_variant,
           scene->show_hidden ? "shown" : "game", gl->textures ? "on" : "off",
           gl->lighting ? "on" : "off",
           (int) gx_hle_display_list_count(), scene->hidden_dobjs);
}

static int handle_key(RenderScene* scene, GxGlOptions* gl, SDL_Keycode key,
                      int* want_shot)
{
    switch (key) {
    case SDLK_ESCAPE:
        return 0;
    case SDLK_N:
    case SDLK_P: {
        char error[256];
        if (!render_scene_cycle(scene, key == SDLK_N ? 1 : -1, error,
                                sizeof(error))) {
            fprintf(stderr, "[viewer] no other model loads: %s\n", error);
        }
        break;
    }
    case SDLK_B:
        scene->vis_slot = (scene->vis_slot + 1) % 4;
        render_scene_update_visibility(scene);
        break;
    case SDLK_V:
        scene->vis_variant = (scene->vis_variant + 1) % 4;
        render_scene_update_visibility(scene);
        break;
    case SDLK_Y:
        scene->show_hidden = !scene->show_hidden;
        render_scene_update_visibility(scene);
        break;
    case SDLK_L:
        gl->lighting = !gl->lighting;
        gx_gl_set_options(gl);
        break;
    case SDLK_T:
        gl->textures = !gl->textures;
        gx_gl_set_options(gl);
        break;
    case SDLK_R:
        scene->angle = 25.0f;
        scene->elevation = -12.0f;
        scene->zoom = 1.0f;
        scene->need_view_update = 1;
        break;
    case SDLK_F12:
        *want_shot = 1;
        break;
    default:
        break;
    }
    return 1;
}

int main(int argc, char** argv)
{
    RenderSceneOptions opt;
    RenderScene scene;
    GxGlOptions gl = { 1, 1, -1 };
    const char* shot = NULL;
    int width = 1280;
    int height = 800;
    int frames = 0;
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
    opt.disc = RENDER_SCENE_DISC_DEFAULT;
    opt.angle = 25.0f;
    opt.elevation = -12.0f;
    opt.zoom = 1.0f;
    opt.scale_override = -1.0f;

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
        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        printf("viewer: requested %dx%d, drawable %dx%d\n", width, height, dw,
               dh);
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
    gx_gl_set_options(&gl);

    if (!render_scene_boot()) {
        fprintf(stderr, "viewer: HSD bootstrap failed\n");
        return 1;
    }
    loaded = render_scene_open(&scene, &opt, error, sizeof(error));
    if (loaded == 0) {
        printf("viewer: SKIP (%s: %s)\n", opt.disc, error);
        return 0;
    }
    if (loaded < 0) {
        fprintf(stderr, "viewer: load failed: %s\n", error);
        return 1;
    }
    printf("viewer: drag=orbit wheel=zoom N/P=model B=slot V=variant "
           "Y=hidden L=lights T=textures F12=shot R=reset ESC=quit\n");

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                quit = 1;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!e.key.repeat &&
                    !handle_key(&scene, &gl, e.key.key, &want_shot)) {
                    quit = 1;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (e.motion.state & SDL_BUTTON_LMASK) {
                    scene.angle -= e.motion.xrel * 0.5f;
                    scene.elevation += e.motion.yrel * 0.5f;
                    if (scene.elevation > 85.0f) {
                        scene.elevation = 85.0f;
                    }
                    if (scene.elevation < -85.0f) {
                        scene.elevation = -85.0f;
                    }
                    scene.need_view_update = 1;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                scene.zoom *= 1.0f - e.wheel.y * 0.1f;
                if (scene.zoom < 0.2f) {
                    scene.zoom = 0.2f;
                }
                if (scene.zoom > 6.0f) {
                    scene.zoom = 6.0f;
                }
                scene.need_view_update = 1;
                break;
            case SDL_EVENT_WINDOW_RESIZED: {
                int w = e.window.data1;
                int h = e.window.data2;
                if (w > 0 && h > 0) {
                    scene.width = w;
                    scene.height = h;
                    gx_gl_set_size(w, h);
                    scene.need_view_update = 1;
                }
                break;
            }
            default:
                break;
            }
        }

        render_scene_draw(&scene);
        draws = gx_gl_render_frame();
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
            print_status(&scene, &gl);
            break;
        }
        if ((frame_count % 60) == 0) {
            print_status(&scene, &gl);
        }
    }
    printf("viewer: %d frames, %d draws\n", frame_count, draws);

    render_scene_close(&scene);
    gx_gl_shutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

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
 *                       [--match [FRAME]]   live compiled match (S4)
 *                       [--record FILE|-] [--record-every N]  PPM frames
 *
 * `--match` runs the compiled game itself (Link vs Mario, Final Destination)
 * with scripted PAD input and presents every VI frame.  With no `--frames`
 * it plays until ESC/window close, paced at 60 Hz, looping the input script
 * so a full match stays in action.  `--record` streams raw PPM frames
 * (concatenated P6) for ffmpeg; `-` writes to stdout.
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
#include <unistd.h>

#include "decomp/assets/hsd_convert.h"
#include "decomp/boot/boot_triage.h"
#include "decomp/boot/match_boot.h"
#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"
#include "decomp/render/hud.h"
#include "decomp/render/render_scene.h"
#include "platform/platform.h"

extern int gm_main(void);

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
           v->scene.stage_mode ? v->scene.stage : v->scene.model,
           (double) v->scene.angle, (double) v->scene.elevation,
           (double) v->scene.zoom, v->part + 1, v->draw_total,
           part_mode_name(v->part_mode), v->scene.vis_slot,
           v->scene.vis_variant, v->scene.show_hidden ? "shown" : "game",
           v->gl.textures ? "on" : "off", v->gl.lighting ? "on" : "off",
           v->gl.wireframe ? "on" : "off", (int) v->draw_total,
           v->scene.hidden_dobjs);
    if (v->scene.stage_mode && v->scene.have_fighter) {
        if (v->scene.stage_map < 0) {
            printf("[viewer] map=ALL(cam %d) fighter=%s%s  camera=%s\n",
                   v->scene.stage_camera_map, v->scene.fighter_name,
                   v->scene.show_fighter ? "" : " (hidden)",
                   v->scene.stage_camera ? "stage" : "orbit");
        } else {
            printf("[viewer] map=%d fighter=%s%s  camera=%s\n",
                   v->scene.stage_map, v->scene.fighter_name,
                   v->scene.show_fighter ? "" : " (hidden)",
                   v->scene.stage_camera ? "stage" : "orbit");
        }
    }
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
    hud_printf(10.0f, 30.0f, 1.5f, "%s %s",
               v->scene.stage_mode ? "STAGE" : "MODEL",
               v->scene.stage_mode ? v->scene.stage : v->scene.model);
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
    if (v->scene.free_cam) {
        hud_printf(10.0f, 104.0f, 1.4f, "FREECAM %.1f %.1f %.1f",
                   (double) v->scene.free_pos[0],
                   (double) v->scene.free_pos[1],
                   (double) v->scene.free_pos[2]);
    } else {
        hud_printf(10.0f, 104.0f, 1.4f, "ANGLE %.0f ELEV %.0f ZOOM %.2f",
                   (double) v->scene.angle, (double) v->scene.elevation,
                   (double) v->scene.zoom);
    }
    if (v->scene.stage_mode) {
        char mapinfo[24];
        if (v->scene.stage_map < 0) {
            snprintf(mapinfo, sizeof(mapinfo), "ALL/CAM%d",
                     v->scene.stage_camera_map);
        } else {
            snprintf(mapinfo, sizeof(mapinfo), "%d", v->scene.stage_map);
        }
        hud_set_color(0.95f, 0.8f, 0.7f, 1.0f);
        hud_printf(10.0f, 122.0f, 1.4f, "MAP %s FIGHTER %s %s  CAMERA %s",
                   mapinfo,
                   v->scene.have_fighter ? v->scene.fighter_name : "(none)",
                   v->scene.show_fighter ? "ON" : "OFF",
                   v->scene.stage_camera ? "STAGE" : "ORBIT");
    }
    hud_set_color(0.65f, 0.72f, 0.82f, 1.0f);
    hud_printf(10.0f, (float) v->scene.height - 22.0f, 1.2f,
               "DRAG LOOK WHEEL ZOOM N/P NEXT , . MAP M MODE F FIGHTER "
               "K CAMERA G FREECAM WASD/QE FLY [ ] PART V MODE Y HIDDEN "
               "L LIGHT T TEX W WIRE C CULL H HUD F12 SHOT R RESET ESC");
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
    case SDLK_M: {
        char error[256];
        if (!render_scene_toggle_mode(&v->scene, error, sizeof(error))) {
            fprintf(stderr, "[viewer] mode switch failed: %s\n", error);
        }
        v->part = 0;
        update_part_filter(v);
        break;
    }
    case SDLK_F:
        render_scene_toggle_fighter(&v->scene);
        break;
    case SDLK_K:
        v->scene.stage_camera = !v->scene.stage_camera;
        v->scene.need_view_update = 1;
        break;
    case SDLK_COMMA:
    case SDLK_PERIOD: {
        char error[256];
        if (!render_scene_cycle_map(&v->scene, code == SDLK_PERIOD ? 1 : -1,
                                    error, sizeof(error))) {
            fprintf(stderr, "[viewer] no other map loads: %s\n", error);
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
        if (!v->scene.free_cam) {
            v->gl.wireframe = !v->gl.wireframe;
            gx_gl_set_options(&v->gl);
        }
        break;
    case SDLK_G:
        render_scene_set_free_cam(&v->scene, !v->scene.free_cam);
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
            "usage: %s [--disc PATH] [--model NAME] [--stage NAME]\n"
            "          [--fighter NAME] [--stage-map N] [--stage-cam]\n"
            "          [--no-fighter] [--width N] [--height N]\n"
            "          [--angle DEG] [--elevation DEG] [--zoom F]\n"
            "          [--frames N] [--shot FILE] [--hidden] [--no-lights]\n"
            "          [--match [FRAME]] [--record FILE|-] [--record-every N]\n"
            "          [--dump-draws FRAME]\n"
            "          [--unlit] [--wire] [--no-hud] [--cycle N] [--spin DEG]\n"
            "          [--cycle-maps N] [--freecam]\n"
            "          [--no-cull] [--no-alpha-test] [--part N] [--part-mode "
            "all|only|hide]\n",
            argv0);
}

/*
 * Live match mode (S4): runs the compiled game's own main() in this process
 * and presents the GX HLE frame captured during each game frame.  The game
 * owns the simulation; this process only supplies the window, the GL context
 * and the VI-frame present hook.
 */
typedef struct MatchView {
    SDL_Window* window;
    SDL_GLContext context;
    const char* shot;
    FILE* record;
    Uint64 start_ns;
    unsigned record_every;
    unsigned dump_frame;
    Uint64 last_render_ns;
    Uint64 swap_start;
    Uint64 swap_ns;
    int shot_written;
    unsigned frames;
    unsigned limit;
    int quit;
} MatchView;

static MatchView match_view;

/* Debug aid: list the captured frame's draws with their texture bindings,
 * blend state and normalised-device bounding box (find runaway quads/fighters
 * without re-running the viewer per draw). */
static void dump_draws(unsigned frame)
{
    const GxHleVertex* vertices = NULL;
    const GxHleDraw* draws = NULL;
    GxHleTexture* textures = NULL;
    size_t vertex_count = 0;
    size_t draw_count = 0;
    size_t texture_count = 0;
    size_t i;

    gx_hle_get_frame(&vertices, &vertex_count, &draws, &draw_count,
                     &textures, &texture_count);
    fprintf(stderr, "[draws] frame %u: %zu draws, %zu verts, %zu textures\n",
            frame, draw_count, vertex_count, texture_count);
    for (i = 0; i < draw_count; ++i) {
        const GxHleDraw* d = &draws[i];
        float min_x = 1e9f;
        float max_x = -1e9f;
        float min_y = 1e9f;
        float max_y = -1e9f;
        size_t v;
        if (d->kind == GX_HLE_DRAW_COPY_TEX) {
            fprintf(stderr, "[draw %zu] copytex %ux%u\n", i, d->copy_w,
                    d->copy_h);
            continue;
        }
        for (v = d->first_vertex; v < d->first_vertex + d->vertex_count;
             ++v) {
            float w = vertices[v].clip[3];
            float x = vertices[v].clip[0] / (w != 0.0f ? w : 1.0f);
            float y = vertices[v].clip[1] / (w != 0.0f ? w : 1.0f);
            if (x < min_x) {
                min_x = x;
            }
            if (x > max_x) {
                max_x = x;
            }
            if (y < min_y) {
                min_y = y;
            }
            if (y > max_y) {
                max_y = y;
            }
        }
        fprintf(stderr,
                "[draw %zu] v=%zu tex0=%d tex1=%d blend=%d/%d/%d z=%d/%d "
                "ndc x[%.2f,%.2f] y[%.2f,%.2f]\n",
                i, d->vertex_count, d->state.texmap[0], d->state.texmap[1],
                d->state.blend_type, d->state.blend_src, d->state.blend_dst,
                d->state.z_enable, d->state.z_func, min_x, max_x, min_y,
                max_y);
        if (d->state.texmap[0] >= 0 &&
            (size_t) d->state.texmap[0] < texture_count)
        {
            const GxHleTexture* t = &textures[d->state.texmap[0]];
            fprintf(stderr, "    tex0: %p %ux%u fmt=%u pal=%p\n", t->image,
                    t->width, t->height, t->format, t->palette);
        }
    }
}

static void match_present(void)
{
    SDL_Event e;

    if (match_view.quit) {
        SDL_GL_DestroyContext(match_view.context);
        SDL_DestroyWindow(match_view.window);
        SDL_Quit();
        exit(0);
    }
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            match_view.quit = 1;
        } else if (e.type == SDL_EVENT_KEY_DOWN &&
                   e.key.key == SDLK_ESCAPE)
        {
            match_view.quit = 1;
        }
    }
    if (match_view.quit) {
        return;
    }

    match_view.frames++;
    {
        Uint64 frame_start = SDL_GetTicksNS();
        int draws = gx_gl_render_frame();
        match_view.last_render_ns = SDL_GetTicksNS() - frame_start;
        if ((match_view.frames % 30) == 0) {
            fprintf(stderr,
                    "[match] frame %u draws=%d render=%.2fms\n",
                    match_view.frames, draws,
                    (double) match_view.last_render_ns / 1e6);
        }
    }
    if (match_view.dump_frame != 0 &&
        match_view.dump_frame == match_view.frames)
    {
        dump_draws(match_view.frames);
    }
    if (match_view.record != NULL &&
        (match_view.frames % match_view.record_every) == 0)
    {
        gx_gl_write_ppm(match_view.record);
        fflush(match_view.record);
    }
    if (match_view.shot != NULL && !match_view.shot_written &&
        match_view.limit != 0 && match_view.frames >= match_view.limit)
    {
        match_view.shot_written = gx_gl_save_bmp(match_view.shot);
    }
    match_view.swap_start = SDL_GetTicksNS();
    SDL_GL_SwapWindow(match_view.window);
    match_view.swap_ns = SDL_GetTicksNS() - match_view.swap_start;
    gx_hle_begin_frame();

    /* Interactive sessions run at the GameCube's 60 Hz regardless of the
     * display refresh; capture/record runs stay unthrottled.  When the swap
     * already blocks for a refresh (vsync), adding our own delay would fight
     * the compositor and stutter, so only pace when the swap returned
     * quickly.  Never burst-catch-up after a slow frame: re-anchor the clock
     * instead of running the next frames fast. */
    if (match_view.record == NULL && match_view.shot == NULL &&
        match_view.swap_ns < 12000000ull) {
        const Uint64 period = 1000000000ull / 60u;
        Uint64 target;
        Uint64 now;
        if (match_view.start_ns == 0) {
            match_view.start_ns = SDL_GetTicksNS();
        }
        target = match_view.start_ns +
                 (Uint64) match_view.frames * period;
        now = SDL_GetTicksNS();
        if (target > now) {
            SDL_DelayNS(target - now);
        } else if (now - target > 2 * period) {
            match_view.start_ns = now - (Uint64) match_view.frames * period;
        }
    }

    if (match_view.limit != 0 && match_view.frames >= match_view.limit) {
        SDL_GL_DestroyContext(match_view.context);
        SDL_DestroyWindow(match_view.window);
        SDL_Quit();
        exit(0);
    }
}

static int run_match(SDL_Window* window, SDL_GLContext context,
                     const char* shot, FILE* record, unsigned record_every,
                     unsigned dump_frame, unsigned match_frame, unsigned limit,
                     GxGlOptions* gl)
{
    FILE* devnull = fopen("/dev/null", "w");

    match_view.window = window;
    match_view.context = context;
    match_view.shot = shot;
    match_view.record = record;
    match_view.start_ns = 0;
    match_view.record_every = record_every != 0 ? record_every : 1;
    match_view.dump_frame = dump_frame;
    match_view.shot_written = 0;
    match_view.frames = 0;
    match_view.limit = limit;
    match_view.quit = 0;

    boot_triage_init(devnull != NULL ? devnull : stderr, 0, 0);
    boot_triage_set_frame_budget(limit != 0 ? limit + 240 : 0);
    hsd_asset_set_register_hook(gx_hle_register_asset);
    gx_gl_set_options(gl);
    boot_platform_set_present_hook(match_present);
    match_boot_init(match_frame);
    /* The S4 script is deterministic but finite; loop it so a full match
     * keeps playing until the window is closed. */
    pad_set_input_loop(1);
    fprintf(stderr,
            "viewer: match mode: compiled game, real camera, scripted PAD "
            "input%s%s\n",
            limit != 0 ? "" : " (looping, 60 Hz, ESC quits)",
            record != NULL ? " (recording)" : "");
    gm_main();
    return 0;
}

int main(int argc, char** argv)
{
    RenderSceneOptions opt;
    Viewer viewer;
    Viewer* v = &viewer;
    const char* shot = NULL;
    const char* record_path = NULL;
    FILE* record = NULL;
    unsigned record_every = 1;
    unsigned dump_frame = 0;
    int width = 1280;
    int height = 800;
    int frames = 0;
    int cycle = 0;
    int map_cycle = 0;
    int toggle_mode = 0;
    int freecam = 0;
    float spin = 0.0f;
    int hidden = 0;
    int want_shot = 0;
    int match_mode = 0;
    unsigned match_frame = 20;
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
    opt.stage_map = -1; /* all maps (the game's stage layout) */
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
        } else if (strcmp(argv[i], "--match") == 0) {
            match_mode = 1;
            if ((int) i + 1 < argc && argv[i + 1][0] != '-') {
                match_frame = (unsigned) strtoul(argv[++i], NULL, 0);
            }
        } else if (strcmp(argv[i], "--record") == 0 &&
                   (int) i + 1 < argc) {
            record_path = argv[++i];
        } else if (strcmp(argv[i], "--record-every") == 0 &&
                   (int) i + 1 < argc) {
            record_every = (unsigned) strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--dump-draws") == 0 &&
                   (int) i + 1 < argc) {
            dump_frame = (unsigned) strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--stage") == 0 && (int) i + 1 < argc) {
            opt.stage = argv[++i];
        } else if (strcmp(argv[i], "--fighter") == 0 && (int) i + 1 < argc) {
            opt.fighter = argv[++i];
        } else if (strcmp(argv[i], "--stage-map") == 0 &&
                   (int) i + 1 < argc) {
            opt.stage_map = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--stage-cam") == 0) {
            opt.stage_camera = 1;
        } else if (strcmp(argv[i], "--freecam") == 0) {
            freecam = 1;
        } else if (strcmp(argv[i], "--no-fighter") == 0) {
            opt.no_fighter = 1;
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
        } else if (strcmp(argv[i], "--cycle-maps") == 0 &&
                   (int) i + 1 < argc) {
            map_cycle = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--toggle-mode") == 0 &&
                   (int) i + 1 < argc) {
            toggle_mode = atoi(argv[++i]);
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

    if (match_mode && record_path != NULL && strcmp(record_path, "-") == 0) {
        /* Keep stdout clean for the PPM pipe: move logs to /dev/null and
         * hand the original fd to the recorder before anything prints. */
        int fd;
        fflush(stdout);
        fd = dup(STDOUT_FILENO);
        if (fd >= 0) {
            freopen("/dev/null", "w", stdout);
            record = fdopen(fd, "wb");
        }
        if (record == NULL) {
            fprintf(stderr, "viewer: cannot open record stdout\n");
            return 1;
        }
    } else if (match_mode && record_path != NULL) {
        record = fopen(record_path, "wb");
        if (record == NULL) {
            fprintf(stderr, "viewer: cannot open record output %s\n",
                    record_path);
            return 1;
        }
    }

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
    if (match_mode) {
        int status;
        /* The HUD's part-isolation toggles are capture-side GL options, so
         * they work in match mode too (find a draw in a live scene). */
        if (v->part_mode == 1) {
            v->gl.only_draw = v->part;
        } else if (v->part_mode == 2) {
            v->gl.hide_draw = v->part;
        }
        status = run_match(window, context, shot, record, record_every,
                           dump_frame, match_frame, (unsigned) frames,
                           &v->gl);
        if (record != NULL) {
            fclose(record);
        }
        return status;
    }

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
    while (map_cycle-- > 0) {
        char map_error[256];
        if (!render_scene_cycle_map(&v->scene, 1, map_error,
                                    sizeof(map_error))) {
            fprintf(stderr, "viewer: map cycle failed: %s\n", map_error);
            break;
        }
    }
    while (toggle_mode-- > 0) {
        char toggle_error[256];
        if (!render_scene_toggle_mode(&v->scene, toggle_error,
                                      sizeof(toggle_error))) {
            fprintf(stderr, "viewer: mode toggle failed: %s\n",
                    toggle_error);
            break;
        }
    }
    if (freecam) {
        render_scene_set_free_cam(&v->scene, 1);
    }
    printf("viewer: drag=orbit wheel=zoom N/P=next M=mode F=fighter "
           "K=camera [ ]=part V=mode shift+V=variant B=slot Y=hidden "
           "L=lights T=textures W=wire H=hud F12=shot R=reset ESC=quit\n");
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
                    /* Dragging means the user wants orbit control even if the
                     * stage camera was active. */
                    if (v->scene.stage_camera) {
                        v->scene.stage_camera = 0;
                    }
                    if (v->scene.free_cam) {
                        render_scene_freecam_look(&v->scene,
                                                  e.motion.xrel * 0.3f,
                                                  -e.motion.yrel * 0.3f);
                        break;
                    }
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
        if (v->scene.free_cam) {
            const bool* keys = SDL_GetKeyboardState(NULL);
            float forward = (keys[SDL_SCANCODE_W] ? 1.0f : 0.0f) -
                            (keys[SDL_SCANCODE_S] ? 1.0f : 0.0f);
            float strafe = (keys[SDL_SCANCODE_D] ? 1.0f : 0.0f) -
                           (keys[SDL_SCANCODE_A] ? 1.0f : 0.0f);
            float vertical = (keys[SDL_SCANCODE_E] ? 1.0f : 0.0f) -
                             (keys[SDL_SCANCODE_Q] ? 1.0f : 0.0f);
            float speed = 1.2f * (keys[SDL_SCANCODE_LSHIFT] ||
                                          keys[SDL_SCANCODE_RSHIFT]
                                      ? 5.0f
                                      : 1.0f);
            if (forward != 0.0f || strafe != 0.0f || vertical != 0.0f) {
                render_scene_freecam_move(&v->scene, forward, strafe, vertical,
                                          speed);
            }
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

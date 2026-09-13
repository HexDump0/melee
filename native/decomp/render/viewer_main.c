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
#include <time.h>
#include <unistd.h>

#include "decomp/assets/hsd_convert.h"
#include "decomp/boot/boot_triage.h"
#include "decomp/boot/match_boot.h"
#include "decomp/gx/gx_gl.h"
#include "decomp/gx/gx_hle.h"
#include "decomp/render/hud.h"
#include <sysdolphin/baselib/state.h>

#include "decomp/render/render_scene.h"
#include "decomp/render/sdl_audio.h"
#include "platform/platform.h"

#include <dolphin/pad.h>
#include <melee/db/db.h>
#include <melee/gm/gm_1A3F.h>

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
            "          [--match [FRAME]] [--frontend] [--input FILE]\n"
            "          [--no-items] [--items] [--record FILE|-]\n"
            "          [--record-every N]\n"
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
    Uint64 last_present_ns;
    Uint64 max_interval_ns;
    Uint64 game_start_ns;
    Uint64 game_ns;
    Uint64 sleep_ns;
    Uint64 last_cpu_ns;
    Uint64 cpu_ns;
    size_t last_verts;
    Uint64 swap_start;
    Uint64 swap_ns;
    int shot_written;
    unsigned frames;
    unsigned limit;
    int quit;
    /* S6 frontend mode: run the retail flow with live or scripted input. */
    int frontend;
    int live_input;
    int no_items;
    PadInputFrame live[4];
    unsigned last_mode;
    unsigned last_scene;
} MatchView;

static MatchView match_view;

/*
 * Frontend input
 * ---------------
 * Live mode maps the keyboard to PAD channel 0 (Enter/Start, Z/A, X/B, C/X,
 * V/Y, A/L, S/R, Q/Z, arrows stick) and IJKL/F/G to channel 1.  Capture mode
 * reads a small text script instead:
 *
 *   channels <1-4>              # connected controller count (default 1)
 *   <frame> <chan|*> <buttons> [stick_x stick_y [cstick_x cstick_y
 *                                              [trigger_l trigger_r]]]
 *
 * `buttons` is a comma list of a,b,x,y,z,l,r,start,up,down,left,right or `-`
 * for none.  An event holds until the next event for that channel; frames
 * before the first event are neutral.  Example (press Start on P1 at 300,
 * then A at 340):
 *
 *   channels 1
 *   300 * start
 *   305 * -
 *   340 * a
 *   345 * -
 */
#define FRONTEND_MAX_EVENTS 1024

typedef struct FrontendEvent {
    unsigned frame;
    unsigned channel; /* 4 = every connected channel */
    PadInputFrame pad;
} FrontendEvent;

static int parse_pad_buttons(const char* names, unsigned short* out)
{
    char buf[64];
    char* tok;
    char* save = NULL;
    unsigned short bits = 0;

    if (names == NULL || strcmp(names, "-") == 0 || strcmp(names, "none") == 0)
    {
        *out = 0;
        return 1;
    }
    snprintf(buf, sizeof(buf), "%s", names);
    for (tok = strtok_r(buf, ",", &save); tok != NULL;
         tok = strtok_r(NULL, ",", &save))
    {
        if (strcmp(tok, "left") == 0) {
            bits |= PAD_BUTTON_LEFT;
        } else if (strcmp(tok, "right") == 0) {
            bits |= PAD_BUTTON_RIGHT;
        } else if (strcmp(tok, "down") == 0) {
            bits |= PAD_BUTTON_DOWN;
        } else if (strcmp(tok, "up") == 0) {
            bits |= PAD_BUTTON_UP;
        } else if (strcmp(tok, "z") == 0) {
            bits |= PAD_TRIGGER_Z;
        } else if (strcmp(tok, "r") == 0) {
            bits |= PAD_TRIGGER_R;
        } else if (strcmp(tok, "l") == 0) {
            bits |= PAD_TRIGGER_L;
        } else if (strcmp(tok, "a") == 0) {
            bits |= PAD_BUTTON_A;
        } else if (strcmp(tok, "b") == 0) {
            bits |= PAD_BUTTON_B;
        } else if (strcmp(tok, "x") == 0) {
            bits |= PAD_BUTTON_X;
        } else if (strcmp(tok, "y") == 0) {
            bits |= PAD_BUTTON_Y;
        } else if (strcmp(tok, "start") == 0) {
            bits |= PAD_BUTTON_START;
        } else {
            return 0;
        }
    }
    *out = bits;
    return 1;
}

static PadInputFrame* frontend_load_script(const char* path, unsigned total,
                                           unsigned* channels_out)
{
    FILE* f = fopen(path, "r");
    static FrontendEvent events[FRONTEND_MAX_EVENTS];
    int event_count = 0;
    unsigned channels = 1;
    char line[256];
    unsigned i;
    PadInputFrame* frames;

    if (f == NULL) {
        fprintf(stderr, "viewer: cannot open input script %s\n", path);
        return NULL;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char chan_tok[8];
        char buttons[64];
        unsigned fr;
        unsigned channel;
        int sx = 0;
        int sy = 0;
        int cx = 0;
        int cy = 0;
        int tl = 0;
        int tr = 0;
        int n;
        char* hash = strchr(line, '#');
        PadInputFrame* pad;

        if (hash != NULL) {
            *hash = '\0';
        }
        if (line[0] == '\0' || line[0] == '\n') {
            continue;
        }
        if (strncmp(line, "channels", 8) == 0) {
            unsigned value;
            if (sscanf(line + 8, "%u", &value) == 1 && value >= 1 &&
                value <= 4)
            {
                channels = value;
            }
            continue;
        }
        chan_tok[0] = '\0';
        buttons[0] = '\0';
        n = sscanf(line, "%u %7s %63s %d %d %d %d %d %d", &fr, chan_tok,
                   buttons, &sx, &sy, &cx, &cy, &tl, &tr);
        if (n < 3) {
            fprintf(stderr, "viewer: input: bad line: %s\n", line);
            fclose(f);
            return NULL;
        }
        if (strcmp(chan_tok, "*") == 0) {
            channel = 4;
        } else {
            channel = (unsigned) atoi(chan_tok);
            if (channel > 3) {
                fprintf(stderr, "viewer: input: bad channel: %s\n", chan_tok);
                fclose(f);
                return NULL;
            }
        }
        if (event_count >= FRONTEND_MAX_EVENTS) {
            break;
        }
        pad = &events[event_count].pad;
        memset(pad, 0, sizeof(*pad));
        if (!parse_pad_buttons(buttons, &pad->buttons)) {
            fprintf(stderr, "viewer: input: unknown buttons: %s\n", buttons);
            fclose(f);
            return NULL;
        }
        pad->stick_x = (signed char) sx;
        pad->stick_y = (signed char) sy;
        pad->cstick_x = (signed char) cx;
        pad->cstick_y = (signed char) cy;
        pad->trigger_l = (unsigned char) (tl < 0 ? 0 : tl > 255 ? 255 : tl);
        pad->trigger_r = (unsigned char) (tr < 0 ? 0 : tr > 255 ? 255 : tr);
        events[event_count].frame = fr;
        events[event_count].channel = channel;
        event_count++;
    }
    fclose(f);
    if (event_count == 0) {
        return NULL;
    }
    frames = calloc((size_t) total * channels, sizeof(PadInputFrame));
    if (frames == NULL) {
        return NULL;
    }
    for (i = 0; i < (unsigned) event_count; i++) {
        unsigned start = events[i].frame;
        unsigned end =
            (i + 1 < (unsigned) event_count) ? events[i + 1].frame : total;
        unsigned frame;
        unsigned c;
        if (start >= total) {
            break;
        }
        if (end > total) {
            end = total;
        }
        for (frame = start; frame < end; frame++) {
            if (events[i].channel == 4) {
                for (c = 0; c < channels; c++) {
                    frames[frame * channels + c] = events[i].pad;
                }
            } else if (events[i].channel < channels) {
                frames[frame * channels + events[i].channel] = events[i].pad;
            }
        }
    }
    *channels_out = channels;
    return frames;
}

static void frontend_poll_live(void)
{
    const bool* keys = SDL_GetKeyboardState(NULL);
    PadInputFrame* p0 = &match_view.live[0];
    PadInputFrame* p1 = &match_view.live[1];
    int sx = 0;
    int sy = 0;

    memset(match_view.live, 0, sizeof(match_view.live));
    if (keys[SDL_SCANCODE_LEFT]) {
        sx -= 80;
    }
    if (keys[SDL_SCANCODE_RIGHT]) {
        sx += 80;
    }
    if (keys[SDL_SCANCODE_UP]) {
        sy += 80;
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        sy -= 80;
    }
    p0->stick_x = (signed char) sx;
    p0->stick_y = (signed char) sy;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]) {
        p0->buttons |= PAD_BUTTON_START;
    }
    if (keys[SDL_SCANCODE_Z]) {
        p0->buttons |= PAD_BUTTON_A;
    }
    if (keys[SDL_SCANCODE_X]) {
        p0->buttons |= PAD_BUTTON_B;
    }
    if (keys[SDL_SCANCODE_C]) {
        p0->buttons |= PAD_BUTTON_X;
    }
    if (keys[SDL_SCANCODE_V]) {
        p0->buttons |= PAD_BUTTON_Y;
    }
    if (keys[SDL_SCANCODE_A]) {
        p0->buttons |= PAD_TRIGGER_L;
    }
    if (keys[SDL_SCANCODE_S]) {
        p0->buttons |= PAD_TRIGGER_R;
    }
    if (keys[SDL_SCANCODE_Q]) {
        p0->buttons |= PAD_TRIGGER_Z;
    }
    if (keys[SDL_SCANCODE_J]) {
        p1->stick_x -= 80;
    }
    if (keys[SDL_SCANCODE_L]) {
        p1->stick_x += 80;
    }
    if (keys[SDL_SCANCODE_I]) {
        p1->stick_y += 80;
    }
    if (keys[SDL_SCANCODE_K]) {
        p1->stick_y -= 80;
    }
    if (keys[SDL_SCANCODE_F]) {
        p1->buttons |= PAD_BUTTON_A;
    }
    if (keys[SDL_SCANCODE_G]) {
        p1->buttons |= PAD_BUTTON_B;
    }
    if (keys[SDL_SCANCODE_T]) {
        p1->buttons |= PAD_BUTTON_START;
    }
    pad_set_live_input(match_view.live, 4);
}

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
                "gens=%d ndc x[%.2f,%.2f] y[%.2f,%.2f]",
                i, d->vertex_count, d->state.texmap[0], d->state.texmap[1],
                d->state.blend_type, d->state.blend_src, d->state.blend_dst,
                d->state.z_enable, d->state.z_func,
                (int) d->state.num_texgens, min_x, max_x, min_y, max_y);
        {
            int g;
            for (g = 0; g < (int) d->state.num_texgens && g < 8; ++g) {
                fprintf(stderr, " %d:%d/m%u/p%u", (int) d->state.texgen[g].type,
                        (int) d->state.texgen[g].src,
                        (unsigned) d->state.texgen[g].mtx_id,
                        (unsigned) d->state.texgen[g].postmtx);
            }
        }
        fprintf(stderr, "\n");
        if (d->state.texmap[0] >= 0 &&
            (size_t) d->state.texmap[0] < texture_count)
        {
            const GxHleTexture* t = &textures[d->state.texmap[0]];
            fprintf(stderr, "    tex0: %p %ux%u fmt=%u pal=%p\n", t->image,
                    t->width, t->height, t->format, t->palette);
        }
        if (d->state.texmap[1] >= 0 &&
            (size_t) d->state.texmap[1] < texture_count)
        {
            const GxHleTexture* t = &textures[d->state.texmap[1]];
            fprintf(stderr, "    tex1: %p %ux%u fmt=%u pal=%p\n", t->image,
                    t->width, t->height, t->format, t->palette);
        }
        if (d->state.blend_type != 0) {
            int stage;
            fprintf(stderr,
                    "    alpha: tev C0=%.3f C1=%.3f C2=%.3f K0=%.3f "
                    "ras0_amb=%.3f ras0_mat=%.3f chan=%u/%u\n",
                    d->state.tev_color[1][3], d->state.tev_color[2][3],
                    d->state.tev_color[3][3], d->state.tev_kcolor[0][3],
                    d->state.ch_amb[2][3], d->state.ch_mat[2][3],
                    d->state.ch_enable[2], d->state.ch_mat_src[2]);
            fprintf(stderr,
                    "    ch0: en=%u amb_src=%u mat_src=%u mask=0x%x "
                    "amb=%.3f,%.3f,%.3f,%.3f mat=%.3f,%.3f,%.3f,%.3f "
                    "K0rgb=%.3f,%.3f,%.3f\n",
                    d->state.ch_enable[0], d->state.ch_amb_src[0],
                    d->state.ch_mat_src[0], d->state.ch_light_mask[0],
                    d->state.ch_amb[0][0], d->state.ch_amb[0][1],
                    d->state.ch_amb[0][2], d->state.ch_amb[0][3],
                    d->state.ch_mat[0][0], d->state.ch_mat[0][1],
                    d->state.ch_mat[0][2], d->state.ch_mat[0][3],
                    d->state.tev_kcolor[0][0], d->state.tev_kcolor[0][1],
                    d->state.tev_kcolor[0][2]);
            if (d->vertex_count != 0) {
                const GxHleVertex* v0 = &vertices[d->first_vertex];
                fprintf(stderr,
                        "    v0: color=%u,%u,%u,%u has_color=%.0f "
                        "uv0=%.3f,%.3f uv1=%.3f,%.3f nrm=%.2f,%.2f,%.2f\n",
                        v0->color[0], v0->color[1], v0->color[2], v0->color[3],
                        (double) v0->has_color, (double) v0->uv[0][0],
                        (double) v0->uv[0][1], (double) v0->uv[1][0],
                        (double) v0->uv[1][1], (double) v0->nrm[0],
                        (double) v0->nrm[1], (double) v0->nrm[2]);
                if (d->vertex_count == 6) {
                    size_t k;
                    fprintf(stderr, "    quads:");
                    for (k = 0; k < 6; ++k) {
                        const GxHleVertex* vk = &vertices[d->first_vertex + k];
                        fprintf(stderr, " (%.3f,%.3f|%.3f,%.3f)",
                                (double) vk->uv[0][0], (double) vk->uv[0][1],
                                (double) vk->uv[1][0], (double) vk->uv[1][1]);
                    }
                    fprintf(stderr, "\n");
                }
            }
            for (stage = 0; stage < d->state.num_stages &&
                            stage < GX_HLE_MAX_STAGES;
                 ++stage)
            {
                const GxHleTevStage* s = &d->state.stages[stage];
                fprintf(stderr,
                        "    tev%d order=%u/%u/%u cin=%u,%u,%u,%u "
                        "ain=%u,%u,%u,%u aop=%u/%u/%u/%u reg=%u/%u "
                        "kc=%u ka=%u\n",
                        stage, s->order_coord, s->order_map, s->order_chan,
                        s->color_a, s->color_b, s->color_c, s->color_d,
                        s->alpha_a, s->alpha_b, s->alpha_c, s->alpha_d,
                        s->alpha_op, s->alpha_bias, s->alpha_scale,
                        s->alpha_clamp, s->color_reg, s->alpha_reg,
                        s->kc_sel, s->ka_sel);
            }
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
        } else if (e.type == SDL_EVENT_WINDOW_RESIZED ||
                   e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
        {
            /* A tiling WM may map the window at a different size than requested;
             * without following the actual drawable the fixed-size GL viewport
             * crops/zooms the frame. */
            int w = 0;
            int h = 0;
            SDL_GetWindowSizeInPixels(match_view.window, &w, &h);
            if (w > 0 && h > 0) {
                gx_gl_set_size(w, h);
            }
        }
    }
    if (match_view.quit) {
        return;
    }

    if (match_view.frontend) {
        unsigned mode;
        unsigned scene;
        if (match_view.no_items) {
            /* main() re-enables item spawns after our startup call; keep the
             * game's debug switch off from the first presented frame. */
            db_DisableItemSpawns();
        }
        if (match_view.live_input) {
            frontend_poll_live();
        }
        mode = gm_GetCurrentGameMode();
        scene = gm_GetCurrentSceneIndex();
        if (mode != match_view.last_mode || scene != match_view.last_scene) {
            fprintf(stderr, "[frontend] frame %u mode=%u scene=%u\n",
                    match_view.frames, mode, scene);
            match_view.last_mode = mode;
            match_view.last_scene = scene;
        }
    }

    /* The game owns the camera, so a resize only has to resize the GL target.
     * Poll instead of trusting events: a tiling WM may map/resize the window
     * before the first present, and fractional-scale changes can arrive
     * without a pixel-size event. */
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(match_view.window, &w, &h);
        if (w > 0 && h > 0) {
            gx_gl_set_size(w, h);
        }
    }

    match_view.frames++;
    {
        Uint64 frame_start = SDL_GetTicksNS();
        Uint64 interval = 0;
        int draws;
        if (match_view.last_present_ns != 0) {
            interval = frame_start - match_view.last_present_ns;
            if (interval > match_view.max_interval_ns) {
                match_view.max_interval_ns = interval;
            }
        }
        match_view.game_ns = frame_start - match_view.game_start_ns;
        match_view.last_present_ns = frame_start;
        {
            struct timespec ts;
            Uint64 cpu;
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
            cpu = (Uint64) ts.tv_sec * 1000000000ull + (Uint64) ts.tv_nsec;
            if (match_view.last_cpu_ns != 0) {
                match_view.cpu_ns += cpu - match_view.last_cpu_ns;
            }
            match_view.last_cpu_ns = cpu;
        }
        draws = gx_gl_render_frame();
        match_view.last_render_ns = SDL_GetTicksNS() - frame_start;
        {
            size_t vc = 0;
            gx_hle_get_frame(NULL, &vc, NULL, NULL, NULL, NULL);
            match_view.last_verts = vc;
        }
        if (interval > 25000000ull) {
            fprintf(stderr, "[match] spike frame=%u interval=%.1fms "
                    "game=%.2fms sleep=%.2fms render=%.2fms draws=%d "
                    "verts=%zu\n",
                    match_view.frames, (double) interval / 1e6,
                    (double) match_view.game_ns / 1e6,
                    (double) match_view.sleep_ns / 1e6,
                    (double) match_view.last_render_ns / 1e6, draws,
                    match_view.last_verts);
        }
        if ((match_view.frames % 30) == 0) {
            fprintf(stderr,
                    "[match] frame %u draws=%d verts=%zu lists=%zu prims=%zu "
                    "game=%.2fms cpu=%.2fms sleep=%.2fms render=%.2fms "
                    "frame=%.2fms max=%.2fms\n",
                    match_view.frames, draws, match_view.last_verts,
                    gx_hle_display_list_count(), gx_hle_primitive_count(),
                    (double) match_view.game_ns / 1e6,
                    (double) (match_view.cpu_ns / 30) / 1e6,
                    (double) match_view.sleep_ns / 1e6,
                    (double) match_view.last_render_ns / 1e6,
                    (double) interval / 1e6,
                    (double) match_view.max_interval_ns / 1e6);
            match_view.max_interval_ns = 0;
            match_view.cpu_ns = 0;
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
    /* The backend state reset must invalidate the compiled engine's GX
     * caches, or the shadow pass's first material setter is skipped as
     * "unchanged" and its white background renders with the reset's default
     * (black) channel colour (G-054). */
    HSD_StateInvalidate(-1);

    /* Interactive sessions run at the GameCube's 60 Hz regardless of the
     * display refresh; capture/record runs stay unthrottled.  When the swap
     * already blocks for a refresh (vsync), adding our own delay would fight
     * the compositor and stutter, so only pace when the swap returned
     * quickly.  Never burst-catch-up after a slow frame: re-anchor the clock
     * instead of running the next frames fast. */
    match_view.sleep_ns = 0;
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
            Uint64 sleep_start = SDL_GetTicksNS();
            SDL_DelayNS(target - now);
            match_view.sleep_ns = SDL_GetTicksNS() - sleep_start;
        } else {
            match_view.start_ns = now - (Uint64) match_view.frames * period;
        }
    }
    match_view.game_start_ns = SDL_GetTicksNS();

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
                     GxGlOptions* gl, const char* input_path, int frontend,
                     int no_items)
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
    match_view.frontend = frontend;
    match_view.live_input = frontend && input_path == NULL;
    match_view.no_items = no_items;
    match_view.last_mode = 0xFFFFFFFFu;
    match_view.last_scene = 0xFFFFFFFFu;

    boot_triage_init(
        getenv("MELEE_VIEWER_TRIAGE") != NULL
            ? stderr
            : (devnull != NULL ? devnull : stderr),
        0, 0);
    boot_triage_set_frame_budget(limit != 0 ? limit + 240 : 0);
    hsd_asset_set_register_hook(gx_hle_register_asset);
    gx_gl_set_options(gl);
    boot_platform_set_present_hook(match_present);
    if (no_items) {
        /* The game's own debug item switch for deterministic flow tests. */
        db_DisableItemSpawns();
    }
    if (frontend) {
        if (input_path != NULL) {
            unsigned channels = 0;
            unsigned total = limit != 0 ? limit + 2 : 18000;
            PadInputFrame* script =
                frontend_load_script(input_path, total, &channels);
            if (script == NULL) {
                fprintf(stderr, "viewer: frontend input failed: %s\n",
                        input_path);
                return 1;
            }
            pad_set_input_script(script, channels, total);
            pad_set_input_loop(0);
            fprintf(stderr,
                    "viewer: frontend mode: retail flow, %u-frame input script "
                    "%s (%u channels)\n",
                    total, input_path, channels);
        } else {
            fprintf(stderr,
                    "viewer: frontend mode: retail flow, live input "
                    "(Enter=START Z=A X=B C=X V=Y A=L S=R Q=Z, arrows=stick)\n");
        }
    } else {
        match_boot_init(match_frame);
        /* The S4 script is deterministic but finite; loop it so a full match
         * keeps playing until the window is closed. */
        pad_set_input_loop(1);
        fprintf(stderr,
                "viewer: match mode: compiled game, real camera, scripted PAD "
                "input%s%s\n",
                limit != 0 ? "" : " (looping, 60 Hz, ESC quits)",
                record != NULL ? " (recording)" : "");
    }
    gm_main();
    if (shot != NULL && !match_view.shot_written) {
        match_view.shot_written = gx_gl_save_bmp(shot);
        if (match_view.shot_written) {
            fprintf(stderr, "viewer: wrote %s (game stopped)\n", shot);
        }
    }
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
    int frontend_mode = 0;
    int no_items = 0;
    const char* input_path = NULL;
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
        } else if (strcmp(argv[i], "--frontend") == 0) {
            frontend_mode = 1;
        } else if (strcmp(argv[i], "--no-items") == 0) {
            no_items = 1;
        } else if (strcmp(argv[i], "--items") == 0) {
            no_items = 0;
        } else if (strcmp(argv[i], "--input") == 0 && (int) i + 1 < argc) {
            input_path = argv[++i];
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

    /* The product runs the retail flow from a bare invocation. */
    if (argc == 1 && !match_mode && !frontend_mode) {
        frontend_mode = 1;
        fprintf(stderr,
                "melee: retail frontend (live input; ESC quits).  Use "
                "--help for the development viewer options.\n");
    }
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

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "viewer: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    viewer_audio_init();
    /* A fixed 1280x800 window can exceed a small/HiDPI desktop's usable area,
     * which the compositor then crops.  Fit the default to the display (hidden
     * captures keep their requested resolution for deterministic screenshots). */
    if (!hidden) {
        SDL_Rect bounds;
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (display != 0 && SDL_GetDisplayUsableBounds(display, &bounds)) {
            if (bounds.w > 0 && width > bounds.w) {
                width = bounds.w;
            }
            if (bounds.h > 0 && height > bounds.h) {
                height = bounds.h;
            }
        }
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
    if (match_mode || frontend_mode) {
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
                           &v->gl, input_path, frontend_mode, no_items);
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

        /* Follow the real drawable size every frame.  A tiling WM can resize
         * the window at map time before the event loop runs, and live drags
         * can outpace the resize events; polling keeps the GL viewport and the
         * camera aspect in step with the window. */
        {
            int w = 0;
            int h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            if (w > 0 && h > 0 &&
                (w != v->scene.width || h != v->scene.height))
            {
                v->scene.width = w;
                v->scene.height = h;
                gx_gl_set_size(w, h);
                v->scene.need_view_update = 1;
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

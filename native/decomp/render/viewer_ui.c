/*
 * Interactive model-viewer UI: part filtering, status line, HUD and keys.
 *
 * Split out of viewer_main.c; see viewer_internal.h.
 */
#include "decomp/render/viewer_internal.h"

static const char* part_mode_name(int mode)
{
    static const char* names[3] = { "ALL", "ONLY", "HIDE" };
    return names[mode];
}

void update_part_filter(Viewer* v)
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

void print_status(const Viewer* v)
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

void draw_hud(const Viewer* v)
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

int handle_key(Viewer* v, const SDL_KeyboardEvent* key,
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

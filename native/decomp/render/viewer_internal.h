/*
 * Shared declarations for the viewer and runner translation units.
 *
 * viewer_main.c was 1,647 lines.  Measuring coupling rather than counting
 * lines showed where the seams actually are: `match_view` has 104 references
 * and 98 of them sit inside match_present and run_match, so that state machine
 * stays whole.  What moved out are the parts that barely touch it: the
 * interactive model-viewer UI (0 references, works on Viewer), the draw-list
 * dump (0), and the pad-input plumbing (4).
 */
#ifndef MELEE_VIEWER_INTERNAL_H
#define MELEE_VIEWER_INTERNAL_H

#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "audio/sfx_debug.h"
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
#include <melee/ft/types.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/pl/player.h>

extern int gm_main(void);

typedef struct Viewer {
    RenderScene scene;
    GxGlOptions gl;
    int part;      /* selected draw index */
    int part_mode; /* 0 all, 1 only, 2 hide */
    int hud;
    int draw_total;
} Viewer;

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
    int banner_probed;
    int markers_probed;
    int names_probed;
    int level_probed;
    PadInputFrame live[4];
    unsigned last_mode;
    unsigned last_scene;
} MatchView;

extern MatchView match_view;

void draw_hud(const Viewer* v);
void dump_draws(unsigned frame);
PadInputFrame* frontend_load_script(const char* path, unsigned total, unsigned* channels_out);
void frontend_poll_live(void);
/* --controls: the resolved bindings, without a window. */
void frontend_print_bindings(void);
void frontend_gamepads_changed(void);
int handle_key(Viewer* v, const SDL_KeyboardEvent* key, int* want_shot);
int parse_pad_buttons(const char* names, unsigned short* out);
void print_status(const Viewer* v);
void update_part_filter(Viewer* v);

#endif

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
#include "decomp/render/viewer_internal.h"
#include "mod/mod.h"

/* The registry does not link the GL backend itself (melee_decomp_boot has no
 * renderer at all), so the target that owns a drawable hands it over. */
static const ModDisplayBackend mod_gx_gl_display = { gx_gl_get_window_size,
                                                     gx_gl_set_display_aspect,
                                                     gx_gl_get_display_aspect };

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

MatchView match_view;

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

static void update_sfx_debug_state(void)
{
    MeleeSfxDebugGameState state;
    int slot;

    if (!melee_sfx_debug_enabled()) {
        return;
    }
    memset(&state, 0, sizeof(state));
    state.video_frame = match_view.frames;
    state.mode = gm_GetCurrentGameMode();
    state.scene = gm_GetCurrentSceneIndex();
    for (slot = 0; slot < MELEE_SFX_DEBUG_FIGHTERS; slot++) {
        HSD_GObj* gobj = Player_GetEntity(slot);
        Fighter* fp;
        MeleeSfxDebugFighter* out;

        if (gobj == NULL || gobj->user_data == NULL) {
            continue;
        }
        fp = (Fighter*) gobj->user_data;
        out = &state.fighters[slot];
        out->active = 1;
        out->player = fp->player_id;
        out->kind = fp->kind;
        out->motion = fp->motion_id;
        out->damage = Player_GetDamage(slot);
        out->x = fp->cur_pos.x;
        out->y = fp->cur_pos.y;
    }
    melee_sfx_debug_set_game_state(&state);
}

static void match_present(void)
{
    SDL_Event e;
    /* A drawable change seen this frame, held back until the frame is
     * submitted.  See the deferral note below. */
    static int resize_w, resize_h;

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
        static int last_w, last_h;
        static int trace = -1;
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(match_view.window, &w, &h);
        if (w > 0 && h > 0) {
            gx_gl_set_size(w, h);
            if (w != last_w || h != last_h) {
                if (trace < 0) {
                    trace = getenv("MELEE_WIDESCREEN_TRACE") != NULL;
                }
                if (trace) {
                    /* Logical vs pixel size separates a compositor that has
                     * not told SDL about the new size from one that has and a
                     * fractional scale we are mishandling. */
                    int lw = 0;
                    int lh = 0;
                    SDL_GetWindowSize(match_view.window, &lw, &lh);
                    fprintf(stderr,
                            "[ws] frame %u  sdl logical %dx%d  pixels %dx%d  "
                            "hook %s\n",
                            match_view.frames, lw, lh, w, h,
                            mod_hook_active(UNBOUND_HOOK_DISPLAY_RESIZED)
                                ? "fires"
                                : "NOT INSTALLED");
                }
                resize_w = w;
                resize_h = h;
            }
            last_w = w;
            last_h = h;
        }
    }

    match_view.frames++;
    update_sfx_debug_state();
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
        /*
         * Tell mods about a resize only now, after the frame is on screen.
         *
         * match_present is the *present* hook: the game already built this
         * frame's draw list, and its cameras were widened with whatever
         * factor the display mod held at the time.  Handing the mod a new
         * size before the submit updates that factor and the presentation
         * aspect together -- but only the presentation aspect can still
         * affect this frame, so geometry built for the old aspect gets
         * fitted to the new one and the frame comes out stretched by the
         * ratio between them.  Small on a drag, gross on a fullscreen toggle
         * from a small window, which is exactly where it was reported.
         *
         * Deferring costs one frame fitted to the old aspect inside the new
         * drawable -- bars, briefly, at correct proportions -- and the next
         * frame's cameras and rect agree because they come from the same
         * generation.  gx_gl_set_size still runs immediately above, because
         * the GL target has to match the real surface right away.
         */
        if (resize_h > 0) {
            if (mod_hook_active(UNBOUND_HOOK_DISPLAY_RESIZED)) {
                UnboundDisplayResized size;
                size.width = resize_w;
                size.height = resize_h;
                mod_dispatch(UNBOUND_HOOK_DISPLAY_RESIZED, &size,
                             sizeof(size));
            }
            resize_w = 0;
            resize_h = 0;
        }
        /* P-698: the 1P clear banner ("STAGE CLEAR") is a POBJ_SHAPEANIM mesh
         * blended on the CPU from big-endian pools.  When that read is wrong
         * the mesh collapses and the band over its black backdrop is
         * uniformly black, which is exactly what the owner saw (G-147). */
        /* P-701: the Classic splash's stage-marker chain is a lit-only
         * material whose HSD_PEDesc the converter used to corrupt, so it drew
         * nothing and the row read as bare background (G-148). */
        /* P-700: the CSS level text is drawn by the SIS engine from a
         * vsnprintf'd string.  With the console's unbounded -1 size glibc
         * drops the last character, so "VERY EASY" rendered as "VERY EAS"
         * plus the two glyphs the parser then ran into (G-149).  The broken
         * "TOTAL HIGH SCORE" value is right-aligned, so a dropped character
         * shortens it on the left: probe the strip the leading digit
         * occupies only when the string survives intact. */
        if (!match_view.level_probed && match_boot_classic_active()) {
            static unsigned settle3;
            if (++settle3 > 60) {
                match_view.level_probed = 1;
                fprintf(stderr, "[classic] score_lead=%u\n",
                        gx_gl_probe_white(408, 402, 14, 20));
            }
        }
        if (!match_view.markers_probed && match_boot_intro_active()) {
            static unsigned settle2;
            if (++settle2 > 30) {
                match_view.markers_probed = 1;
                fprintf(stderr, "[intro] markers nonblack=%u\n",
                        gx_gl_probe_nonblack(48, 28, 544, 40));
            }
        }
        /* P-704: the fighter names on the VS splash sit on the row the
         * layout table puts at y=380.  They vanished because fn_80160DE8 read
         * the US name-width table through an out-of-bounds index into its
         * neighbour, which is 0 here, and a 0 width makes the SIS renderer
         * store an x-scale of 0.  Only the US branch does that, so this probe
         * is meaningful only with MELEE_INTRO_US set.  White, not non-black:
         * the splash's dark backdrop reads non-black both with and without the
         * glyphs, so a non-black count does not flip.  Stop at x=280: the
         * white "VS" logo sits at x~298..340 and would count on its own. */
        if (!match_view.names_probed && match_boot_intro_active()) {
            static unsigned settle4;
            if (++settle4 > 45) {
                match_view.names_probed = 1;
                fprintf(stderr, "[intro] names white=%u\n",
                        gx_gl_probe_white(60, 380, 220, 32));
            }
        }
        if (!match_view.banner_probed && match_boot_gameover_active()) {
            static unsigned settle;
            if (++settle > 30) {
                match_view.banner_probed = 1;
                fprintf(stderr, "[gameover] banner nonblack=%u\n",
                        gx_gl_probe_nonblack(64, 16, 512, 88));
            }
        }
        {
            size_t vc = 0;
            gx_hle_get_frame(NULL, &vc, NULL, NULL, NULL, NULL);
            match_view.last_verts = vc;
        }
        if (interval > 25000000ull) {
            /* swap= is the rest of the interval: the time SDL_GL_SwapWindow
             * blocked.  Without it a spike whose game and render are both
             * under a millisecond has no visible cause at all, and the
             * pacing code deliberately reports sleep=0 for exactly those
             * frames (it skips its own delay once the swap has already
             * blocked), which makes them look emptier still. */
            fprintf(stderr, "[match] spike frame=%u interval=%.1fms "
                    "game=%.2fms sleep=%.2fms render=%.2fms swap=%.2fms "
                    "draws=%d verts=%zu\n",
                    match_view.frames, (double) interval / 1e6,
                    (double) match_view.game_ns / 1e6,
                    (double) match_view.sleep_ns / 1e6,
                    (double) match_view.last_render_ns / 1e6,
                    (double) match_view.swap_ns / 1e6, draws,
                    match_view.last_verts);
        }
        if ((match_view.frames % 30) == 0 &&
            getenv("MELEE_GX_TEX_STATS") != NULL)
        {
            extern void gx_gl_texture_stats(unsigned long*, unsigned long*,
                                            unsigned long*, unsigned long*,
                                            unsigned long*, size_t*);
            unsigned long hits = 0, misses = 0, evictions = 0, decodes = 0;
            unsigned long invalidations = 0;
            size_t live = 0;
            gx_gl_texture_stats(&hits, &misses, &evictions, &decodes,
                                &invalidations, &live);
            {
                extern void gx_gl_bind_stats(unsigned long*, unsigned long*);
                unsigned long bt = 0, bs = 0;
                gx_gl_bind_stats(&bt, &bs);
                fprintf(stderr,
                        "[match] texbinds %lu of %lu issued (%.1f%% skipped)\n",
                        bt - bs, bt, bt ? 100.0 * (double) bs / (double) bt : 0.0);
            }
            fprintf(stderr,
                    "[match] texcache hits=%lu misses=%lu evictions=%lu "
                    "decodes=%lu invalidations=%lu live=%zu\n",
                    hits, misses, evictions, decodes, invalidations, live);
        }
        if ((match_view.frames % 30) == 0 &&
            getenv("MELEE_GX_UNI_STATS") != NULL)
        {
            extern void gx_gl_uniform_stats(unsigned long*, unsigned long*);
            unsigned long total = 0, skipped = 0;
            gx_gl_uniform_stats(&total, &skipped);
            fprintf(stderr,
                    "[match] uniform uploads %lu of %lu (%.1f%% skipped)\n",
                    total - skipped, total,
                    total ? 100.0 * (double) skipped / (double) total : 0.0);
        }
        if ((match_view.frames % 30) == 0) {
            extern void gx_gl_bind_stats(unsigned long*, unsigned long*);
            extern void gx_gl_bind_stats_reset(void);
            unsigned long frame_binds_total = 0, frame_binds_skipped = 0;
            unsigned long frame_binds_issued;
            gx_gl_bind_stats(&frame_binds_total, &frame_binds_skipped);
            frame_binds_issued = frame_binds_total - frame_binds_skipped;
            fprintf(stderr,
                    "[match] frame %u draws=%d verts=%zu lists=%zu prims=%zu "
                    "game=%.2fms cpu=%.2fms sleep=%.2fms render=%.2fms "
                    "swap=%.2fms frame=%.2fms max=%.2fms binds=%lu/%lu\n",
                    match_view.frames, draws, match_view.last_verts,
                    gx_hle_display_list_count(), gx_hle_primitive_count(),
                    (double) match_view.game_ns / 1e6,
                    (double) (match_view.cpu_ns / 30) / 1e6,
                    (double) match_view.sleep_ns / 1e6,
                    (double) match_view.last_render_ns / 1e6,
                    (double) match_view.swap_ns / 1e6,
                    (double) interval / 1e6,
                    (double) match_view.max_interval_ns / 1e6,
                    frame_binds_issued, frame_binds_total);
            match_view.max_interval_ns = 0;
            match_view.cpu_ns = 0;
            gx_gl_bind_stats_reset();
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
    match_view.banner_probed = 0;
    match_view.markers_probed = 0;
    match_view.names_probed = 0;
    match_view.level_probed = 0;
    match_view.last_mode = 0xFFFFFFFFu;
    match_view.last_scene = 0xFFFFFFFFu;

    boot_triage_init(
        getenv("MELEE_VIEWER_TRIAGE") != NULL
            ? stderr
            : (devnull != NULL ? devnull : stderr),
        0, 0);
    boot_triage_set_frame_budget(limit != 0 ? limit + 240 : 0);
    hsd_asset_set_register_hook(gx_hle_register_asset);
    /* Before the first frame, so a mod's display settings are in place for
     * it rather than applying one frame late. */
    mod_set_display_backend(&mod_gx_gl_display);
    mod_system_init();
    gx_gl_set_options(gl);
    boot_platform_set_present_hook(match_present);
    if (no_items) {
        /* The game's own debug item switch for deterministic flow tests. */
        db_DisableItemSpawns();
    }
    if (frontend) {
        /* Title/CPU probes run off the frame hook; the frontend flow never
         * enters the debug-match harness, so install only explicit probes.
         *
         * **This list is the second half of adding a probe.** `match_boot_init`
         * arms the hook for whatever variable it sees, but on the frontend it
         * is only *called* when one of these is set -- so a new probe that is
         * not named here is silently dead in exactly the place the owner runs
         * it, while working in every headless test. That cost a round-trip
         * with `MELEE_ANIM_STALL`; add the name here in the same commit. */
        if (getenv("MELEE_TITLE_TEST") != NULL ||
            getenv("MELEE_CPU_TEST") != NULL ||
            getenv("MELEE_STADIUM_TRACE") != NULL ||
            getenv("MELEE_STUCK_TRACE") != NULL ||
            getenv("MELEE_ANIM_STALL") != NULL ||
            getenv("MELEE_RNG_TRACE") != NULL)
        {
            match_boot_init(0);
        }
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

    /* A segfault in the windowed build used to print nothing at all, while
     * the headless harness printed a full stack for the same fault -- three
     * owner crash reports this session arrived with no frames because of it
     * (P-784). */
    boot_triage_install_crash_reporter();
    match_boot_install_crash_dump();
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
    if ((match_mode || frontend_mode) && record_path != NULL &&
        strcmp(record_path, "-") == 0) {
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
    } else if ((match_mode || frontend_mode) && record_path != NULL) {
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
    /*
     * Hard vsync by default, matching the console's 60 Hz presentation.
     *
     * But it is the compositor that decides when a swap returns, and when it
     * withholds frame callbacks -- window occluded, another client taking the
     * scanout, a driver hiccup -- SDL_GL_SwapWindow blocks for however long
     * that lasts.  The owner sees bursts of 150-226 ms intervals on frames
     * whose game and render are both under a millisecond, which is ten-plus
     * vblanks spent inside the swap and nothing to do with the game.
     *
     * MELEE_SWAP_INTERVAL picks the policy so that can be tested rather than
     * argued about: 1 (default) hard vsync, -1 adaptive (tear-free when the
     * frame is on time, immediate when it is late, so a missed vblank costs
     * one frame instead of stalling until the next callback), 0 immediate --
     * which is not as reckless as it sounds here, because this loop already
     * paces itself to 60 Hz and re-anchors rather than burst-catching-up.
     * A request the driver refuses falls back to hard vsync and says so.
     */
    {
        const char* e = getenv("MELEE_SWAP_INTERVAL");
        int want = e != NULL ? atoi(e) : 1;
        if (want != 1 && !SDL_GL_SetSwapInterval(want)) {
            fprintf(stderr,
                    "viewer: swap interval %d refused (%s); using vsync\n",
                    want, SDL_GetError());
            want = 1;
        }
        if (want == 1) {
            SDL_GL_SetSwapInterval(1);
        }
        if (want != 1) {
            fprintf(stderr, "viewer: swap interval %d\n", want);
        }
    }
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
    if (getenv("MELEE_GX_UNI_STATS") != NULL) {
        extern void gx_gl_uniform_stats(unsigned long*, unsigned long*);
        unsigned long total = 0, skipped = 0;
        gx_gl_uniform_stats(&total, &skipped);
        printf("viewer: uniform uploads %lu of %lu issued (%.1f%% skipped)\n",
               total - skipped, total,
               total ? 100.0 * (double) skipped / (double) total : 0.0);
    }

    hud_shutdown();
    render_scene_close(&v->scene);
    gx_gl_shutdown();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

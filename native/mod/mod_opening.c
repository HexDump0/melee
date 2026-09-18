/*
 * The Unbound opening movie (P-847).
 *
 * Melee's boot plays one movie: `gm_Scene_Opening_OnEnter` calls
 * `lbMthp_8001F410("MvOpen.mth", ...)` and `OnFrame` pumps it, watches its
 * frame counter and skips it on Start/A.  Unbound plays two, and it does it
 * with the game's own player rather than beside it -- `MvUnbound.mth` is a
 * real MTH file (scripts/make_boot_mth.py) streamed through the same
 * entrynum path, decoded by the same THP decoder, drawn by the same SObj.
 * Nothing here draws a pixel.
 *
 * `MoviePlayer` is a single global, so "two movies" means stop and restart,
 * which is exactly what `lbMthp_8001F800` + `lbMthp_8001F410` are for.  Two
 * link-time interposers (decomp_shim.h) are the whole mechanism:
 *
 *   lbMthp_8001F410  the game asks for MvOpen.mth; we remember the request
 *                    and start the Unbound clip instead.
 *   lbMthp_8001F578  the opening scene's per-frame pump, and the only place
 *                    it is called from -- so it is a frame hook scoped to
 *                    the one scene that needs it, with no new callback and
 *                    no cost anywhere else.
 *
 * A third rename, `gm_GetButtonsTriggered`, exists to *consume* the press
 * that skips the clip.  The pump runs at the top of the scene's OnFrame and
 * the scene reads the buttons further down the same frame, where its Start/A
 * branch jumps to GM_TITLE -- so without this, one press on the Unbound logo
 * would swallow the Melee movie instead of revealing it.  The engine keeps
 * the triggered bits in a file-static (`gm_1A36.c`), unreachable from here,
 * so the read is intercepted rather than the state cleared: for the rest of
 * the frame in which the splash acted on a press, that press is spent.
 *
 * The clip itself is silent, which is what the artwork ships as; the Melee
 * movie's music is deferred to the hand-off rather than left to run ahead of
 * its own picture (see `deferred_music`).
 *
 * The retail scene's timings are untouched because they are all relative to
 * the *movie's* frame counter (`lbMthp_8001F5C4`), which restarts at zero
 * when MvOpen.mth starts.  The Nintendo and HAL cards, the audio cues and
 * the hand-off to the title all still land on the frames they always did.
 * The Unbound clip is 120 frames, far short of the first of those (0x1C6),
 * so none of them can fire early either.
 *
 * MELEE_NO_MODS=1 skips registration and the boot is byte-for-byte retail:
 * the file is never published to the FST and every interposer is one
 * predictable branch.
 */
#include "mod/mod.h"

#include <dolphin/pad.h>
#include <melee/gm/forward.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbmthp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/boot/boot_triage.h"
#include "platform/platform.h"

/* The name the clip is published under, and where it comes from. */
#define UNBOUND_MOVIE_NAME "MvUnbound.mth"
#define UNBOUND_MOVIE_DIR "unbound/files/"
#define UNBOUND_MODS_DIR "mods"

/*
 * Every frame at 30 fps, which is how the clip is encoded.  The table is the
 * player's tick-to-frame map: `count, ticks-per-frame` pairs, so {65536, 2}
 * is "60 Hz ticks, two per frame, forever".  MvOpen.mth carries a three-entry
 * table because it has a 60 fps section; this one does not.
 */
static u32 unbound_rate_table[] = { 65536, 2 };

/* Set once the file is published and the feature is on for this run. */
static int enabled;

/* Non-zero while our clip is the movie the player is streaming. */
static int clip_playing;

/* The request the game made, replayed verbatim when the clip is done. */
static struct {
    const char* filename;
    u32* rate_table;
    void* buf;
    size_t heap_size;
    int loop;
} pending;

/* One boot, one Unbound intro: a later movie (the title's attract demo re-
 * enters this scene) must be the game's own. */
static int intro_done;

/* Set on the frame the splash acted on a press; cleared by the next pump. */
static int press_spent;

/*
 * The movie's music, held back.
 *
 * `gm_Scene_Opening_OnEnter` starts the BGM and only then starts the movie,
 * which is right when there is one movie.  With the clip in front, the Melee
 * fanfare would play over the Unbound logo and be four seconds into itself by
 * the time its own picture appeared -- the owner heard exactly that.  The
 * request is parked here and made at the hand-off, so the music still starts
 * on the frame MvOpen.mth does.
 *
 * 0 means nothing parked: the scene asks for track 0x3E, and no caller asks
 * for 0.
 */
static int deferred_music;

void mod_opening_init(void)
{
    char resolved[512];
    const char* path = getenv("MELEE_UNBOUND_MOVIE");

    if (path == NULL || path[0] == '\0') {
        /* Same root the wasm binding scans, so moving mods/ moves the clip
         * with it rather than leaving one file behind. */
        const char* dir = getenv("MELEE_MODS_DIR");

        if (dir == NULL || dir[0] == '\0') {
            dir = UNBOUND_MODS_DIR;
        }
        snprintf(resolved, sizeof(resolved), "%s/%s%s", dir,
                 UNBOUND_MOVIE_DIR, UNBOUND_MOVIE_NAME);
        path = resolved;
    }
    if (!platform_disc_add_host_file(UNBOUND_MOVIE_NAME, path)) {
        boot_triage_note(
            "[unbound] opening: no movie at '%s'; retail boot unchanged\n",
            path);
        return;
    }
    enabled = 1;
}

/*
 * Any button on any port, which is what a splash owes the player.  The engine
 * keeps a synthetic aggregate entry at PAD_MAX_CONTROLLERS, so one read
 * covers every controller.
 */
static int skip_requested(void)
{
    return gm_GetButtonsTriggered(PAD_MAX_CONTROLLERS) != 0;
}

/*
 * Every other caller in the game sees the real edge; only the frame on which
 * the splash already acted on a press reads as "nothing pressed", and only
 * until the next pump clears the flag.
 */
u64 unbound_gm_GetButtonsTriggered(u8 idx)
{
    if (press_spent) {
        return 0;
    }
    return gm_GetButtonsTriggered(idx);
}

/*
 * Hold back the opening scene's own BGM request, and only that one: the
 * deferral is gated on the scene, on the clip still being ahead of us, and on
 * nothing having been parked yet.  Every other one of the 50 callers in the
 * game passes straight through.
 */
int unbound_lbAudioAx_80023F28(int track)
{
    if (enabled && !intro_done && deferred_music == 0 && track != 0 &&
        gm_GetCurrentGameMode() == GM_OPENING_MV)
    {
        deferred_music = track;
        boot_triage_note(
            "[unbound] opening: holding track 0x%x until %s starts\n",
            (unsigned) track, "MvOpen.mth");
        return 0;
    }
    return lbAudioAx_80023F28(track);
}

static void start_pending_movie(void)
{
    lbMthp_8001F410(pending.filename, pending.rate_table, pending.buf,
                    pending.heap_size, pending.loop);
    if (deferred_music != 0) {
        boot_triage_note("[unbound] opening: starting track 0x%x\n",
                         (unsigned) deferred_music);
        lbAudioAx_80023F28(deferred_music);
        deferred_music = 0;
    }
}

void unbound_lbMthp_8001F410(const char* filename, u32* rate_table, void* buf,
                             size_t heap_size, int loop)
{
    if (!enabled || intro_done || filename == NULL ||
        strcmp(filename, "MvOpen.mth") != 0)
    {
        lbMthp_8001F410(filename, rate_table, buf, heap_size, loop);
        return;
    }

    pending.filename = filename;
    pending.rate_table = rate_table;
    pending.buf = buf;
    pending.heap_size = heap_size;
    pending.loop = loop;

    /*
     * The caller sized `buf` for MvOpen.mth's frames.  Ours are smaller, but
     * "smaller" is not a guarantee worth betting a buffer overrun on, so the
     * clip always allocates its own heap (buf = NULL) and frees it in
     * lbMthp_8001F800 before the real movie takes the caller's.
     */
    clip_playing = 1;
    intro_done = 1;
    lbMthp_8001F410(UNBOUND_MOVIE_NAME, unbound_rate_table, NULL, 0, 0);
    boot_triage_note("[unbound] opening: playing %s before %s\n",
                     UNBOUND_MOVIE_NAME, filename);
}

void unbound_lbMthp_8001F578(void)
{
    int finished;

    press_spent = 0;
    lbMthp_8001F578();
    if (!clip_playing) {
        return;
    }

    /* 8001F604 reports the last frame of a non-looping movie. */
    finished = lbMthp_8001F604() != 0;
    if (!finished) {
        if (!skip_requested()) {
            return;
        }
        press_spent = 1;
        boot_triage_note("[unbound] opening: skipped at frame %d\n",
                         lbMthp_8001F5C4());
    } else {
        boot_triage_note("[unbound] opening: clip finished at frame %d\n",
                         lbMthp_8001F5C4());
    }

    clip_playing = 0;
    lbMthp_8001F800();
    start_pending_movie();
    /*
     * The scene reads the movie's frame counter immediately after this call
     * returns.  Pump once more so it sees the new movie's frame 0 rather
     * than the last frame of the one that just ended.
     */
    lbMthp_8001F578();
}

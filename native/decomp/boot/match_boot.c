/*
 * S4 match bring-up: enter a real match headless without the frontend.
 *
 * The compiled boot reaches the memory-card/pad wait and would need menus,
 * fonts and saves (S6) to navigate to VS mode.  Instead, once the engine is
 * initialized, this hook uses the game's own debug-VS mode
 * (`GM_DEBUG_VS` -> `gm_Mode_DebugVs_States[0]` -> `onEnterDebugVs`), which
 * fills `StartMeleeData` with a Link/Mario match on Final Destination and
 * runs the compiled GS_VS scene.  The mode switch is requested from the VI
 * frame hook and the current scene is ended with `gm_801A4B60`, exactly the
 * pair the debug menu and stage scenes use.
 *
 * The game's own `onEnterDebugVs` is the specification here; nothing about
 * the match setup is port code.
 */
#include "decomp/boot/match_boot.h"

#include <stdio.h>
#include <stdlib.h>

#include <melee/gm/forward.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/lb/lblanguage.h>
#include <melee/gm/gmscene.h>
#include <melee/gm/gmvs.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmvsmode.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ftcpuattack.h>
#include <melee/if/forward.h>
#include <melee/mn/mnmain.h>
#include <melee/pl/player.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/cm/camera.h>
#include <melee/gr/forward.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/jobj.h>

#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/initialize.h>
#include <dolphin/os/OSAlloc.h>

#include <dolphin/mtx.h>
#include <dolphin/pad.h>

#include "decomp/boot/boot_triage.h"
#include "platform/platform.h"

#define MATCH_INPUT_FRAMES 900
#define MATCH_INPUT_CHANNELS 2

static unsigned start_frame;
static unsigned frame;
static int stocks_frames;
static int hit_test;
static int hit_logged;
static int icon_test;
static int icon_logged;
static int title_test;
static int title_logged;
static int title_seen;
static int gameover_test;
static int gameover_stage;
/* P-759 matrix mode: -1 means "leave onEnterDebugVs's own choice alone". */
static int select_p0 = -1;
static int select_p1 = -1;
static int select_stage = -1;
/* -2 means "leave onEnterDebugVs's choice"; -1..4 are real item_freq values
 * (-1 off, 4 very high), matching mnitemsw.c's menu index minus one. */
static int select_items = -2;
static int select_logged;
static int stadium_trace;
static int item_trace;
static int item_verbose;
static int shield_test;
static int heap_trace;
static int rng_trace;
static void log_shield_state(void);
static int shield_ok_said;
static int shield_bad_said;
#define ABSF(x) ((x) < 0 ? -(x) : (x))
static int classic_test;
static int classic_named;
static int intro_test;
static int intro_stage;
static int cpu_test;
static int cpu_hit_logged;
static int cpu_tables_logged;
static unsigned cpu_attack_entries;
static unsigned char cpu_was_attacking[6];
static PadInputFrame match_input[MATCH_INPUT_FRAMES][MATCH_INPUT_CHANNELS];

static void log_item_trace(void);
static void log_stadium_display(void);
static int classic_test;
static int classic_named;
static int intro_test;
static int intro_stage;
static int cpu_test;
static int cpu_hit_logged;
static int cpu_tables_logged;
static unsigned cpu_attack_entries;
static unsigned char cpu_was_attacking[6];
static PadInputFrame match_input[MATCH_INPUT_FRAMES][MATCH_INPUT_CHANNELS];

static void log_item_trace(void);
static void log_stadium_display(void);

/* MELEE_ITEM_TEST: p0 taps and holds B so the neutral-special command script
 * runs.  Projectiles are spawned by a subaction command (`ftAction_80071974`
 * sets `throw_flags_b0`, and the action's Anim callback consumes it the same
 * frame), so a plain jab script never exercises the path at all (P-739). */
static void build_item_test_input(void)
{
    unsigned f;

    for (f = 0; f < MATCH_INPUT_FRAMES; f++) {
        PadInputFrame* p0 = &match_input[f][0];
        PadInputFrame* p1 = &match_input[f][1];

        /* p0 fires neutral-B on a cycle: short taps for the instant
         * projectiles, then longer holds for the charged ones. */
        if (f >= 180 && (f % 45) < 10) {
            p0->buttons |= PAD_BUTTON_B;
        }
        /* p1 walks into the line of fire, the way MELEE_HIT_TEST does for
         * normals -- a projectile that spawns but never connects proves only
         * half of the path (P-739/P-743) -- and holds B itself, which is what
         * drives the effect system hard enough to reach P-742's runaway
         * generator.  One run therefore covers the whole chain. */
        if (f >= 220 && f < 420) {
            /* Standing neutral-B, no stick: side-B is a different move, and
             * it is neutral-B's effect that reaches P-742's generator. */
            p1->buttons |= PAD_BUTTON_B;
        } else if (f >= 430) {
            p1->stick_x = -60;
        }
    }
    pad_set_input_script(&match_input[0][0], MATCH_INPUT_CHANNELS,
                         MATCH_INPUT_FRAMES);
}

/* P-794: `player_slots[].player_entity` is not cleared when a match tears
 * down, so `Player_GetEntity` keeps handing back the slot's old entity long
 * after HSD has freed it -- and on a results or record screen the block has
 * been handed out again as something else.  Every probe below took the
 * pointer on the strength of a NULL check and read it as a `Fighter*`; the
 * owner's Home-Run Contest run segfaulted inside the VI frame hook on the
 * record screen for exactly that reason (`VIWaitForRetrace+0x64` is the
 * return address of `call *vi_frame_hook`, and the two frames under it were
 * `<unknown>` because they are `static`).
 *
 * The classifier alone is not a test: a freed block keeps its old bytes.
 * `Fighter_Create` puts every fighter on GX link 5, and unloading takes it
 * off, so membership of that list is what actually distinguishes a live
 * fighter from a stale pointer.  Nothing here dereferences the candidate --
 * only list nodes, which are live by construction -- so the check is safe
 * even when the slot holds outright garbage. */
static HSD_GObj* match_boot_fighter_gobj(int slot)
{
    HSD_GObj* gobj;
    HSD_GObj* live;

    if (HSD_GObjGXLinkHead == NULL) {
        return NULL;
    }
    gobj = Player_GetEntity(slot);
    if (gobj == NULL) {
        return NULL;
    }
    for (live = HSD_GObjGXLinkHead[5]; live != NULL; live = live->next_gx) {
        if (live != gobj) {
            continue;
        }
        if (live->classifier != HSD_GOBJ_CLASS_FIGHTER ||
            live->user_data == NULL)
        {
            return NULL;
        }
        return live;
    }
    return NULL;
}

/* P-747: the shield bubble is created and submitted but draws nothing.
 * `efLib_Update` derives the bubble's scale from the attach joint's matrix
 * (`HSD_MtxGetScale(HSD_JObjGetMtxPtr(effect->attach_jobj), ...)`), so a
 * degenerate matrix there becomes a NaN bubble.  The attach joint is the
 * fighter's shield joint, `fp->parts[fp->ft_data->x8->x11].joint`; print its
 * own transform and its world matrix so a bad source can be told from a bad
 * concatenation. */
static void log_shield_state(void)
{
    HSD_GObj* g;
    Fighter* fp;
    HSD_JObj* j;
    MtxPtr m;
    int idx;

    if (!shield_test || (frame % 10) != 0) {
        return;
    }
    g = match_boot_fighter_gobj(0);
    if (g == NULL || g->user_data == NULL) {
        return;
    }
    fp = (Fighter*) g->user_data;
    if (fp->ft_data == NULL || fp->ft_data->x8 == NULL) {
        return;
    }
    idx = (int) fp->ft_data->x8->x11;
    j = fp->parts[idx].joint;
    if (j == NULL) {
        fprintf(stderr, "[shield] frame=%u part=%d joint=NULL\n", frame, idx);
        return;
    }
    m = HSD_JObjGetMtxPtr(j);
    /* The scale only leaves 1.0 while the shield is actually up
     * (`ftCo_80091D58` writes it), so that is the cheapest "is the fighter
     * guarding" test.  What the bubble needs is a finite matrix: `efLib_Update`
     * takes its scale from this joint, so an infinity or NaN here is a bubble
     * nobody can see (P-747). */
    if (j->scale.x != 1.0f) {
        int r, bad = 0;
        /* Two things have to hold, and the first alone is not enough: the
         * joint's own translate must be a real offset (the bug put 1e31 in
         * z), and the resulting matrix must not have collapsed -- the 1e31
         * did not make the matrix infinite, it made every row a denormal
         * around 1e-40, which is a bubble scaled to nothing.  Check the row
         * magnitudes, which is what `HSD_MtxGetScale` reads for the bubble. */
        if (!(ABSF(j->translate.x) < 1.0e4f &&
              ABSF(j->translate.y) < 1.0e4f && ABSF(j->translate.z) < 1.0e4f))
        {
            bad = 1;
        }
        for (r = 0; r < 3; r++) {
            float mag = m[r][0] * m[r][0] + m[r][1] * m[r][1] +
                        m[r][2] * m[r][2];
            if (!(mag > 1.0e-6f && mag < 1.0e12f)) {
                bad = 1;
            }
        }
        if (bad && !shield_bad_said) {
            shield_bad_said = 1;
            fprintf(stderr,
                    "[shield] frame=%u POSE BAD t=(%g,%g,%g) "
                    "mtx_row2=(%g,%g,%g,%g)\n",
                    frame, (double) j->translate.x, (double) j->translate.y,
                    (double) j->translate.z, (double) m[2][0],
                    (double) m[2][1], (double) m[2][2], (double) m[2][3]);
        } else if (!bad && !shield_ok_said) {
            shield_ok_said = 1;
            fprintf(stderr,
                    "[shield] frame=%u guard pose sane t=(%g,%g,%g) "
                    "scale=%g row0=%g ok\n",
                    frame, (double) j->translate.x, (double) j->translate.y,
                    (double) j->translate.z, (double) j->scale.x,
                    (double) (m[0][0] * m[0][0] + m[0][1] * m[0][1] +
                              m[0][2] * m[0][2]));
        }
    }
}

/* P-739: no projectile special produces an article.  Everything between the
 * fighter and the screen is invisible from outside -- the command script, the
 * throw flag, `Item_8026862C`, the item's own state table -- so count the
 * live item GObjs and name what they are.  Items are class 6 on GX link 6
 * (`Item_8026862C` -> `GObj_SetupGXLink(gobj, ..., 6, 0)`). */
/* P-779, and it is a detector rather than a fix because the crash is three
 * steps downstream of the damage.
 *
 * The owner's panic is `ftcoll.c:1296 "attack power over 500!! inf"`, and the
 * capture that named it reads:
 *
 *     P779-HIT kind=0 dmg=0.000000 vel=-9.93e22,2.53e23,0.000000 flag=1
 *
 * The hitbox damage is **zero** and the multipliers are sane; the `inf` is
 * manufactured inside `it_8026B1D4` (it_26B1.c:68), which adds
 * `sqrt(vel.x^2 + vel.y^2 + vel.z^2) * itCommonData->x80_float[5]` when the
 * item's `flags.x14` is set.  `9.93e22` squared is `9.87e45`, well past the
 * `3.4e38` a float can hold, so the square alone is already `inf` -- the
 * item's *velocity* is the bug and the damage assert is just where it
 * surfaces.
 *
 * A capsule moving at 1e23 is never legitimate, and it is wrong long before
 * it touches anybody, so check it every frame: that turns an owner-only
 * report into something the soak can hit, and prints the item's identity at
 * the moment it goes wrong rather than at the moment it kills someone.
 * Capped, because a stuck item would otherwise print every frame. */
#define ITEM_VEL_SANE 1.0e6f
#define ITEM_VEL_REPORTS 8

static void check_item_velocity(void)
{
    static int reports;
    HSD_GObj* gobj;

    if (reports >= ITEM_VEL_REPORTS || HSD_GObjGXLinkHead == NULL) {
        return;
    }
    for (gobj = HSD_GObjGXLinkHead[6]; gobj != NULL; gobj = gobj->next_gx) {
        Item* it;
        float x, y, z;

        if (gobj->classifier != HSD_GOBJ_CLASS_ITEM ||
            gobj->user_data == NULL)
        {
            continue;
        }
        it = (Item*) gobj->user_data;
        x = it->x40_vel.x;
        y = it->x40_vel.y;
        z = it->x40_vel.z;
        /* `!(v == v)` catches NaN, which fails every ordered comparison and
         * would otherwise slip through the magnitude test (the mplib.c:4804
         * lesson from P-769). */
        if (!(x == x) || !(y == y) || !(z == z) || x > ITEM_VEL_SANE ||
            x < -ITEM_VEL_SANE || y > ITEM_VEL_SANE || y < -ITEM_VEL_SANE ||
            z > ITEM_VEL_SANE || z < -ITEM_VEL_SANE)
        {
            reports++;
            boot_triage_note(
                "[item] BAD VELOCITY: frame=%u kind=%d vel=(%g,%g,%g) "
                "pos=(%.2f,%.2f) life=%.1f item=%p owner=%p\n",
                frame, (int) it->kind, (double) x, (double) y, (double) z,
                (double) it->pos.x, (double) it->pos.y,
                (double) it->xD44_lifeTimer, (void*) it, (void*) it->owner);
            if (reports >= ITEM_VEL_REPORTS) {
                return;
            }
        }
    }
}

static void log_item_trace(void)
{
    HSD_GObj* gobj;
    unsigned live = 0;
    static unsigned char ever_seen[256];
    static int armed_said;

    if (!item_trace) {
        return;
    }
    if (!armed_said) {
        armed_said = 1;
        fprintf(stderr, "[item] trace armed\n");
    }
    if (HSD_GObjGXLinkHead == NULL) {
        return;
    }
    for (gobj = HSD_GObjGXLinkHead[6]; gobj != NULL; gobj = gobj->next_gx) {
        Item* it;
        if (gobj->classifier != HSD_GOBJ_CLASS_ITEM || gobj->user_data == NULL)
        {
            continue;
        }
        it = (Item*) gobj->user_data;
        live++;
        if (item_verbose && (it->kind == 64 || it->kind == 48)) {
            fprintf(stderr,
                    "[itemv] f=%u kind=%d life=%.1f pos=(%.1f,%.1f) "
                    "vel=(%.2f,%.2f)\n",
                    frame, (int) it->kind, (double) it->xD44_lifeTimer,
                    (double) it->pos.x, (double) it->pos.y,
                    (double) it->x40_vel.x, (double) it->x40_vel.y);
        }
        if (!ever_seen[(unsigned) it->kind & 0xFFu]) {
            ever_seen[(unsigned) it->kind & 0xFFu] = 1;
            boot_triage_note(
                "[item] frame=%u article kind=%d life=%.1f "
                "pos=(%.1f,%.1f) vel=(%.2f,%.2f)\n",
                frame, (int) it->kind, (double) it->xD44_lifeTimer,
                (double) it->pos.x, (double) it->pos.y,
                (double) it->x40_vel.x, (double) it->x40_vel.y);
        }
    }
    (void) live;
}

/* MELEE_HIT_TEST: p0 jabs in place while p1 walks into it.  The default
 * script never guarantees contact, so this is the headless regression for
 * the command -> hitbox -> collision -> damage path. */
static void build_hit_test_input(void)
{
    unsigned f;

    for (f = 0; f < MATCH_INPUT_FRAMES; f++) {
        PadInputFrame* p0 = &match_input[f][0];
        PadInputFrame* p1 = &match_input[f][1];

        if (f >= 160 && f < 520) {
            p1->stick_x = -60;
        }
        if (f >= 200 && f < 520 && (f % 15) < 5) {
            p0->buttons |= PAD_BUTTON_A;
        }
    }
    pad_set_input_script(&match_input[0][0], MATCH_INPUT_CHANNELS,
                         MATCH_INPUT_FRAMES);
}

static void build_match_input(void)
{
    unsigned f;

    for (f = 0; f < MATCH_INPUT_FRAMES; f++) {
        PadInputFrame* p0 = &match_input[f][0];
        PadInputFrame* p1 = &match_input[f][1];

        /* Player 1 (channel 0): neutral, walk/run right with jumps and
         * aerials, then walk left, then idle. */
        if (f >= 120 && f < 420) {
            p0->stick_x = (f < 300) ? 60 : 90;
        } else if (f >= 450 && f < 700) {
            p0->stick_x = -60;
        }
        if ((f >= 200 && f < 207) || (f >= 500 && f < 507) ||
            (f >= 620 && f < 627))
        {
            p0->buttons |= PAD_BUTTON_Y;
        }
        if ((f >= 280 && f < 287) || (f >= 660 && f < 667)) {
            p0->buttons |= PAD_BUTTON_A;
        }
        /* MELEE_SHIELD_TEST: hold L from frame 260 once the entry animation
         * is well past, so the shield state can be inspected from frame 260
         * on without the frontend's character-select timing. */
        if (shield_test && f >= 260) {
            p0->buttons |= PAD_TRIGGER_L;
        }
        /* Player 2 (channel 1): walk the other way, then idle. */
        if (f >= 180 && f < 480) {
            p1->stick_x = -60;
        } else if (f >= 520 && f < 760) {
            p1->stick_x = 60;
        }
        if (f >= 400 && f < 407) {
            p1->buttons |= PAD_BUTTON_Y;
        }
        if (f >= 560 && f < 567) {
            p1->buttons |= PAD_BUTTON_A;
        }
    }
    pad_set_input_script(&match_input[0][0], MATCH_INPUT_CHANNELS,
                         MATCH_INPUT_FRAMES);
}

/* P-759: pick the fighters and the stage, so the soak can sweep a matrix
 * instead of re-running one scenario.
 *
 * `onEnterDebugVs` (gmvsmode.c:161) hardcodes Link vs Mario and leaves
 * `rules.stkind` at `St_Kind_Last`, writing into `gmVsMelee_StartData` --
 * which is what `gm_Mode_DebugVs_States[0]` names as its enter data and what
 * `gm_Scene_Vs_OnEnter` then reads.
 *
 * **Rewriting that global from the VI-frame hook does not work**, and the
 * trace says why: `onEnterDebugVs` and `gm_Scene_Vs_OnEnter` run in the *same*
 * game frame, so the hook only ever sees the value before the transition or
 * after the fighters have already loaded.  It looks like it works -- the
 * global does hold the selection from the next frame on -- while the match
 * that actually loaded is the stock one.
 *
 * So wrap the mode state's own `on_enter` instead.  The table is ordinary
 * writable data, the wrapper runs `onEnterDebugVs` first and then overrides
 * its choices, and the scene reads the result in the same frame.  Nothing
 * under `src/` changes. */
static void (*debug_vs_on_enter)(GameModeState*);

static void match_boot_on_enter_debug_vs(GameModeState* state)
{
    if (debug_vs_on_enter != NULL) {
        debug_vs_on_enter(state);
    }
    {
        StartMeleeData* start = gm_GetGameModeStateEnterData(state);
        if (start == NULL) {
            return;
        }
        if (select_p0 >= 0) {
            start->players[0].ckind = (s8) select_p0;
        }
        if (select_p1 >= 0) {
            start->players[1].ckind = (s8) select_p1;
        }
        if (select_stage >= 0) {
            start->rules.stkind = (u16) select_stage;
        }
        /* `onEnterDebugVs` sets item_freq = -1, i.e. items off, so the whole
         * soak matrix ran without ever spawning one.  That is a large hole:
         * items are their own article/collision/dynamics path, and the owner
         * hit `itcoll.c:1050 "item dynamics hit num over!"` in normal play
         * that no headless run could reach. */
        if (select_items >= -1) {
            start->rules.item_freq = (s8) select_items;
        }
    }
}

static void install_match_selection(void)
{
    if (select_p0 < 0 && select_p1 < 0 && select_stage < 0) {
        return;
    }
    if (gm_Mode_DebugVs_States[0].on_enter == match_boot_on_enter_debug_vs) {
        return;
    }
    debug_vs_on_enter = gm_Mode_DebugVs_States[0].on_enter;
    gm_Mode_DebugVs_States[0].on_enter = match_boot_on_enter_debug_vs;
}

/* P-780: a fighter can wedge -- stop responding to input -- while the match
 * keeps running normally.  The soak detects crashes, hangs and asserts, and a
 * stuck fighter in a healthy game is none of those, so it was invisible.
 *
 * The signal is a fighter whose `motion_id` *and* position are both unchanged
 * for a long stretch while the frame counter advances.  Either alone is
 * normal: a fighter can stand still in Wait for a while, and a motion can run
 * for many frames.  Both frozen together, for longer than any real animation,
 * is not.
 *
 * The threshold is deliberately generous.  This must not cry wolf across 754
 * matrix runs, so it is set well past the longest legitimate motion; a real
 * wedge lasts until the match ends. */
#define MATCH_STUCK_FRAMES 420

/* `MELEE_STUCK_TRACE` is the owner-facing half of P-780, and it answers a
 * different question from the soak's.
 *
 * The soak only needs "did a fighter wedge", once, without crying wolf over
 * 754 runs, so it waits 420 frames and prints one line.  The owner is sitting
 * in front of the game and already knows a fighter is wedged -- what he cannot
 * see is *which layer* stopped.  Three are indistinguishable from the couch:
 *
 *   - the pad never reached the fighter        (input delivery)
 *   - the pad reached it and the state machine ignored it   (logic)
 *   - the state machine advanced and the animation did not  (ftAnim/FObj)
 *
 * So the trace prints `fp->input`, `motion_id` *and* `cur_anim_frame`
 * together.  `input.lstick[0]` moving while `motion_id` does not is the second
 * case; everything frozen together is the first; `motion_id` changing with
 * `cur_anim_frame` pinned is the third -- and that third one would put this
 * with P-781, which is also an animation-layer failure.
 *
 * It also reports repeatedly rather than once, because the useful signal is
 * whether the numbers move *at all* during the wedge, and it covers four
 * slots and any game mode, since the owner plays ordinary VS rather than the
 * harness's GM_DEBUG_VS. */
static int stuck_trace;
static unsigned stuck_frames; /* MELEE_STUCK_TRACE=<frames> overrides */
static unsigned pos_period = 60; /* MELEE_POS_TRACE=<frames> */

#define MATCH_STUCK_SLOTS 4
#define MATCH_STUCK_LIVE_FRAMES 120
#define MATCH_STUCK_REPEAT 120

static void report_stuck(const char* why, int slot, const Fighter* fp,
                         unsigned still)
{
    /* `stocks` is what makes a Sleep report mean anything.  `ftCo_MS_Sleep`
     * (11) is the state a fighter is in between being KO'd and reappearing on
     * the revival platform, and staying there forever is **correct** for a
     * fighter that is out of stocks -- so without the stock count, "asleep for
     * 1500 frames" cannot be told apart from "eliminated, working as
     * intended".  With stocks > 0 it is a stalled respawn and a real bug. */
    fprintf(stderr,
            "[stuck] %s: slot %d pad %u kind %d stocks %d %u frames: "
            "motion_id=%d anim_frame=%.2f pos=(%.2f,%.2f) "
            "lstick=(%.3f,%.3f) held=0x%04x hitlag=%.1f invis=%d\n",
            why, slot, (unsigned) fp->x61A_controller_index, (int) fp->kind,
            (int) Player_GetStocks(slot), still, (int) fp->motion_id,
            (double) fp->cur_anim_frame, (double) fp->cur_pos.x,
            (double) fp->cur_pos.y, (double) fp->input.lstick[0].x,
            (double) fp->input.lstick[0].y,
            (unsigned) fp->input.held_buttons[0],
            (double) fp->dmg.x195c_hitlag_frames, (int) fp->invisible);
}

static void check_fighter_stuck(void)
{
    static int last_motion[MATCH_STUCK_SLOTS];
    static float last_frame[MATCH_STUCK_SLOTS];
    static float last_x[MATCH_STUCK_SLOTS], last_y[MATCH_STUCK_SLOTS];
    static unsigned still[MATCH_STUCK_SLOTS];   /* motion + anim both frozen */
    static unsigned deaf[MATCH_STUCK_SLOTS];    /* motion frozen under input */
    static int reported[MATCH_STUCK_SLOTS];
    static int announced;
    int slots = stuck_trace ? MATCH_STUCK_SLOTS : 2;
    unsigned threshold = stuck_trace
                             ? (stuck_frames ? stuck_frames
                                             : MATCH_STUCK_LIVE_FRAMES)
                             : MATCH_STUCK_FRAMES;
    int seen = 0;
    int slot;

    /* The harness only ever runs GM_DEBUG_VS and the gate keeps it from
     * reporting on the frontend; the owner's matches are ordinary VS, so
     * with the trace on, "a fighter exists" is the gate instead. */
    if (!stuck_trace && gm_GetCurrentGameMode() != GM_DEBUG_VS) {
        return;
    }
    for (slot = 0; slot < slots; slot++) {
        HSD_GObj* gobj = match_boot_fighter_gobj(slot);
        Fighter* fp;
        int motion_frozen;
        int anim_frozen;
        int pressing;

        if (gobj == NULL || gobj->user_data == NULL) {
            still[slot] = deaf[slot] = 0;
            reported[slot] = 0;
            continue;
        }
        fp = (Fighter*) gobj->user_data;
        seen++;

        motion_frozen = (int) fp->motion_id == last_motion[slot];
        anim_frozen = fp->cur_anim_frame == last_frame[slot];
        /* "Pressing" deliberately ignores the buttons a wedged fighter might
         * be holding by accident: a stick well out of deadzone is the clearest
         * evidence the owner is trying to move and nothing is happening. */
        pressing = fp->input.lstick[0].x > 0.5f ||
                   fp->input.lstick[0].x < -0.5f ||
                   fp->input.lstick[0].y > 0.5f ||
                   fp->input.lstick[0].y < -0.5f ||
                   fp->input.held_buttons[0] != 0;

        /* **Position is deliberately not part of the test any more.**  The
         * first version required motion_id, x and y all frozen together, and
         * the owner hit the wedge with it armed and got nothing -- a fighter
         * that is stuck but still sliding, falling or being pushed keeps
         * moving, so the position term suppressed the very report it was
         * meant to produce.  `cur_anim_frame` is the right second term: a
         * looping Wait animation advances it, so standing still does not
         * trip, while a genuinely stalled motion pins it. */
        if (motion_frozen && anim_frozen) {
            still[slot]++;
        } else {
            if (stuck_trace && still[slot] >= threshold) {
                fprintf(stderr,
                        "[stuck] recovered: slot %d after %u frames, "
                        "motion_id=%d -> %d\n",
                        slot, still[slot], last_motion[slot],
                        (int) fp->motion_id);
            }
            still[slot] = 0;
        }
        /* The owner's actual complaint, encoded directly: the character does
         * not respond to input.  A motion that never changes while the stick
         * is held is that, whatever the animation is doing.  The window is
         * longer than the frozen-animation one because holding shield or
         * charging a smash legitimately pins motion_id for a while. */
        if (motion_frozen && pressing) {
            deaf[slot]++;
        } else {
            deaf[slot] = 0;
        }

        if (stuck_trace) {
            if (still[slot] >= threshold &&
                ((still[slot] - threshold) % MATCH_STUCK_REPEAT) == 0)
            {
                report_stuck("frozen", slot, fp, still[slot]);
            }
            if (deaf[slot] >= threshold + 60 &&
                ((deaf[slot] - threshold - 60) % MATCH_STUCK_REPEAT) == 0)
            {
                report_stuck("ignoring input", slot, fp, deaf[slot]);
            }
        } else if (still[slot] == threshold && !reported[slot]) {
            reported[slot] = 1;
            boot_triage_note("[match] STUCK: slot %d motion_id=%d frozen at "
                             "(%.2f,%.2f) for %u frames\n",
                             slot, (int) fp->motion_id, fp->cur_pos.x,
                             fp->cur_pos.y, still[slot]);
        }

        last_motion[slot] = (int) fp->motion_id;
        last_frame[slot] = fp->cur_anim_frame;
        last_x[slot] = fp->cur_pos.x;
        last_y[slot] = fp->cur_pos.y;
    }
    /* G-168: "no output" must never be able to mean "the probe never ran".
     * Say so once, as soon as there is anything to watch. */
    if (stuck_trace && !announced && seen > 0) {
        announced = 1;
        fprintf(stderr,
                "[stuck] armed: watching %d fighters, threshold %u frames\n",
                seen, threshold);
    }
    (void) last_x;
    (void) last_y;
}

/* Report what actually loaded, not what was asked for.  A selection that
 * silently does not take would make a matrix sweep look like broad coverage
 * while re-running one scenario, which is worse than not sweeping. */
static void log_match_selection(void)
{
    HSD_GObj* g0;
    HSD_GObj* g1;

    /* Only once the DebugVs match is the live one.  The boot sequence loads
     * fighters of its own before the mode sticks, and reporting those would
     * describe a match the selection never applied to. */
    if (select_logged || gm_GetCurrentGameMode() != GM_DEBUG_VS) {
        return;
    }
    g0 = match_boot_fighter_gobj(0);
    g1 = match_boot_fighter_gobj(1);
    if (g0 == NULL || g1 == NULL) {
        return;
    }
    select_logged = 1;
    boot_triage_note(
        "[match] loaded p0=%d p1=%d grkind=%d items=%d (asked p0=%d p1=%d "
        "stage=%d)\n",
        (int) ((Fighter*) g0->user_data)->kind,
        (int) ((Fighter*) g1->user_data)->kind, (int) stage_info.grkind,
        (int) gmVsMelee_StartData.rules.item_freq, select_p0, select_p1,
        select_stage);
}

static void log_match_state(void)
{
    int slot;
    static int attrs_logged;
    for (slot = 0; slot < 2; slot++) {
        HSD_GObj* gobj = match_boot_fighter_gobj(slot);
        Vec3 pos;
        Fighter* fp;
        if (gobj == NULL) {
            continue;
        }
        fp = (Fighter*) gobj->user_data;
        ftLib_80086644((Fighter_GObj*) gobj, &pos);
        boot_triage_note(
            "[match] slot %d stocks=%d dmg=%d pos=(%.2f,%.2f,%.2f)\n", slot,
            (int) Player_GetStocks(slot), (int) Player_GetDamage(slot),
            pos.x, pos.y, pos.z);
        if (!(attrs_logged & (1 << slot))) {
            attrs_logged |= 1 << slot;
            boot_triage_note(
                "[match] attrs slot %d accel=%.3f friction=%.3f run=%.3f "
                "gravity=%.3f terminal=%.3f jump_v=%.3f model_scale=%.3f\n",
                slot, fp->co_attrs.walk_accel_base,
                fp->co_attrs.ground_friction, fp->co_attrs.walk_max_vel,
                fp->co_attrs.gravity, fp->co_attrs.terminal_velocity,
                fp->co_attrs.jump_v_initial_velocity, fp->x34_scale.y);
        }
        if ((hit_test || item_trace) && !hit_logged &&
            Player_GetDamage(slot) > 0)
        {
            hit_logged = 1;
            boot_triage_note("[match] hit: slot %d damage=%d\n", slot,
                             (int) Player_GetDamage(slot));
        }
    }
}

/* P-705: the title demo is four level-9 CPU fighters with no PAD script, so
 * it is the closest automated reproduction of the owner's report.  Record
 * both attack-state entries and actual damage; damage proves the CPU selected
 * an attack, emitted its command script and connected a hit. */
static void log_cpu_test(void)
{
    int slot;

    if (!cpu_test || cpu_hit_logged ||
        gm_GetCurrentGameMode() != GM_OPENING_MV ||
        gm_GetCurrentSceneIndex() != 1)
    {
        return;
    }
    if (!cpu_tables_logged) {
        int checked = 0;
        int valid = 0;
        int first_cmd = 0;
        float first_weight = 0.0f;

        if (Fighter_804D64FC == NULL || Fighter_804D64FC->x4 == NULL) {
            return;
        }
        for (slot = 0; slot < 6; slot++) {
            HSD_GObj* gobj = match_boot_fighter_gobj(slot);
            Fighter* fp;
            unsigned char* list;
            int cmd;
            float weight;
            if (gobj == NULL) {
                continue;
            }
            fp = (Fighter*) gobj->user_data;
            list = Fighter_804D64FC->x4[fp->kind];
            if (list == NULL) {
                continue;
            }
            cmd = *(int*) list;
            weight = *(float*) (list + 0x18);
            if (checked == 0) {
                first_cmd = cmd;
                first_weight = weight;
            }
            checked++;
            if (cmd >= 0 && cmd < 0x100 && weight >= -1000.0f &&
                weight <= 1000.0f)
            {
                valid++;
            } else {
                boot_triage_note(
                    "[cpu] table slot=%d kind=%d cmd=%d weight=%g invalid\n",
                    slot, (int) fp->kind, cmd, (double) weight);
            }
        }
        if (checked != 0) {
            cpu_tables_logged = 1;
            boot_triage_note(
                "[cpu] tables checked=%d valid=%d first_cmd=%d "
                "first_weight=%g ok=%d\n",
                checked, valid, first_cmd, (double) first_weight,
                valid == checked);
        }
    }
    for (slot = 0; slot < 6; slot++) {
        HSD_GObj* gobj = match_boot_fighter_gobj(slot);
        Fighter* fp;
        int attacking;

        if (gobj == NULL) {
            cpu_was_attacking[slot] = 0;
            continue;
        }
        fp = (Fighter*) gobj->user_data;
        attacking = ftCo_800B630C(fp);
        if (attacking && !cpu_was_attacking[slot]) {
            cpu_attack_entries++;
        }
        cpu_was_attacking[slot] = attacking;
        if (Player_GetDamage(slot) > 0) {
            cpu_hit_logged = 1;
            boot_triage_note(
                "[cpu] hit slot=%d damage=%d attack_entries=%u motion=%d\n",
                slot, (int) Player_GetDamage(slot), cpu_attack_entries,
                (int) fp->motion_id);
            return;
        }
    }
}

void match_boot_force(u8 mode)
{
    boot_triage_note("[match] switching to game mode %u\n", (unsigned) mode);
    /* The boot mode installs lbCardGame_DecideGameMode as the state-machine
     * override (save data / memcard routing, S6); clear it or it re-routes
     * every scene and drops the pending mode change. */
    gm_SetGameModeOverride(NULL);
    gm_ChangeGameModeAfterCurrentScene(mode);
    gm_801A4B60();
}

/* P-624/G-090: the title logo must be set up in the retail state.  When the
 * mode is GM_TITLE and gm_804D67EC is 0 (the skip-intro boot), the correct
 * path requests animation frame 400 and loops 400..1600; the opening-movie
 * path (`fn_801A1498`) instead re-requests `gm_804D67EC - 5130` every frame,
 * pinning the logo on its opaque frame-0 reveal card.  GX link 9 carries the
 * title logo; its JObj animation frame must be past the card. */
static void log_title_state(int force)
{
    HSD_GObj* gobj;
    f32 logo_anim = 0.0f;
    int found = 0;

    if (title_logged || HSD_GObjGXLinkHead == NULL ||
        (gm_GetCurrentGameMode() != GM_TITLE && !force))
    {
        return;
    }
    for (gobj = HSD_GObjGXLinkHead[9]; gobj != NULL; gobj = gobj->next_gx) {
        f32 anim;
        if (gobj->classifier != HSD_GOBJ_CLASS_UI || gobj->hsd_obj == NULL) {
            continue;
        }
        anim = mn_8022F298(gobj->hsd_obj);
        if (!found || anim > logo_anim) {
            logo_anim = anim;
        }
        found = 1;
    }
    if (!title_seen) {
        title_seen = 1;
        boot_triage_note("[title] first mode=%u logos=%d logo_anim=%.1f\n",
                         (unsigned) gm_GetCurrentGameMode(), found,
                         (double) logo_anim);
    }
    if (!force && (!found || logo_anim < 400.0f || logo_anim > 1600.0f)) {
        return;
    }
    title_logged = 1;
    boot_triage_note("[title] mode=%u logos=%d logo_anim=%.1f ok=%d\n",
                     (unsigned) gm_GetCurrentGameMode(), found,
                     (double) logo_anim,
                     found && logo_anim >= 400.0f && logo_anim <= 1600.0f &&
                         gm_GetCurrentGameMode() == GM_TITLE);
}

static void match_boot_frame(void)
{
    frame++;
    log_cpu_test();
    /* Before the start_frame gate below: the owner runs this on the ordinary
     * frontend, where start_frame is 0 and everything past that returns. */
    log_stadium_display();
    log_item_trace();
    check_item_velocity();
    log_shield_state();
    /* Also before the gate: MELEE_STUCK_TRACE is for the owner's own play,
     * which never enters the harness's match flow (P-780). */
    if (stuck_trace) {
        check_fighter_stuck();
        /* A fighter that runs away rather than wedging never trips the stuck
         * rule -- the Home-Run Contest sandbag slides forever, so `motion_id`
         * and `cur_anim_frame` both keep advancing while the contest waits for
         * it to come to rest.  Position is the thing to watch there, and
         * `log_match_state` already prints it; it is just gated to
         * `GM_DEBUG_VS` and to two slots.  With the trace on, print all four
         * every second in whatever mode is running (P-793). */
        /* Once a second is too coarse for a launch -- the Home-Run sandbag
         * covers 34,000 units between two samples, so every interesting frame
         * is invisible.  `MELEE_POS_TRACE=<n>` sets the sampling period in
         * frames; it is **separate from** `MELEE_STUCK_TRACE`'s value, which
         * is the wedge threshold.  Folding the two into one number made
         * `=1` mean "every 60 frames" and `=2` mean "every frame", which is
         * exactly the kind of switch that wastes someone's evening. */
        if ((frame % pos_period) == 0) {
            int slot;
            /* The camera's interest is what drives Home-Run Contest's ground
             * streaming: `grHomeRun_8021D680` derives the 64-segment window
             * from `cam_interest.x`, so if the camera stops following the
             * sandbag the ground runs out from under it and it falls forever
             * while the contest waits for it to rest (P-793).  Printing the
             * camera next to the fighters is what tells those two apart. */
            {
                Vec3 ci;
                Camera_GetTransformInterest(&ci);
                fprintf(stderr, "[pos] f=%u camera interest=(%.1f,%.1f)\n",
                        frame, (double) ci.x, (double) ci.y);
            }
            for (slot = 0; slot < MATCH_STUCK_SLOTS; slot++) {
                HSD_GObj* g = match_boot_fighter_gobj(slot);
                Fighter* f;
                Vec3 p;
                if (g == NULL || g->user_data == NULL) {
                    continue;
                }
                f = (Fighter*) g->user_data;
                ftLib_80086644((Fighter_GObj*) g, &p);
                fprintf(stderr,
                        "[pos] f=%u slot %d kind %d motion=%d "
                        "pos=(%.1f,%.1f,%.1f) vel=(%.2f,%.2f) "
                        "kb=(%.2f,%.2f) floor=%d env=0x%x "
                        "grv=%.3f norm=(%.3f,%.3f)\n",
                        frame, slot, (int) f->kind, (int) f->motion_id,
                        (double) p.x, (double) p.y, (double) p.z,
                        (double) f->self_vel.x, (double) f->self_vel.y,
                        (double) f->x8c_kb_vel.x, (double) f->x8c_kb_vel.y,
                        (int) f->coll_data.floor.index,
                        (unsigned) f->coll_data.env_flags,
                        (double) f->gr_vel,
                        (double) f->coll_data.floor.normal.x,
                        (double) f->coll_data.floor.normal.y);
            }
        }
    }
    /* P-749: HSD_ShadowSetSize asks for a fixed 32 KB and the owner sees it
     * fail after a 1P stage ends, so the question is whether the HSD heap
     * shrinks across scene changes.  Print the free total next to the mode
     * and scene so a leak shows up as a staircase. */
    /* P-751: is the RNG advancing the way the console's does?  Melee has no
     * entropy -- HSD_Rand seeds at 1 and steps per call -- so the variety a
     * player sees comes entirely from how many calls have happened by the
     * time something asks.  If the seed at a given scene is identical across
     * runs with different input timing, the port is not stepping it. */
    if (rng_trace && (frame % 30) == 0) {
        fprintf(stderr, "[rng] frame=%u mode=%u seed=0x%08x\n", frame,
                (unsigned) gm_GetCurrentGameMode(),
                (unsigned) *HSD_RandSeedPtr);
    }
    if (heap_trace && (frame % 60) == 0) {
        fprintf(stderr, "[heap] frame=%u mode=%u free=%ld\n", frame,
                (unsigned) gm_GetCurrentGameMode(),
                (long) OSCheckHeap(HSD_GetHeap()));
    }
    if (title_test) {
        unsigned deadline = start_frame != 0 ? start_frame : 1200;
        log_title_state(0);
        if (!title_logged && frame >= deadline) {
            log_title_state(1);
        }
    }
    if (start_frame == 0) {
        return;
    }
    if (frame < start_frame) {
        return;
    }

    /* MELEE_INTRO_TEST: GS_INTRO_EASY is the Classic "STAGE n" splash (the
     * stage-marker chain, the big VS, the fighter names).  The 1P menus are
     * the only in-game route to it, but gm_Mode_Debug_States carries the same
     * scene at state id 6, so force GM_DEBUG and step the state machine onto
     * it.  gm_SetNextGameModeStateId(n) lands on state id n (it stores n + 1
     * and gm_801A4014 takes next_state_id - 1), so 6 (P-700). */
    if (intro_test) {
        /* The debug-mode route leaves the saved language as JP, which picks
         * the JP name table; force US so the probe exercises the same path a
         * retail US save does (G-151). */
        if (getenv("MELEE_INTRO_US") != NULL) {
            gmMainLib_GetGamePrefs()->saved_language = LANG_US;
        }
        /* gm_GetCurrentSceneIndex returns the state machine's current state
         * id, not the GS_* scene kind, so match the debug table's id 6. */
        unsigned scene = (unsigned) gm_GetCurrentSceneIndex();
        if (gm_GetCurrentGameMode() == GM_DEBUG && scene == 6) {
            intro_stage = 1;
            return;
        }
        if (frame == start_frame || ((frame - start_frame) % 30) == 0) {
            boot_triage_note("[intro] frame %u: mode %u scene %u stage %d\n",
                             frame, (unsigned) gm_GetCurrentGameMode(), scene,
                             intro_stage);
            if (gm_GetCurrentGameMode() != GM_DEBUG) {
                match_boot_force(GM_DEBUG);
            } else {
                gm_SetNextGameModeStateId(6);
                gm_801A4B60();
            }
        }
        return;
    }

    /* MELEE_CLASSIC_TEST: drive 1P Classic instead of the debug VS scene, so
     * the Classic approach screen (stage-marker chain, the big "VS", the
     * fighter names) and the clear screen after it can be rendered headlessly
     * (P-700). */
    if (classic_test) {
        if (gm_GetCurrentGameMode() == GM_CLASSIC) {
            /* G-150: the fighter-name literals are non-ASCII.  The GameCube
             * build runs the source through sjiswrap so MWCC emits Shift-JIS
             * bytes, which is what HSD_SisLib_803A67EC looks up two at a
             * time.  Compiled as UTF-8 they start 0xEF/0xE3 and every lookup
             * fails, so the VS screen's names render as garbage or nothing.
             * A Shift-JIS lead byte is 0x81..0x9F. */
            if (!classic_named) {
                unsigned char lead = (unsigned char) gm_80160980(0)[0];
                classic_named = 1;
                boot_triage_note("[classic] name_lead=%02x sjis=%d\n",
                                 (unsigned) lead,
                                 lead >= 0x81 && lead <= 0x9F ? 1 : 0);
            }
            /* MELEE_CLASSIC_INTRO: step Classic onto its own state id 0,
             * which is the real GS_INTRO_EASY splash with
             * gmClassicIntroDataBuffer -- unlike MELEE_INTRO_TEST, which
             * borrows gm_Mode_Debug_States and so carries junk enter data. */
            if (getenv("MELEE_CLASSIC_INTRO") != NULL &&
                gm_GetCurrentSceneIndex() != 0 &&
                ((frame - start_frame) % 60) == 0)
            {
                boot_triage_note("[classic] -> intro (state %u)\n",
                                 (unsigned) gm_GetCurrentSceneIndex());
                gm_SetNextGameModeStateId(0);
                gm_801A4B60();
            }
            return;
        }
        if (frame == start_frame || ((frame - start_frame) % 30) == 0) {
            boot_triage_note("[classic] frame %u: mode %u\n", frame,
                             (unsigned) gm_GetCurrentGameMode());
            match_boot_force(GM_CLASSIC);
        }
        return;
    }
    /* Before anything else: whatever `onEnterDebugVs` just wrote into
     * `gmVsMelee_StartData`, put the matrix selection back (P-759). */
    install_match_selection();
    log_match_selection();
    if (!stuck_trace) {
        check_fighter_stuck(); /* already run above when tracing */
    }
    /* `onEnterDebugVs` starts every player with 0 stocks (the debug menu
     * usually overrides this) and the current game mode only flips to
     * GM_DEBUG_VS one scene later, so top the human players up while the VS
     * scene is being entered. */
    if (frame >= start_frame + 45 && stocks_frames < 60) {
        Player_SetStocks(0, 3);
        Player_SetStocks(1, 3);
        stocks_frames++;
        if (stocks_frames == 1) {
            boot_triage_note("[match] stocks set to 3\n");
        }
    }
    if (frame >= start_frame + 60 && (frame % 120) == 0) {
        log_match_state();
    }
    /* MELEE_GAMEOVER_TEST: drive a real match end into the 1P "clear"
     * overlay.  `onEnterDebugVs` starts a time match with the results
     * overlay disabled, while the 1P modes (`gm_8017CE34`) set
     * `rules.x4_4` and a stock match; only that combination makes
     * `fn_8016D634` call `gmregclear.c`'s `fn_80180630`, which is the path
     * that builds the screen-blur GObj (`lb_800138EC`).  Zeroing player 2's
     * stocks makes `gm_GetFFAOutcome` return `OUTCOME_ELIMINATION` on the
     * next frame, exactly like the last KO of a 1P stage. */
    if (gameover_test && gm_GetCurrentGameMode() == GM_DEBUG_VS) {
        VsSceneController* vs = gmVs_GetSceneController();
        if (gameover_stage == 0 && frame >= start_frame + 120) {
            vs->start.x4_4 = 1;
            vs->start.match_kind = MatchKind_Stock;
            Player_SetStocks(1, 0);
            gameover_stage = 1;
            boot_triage_note("[gameover] forced elimination at frame %u\n",
                             frame);
        } else if (gameover_stage == 1 && vs->state.unk_0 == 2) {
            gameover_stage = 2;
            boot_triage_note(
                "[gameover] clear scene active frame=%u outcome=%d\n", frame,
                (int) vs->state.match_result);
        } else if (gameover_stage == 2 && vs->state.unk_0 != 2) {
            gameover_stage = 3;
            boot_triage_note("[gameover] clear scene finished frame=%u\n",
                             frame);
        }
    }
    /* MELEE_ICON_TEST: the HUD stock icon frame is selected by
     * gm_80168B34/gm_80168BF8.  With the decompiled `base` left
     * uninitialized and gm_80168BF8 missing its return, every player asked
     * for frame 0 (Captain Falcon) and both bugs are invisible to a plain
     * boot.  Mario's frame must be CKind_Mario and DK's CKind_Donkey, and
     * the two live players (Link vs Mario) must differ. */
    if (icon_test && !icon_logged && frame >= start_frame + 90) {
        f32 p0 = gm_80168BF8(0);
        f32 p1 = gm_80168BF8(1);
        f32 mario = gm_80168B34(CKind_Mario, 0, 0);
        f32 dk = gm_80168B34(CKind_Donkey, 0, 0);
        int distinct =
            p0 != p1 && p0 != 0.0f && mario == 8.0f && dk == 1.0f;
        icon_logged = 1;
        boot_triage_note(
            "[icons] p0=%.1f p1=%.1f mario=%.1f dk=%.1f distinct=%d\n", p0,
            p1, mario, dk, distinct);
    }
    /* The boot chain keeps requesting its own mode changes (GM_BOOT states
     * move to GM_MEMCARD), so keep re-requesting until the VS mode sticks. */
    if (gm_GetCurrentGameMode() == GM_DEBUG_VS) {
        return;
    }
    if (frame == start_frame || ((frame - start_frame) % 30) == 0) {
        boot_triage_note("[match] frame %u: mode %u\n", frame,
                         (unsigned) gm_GetCurrentGameMode());
    }
    if (frame == start_frame || ((frame - start_frame) % 30) == 0) {
        match_boot_force(GM_DEBUG_VS);
    }
}

/* P-738: the Pokemon Stadium monitor has three possible sources and the
 * display state machine (`grStadium_801D2528`) picks which one the material
 * samples.  From outside, all three look like anonymous HSD_MemAlloc buffers,
 * so `MELEE_EFB_TRACE` alone cannot say whether a missing capture means "the
 * state was never active" or "the state was active and the capture did not
 * run".  Print the state the game is in alongside the three buffers and the
 * one the monitor's TObj currently points at.  Read-only; game code is the
 * specification, this just observes it. */
static void log_stadium_display(void)
{
    Ground_GObj* gobj;
    Ground* gp;
    HSD_ImageDesc* shown;

    /* Silence has to be impossible here: a probe that prints nothing whether
     * it is disarmed, on the wrong stage, or looking at a NULL object costs a
     * whole round trip to tell apart (G-168, and this probe already cost one
     * -- the frontend path installs the frame hook only for named env vars,
     * and MELEE_STADIUM_TRACE was not one of them). */
    static int armed_said;
    static int kind_said = -1;
    static int nogobj_said;

    if (!stadium_trace) {
        return;
    }
    if (!armed_said) {
        armed_said = 1;
        fprintf(stderr, "[stadium] trace armed\n");
    }
    if ((frame % 30) != 0) {
        return;
    }
    if (stage_info.grkind != Gr_Kind_PStadium) {
        if (kind_said != (int) stage_info.grkind) {
            kind_said = (int) stage_info.grkind;
            fprintf(stderr,
                    "[stadium] not on Pokemon Stadium (grkind=%d); nothing to "
                    "report\n",
                    kind_said);
        }
        return;
    }
    kind_said = -1;
    gobj = Ground_GetMapGObj(PsType_Display);
    if (gobj == NULL) {
        if (!nogobj_said) {
            nogobj_said = 1;
            fprintf(stderr,
                    "[stadium] on Stadium but PsType_Display has no GObj\n");
        }
        return;
    }
    gp = (Ground*) HSD_GObjGetUserData(gobj);
    if (gp == NULL) {
        return;
    }
    shown = gp->u.display.xC8 != NULL ? gp->u.display.xC8->imagedesc : NULL;
    fprintf(
        stderr,
        "[stadium] frame=%u state=%d prev=%d timer=%d player=%d "
        "tobj=%p shown=%p (%ux%u fmt=%u ptr=%p) text=%p feed=%p zoom=%p "
        "dirty=%d\n",
        frame, (int) gp->u.display.xE4, (int) gp->u.display.xEA,
        gp->u.display.xE0, (int) gp->u.display.xEE,
        (void*) gp->u.display.xC8, (void*) shown,
        shown != NULL ? (unsigned) shown->width : 0u,
        shown != NULL ? (unsigned) shown->height : 0u,
        shown != NULL ? (unsigned) shown->format : 0u,
        shown != NULL ? shown->image_ptr : NULL,
        (void*) gp->u.display.xD4, (void*) gp->u.display.xD8,
        (void*) gp->u.display.xDC, (int) gp->u.display.xF8_0);
}

/* ------------------------------------------------------------------------
 * Post-mortem fighter dump (P-796).
 *
 * Every member of the `lbvector.c` position-sanity family dies in the *render*
 * pass, one call below `ftLib_80086A8C`, and the backtrace on its own says
 * only that some joint's world matrix is absurd.  P-772 and P-781 each needed
 * two hardware watchpoints to get from that stack to the transform that was
 * actually wrong -- and a watchpoint is exactly what is unavailable when the
 * report arrives from the owner's own play session.
 *
 * So print, at the moment of the stop, what the watchpoints had to go and
 * find.  `prev_pos` and `pos_delta` are last frame's values, so the fighter
 * lines alone separate the two causes the family has had so far: a position
 * that was *integrated* into the absurd value (P-793's runaway, where
 * `pos_delta` is huge too) from one that was *written* (P-772, where
 * `prev_pos` is a few units away from a position of 1e14).  The joint chain
 * separates a bad fighter position from a bad animation track: walking from
 * the camera bone up to the root, the first level whose world translation is
 * sane is the level below the corruption.
 *
 * Read-only and re-entrancy-guarded: it runs on a path that is already dying,
 * so it must never be the reason a crash report is empty.
 */
static int crash_dump_active;

/* Written as the engine's own assert is written, not as `isnan() || fabs()`:
 * a NaN fails both comparisons, and that is the case both earlier instances
 * of this family turned out to be. */
static int pos3d_bad(float v)
{
    return !(v > -50000.0f && v < 50000.0f);
}

static int vec3_bad(const Vec3* v)
{
    return pos3d_bad(v->x) || pos3d_bad(v->y) || pos3d_bad(v->z);
}

static void dump_joint_chain(FILE* out, HSD_JObj* joint)
{
    int level;

    for (level = 0; joint != NULL && level < 24; level++) {
        const float* m = &joint->mtx[0][0];
        Vec3 world;
        world.x = joint->mtx[0][3];
        world.y = joint->mtx[1][3];
        world.z = joint->mtx[2][3];
        fprintf(out,
                "[crash]     j%-2d %p id=%u t=(%.6g,%.6g,%.6g) "
                "s=(%.6g,%.6g,%.6g) world=(%.6g,%.6g,%.6g)%s\n",
                level, (void*) joint, (unsigned) joint->id, joint->translate.x,
                joint->translate.y, joint->translate.z, joint->scale.x,
                joint->scale.y, joint->scale.z, world.x, world.y, world.z,
                vec3_bad(&world) ? "  <== BAD" : "");
        (void) m;
        joint = joint->parent;
    }
}

void match_boot_dump_fighters(FILE* out)
{
    HSD_GObj* gobj;
    int n;

    if (out == NULL || crash_dump_active || HSD_GObjGXLinkHead == NULL) {
        return;
    }
    crash_dump_active = 1;
    fprintf(out, "[crash] fighters at frame %u (mode %u scene %u):\n", frame,
            (unsigned) gm_GetCurrentGameMode(),
            (unsigned) gm_GetCurrentSceneIndex());
    n = 0;
    for (gobj = HSD_GObjGXLinkHead[5]; gobj != NULL && n < 8;
         gobj = gobj->next_gx)
    {
        Fighter* fp;
        CmSubject* box;
        int bone = -1;
        HSD_JObj* joint = NULL;

        if (gobj->classifier != HSD_GOBJ_CLASS_FIGHTER ||
            gobj->user_data == NULL)
        {
            continue;
        }
        n++;
        fp = (Fighter*) gobj->user_data;
        fprintf(out,
                "[crash]   #%d gobj=%p kind=%d player=%d motion=%d anim=%d "
                "ga=%d facing=%.3g scale=(%.4g,%.4g,%.4g)\n",
                n - 1, (void*) gobj, (int) fp->kind, (int) fp->player_id,
                (int) fp->motion_id, (int) fp->anim_id,
                (int) fp->ground_or_air, fp->facing_dir, fp->x34_scale.x,
                fp->x34_scale.y, fp->x34_scale.z);
        fprintf(out,
                "[crash]     cur=(%.6g,%.6g,%.6g) prev=(%.6g,%.6g,%.6g) "
                "delta=(%.6g,%.6g,%.6g)%s\n",
                fp->cur_pos.x, fp->cur_pos.y, fp->cur_pos.z, fp->prev_pos.x,
                fp->prev_pos.y, fp->prev_pos.z, fp->pos_delta.x,
                fp->pos_delta.y, fp->pos_delta.z,
                vec3_bad(&fp->cur_pos) ? "  <== BAD" : "");
        fprintf(out,
                "[crash]     self_vel=(%.6g,%.6g,%.6g) kb=(%.6g,%.6g,%.6g) "
                "gr_vel=%.6g\n",
                fp->self_vel.x, fp->self_vel.y, fp->self_vel.z,
                fp->x8c_kb_vel.x, fp->x8c_kb_vel.y, fp->x8c_kb_vel.z,
                fp->gr_vel);
        box = fp->x890_cameraBox;
        if (box != NULL) {
            fprintf(out,
                    "[crash]     cambox=%p state=%d pos=(%.6g,%.6g,%.6g) "
                    "bone_pos=(%.6g,%.6g,%.6g)%s\n",
                    (void*) box, (int) box->state, box->pos.x, box->pos.y,
                    box->pos.z, box->bone_pos.x, box->bone_pos.y,
                    box->bone_pos.z,
                    vec3_bad(&box->bone_pos) ? "  <== BAD" : "");
        }
        /* The same two inputs `ftLib_800866DC` uses, and in the same order:
         * a bone index out of range or a nonzero offset changes which of the
         * two readings of `lb_8000B1CC` applies, and P-781 spent a while
         * ruling both out by hand. */
        if (fp->ft_data != NULL && fp->ft_data->x0 != NULL) {
            bone = fp->ft_data->x0->camera_zoom_target_bone;
            fprintf(out, "[crash]     cam_bone=%d offset=(%.6g,%.6g,%.6g)\n",
                    bone, fp->co_attrs.x170.x, fp->co_attrs.x170.y,
                    fp->co_attrs.x170.z);
        }
        if (bone >= 0 && bone < 256 && fp->parts != NULL) {
            joint = fp->parts[bone].joint;
        }
        if (joint != NULL) {
            fprintf(out, "[crash]     camera-bone chain (bone -> root):\n");
            dump_joint_chain(out, joint);
        }
    }
    if (n == 0) {
        fprintf(out, "[crash]   (no live fighters)\n");
    }
    fflush(out);
    crash_dump_active = 0;
}

void match_boot_install_crash_dump(void)
{
    boot_triage_set_state_dumper(match_boot_dump_fighters);
}

int match_boot_classic_active(void)
{
    return classic_test && gm_GetCurrentGameMode() == GM_CLASSIC;
}

int match_boot_intro_active(void)
{
    return intro_test && intro_stage != 0;
}

int match_boot_gameover_active(void)
{
    return gameover_test && gameover_stage >= 2;
}

void match_boot_init(unsigned frame_in)
{
    start_frame = frame_in;
    frame = 0;
    if (getenv("MELEE_STADIUM_TRACE") != NULL) {
        stadium_trace = 1;
        boot_platform_set_frame_hook(match_boot_frame);
    }
    if (getenv("MELEE_RNG_TRACE") != NULL) {
        rng_trace = 1;
        boot_platform_set_frame_hook(match_boot_frame);
    }
    if (getenv("MELEE_TITLE_TEST") != NULL) {
        title_test = 1;
        boot_platform_set_frame_hook(match_boot_frame);
    }
    if (getenv("MELEE_CPU_TEST") != NULL) {
        cpu_test = 1;
        boot_platform_set_frame_hook(match_boot_frame);
    }
    {
        /* `MELEE_STUCK_TRACE=<frames>` sets how long a fighter has to be
         * frozen before it is reported; bare `=1` keeps the 2-second default,
         * which is short enough to catch the wedge and long enough that
         * standing still in Wait does not trip it. */
        const char* e = getenv("MELEE_STUCK_TRACE");
        if (e != NULL) {
            long n = strtol(e, NULL, 0);
            stuck_trace = 1;
            stuck_frames = n > 1 ? (unsigned) n : 0;
            {
                const char* pe = getenv("MELEE_POS_TRACE");
                long pn = pe != NULL ? strtol(pe, NULL, 0) : 0;
                if (pn > 0) {
                    pos_period = (unsigned) pn;
                }
            }
            boot_platform_set_frame_hook(match_boot_frame);
        }
    }
    {
        const char* e;
        if ((e = getenv("MELEE_MATCH_P0")) != NULL) {
            select_p0 = (int) strtol(e, NULL, 0);
        }
        if ((e = getenv("MELEE_MATCH_P1")) != NULL) {
            select_p1 = (int) strtol(e, NULL, 0);
        }
        if ((e = getenv("MELEE_MATCH_STAGE")) != NULL) {
            select_stage = (int) strtol(e, NULL, 0);
        }
        if ((e = getenv("MELEE_MATCH_ITEMS")) != NULL) {
            select_items = (int) strtol(e, NULL, 0);
        }
    }
    if (frame_in != 0) {
        if (getenv("MELEE_ITEM_TEST") != NULL) {
            item_trace = 1;
            item_verbose = getenv("MELEE_ITEM_VERBOSE") != NULL;
            build_item_test_input();
        } else if (getenv("MELEE_HIT_TEST") != NULL) {
            hit_test = 1;
            build_hit_test_input();
        } else {
            shield_test = getenv("MELEE_SHIELD_TEST") != NULL;
            build_match_input();
        }
        if (getenv("MELEE_HEAP_TRACE") != NULL) {
            heap_trace = 1;
        }
        if (getenv("MELEE_ICON_TEST") != NULL) {
            icon_test = 1;
        }
        if (getenv("MELEE_GAMEOVER_TEST") != NULL) {
            gameover_test = 1;
        }
        if (getenv("MELEE_CLASSIC_TEST") != NULL) {
            classic_test = 1;
        }
        if (getenv("MELEE_INTRO_TEST") != NULL) {
            intro_test = 1;
        }
        boot_platform_set_frame_hook(match_boot_frame);
    }
}

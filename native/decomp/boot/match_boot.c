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
#include <melee/ft/ftparts.h>
#include <sysdolphin/baselib/aobj.h>
#include <melee/ft/ftcpuattack.h>
#include <melee/if/forward.h>
#include <melee/mn/mnmain.h>
#include <melee/pl/player.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <math.h>

#include <melee/cm/camera.h>
#include <melee/gr/forward.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/jobj.h>

#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
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
static int select_cpu = -1;
static int select_cpu_kind = 4;
/* -2 means "leave onEnterDebugVs's choice"; -1..4 are real item_freq values
 * (-1 off, 4 very high), matching mnitemsw.c's menu index minus one. */
static int select_items = -2;
static int select_logged;
static int stadium_trace;
static int item_trace;
static int item_verbose;
static int shield_test;
static int special_test;
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
        /* MELEE_SPECIAL_TEST: no automated run has ever pressed B, so every
         * special move -- and with it most of the particle and effect work --
         * has never been drawn outside the owner's own play.  That is where
         * P-798 lives.  Held rather than tapped: Bowser's fire breath and the
         * other charge/stream specials only reach their effect while B is
         * down, and the hold has to outlast the startup. */
        if (special_test) {
            /* Four specials, not one.  The stick direction at the moment B is
             * pressed is what picks the move, so a neutral-only script
             * exercises a quarter of the special moves and none of the
             * articles the other three spawn -- P-800 is Pikachu's *down*
             * special, which nothing automated had ever run.  Each 120-frame
             * phase holds one direction with B down for 50 frames; the
             * fighter is airborne for part of every cycle thanks to the
             * jumps the base script already presses, so the aerial variants
             * run too. */
            unsigned cycle = (f >= 240) ? ((f - 240) / 120) % 4 : 0;
            unsigned phase = (f >= 240) ? (f - 240) % 120 : 0;
            if (f >= 240 && phase < 50) {
                signed char sx = 0;
                signed char sy = 0;
                switch (cycle) {
                case 1:
                    sx = 90; /* side */
                    break;
                case 2:
                    sy = 90; /* up */
                    break;
                case 3:
                    sy = -90; /* down */
                    break;
                default:
                    break; /* neutral */
                }
                p0->stick_x = sx;
                p0->stick_y = sy;
                p1->stick_x = sx;
                p1->stick_y = sy;
                p0->buttons |= PAD_BUTTON_B;
                p1->buttons |= PAD_BUTTON_B;
            }
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
        /*
         * MELEE_MATCH_CPU=<1..9>: both slots become CPUs at that level, which
         * is what a recording wants -- the harness otherwise leaves them as
         * human slots reading the scripted pad, so the match is two players
         * doing whatever the script says rather than a fight (P-862).
         *
         * **`cpu_kind` 4, not 0.**  0 is the training dummy -- it is what
         * `gmtrainingmode.c:132` gives the partner, and it jumps on the spot
         * and never closes distance, which is exactly what 0 produced here.
         * A normal player's type is 4: `Player_InitPlayer` (player.c:1961)
         * sets it as the default and `gmopeningmode.c:494` uses it for the
         * demo fighters that actually fight.  `MELEE_MATCH_CPU_KIND`
         * overrides it for anyone who wants one of the other behaviours.
         *
         * `onEnterDebugVs` has already filled the rest of the struct, so only
         * these three fields move.
         */
        if (select_cpu > 0) {
            int i;
            for (i = 0; i < 2; i++) {
                start->players[i].slot_type = Gm_PKind_Cpu;
                start->players[i].cpu_kind = (u8) select_cpu_kind;
                start->players[i].cpu_level = (u8) select_cpu;
            }
        }
    }
}

static void install_match_selection(void)
{
    if (select_p0 < 0 && select_p1 < 0 && select_stage < 0 &&
        select_cpu < 0) {
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
static int gobj_watch;
static void check_gobj_watch(void);
static unsigned stuck_frames; /* MELEE_STUCK_TRACE=<frames> overrides */
static unsigned anim_stall_frames; /* MELEE_ANIM_STALL=<frames> */
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
            "lstick=(%.3f,%.3f) held=0x%04x hitlag=%.1f invis=%d "
            /* The owner's wedge happens while holding and swinging the
             * Home-Run bat, so the item the fighter is carrying is part of
             * the state that matters.  `item_gobj` is the held article and
             * `x1988` is the hold/throw state the item paths drive; a wedge
             * with a live item and a frozen `anim_frame` is a different bug
             * from a wedge with neither (P-780). */
            "item=%p item_kind=%d x1988=%d subaction_timer=%.1f\n",
            why, slot, (unsigned) fp->x61A_controller_index, (int) fp->kind,
            (int) Player_GetStocks(slot), still, (int) fp->motion_id,
            (double) fp->cur_anim_frame, (double) fp->cur_pos.x,
            (double) fp->cur_pos.y, (double) fp->input.lstick[0].x,
            (double) fp->input.lstick[0].y,
            (unsigned) fp->input.held_buttons[0],
            (double) fp->dmg.x195c_hitlag_frames, (int) fp->invisible,
            (void*) fp->item_gobj,
            fp->item_gobj != NULL ? (int) itGetKind(fp->item_gobj) : -1,
            (int) fp->x1988,
            (double) fp->x3E4_fighterCmdScript.timer);
}

/* Motion states in which the engine is deliberately holding the fighter with
 * its animation frozen.  These are not wedges and must not be counted as
 * such, because the stuck rule's second term is `cur_anim_frame`, and these
 * states have no animation to advance.
 *
 * **Measured, not assumed.**  Kongo Jungle's barrel sets its own release
 * countdown from `yakumono_param->unk20/unk24` (`grkongo.c:442`), and on the
 * matrix's own seed it came out at **479 frames** -- longer than the
 * detector's 420-frame threshold, so `BarrelWait` was guaranteed to report
 * before the barrel had any chance to fire.  Four of the five `fighter stuck`
 * rows in the 754-cell matrix were this, on three different stages, and with
 * items on the Barrel Cannon is an item too, which is why Corneria was among
 * them (P-817).
 *
 * The cost of the exemption is that a fighter genuinely wedged *inside* a
 * barrel is no longer reported.  That is the right trade: the rule existed to
 * catch the owner's "my character stopped responding" (P-780), and five false
 * rows out of nine failures were burying every real one. */
static int motion_is_parked(int motion)
{
    switch (motion) {
    case 174: /* ftCo_MS_LiftWait   */
    case 293: /* ftCo_MS_BarrelWait */
    case 340: /* ftCo_MS_Barrel     */
        return 1;
    default:
        return 0;
    }
}

/* `MELEE_ANIM_STALL=<frames>`: a fighter state that never exits.
 *
 * Most of the "my character froze" reports (P-780) are a motion state whose
 * exit condition is `ftAnim_IsFramesRemaining(gobj) == 0`, and that is true
 * for as long as **any** part still has a live `HSD_AObj` -- `lb_8000B074`
 * is just "aobj != NULL && !(aobj->flags & AOBJ_NO_ANIM)".  So the question
 * is never "is the fighter stuck", it is **which part's animation never
 * ends**, and nothing printed that.
 *
 * On the owner's Home-Run bat wedge (`ftCo_MS_BatSwingDash`, 127) the
 * ordinary stuck rule stays quiet, because `cur_anim_frame` keeps advancing
 * while the state refuses to leave -- exactly the case the motion+anim test
 * cannot see.  This one keys on the motion id alone and prints the census
 * once per wedge: every part still holding the state open, with its AObj
 * flags, current frame and end frame.
 *
 * `AOBJ_LOOP` set on a part that should finish, or `curr_frame` stopped
 * short of `end_frame`, are the two shapes to look for and they point at
 * different files. */
static void check_anim_stall(void)
{
    static int last_motion[MATCH_STUCK_SLOTS];
    static unsigned held[MATCH_STUCK_SLOTS];
    static int said[MATCH_STUCK_SLOTS];
    int slot;

    if (anim_stall_frames == 0) {
        return;
    }
    for (slot = 0; slot < MATCH_STUCK_SLOTS; slot++) {
        HSD_GObj* gobj = match_boot_fighter_gobj(slot);
        Fighter* fp;
        int motion;

        if (gobj == NULL || gobj->user_data == NULL) {
            held[slot] = 0;
            said[slot] = 0;
            continue;
        }
        fp = (Fighter*) gobj->user_data;
        motion = (int) fp->motion_id;
        if (motion != last_motion[slot]) {
            last_motion[slot] = motion;
            held[slot] = 0;
            said[slot] = 0;
            continue;
        }
        held[slot]++;
        if (held[slot] != anim_stall_frames || said[slot]) {
            continue;
        }
        said[slot] = 1;
        fprintf(stderr,
                "[anim-stall] slot %d kind %d motion_id=%d for %u frames, "
                "anim_frame=%.2f blend=%.2f\n",
                slot, (int) fp->kind, motion, held[slot],
                (double) fp->cur_anim_frame,
                (double) fp->x8A4_animBlendFrames);
        {
            int i;
            int live = 0;
            int parts = (int) ftPartsTable[fp->kind]->parts_num;
            for (i = 0; i < parts && i < 0x8C; i++) {
                HSD_JObj* j;
                HSD_AObj* a;
                if (!fp->parts[i].flags_b1 || fp->parts[i].flags_b0 ||
                    fp->parts[i].flags_b5)
                {
                    continue;
                }
                j = fp->x8A4_animBlendFrames == 0.0f ? fp->parts[i].joint
                                                     : fp->parts[i].x4_jobj2;
                if (j == NULL) {
                    continue;
                }
                a = j->aobj;
                if (a == NULL || (a->flags & AOBJ_NO_ANIM)) {
                    continue;
                }
                live++;
                if (live > 6) {
                    continue; /* the first few are enough; total below */
                }
                fprintf(stderr,
                        "[anim-stall]   part %3d jobj=%p aobj=%p "
                        "flags=0x%08x%s curr=%.2f end=%.2f rate=%.2f "
                        "fobj=%p\n",
                        i, (void*) j, (void*) a, (unsigned) a->flags,
                        (a->flags & AOBJ_LOOP) ? " LOOP" : "",
                        (double) a->curr_frame, (double) a->end_frame,
                        (double) a->framerate, (void*) a->fobj);
            }
            fprintf(stderr,
                    "[anim-stall]   %d part(s) holding the state open "
                    "(a looping Wait/Walk is normal here; a *swing* that "
                    "loops is not)\n",
                    live);
        }
    }
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
        /* A fighter riding a barrel or a lift is parked on purpose, and a
         * fighter with no stocks left lying in `DeadDown` is eliminated and
         * correct -- neither is a wedge.  `DeadDown` is gated on the stock
         * count rather than exempted outright, because the same state with
         * stocks remaining *is* a stalled respawn and worth reporting. */
        if (motion_is_parked((int) fp->motion_id) ||
            ((int) fp->motion_id == 0 && Player_GetStocks(slot) <= 0))
        {
            still[slot] = deaf[slot] = 0;
            reported[slot] = 0;
            last_motion[slot] = (int) fp->motion_id;
            last_frame[slot] = fp->cur_anim_frame;
            last_x[slot] = fp->cur_pos.x;
            last_y[slot] = fp->cur_pos.y;
            continue;
        }
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
    check_anim_stall();
    check_gobj_watch();
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
                        "grv=%.3f norm=(%.3f,%.3f) "
                        /* The ECB is what one fighter pushes another with,
                         * so a per-character push anomaly ("this character
                         * has a force field", "I walk through that one") is
                         * a per-character ECB anomaly.  Printed as width and
                         * height plus the raw corners, because a degenerate
                         * box and an oversized one are the two failure modes
                         * and they look nothing alike (P-824). */
                        "ecb=%.2fx%.2f L%.2f R%.2f T%.2f B%.2f\n",
                        frame, slot, (int) f->kind, (int) f->motion_id,
                        (double) p.x, (double) p.y, (double) p.z,
                        (double) f->self_vel.x, (double) f->self_vel.y,
                        (double) f->x8c_kb_vel.x, (double) f->x8c_kb_vel.y,
                        (int) f->coll_data.floor.index,
                        (unsigned) f->coll_data.env_flags,
                        (double) f->gr_vel,
                        (double) f->coll_data.floor.normal.x,
                        (double) f->coll_data.floor.normal.y,
                        (double) (f->coll_data.ecb.right.x -
                                  f->coll_data.ecb.left.x),
                        (double) (f->coll_data.ecb.top.y -
                                  f->coll_data.ecb.bottom.y),
                        (double) f->coll_data.ecb.left.x,
                        (double) f->coll_data.ecb.right.x,
                        (double) f->coll_data.ecb.top.y,
                        (double) f->coll_data.ecb.bottom.y);
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
    /* MELEE_BOSSCAM=<frame>: run Master Hand's entry camera sequence on a
     * live match camera at that frame.  It is the only way to exercise the
     * boss-intro camera headlessly -- the Classic Master Hand fight is the
     * eleventh round and the harness cannot win ten matches to get there --
     * and it reproduces P-847's panic exactly, backtrace for backtrace.
     * The calls are `ftmasterhandentry.c:70`'s `ftMh_UnkEnum0_Unk00` arm
     * verbatim, minus the enemy lookup that picks the interest slot. */
    {
        const char* e = getenv("MELEE_BOSSCAM");
        static int boss_cam_done;
        if (e != NULL && !boss_cam_done &&
            frame >= (unsigned) strtoul(e, NULL, 0))
        {
            boss_cam_done = 1;
            boot_triage_note("[bosscam] frame %u: entry camera sequence\n",
                             frame);
            Camera_8002E6FC(0);
            Camera_8002ED9C(40.0f);
            Camera_8002EEC8(45.0f);
            Camera_8002EC7C(-M_PI);
            Camera_8002EF14();
            Camera_8002EC7C(0.0f);
            Camera_8002F0E4(120);
        }
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

/* `Fighter::kind` is a **`FighterKind`**, which is not `CharacterKind`: the
 * two enums order the roster differently, so 12 is Pikachu here and Peach
 * there.  Printing the bare number invites exactly that misreading -- it cost
 * a wrong theory on P-800, where the dump was right and the reading of it was
 * not.  Name it. */
static const char* fighter_kind_name(int kind)
{
    static const char* const names[] = {
        "Mario",  "Fox",     "Captain", "Donkey",   "Kirby",  "Koopa",
        "Link",   "Seak",    "Ness",    "Peach",    "Popo",   "Nana",
        "Pikachu","Samus",   "Yoshi",   "Purin",    "Mewtwo", "Luigi",
        "Mars",   "Zelda",   "CLink",   "DrMario",  "Falco",  "Pichu",
        "GameWatch", "Ganon", "Emblem", "MasterH",  "CrezyH", "Boy",
        "Girl",   "GKoops",  "Sandbag",
    };
    if (kind < 0 || (size_t) kind >= sizeof(names) / sizeof(names[0])) {
        return "?";
    }
    return names[kind];
}

/* A crash dump that crashes tells you nothing.
 *
 * `match_boot_dump_fighters` runs from the signal handler and from `__assert`,
 * and it walks whatever state the game was in -- which, by definition, is
 * state that has just gone wrong.  The owner hit exactly that: a gobj on the
 * fighter GX link whose `user_data` was not a `Fighter` (kind -1, player 255,
 * `self_vel` all NaN) got through the `classifier` test, and the dump faulted
 * on its `x890_cameraBox` before printing a single useful line about the
 * *real* crash (P-816).
 *
 * MEM1 is a fixed 24 MB mapping at 0x80000000 (`native/platform/os.c`), so a
 * pointer that is going to be dereferenced can be checked first.  This is not
 * a guarantee that the object is what it claims -- it cannot be -- but it
 * turns "the report dies" into "the report says the pointer was bad", which
 * is the more useful of the two and is the one the next reader needs. */
#define MEM1_BASE 0x80000000u
#define MEM1_SIZE (24u * 1024u * 1024u)

static int mem1_ok(const void* p, size_t n)
{
    uintptr_t a = (uintptr_t) p;

    if (p == NULL || (a & 3u) != 0) {
        return 0;
    }
    return a >= MEM1_BASE && n <= (size_t) MEM1_SIZE &&
           a - MEM1_BASE <= (uintptr_t) MEM1_SIZE - n;
}

/* **`Fighter::gobj` is at offset 0, and that is what makes this exact.**
 *
 * `HSD_ObjFree` (objalloc.c:119) pushes a freed block onto the pool's free
 * list by writing the list link into the block's **first word** -- and a
 * `Fighter`'s first word is its own `gobj` back-pointer (ft/types.h:1331,
 * written by `initFighter`, fighter.c:726).  So the moment a `Fighter` is
 * freed, `fp->gobj` becomes a pointer to another block of `fighter_alloc_data`
 * and can never again equal the gobj that points at it.  The same holds once
 * the block is handed out again and overwritten by its new owner.
 *
 * `fp->gobj == gobj` is therefore not a plausibility heuristic like the
 * `mem1_ok` tests around it: it is a **freed-or-reused detector with no false
 * positives**, two instructions wide.  Every live fighter passes it; a
 * fighter whose memory has been released cannot.
 *
 * This is the invariant P-816, P-836 and P-843 all break, one frame before
 * they are seen.  Checking it every frame is what turns "SIGSEGV four minutes
 * in" into "went stale at frame N, on the transition out of scene S". */
static int fighter_gobj_ok(const HSD_GObj* gobj)
{
    const Fighter* fp;

    if (gobj->classifier != HSD_GOBJ_CLASS_FIGHTER || gobj->user_data == NULL)
    {
        return 1;
    }
    fp = (const Fighter*) gobj->user_data;
    if (!mem1_ok(fp, sizeof(*fp))) {
        return 0;
    }
    return fp->gobj == gobj;
}

static void dump_joint_chain(FILE* out, HSD_JObj* joint)
{
    int level;

    for (level = 0; mem1_ok(joint, sizeof(*joint)) && level < 24; level++) {
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

/* P-800: the gobj whose proc was running when the process died.
 *
 * The owner's Pikachu Thunder crash printed two fighters -- Peach and Fox --
 * while the stack was three frames deep inside Pikachu's down special.  The
 * walk below only sees GX link 5, so a gobj that is mid-teardown, or simply
 * not on that link, is invisible exactly when it is the one that matters.
 * `HSD_GObj_CurrentInvokedProcGObj` is the engine's own record of what
 * `HSD_GObj_RunProcs` was invoking, so it names the culprit directly whether
 * or not the link list still knows about it. */
static void dump_items(FILE* out)
{
    HSD_GObj* gobj;
    int n = 0;

    if (HSD_GObjGXLinkHead == NULL) {
        return;
    }
    for (gobj = HSD_GObjGXLinkHead[6]; mem1_ok(gobj, sizeof(*gobj)) && n < 12;
         gobj = gobj->next_gx)
    {
        Item* ip;
        if (gobj->classifier != HSD_GOBJ_CLASS_ITEM ||
            gobj->user_data == NULL)
        {
            continue;
        }
        ip = (Item*) gobj->user_data;
        /* Same reasoning as `dump_fighter` (G-204): on the frontend the
         * owner hit a crash whose item list printed `kind=-2132339776` with
         * an unaligned `entity`, which is not an `Item` at all.  Following it
         * is how a crash report becomes a second crash. */
        if (!mem1_ok(ip, sizeof(*ip))) {
            fprintf(out, "[crash]   item #%d gobj=%p user_data=%p NOT IN MEM1"
                         " -- not an Item; skipped\n",
                    n, (void*) gobj, (void*) ip);
            n++;
            continue;
        }
        n++;
        fprintf(out,
                "[crash]   item #%d gobj=%p kind=%d entity=%p article=%p "
                "pos=(%.4g,%.4g,%.4g)\n",
                n - 1, (void*) gobj, (int) ip->kind, (void*) ip->entity,
                (void*) ip->xC4_article_data, ip->pos.x, ip->pos.y,
                ip->pos.z);
    }
    if (n == 0) {
        fprintf(out, "[crash]   (no live items)\n");
    }
}

static void dump_fighter(FILE* out, const char* tag, HSD_GObj* gobj)
{
    Fighter* fp = (Fighter*) gobj->user_data;
    CmSubject* box;
    int bone = -1;
    HSD_JObj* joint = NULL;

    if (!mem1_ok(fp, sizeof(*fp))) {
        fprintf(out, "[crash]   %s gobj=%p user_data=%p NOT IN MEM1 -- not a "
                     "Fighter; skipped\n",
                tag, (void*) gobj, (void*) fp);
        return;
    }
    /* **The classifier alone does not mean this is a fighter.**  See
     * `check_gobj_watch`: `HSD_GOBJ_CLASS_FIGHTER` is 4 and the menus create
     * their widgets with a bare `GObj_Create(4, 5, 0x80)`.  Printing one of
     * those through the lines below is what produced the
     * `kind=?(-1) player=255 motion=-1 scale=(0.6,0.6,1)` entries that P-816,
     * P-836 and P-843 all read as a corrupted fighter.  `fp->gobj == gobj`
     * settles it exactly, so say which of the two this is instead of
     * implying the answer. */
    if (fp->gobj != gobj) {
        char sym[128];
        boot_triage_symbol((const void*) (uintptr_t)
                               gobj->user_data_remove_func,
                           sym, sizeof(sym));
        fprintf(out,
                "[crash]   %s gobj=%p user_data=%p class=%u p_link=%u "
                "kind=%u remove_fn=%s\n",
                tag, (void*) gobj, (void*) fp, (unsigned) gobj->classifier,
                (unsigned) gobj->p_link, (unsigned) gobj->user_data_kind,
                sym);
        fprintf(out,
                "[crash]     NOT A FIGHTER -- fp->gobj=%p, so this is either "
                "a menu object sharing class 4 or a freed Fighter\n",
                (void*) fp->gobj);
        return;
    }
    fprintf(out,
            "[crash]   %s gobj=%p kind=%s(%d) player=%d motion=%d anim=%d "
            "ga=%d facing=%.3g scale=(%.4g,%.4g,%.4g)\n",
            tag, (void*) gobj, fighter_kind_name((int) fp->kind),
            (int) fp->kind, (int) fp->player_id,
            (int) fp->motion_id, (int) fp->anim_id, (int) fp->ground_or_air,
            fp->facing_dir, fp->x34_scale.x, fp->x34_scale.y,
            fp->x34_scale.z);
    fprintf(out,
            "[crash]     cur=(%.6g,%.6g,%.6g) prev=(%.6g,%.6g,%.6g) "
            "delta=(%.6g,%.6g,%.6g)%s\n",
            fp->cur_pos.x, fp->cur_pos.y, fp->cur_pos.z, fp->prev_pos.x,
            fp->prev_pos.y, fp->prev_pos.z, fp->pos_delta.x, fp->pos_delta.y,
            fp->pos_delta.z, vec3_bad(&fp->cur_pos) ? "  <== BAD" : "");
    fprintf(out,
            "[crash]     self_vel=(%.6g,%.6g,%.6g) kb=(%.6g,%.6g,%.6g) "
            "gr_vel=%.6g\n",
            fp->self_vel.x, fp->self_vel.y, fp->self_vel.z,
            fp->x8c_kb_vel.x, fp->x8c_kb_vel.y, fp->x8c_kb_vel.z, fp->gr_vel);
    /* The article this fighter is holding on to.  P-800 died dereferencing
     * one of these from `ftPk_SpecialLw_8012765C`, and the fighter lines
     * alone could not say whether the pointer was stale. */
    fprintf(out,
            "[crash]     item_gobj=%p held=%p mv.pk.speciallw.x0=%p\n",
            (void*) fp->item_gobj, (void*) fp->x1984_heldItemSpec,
            (void*) fp->mv.pk.speciallw.x0);
    box = fp->x890_cameraBox;
    if (box != NULL && !mem1_ok(box, sizeof(*box))) {
        fprintf(out, "[crash]     cambox=%p BAD POINTER\n", (void*) box);
    } else if (box != NULL) {
        fprintf(out,
                "[crash]     cambox=%p state=%d pos=(%.6g,%.6g,%.6g) "
                "bone_pos=(%.6g,%.6g,%.6g)%s\n",
                (void*) box, (int) box->state, box->pos.x, box->pos.y,
                box->pos.z, box->bone_pos.x, box->bone_pos.y, box->bone_pos.z,
                vec3_bad(&box->bone_pos) ? "  <== BAD" : "");
    }
    /* The same two inputs `ftLib_800866DC` uses, and in the same order:
     * a bone index out of range or a nonzero offset changes which of the
     * two readings of `lb_8000B1CC` applies, and P-781 spent a while
     * ruling both out by hand. */
    if (mem1_ok(fp->ft_data, sizeof(*fp->ft_data)) &&
        mem1_ok(fp->ft_data->x0, sizeof(*fp->ft_data->x0)))
    {
        bone = fp->ft_data->x0->camera_zoom_target_bone;
        fprintf(out, "[crash]     cam_bone=%d offset=(%.6g,%.6g,%.6g)\n",
                bone, fp->co_attrs.x170.x, fp->co_attrs.x170.y,
                fp->co_attrs.x170.z);
    }
    if (bone >= 0 && bone < 256 &&
        mem1_ok(fp->parts, ((size_t) bone + 1) * sizeof(fp->parts[0])))
    {
        joint = fp->parts[bone].joint;
    }
    if (joint != NULL) {
        fprintf(out, "[crash]     camera-bone chain (bone -> root):\n");
        dump_joint_chain(out, joint);
    }
}

/* `MELEE_GOBJ_WATCH=1`: two invariants, checked on every proc, every frame.
 *
 * **1. A queued proc must still be owned by its gobj.**  `HSD_GObj_RunProcs`
 * walks `HSD_GObj_GObjProcHead[s_link]` and calls `proc->on_invoke(proc->gobj)`
 * for each entry; `HSD_GObjFree` is supposed to take every one of a gobj's
 * procs off that queue through `HSD_GObjProc_RemoveAllProcs`.  So for a live
 * proc, walking `proc->gobj->proc` down the `child` chain must find `proc`
 * again.  A proc the queue holds but its owner does not is **leaked**: the
 * gobj behind it has been freed, and the entry is a call into whatever now
 * owns that memory.  That is P-836's unfound producer, stated as a test.
 *
 * **2. A fighter proc must run on a live `Fighter`.**  See `fighter_gobj_ok`:
 * `Fighter::gobj` is the first word of the struct and therefore the word
 * `HSD_ObjFree` overwrites, so the check is exact.
 *
 * **Why this is not the obvious check, which does not work.**  The natural
 * test is "walk the gobjs and look at the ones classified as fighters", and
 * it is wrong: `HSD_GOBJ_CLASS_FIGHTER` is **4**, and the menus create their
 * own widgets with a bare `GObj_Create(4, 5, 0x80)` -- `mncharsel.c:4571`,
 * `mnstagesel.c` in a dozen places, `mnmain.c:793`, `toy.c:5909`.  Nothing
 * reserves the number; only `fighter.c:852` and `ftdemo.c:60` make real
 * fighters, and those use **p_link 8**.  The first draft of this watch fired
 * on a character-select name tag -- `user_data_kind=4`, `remove_fn=HSD_Free`,
 * `on_invoke=fn_802633B0` -- within 112 frames of entering Classic.
 *
 * **That same confusion is why three crash reports pointed nowhere.**
 * `match_boot_dump_fighters` filtered on the classifier alone, so every
 * `kind=?(-1) player=255 motion=-1 scale=(0.6,0.6,1)` line in P-816, P-836
 * and P-843 is a menu widget being printed as a `Fighter`, and the
 * "non-Fighter on the fighter GX link" all three chased was the dump's own
 * doing.  Both are fixed here: the watch keys on the proc, and the dump
 * labels what it cannot vouch for.
 *
 * Costs a pointer chase per queued proc, so it can run for a whole session.
 * It reports and keeps going; it repairs nothing. */
static int proc_owned_by_gobj(const HSD_GObjProc* proc, const HSD_GObj* gobj)
{
    const HSD_GObjProc* cur;
    unsigned depth;

    for (cur = gobj->proc, depth = 0; cur != NULL && depth < 32;
         cur = cur->child, depth++)
    {
        if (cur == proc) {
            return 1;
        }
        if (!mem1_ok(cur->child, sizeof(*cur))) {
            break;
        }
    }
    return 0;
}

static int is_fighter_proc(HSD_GObjEvent fn)
{
    return fn == Fighter_procUpdate || fn == Fighter_procMap ||
           fn == Fighter_8006A360 || fn == Fighter_8006C80C ||
           fn == Fighter_8006D9AC;
}

static void report_bad_proc(const HSD_GObjProc* proc, const HSD_GObj* gobj,
                            unsigned s_link, const char* what)
{
    char sym[128];
    const Fighter* fp = (const Fighter*) gobj->user_data;

    boot_triage_symbol((const void*) (uintptr_t) proc->on_invoke, sym,
                       sizeof(sym));
    boot_triage_note("[gobj-watch] frame %u: %s -- s_link=%u proc=%p "
                     "on_invoke=%s\n",
                     frame, what, (unsigned) s_link, (const void*) proc, sym);
    boot_triage_symbol((const void*) (uintptr_t) gobj->user_data_remove_func,
                       sym, sizeof(sym));
    boot_triage_note("[gobj-watch]   gobj=%p class=%u p_link=%u prio=%u "
                     "gx_link=%u user_data=%p kind=%u remove_fn=%s\n",
                     (const void*) gobj, (unsigned) gobj->classifier,
                     (unsigned) gobj->p_link, (unsigned) gobj->p_priority,
                     (unsigned) gobj->gx_link, gobj->user_data,
                     (unsigned) gobj->user_data_kind, sym);
    if (is_fighter_proc(proc->on_invoke)) {
        boot_triage_note("[gobj-watch]   fp->gobj=%p (want %p)\n",
                         mem1_ok(fp, sizeof(*fp)) ? (const void*) fp->gobj
                                                  : NULL,
                         (const void*) gobj);
    }
    boot_triage_note("[gobj-watch]   mode=%u scene=%u\n",
                     (unsigned) gm_GetCurrentGameMode(),
                     (unsigned) gm_GetCurrentSceneIndex());
}

/* The proc walk below cannot see everything.  P-843's dump had **two**
 * fighter gobjs on GX link 5 -- `p_link=8`,
 * `remove_fn=Fighter_Unload_8006DABC`, so real fighters, not the menu
 * widgets G-219 is about -- whose `Fighter` was gone: one with
 * `fp->gobj = 0xffffffff` and one whose `user_data` was `0xffffffff`
 * outright.  Neither carried a proc any more, so neither would ever have
 * tripped a proc-keyed check, and both were still on the link the engine
 * draws from.
 *
 * `0xffffffff` in `fp->gobj` is worth saying out loud: a `Fighter` released
 * through `HSD_ObjFree` has the pool's **free-list link** in that word, which
 * is a live pointer into `fighter_alloc_data`.  `0xffffffff` is not that.
 * The memory was written over, not merely freed. */
static void check_gx_fighters(unsigned* reports)
{
    HSD_GObj* gobj;
    unsigned n = 0;

    if (HSD_GObjGXLinkHead == NULL) {
        return;
    }
    for (gobj = HSD_GObjGXLinkHead[5]; gobj != NULL && n < 64 && *reports < 16;
         gobj = gobj->next_gx, n++)
    {
        const Fighter* fp;
        char sym[128];
        if (!mem1_ok(gobj, sizeof(*gobj)) ||
            gobj->classifier != HSD_GOBJ_CLASS_FIGHTER ||
            gobj->user_data == NULL)
        {
            continue;
        }
        /* Only the real thing: p_link 8 and the fighter destructor.  A menu
         * widget shares the classifier but neither of those (G-219). */
        if (gobj->p_link != 8 ||
            gobj->user_data_remove_func != Fighter_Unload_8006DABC)
        {
            continue;
        }
        if (fighter_gobj_ok(gobj)) {
            continue;
        }
        (*reports)++;
        fp = (const Fighter*) gobj->user_data;
        boot_triage_symbol((const void*) (uintptr_t)
                               gobj->user_data_remove_func,
                           sym, sizeof(sym));
        boot_triage_note("[gobj-watch] frame %u: dead Fighter still on GX "
                         "link 5 -- gobj=%p user_data=%p fp->gobj=%p\n",
                         frame, (void*) gobj, gobj->user_data,
                         mem1_ok(fp, sizeof(*fp)) ? (const void*) fp->gobj
                                                  : NULL);
        boot_triage_note("[gobj-watch]   p_link=%u gx_link=%u kind=%u "
                         "remove_fn=%s proc=%p mode=%u scene=%u\n",
                         (unsigned) gobj->p_link, (unsigned) gobj->gx_link,
                         (unsigned) gobj->user_data_kind, sym,
                         (void*) gobj->proc,
                         (unsigned) gm_GetCurrentGameMode(),
                         (unsigned) gm_GetCurrentSceneIndex());
    }
}

static void check_gobj_watch(void)
{
    static unsigned reports;
    unsigned s;

    if (!gobj_watch || HSD_GObj_GObjProcHead == NULL || reports >= 16) {
        return;
    }
    /* **Periodic, not per-frame, and the default period is not 1.**  The walk
     * is the whole proc queue, which on the frontend is hundreds of entries,
     * and running it every frame cost enough to make `decomp_opening` and
     * `decomp_frontend_card` miss their deadlines -- the probe changing the
     * result it is measuring.  A leaked proc does not heal: once the queue
     * holds an entry its gobj disowns, it stays there until something runs
     * it.  So sampling twice a second finds it in the same transition a
     * per-frame check would, for a thirtieth of the cost. */
    if ((frame % (unsigned) gobj_watch) != 0) {
        return;
    }
    check_gx_fighters(&reports);
    for (s = 0; s <= (unsigned) HSD_GObjLibInitData.gproc_pri_max; s++) {
        const HSD_GObjProc* proc;
        unsigned n = 0;
        for (proc = HSD_GObj_GObjProcHead[s];
             proc != NULL && n < 4096 && reports < 16;
             proc = proc->next, n++)
        {
            const HSD_GObj* gobj;
            if (!mem1_ok(proc, sizeof(*proc))) {
                boot_triage_note("[gobj-watch] frame %u: s_link=%u queue "
                                 "holds a bad proc %p\n",
                                 frame, s, (const void*) proc);
                reports++;
                break;
            }
            gobj = proc->gobj;
            if (!mem1_ok(gobj, sizeof(*gobj))) {
                boot_triage_note("[gobj-watch] frame %u: s_link=%u proc=%p "
                                 "has a bad gobj %p\n",
                                 frame, s, (const void*) proc,
                                 (const void*) gobj);
                reports++;
                continue;
            }
            if (!proc_owned_by_gobj(proc, gobj)) {
                reports++;
                report_bad_proc(proc, gobj, s, "leaked proc: its gobj does "
                                               "not own it");
                continue;
            }
            if (is_fighter_proc(proc->on_invoke) && !fighter_gobj_ok(gobj)) {
                reports++;
                report_bad_proc(proc, gobj, s,
                                "fighter proc on a dead Fighter");
                continue;
            }
            /* **`Fighter::x61C` is an index into a six-element array, and
             * nothing in the game bounds it.**  `ftData_800859A8`
             * (ftdata.c:1668) returns early on -1 and otherwise does
             * `ft_8045993C[fp->x61C].x6_b0 = false` -- an unbounded
             * read-modify-write through an `s8`.  `Player_80031AD0` sets it
             * to -1 for every match fighter and nothing ever assigns
             * anything else, so -1 is the only value that can legitimately
             * be here; `ftdemo.c`'s demo fighters used to inherit stack
             * garbage instead (P-843).  Checking it here is how that fix
             * stays fixed. */
            /* **Three fields of a live `Fighter` come back as `0xFFFFFFFF`
             * on the screen the P-843 crashes happen on**, while everything
             * around them stays sane: `ground_or_air` (+0xE0),
             * `gr_vel` (+0xEC) and `item_gobj` (+0x1974).  The last one is
             * what kills it -- `Fighter_8006A360` guards the call with
             * `if (fp->item_gobj)`, which `0xFFFFFFFF` passes, and
             * `itGetKind` then reads `+0x2C` off it: `0xFFFFFFFF + 0x2C`
             * wraps to **`0x2b`**, the reported fault address.
             *
             * None of the three can be left over from creation.
             * `ftDemo_CreateFighter` calls `Fighter_UnkProcessDeath_80068354`
             * unconditionally (ftdemo.c:161) and that calls
             * `Fighter_UnkInitReset_80067C98`, which sets `item_gobj = 0`.
             * So they are **written after the fighter is built**, and the
             * frame that happens on is the thing five reports have not had.
             *
             * `ground_or_air` is a 4-byte enum that is only ever `GA_Ground`
             * or `GA_Air`, and `gr_vel` is a speed -- neither has a legal
             * NaN or -1. */
            if (is_fighter_proc(proc->on_invoke)) {
                const Fighter* fp = (const Fighter*) gobj->user_data;
                int ga = (int) fp->ground_or_air;
                if (ga != 0 && ga != 1) {
                    reports++;
                    report_bad_proc(proc, gobj, s, "Fighter::ground_or_air "
                                                   "is neither ground nor "
                                                   "air");
                    boot_triage_note("[gobj-watch]   ground_or_air=%d "
                                     "gr_vel=%g item_gobj=%p\n",
                                     ga, (double) fp->gr_vel,
                                     (const void*) fp->item_gobj);
                    continue;
                }
                if (fp->item_gobj != NULL &&
                    !mem1_ok(fp->item_gobj, sizeof(*gobj)))
                {
                    reports++;
                    report_bad_proc(proc, gobj, s,
                                    "Fighter::item_gobj is not a gobj");
                    boot_triage_note("[gobj-watch]   item_gobj=%p\n",
                                     (const void*) fp->item_gobj);
                    continue;
                }
            }
            if (is_fighter_proc(proc->on_invoke)) {
                const Fighter* fp = (const Fighter*) gobj->user_data;
                int slot = (int) fp->x61C;
                if (slot != -1 && (slot < 0 || slot >= 6)) {
                    reports++;
                    report_bad_proc(proc, gobj, s,
                                    "Fighter::x61C out of range");
                    boot_triage_note("[gobj-watch]   x61C=%d (want -1, or "
                                     "0..5 for ft_8045993C)\n",
                                     slot);
                }
            }
        }
    }
}

void match_boot_dump_fighters(FILE* out)
{
    HSD_GObj* gobj;
    char tag[16];
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
        if (gobj->classifier != HSD_GOBJ_CLASS_FIGHTER ||
            gobj->user_data == NULL)
        {
            continue;
        }
        snprintf(tag, sizeof(tag), "#%d", n);
        n++;
        dump_fighter(out, tag, gobj);
    }
    if (n == 0) {
        fprintf(out, "[crash]   (no live fighters)\n");
    }
    /* The gobj the engine was mid-proc on, whether or not it is on the link
     * walked above.  Printed unconditionally: "the running gobj is also the
     * first one listed" is itself worth knowing. */
    if (HSD_GObj_CurrentInvokedProcGObj != NULL) {
        HSD_GObj* cur = HSD_GObj_CurrentInvokedProcGObj;
        fprintf(out, "[crash]   in-proc gobj=%p class=%d user_data=%p\n",
                (void*) cur, (int) cur->classifier, cur->user_data);
        if (cur->classifier == HSD_GOBJ_CLASS_FIGHTER &&
            cur->user_data != NULL)
        {
            dump_fighter(out, "in-proc", cur);
        }
    }
    dump_items(out);
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
    {
        /* `MELEE_GOBJ_WATCH=<frames>` sets the sampling period, and bare
         * `=1` keeps the half-second default -- the same shape as
         * `MELEE_STUCK_TRACE`, so the two read alike. */
        const char* e = getenv("MELEE_GOBJ_WATCH");
        if (e != NULL) {
            long n = strtol(e, NULL, 0);
            gobj_watch = n > 1 ? (int) n : 30;
            boot_platform_set_frame_hook(match_boot_frame);
        }
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
        const char* ase = getenv("MELEE_ANIM_STALL");
        if (ase != NULL) {
            long n = strtol(ase, NULL, 0);
            anim_stall_frames = n > 1 ? (unsigned) n : 90;
            boot_platform_set_frame_hook(match_boot_frame);
        }
    }
    {
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
        if ((e = getenv("MELEE_MATCH_CPU")) != NULL) {
            select_cpu = (int) strtol(e, NULL, 0);
            if (select_cpu > 9) {
                select_cpu = 9;
            }
        }
        if ((e = getenv("MELEE_MATCH_CPU_KIND")) != NULL) {
            select_cpu_kind = (int) strtol(e, NULL, 0);
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
            special_test = getenv("MELEE_SPECIAL_TEST") != NULL;
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

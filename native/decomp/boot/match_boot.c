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
#include <melee/ft/fighter.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ftcpuattack.h>
#include <melee/if/forward.h>
#include <melee/mn/mnmain.h>
#include <melee/pl/player.h>
#include <melee/it/forward.h>
#include <melee/it/types.h>
#include <melee/gr/forward.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/tobj.h>

#include <sysdolphin/baselib/gobj.h>

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
static int stadium_trace;
static int item_trace;
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

        /* Taps first (Mario/Fox-style instant projectiles), then a long hold
         * and a release (Link/Samus-style charged ones). */
        if (f >= 200 && f < 420 && (f % 40) < 8) {
            p0->buttons |= PAD_BUTTON_B;
        }
        if (f >= 500 && f < 560) {
            p0->buttons |= PAD_BUTTON_B;
        }
        if (f >= 640 && f < 700) {
            p0->buttons |= PAD_BUTTON_B;
        }
        match_input[f][1].buttons |=
            (f >= 220 && f < 460 && (f % 40) < 6) ? PAD_BUTTON_B : 0;
    }
    pad_set_input_script(&match_input[0][0], MATCH_INPUT_CHANNELS,
                         MATCH_INPUT_FRAMES);
}

/* P-739: no projectile special produces an article.  Everything between the
 * fighter and the screen is invisible from outside -- the command script, the
 * throw flag, `Item_8026862C`, the item's own state table -- so count the
 * live item GObjs and name what they are.  Items are class 6 on GX link 6
 * (`Item_8026862C` -> `GObj_SetupGXLink(gobj, ..., 6, 0)`). */
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

static void log_match_state(void)
{
    int slot;
    static int attrs_logged;
    for (slot = 0; slot < 2; slot++) {
        HSD_GObj* gobj = Player_GetEntity(slot);
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
        if (hit_test && !hit_logged && Player_GetDamage(slot) > 0) {
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
            HSD_GObj* gobj = Player_GetEntity(slot);
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
        HSD_GObj* gobj = Player_GetEntity(slot);
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
    if (getenv("MELEE_TITLE_TEST") != NULL) {
        title_test = 1;
        boot_platform_set_frame_hook(match_boot_frame);
    }
    if (getenv("MELEE_CPU_TEST") != NULL) {
        cpu_test = 1;
        boot_platform_set_frame_hook(match_boot_frame);
    }
    if (frame_in != 0) {
        if (getenv("MELEE_ITEM_TEST") != NULL) {
            item_trace = 1;
            build_item_test_input();
        } else if (getenv("MELEE_HIT_TEST") != NULL) {
            hit_test = 1;
            build_hit_test_input();
        } else {
            build_match_input();
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

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

#include <melee/gm/forward.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmscene.h>
#include <melee/ft/ftlib.h>
#include <melee/pl/player.h>

#include <dolphin/mtx.h>
#include <dolphin/pad.h>

#include "decomp/boot/boot_triage.h"
#include "platform/platform.h"

#define MATCH_INPUT_FRAMES 900
#define MATCH_INPUT_CHANNELS 2

static unsigned start_frame;
static unsigned frame;
static int stocks_frames;
static PadInputFrame match_input[MATCH_INPUT_FRAMES][MATCH_INPUT_CHANNELS];

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

static void match_boot_frame(void)
{
    if (start_frame == 0) {
        return;
    }
    frame++;
    if (frame < start_frame) {
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

void match_boot_init(unsigned frame_in)
{
    start_frame = frame_in;
    frame = 0;
    if (frame_in != 0) {
        build_match_input();
        boot_platform_set_frame_hook(match_boot_frame);
    }
}

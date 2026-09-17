/*
 * Melee Unbound -- the default mod.
 *
 * Unbound's own features are not privileged port code.  They are a mod, they
 * go through the same ABI a stranger's mod uses, and `MELEE_NO_MODS=1` turns
 * them all off, at which point the port must behave exactly as it did before
 * any of this existed.  If something Unbound needs turns out not to be
 * expressible through unbound_mod.h, that is the API being wrong, and we find
 * out here rather than after other people have built against it.
 *
 * On the desktop this file is compiled to wasm32 and shipped as
 * mods/unbound/unbound.wasm.  In the browser there is no loader and Unbound
 * is the only mod, so the same sources are compiled straight into the binary.
 * Nothing below knows which.
 */
#include "unbound_mod.h"

UNBOUND_MOD_MAIN()

void widescreen_init(void);
void widescreen_on_display_resized(const UnboundDisplayResized* size);
void widescreen_on_camera_setup(UnboundCameraSetup* cam);

void credits_open(void);
int credits_is_open(void);
void credits_on_frame(const UnboundFrame* frame);

void unbound_mod_init(void)
{
    unbound_hook_enable(UNBOUND_HOOK_CAMERA_SETUP, UNBOUND_PRIORITY_NORMAL);
    unbound_hook_enable(UNBOUND_HOOK_DISPLAY_RESIZED, UNBOUND_PRIORITY_NORMAL);
    unbound_hook_enable(UNBOUND_HOOK_FRAME, UNBOUND_PRIORITY_NORMAL);

    widescreen_init();
}

/*
 * Until the menu entry exists (P-838) the credits are reached with Z from a
 * non-gameplay screen, and the footer says so.  When the entry lands, this
 * becomes `credits_open()` from the selection handler and the footer goes
 * away -- credits.c does not change.
 */
static void unbound_on_frame(const UnboundFrame* frame)
{
    int i;

    if (credits_is_open()) {
        credits_on_frame(frame);
        return;
    }
    if (frame->scene == UNBOUND_SCENE_GAMEPLAY) {
        return;
    }
    for (i = 0; i < 4; ++i) {
        if (unbound_buttons_pressed(i) & UNBOUND_BUTTON_Z) {
            credits_open();
            return;
        }
    }
    unbound_draw_color(0.55f, 0.45f, 0.75f, 1.0f);
    unbound_draw_text(8.0f, 462.0f, 1.0f, "MELEE UNBOUND   Z  CREDITS");
}

void unbound_mod_on_hook(unsigned hook)
{
    void* payload = unbound_mod_payload();

    switch (hook) {
    case UNBOUND_HOOK_CAMERA_SETUP:
        widescreen_on_camera_setup((UnboundCameraSetup*) payload);
        break;
    case UNBOUND_HOOK_DISPLAY_RESIZED:
        widescreen_on_display_resized((const UnboundDisplayResized*) payload);
        break;
    case UNBOUND_HOOK_FRAME:
        unbound_on_frame((const UnboundFrame*) payload);
        break;
    default:
        break;
    }
}

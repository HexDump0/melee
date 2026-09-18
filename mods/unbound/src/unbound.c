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

void credits_on_frame(const UnboundFrame* frame);
void credits_draw_description(void);

void unbound_mod_init(void)
{
    unbound_hook_enable(UNBOUND_HOOK_CAMERA_SETUP, UNBOUND_PRIORITY_NORMAL);
    unbound_hook_enable(UNBOUND_HOOK_DISPLAY_RESIZED, UNBOUND_PRIORITY_NORMAL);
    unbound_hook_enable(UNBOUND_HOOK_FRAME, UNBOUND_PRIORITY_NORMAL);

    widescreen_init();
}

/*
 * The credits are opened from the main menu's "Melee Unbound" entry.  The
 * entry is port machinery -- see native/mod/mod_menu.c -- and reaches the mod
 * as a one-shot flag; everything after that point is the mod's own.
 */
static void unbound_on_frame(const UnboundFrame* frame)
{
    int i;

    (void) i;

    /*
     * The page is a real menu kind: the game transitions to it and its Back
     * button leaves it, so the mod only draws.  No open/close state here.
     */
    if (unbound_menu_page_open()) {
        credits_on_frame(frame);
        return;
    }
    if (unbound_menu_hovered()) {
        credits_draw_description();
    }
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

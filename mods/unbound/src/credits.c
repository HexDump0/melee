/*
 * The Unbound credits overlay.
 *
 * Step 1 of the main-menu entry (P-838).  The entry itself will be a real
 * menu item -- cloned geometry with a generated label texture -- but what it
 * opens is this, and this part is the same either way, so it is built first
 * and reached by a button until the entry exists.
 *
 * Everything here goes through the mod ABI: it draws with `unbound_draw_*`,
 * reads the pad with `unbound_buttons_*`, and asks the host what screen is
 * showing.  If any of that turns out to be awkward, the ABI is what needs
 * fixing -- that is the whole reason Unbound is a mod rather than port code.
 */
#include "unbound_mod.h"

/* The authored 2D space; the host scales it to whatever the window is. */
#define SCREEN_W 640.0f
#define SCREEN_H 480.0f

static int open_now;
static unsigned opened_frame;

struct Line {
    const char* text;
    unsigned len;
    float scale;
    float gap; /* pixels below the previous line */
};

#define LINE(t, s, g)                                                         \
    { (t), (unsigned) (sizeof(t) - 1), (s), (g) }

static const struct Line credits[] = {
    LINE("MELEE UNBOUND", 3.0f, 0.0f),
    LINE("A NATIVE PORT OF SUPER SMASH BROS. MELEE", 1.1f, 34.0f),
    LINE("", 1.0f, 8.0f),
    LINE("PORT", 1.4f, 14.0f),
    LINE("HEXDUMP0", 1.2f, 20.0f),
    LINE("", 1.0f, 8.0f),
    LINE("BUILT ON", 1.4f, 14.0f),
    LINE("DOLDECOMP/MELEE", 1.2f, 20.0f),
    LINE("WASM-MICRO-RUNTIME  .  SDL3  .  OPENGL ES", 1.0f, 18.0f),
    LINE("", 1.0f, 8.0f),
    LINE("NO GAME DATA IS DISTRIBUTED WITH THIS PORT.", 1.0f, 16.0f),
    LINE("BRING YOUR OWN DISC.", 1.0f, 14.0f)
};

#define CREDITS_COUNT ((int) (sizeof(credits) / sizeof(credits[0])))

static float text_width(const struct Line* l)
{
    /* The host font is a fixed 6x7 cell advanced by 6 units, so a line's
     * width is exact rather than estimated. */
    return (float) l->len * 6.0f * l->scale;
}

void credits_open(void)
{
    open_now = 1;
    opened_frame = 0;
}

int credits_is_open(void) { return open_now; }

void credits_on_frame(const UnboundFrame* frame)
{
    float y;
    int i;

    if (!open_now) {
        return;
    }
    if (opened_frame == 0) {
        opened_frame = frame->frame;
    }

    /* B or START closes.  Read every port so it does not matter which
     * controller is plugged in. */
    for (i = 0; i < 4; ++i) {
        unsigned pressed = unbound_buttons_pressed(i);
        if (pressed & (UNBOUND_BUTTON_B | UNBOUND_BUTTON_START)) {
            open_now = 0;
            return;
        }
    }

    /* A backdrop dark enough to read against whatever is behind it, drawn as
     * overlapping rules rather than a quad because the host's drawing surface
     * is text-only for now.  If this is still here when the ABI grows a real
     * filled-rect call, it should use it (P-839). */
    unbound_draw_color(0.0f, 0.0f, 0.0f, 0.82f);
    for (y = 0.0f; y < SCREEN_H; y += 7.0f) {
        unbound_draw_text(0.0f, y, 1.0f,
                          "................................................"
                          "................................");
    }

    y = 96.0f;
    for (i = 0; i < CREDITS_COUNT; ++i) {
        const struct Line* l = &credits[i];
        y += l->gap;
        if (l->len == 0) {
            continue;
        }
        if (i == 0) {
            unbound_draw_color(0.72f, 0.45f, 1.0f, 1.0f);
        } else if (l->scale >= 1.4f) {
            unbound_draw_color(0.95f, 0.87f, 0.55f, 1.0f);
        } else {
            unbound_draw_color(0.86f, 0.90f, 0.95f, 1.0f);
        }
        unbound_draw_text_raw(0.5f * (SCREEN_W - text_width(l)), y, l->scale,
                              l->text, l->len);
    }

    unbound_draw_color(0.60f, 0.62f, 0.68f, 1.0f);
    unbound_draw_text(0.5f * SCREEN_W - 54.0f, SCREEN_H - 40.0f, 1.0f,
                      "B  OR  START  TO  CLOSE");
}

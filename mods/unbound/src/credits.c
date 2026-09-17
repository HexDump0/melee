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

/* The menu's own frame, so the page sits inside it. */
#define PANEL_X 56.0f
#define PANEL_Y 64.0f
#define PANEL_W 528.0f
#define PANEL_H 320.0f

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

/*
 * The main menu's description box, measured in the authored 640x480 space.
 * The game leaves it empty for our entry -- native/mod/mod_menu.c sets the
 * box to SIS string 0 while it is hovered -- rather than showing another
 * entry's words under the wrong heading.
 */
#define DESC_CENTRE_X 322.0f
#define DESC_Y 411.0f
#define DESC_SCALE 1.05f

static const char DESC_TEXT[] = "THE PORT AND WHAT IT IS BUILT ON.";

static float text_width_of(unsigned len, float scale)
{
    return (float) len * 6.0f * scale;
}

void credits_draw_description(void)
{
    float w = text_width_of((unsigned) (sizeof(DESC_TEXT) - 1), DESC_SCALE);
    unbound_draw_color(0.90f, 0.91f, 0.95f, 1.0f);
    unbound_draw_text_raw(DESC_CENTRE_X - 0.5f * w, DESC_Y, DESC_SCALE,
                          DESC_TEXT, (unsigned) (sizeof(DESC_TEXT) - 1));
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

    /*
     * A panel inside the menu frame rather than a full-screen wash: this is
     * meant to read as another page of the main menu, not as something
     * printed over the top of it.
     *
     * One quad each.  Drawing a backdrop out of text costs a quad per glyph
     * pixel and silently exhausts the host's vertex budget, which then drops
     * everything after it -- the backdrop appeared and the credits did not.
     */
    unbound_draw_color(0.62f, 0.66f, 0.85f, 0.95f);
    unbound_draw_rect(PANEL_X - 2.0f, PANEL_Y - 2.0f, PANEL_W + 4.0f,
                      PANEL_H + 4.0f);
    unbound_draw_color(0.05f, 0.04f, 0.12f, 0.96f);
    unbound_draw_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);

    y = PANEL_Y + 22.0f;
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
        unbound_draw_text_raw(PANEL_X + 0.5f * (PANEL_W - text_width(l)), y,
                              l->scale, l->text, l->len);
    }

    unbound_draw_color(0.62f, 0.64f, 0.72f, 1.0f);
    unbound_draw_text(PANEL_X + 0.5f * PANEL_W - 60.0f,
                      PANEL_Y + PANEL_H - 22.0f, 1.0f,
                      "B  OR  START  TO  GO  BACK");
}

/*
 * The Unbound credits, drawn onto the mod's own menu page (P-838).
 *
 * The page is a real menu kind: the game transitions into it and its Back
 * button leaves it, so this file owns no open/closed state and no input
 * handling.  It is handed a frame while the page is up and it draws -- which
 * is the right split, and it only became obvious once the page stopped being
 * a popup.
 *
 * Everything here goes through the mod ABI.  If any of it turns out to be
 * awkward, the ABI is what needs fixing -- that is the whole reason Unbound
 * is a mod rather than port code.
 */
#include "unbound_mod.h"

/* The authored 2D space; the host scales it to whatever the window is. */
#define SCREEN_W 640.0f
#define SCREEN_H 480.0f

/*
 * The plate, sized to the menu's own frame.
 *
 * It has to reach the frame's inner edge rather than sit politely inside it:
 * the panel still carries its breadcrumb header along the top and our page's
 * single option pill down the left, both of which are animation state we do
 * not set (P-839).  Covering them is honest for now and looks deliberate;
 * the alternative is a page with another menu's title above it.
 */
#define PANEL_X 34.0f
#define PANEL_Y 40.0f
#define PANEL_W 572.0f
#define PANEL_H 350.0f

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
    LINE("PORT BY", 1.4f, 14.0f),
    LINE("HEXDUMP0", 1.2f, 20.0f),
    LINE("", 1.0f, 8.0f),
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

static const char DESC_TEXT[] = "";

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

/*
 * Draw the credits.  Called only while the page is the current menu, so
 * there is nothing to check and nothing to close.
 */
void credits_on_frame(const UnboundFrame* frame)
{
    float y;
    int i;

    (void) frame;

    /*
     * A plate inside the menu's own frame, so the text reads against the
     * panel rather than against whatever the animated background is doing.
     *
     * One quad each: drawing a backdrop out of text costs a quad per glyph
     * pixel and silently exhausts the host's vertex budget, which then drops
     * everything after it -- the backdrop appeared and the credits did not.
     */
    unbound_draw_color(0.55f, 0.60f, 0.82f, 0.90f);
    unbound_draw_rect(PANEL_X - 2.0f, PANEL_Y - 2.0f, PANEL_W + 4.0f,
                      PANEL_H + 4.0f);
    unbound_draw_color(0.04f, 0.04f, 0.11f, 0.94f);
    unbound_draw_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H);

    y = PANEL_Y + 34.0f;
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
    unbound_draw_text(PANEL_X + 0.5f * PANEL_W - 42.0f,
                      PANEL_Y + PANEL_H - 24.0f, 1.0f, "B  TO  GO  BACK");
}

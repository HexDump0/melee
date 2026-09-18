/*
 * Scripted and live pad input for the frontend and match harnesses.
 *
 * Split out of viewer_main.c; see viewer_internal.h.
 */
#include "decomp/render/viewer_internal.h"

#define FRONTEND_MAX_EVENTS 1024

typedef struct FrontendEvent {
    unsigned frame;
    unsigned channel; /* 4 = every connected channel */
    PadInputFrame pad;
} FrontendEvent;

int parse_pad_buttons(const char* names, unsigned short* out)
{
    char buf[64];
    char* tok;
    char* save = NULL;
    unsigned short bits = 0;

    if (names == NULL || strcmp(names, "-") == 0 || strcmp(names, "none") == 0)
    {
        *out = 0;
        return 1;
    }
    snprintf(buf, sizeof(buf), "%s", names);
    for (tok = strtok_r(buf, ",", &save); tok != NULL;
         tok = strtok_r(NULL, ",", &save))
    {
        if (strcmp(tok, "left") == 0) {
            bits |= PAD_BUTTON_LEFT;
        } else if (strcmp(tok, "right") == 0) {
            bits |= PAD_BUTTON_RIGHT;
        } else if (strcmp(tok, "down") == 0) {
            bits |= PAD_BUTTON_DOWN;
        } else if (strcmp(tok, "up") == 0) {
            bits |= PAD_BUTTON_UP;
        } else if (strcmp(tok, "z") == 0) {
            bits |= PAD_TRIGGER_Z;
        } else if (strcmp(tok, "r") == 0) {
            bits |= PAD_TRIGGER_R;
        } else if (strcmp(tok, "l") == 0) {
            bits |= PAD_TRIGGER_L;
        } else if (strcmp(tok, "a") == 0) {
            bits |= PAD_BUTTON_A;
        } else if (strcmp(tok, "b") == 0) {
            bits |= PAD_BUTTON_B;
        } else if (strcmp(tok, "x") == 0) {
            bits |= PAD_BUTTON_X;
        } else if (strcmp(tok, "y") == 0) {
            bits |= PAD_BUTTON_Y;
        } else if (strcmp(tok, "start") == 0) {
            bits |= PAD_BUTTON_START;
        } else {
            return 0;
        }
    }
    *out = bits;
    return 1;
}

PadInputFrame* frontend_load_script(const char* path, unsigned total,
                                           unsigned* channels_out)
{
    FILE* f = fopen(path, "r");
    static FrontendEvent events[FRONTEND_MAX_EVENTS];
    int event_count = 0;
    unsigned channels = 1;
    char line[256];
    unsigned i;
    PadInputFrame* frames;

    if (f == NULL) {
        fprintf(stderr, "viewer: cannot open input script %s\n", path);
        return NULL;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char chan_tok[8];
        char buttons[64];
        unsigned fr;
        unsigned channel;
        int sx = 0;
        int sy = 0;
        int cx = 0;
        int cy = 0;
        int tl = 0;
        int tr = 0;
        int n;
        char* hash = strchr(line, '#');
        PadInputFrame* pad;

        if (hash != NULL) {
            *hash = '\0';
        }
        if (line[0] == '\0' || line[0] == '\n') {
            continue;
        }
        if (strncmp(line, "channels", 8) == 0) {
            unsigned value;
            if (sscanf(line + 8, "%u", &value) == 1 && value >= 1 &&
                value <= 4)
            {
                channels = value;
            }
            continue;
        }
        chan_tok[0] = '\0';
        buttons[0] = '\0';
        n = sscanf(line, "%u %7s %63s %d %d %d %d %d %d", &fr, chan_tok,
                   buttons, &sx, &sy, &cx, &cy, &tl, &tr);
        if (n < 3) {
            fprintf(stderr, "viewer: input: bad line: %s\n", line);
            fclose(f);
            return NULL;
        }
        if (strcmp(chan_tok, "*") == 0) {
            channel = 4;
        } else {
            channel = (unsigned) atoi(chan_tok);
            if (channel > 3) {
                fprintf(stderr, "viewer: input: bad channel: %s\n", chan_tok);
                fclose(f);
                return NULL;
            }
        }
        if (event_count >= FRONTEND_MAX_EVENTS) {
            break;
        }
        pad = &events[event_count].pad;
        memset(pad, 0, sizeof(*pad));
        if (!parse_pad_buttons(buttons, &pad->buttons)) {
            fprintf(stderr, "viewer: input: unknown buttons: %s\n", buttons);
            fclose(f);
            return NULL;
        }
        pad->stick_x = (signed char) sx;
        pad->stick_y = (signed char) sy;
        pad->cstick_x = (signed char) cx;
        pad->cstick_y = (signed char) cy;
        pad->trigger_l = (unsigned char) (tl < 0 ? 0 : tl > 255 ? 255 : tl);
        pad->trigger_r = (unsigned char) (tr < 0 ? 0 : tr > 255 ? 255 : tr);
        events[event_count].frame = fr;
        events[event_count].channel = channel;
        event_count++;
    }
    fclose(f);
    if (event_count == 0) {
        return NULL;
    }
    frames = calloc((size_t) total * channels, sizeof(PadInputFrame));
    if (frames == NULL) {
        return NULL;
    }
    for (i = 0; i < (unsigned) event_count; i++) {
        unsigned start = events[i].frame;
        unsigned end =
            (i + 1 < (unsigned) event_count) ? events[i + 1].frame : total;
        unsigned frame;
        unsigned c;
        if (start >= total) {
            break;
        }
        if (end > total) {
            end = total;
        }
        for (frame = start; frame < end; frame++) {
            if (events[i].channel == 4) {
                for (c = 0; c < channels; c++) {
                    frames[frame * channels + c] = events[i].pad;
                }
            } else if (events[i].channel < channels) {
                frames[frame * channels + events[i].channel] = events[i].pad;
            }
        }
    }
    *channels_out = channels;
    return frames;
}

/*
 * Gamepads (P-852).
 *
 * One SDL gamepad per port, in the order SDL reports them, merged with the
 * keyboard so both work at once -- unplugging a pad mid-session must not
 * leave the player with no way to press Start.
 *
 * The button names map straight across: SDL's SOUTH/EAST/WEST/NORTH are
 * physical positions, and on the GameCube adapter's own SDL mapping they are
 * exactly A/B/X/Y, so a real GameCube controller reproduces its own layout
 * and an Xbox-style pad gives A->A, B->B, X->X, Y->Y.
 */
#define PAD_GAMEPAD_MAX 4
/* The physical stick's usable range in the units PADStatus reports, which is
 * what the keyboard path above already uses. */
#define PAD_STICK_RANGE 80
/* SDL's axes are +-32767; below this a stick is at rest.  Melee does its own
 * deadzone on top, so this only has to reject drift -- and it has to reject
 * *worn* drift, which is why it is not as tight as it could be (P-855). */
#define PAD_STICK_DEADZONE 4000
/* Overridable per install; see pad_load_bindings. */
static int pad_deadzone = PAD_STICK_DEADZONE;
/* A GameCube trigger clicks at the bottom of its travel; SDL reports the
 * click as the shoulder button on an adapter and as nothing on a pad with
 * analog-only triggers, so take either. */
#define PAD_TRIGGER_CLICK 30000

static SDL_Gamepad* pad_gamepads[PAD_GAMEPAD_MAX];
static int pad_gamepad_scanned;

static void pad_scan_gamepads(void)
{
    int count = 0;
    SDL_JoystickID* ids;
    int i;

    /* Close what went away first, so a pad that was unplugged and replugged
     * does not end up open twice. */
    for (i = 0; i < PAD_GAMEPAD_MAX; ++i) {
        if (pad_gamepads[i] != NULL &&
            !SDL_GamepadConnected(pad_gamepads[i]))
        {
            SDL_CloseGamepad(pad_gamepads[i]);
            pad_gamepads[i] = NULL;
        }
    }

    ids = SDL_GetGamepads(&count);
    if (ids == NULL) {
        return;
    }
    for (i = 0; i < count && i < PAD_GAMEPAD_MAX; ++i) {
        if (pad_gamepads[i] == NULL) {
            pad_gamepads[i] = SDL_OpenGamepad(ids[i]);
            if (pad_gamepads[i] != NULL) {
                fprintf(stderr, "viewer: port %d = %s\n", i + 1,
                        SDL_GetGamepadName(pad_gamepads[i]));
            }
        }
    }
    SDL_free(ids);
}

/*
 * Takes an `int`, not a `Sint16`, and the callers negate in `int` space.
 *
 * SDL's axis range is -32768..32767, so an `Sint16` negation of a stick held
 * fully in the negative direction is `-(-32768)`, which wraps straight back
 * to -32768: full up read as full down, on the two axes that need flipping
 * and only at the very end of their travel (P-853).
 */
/* ------------------------------------------------------------- bindings
 *
 * Every control the player can rebind (P-871), read once from the environment
 * that `melee.toml` populates: `[controls.p1.keyboard] a = "Z"` arrives here
 * as `MELEE_CONTROLS_P1_KEYBOARD_A`.
 *
 * Unset means the default, so a player who has never opened the launcher gets
 * exactly the bindings this file shipped with -- the defaults below are the
 * literal keys the hard-coded version used, not a tidied-up version of them.
 *
 * A keyboard binding is a **comma-separated list**, because Start has always
 * answered to both Return and Keypad Enter and losing that to a single-key
 * model would be a regression nobody asked for.
 */
typedef enum {
    ACT_STICK_UP, ACT_STICK_DOWN, ACT_STICK_LEFT, ACT_STICK_RIGHT,
    ACT_CSTICK_UP, ACT_CSTICK_DOWN, ACT_CSTICK_LEFT, ACT_CSTICK_RIGHT,
    ACT_A, ACT_B, ACT_X, ACT_Y, ACT_Z, ACT_L, ACT_R, ACT_START,
    ACT_DPAD_UP, ACT_DPAD_DOWN, ACT_DPAD_LEFT, ACT_DPAD_RIGHT,
    ACT_COUNT
} PadAction;

/* Config key, and the PAD_* bit it sets (0 for the analog directions, which
 * are handled by their own axis rather than by a button mask). */
static const struct {
    const char* key;
    unsigned int button;
} pad_actions[ACT_COUNT] = {
    { "stick_up", 0 },      { "stick_down", 0 },
    { "stick_left", 0 },    { "stick_right", 0 },
    { "cstick_up", 0 },     { "cstick_down", 0 },
    { "cstick_left", 0 },   { "cstick_right", 0 },
    { "a", PAD_BUTTON_A },  { "b", PAD_BUTTON_B },
    { "x", PAD_BUTTON_X },  { "y", PAD_BUTTON_Y },
    { "z", PAD_TRIGGER_Z }, { "l", PAD_TRIGGER_L },
    { "r", PAD_TRIGGER_R }, { "start", PAD_BUTTON_START },
    { "dpad_up", PAD_BUTTON_UP },     { "dpad_down", PAD_BUTTON_DOWN },
    { "dpad_left", PAD_BUTTON_LEFT }, { "dpad_right", PAD_BUTTON_RIGHT },
};

/* The keys the hard-coded version used, verbatim. */
static const char* const kb_default[2][ACT_COUNT] = {
    { "Up", "Down", "Left", "Right",
      "", "", "", "",
      "Z", "X", "C", "V", "Q", "A", "S", "Return,Keypad Enter",
      "", "", "", "" },
    { "I", "K", "J", "L",
      "", "", "", "",
      "F", "G", "", "", "", "", "", "T",
      "", "", "", "" },
};

/* SDL's own names, so the launcher and the port agree without a table in
 * between: `SDL_GetGamepadButtonFromString` parses what the UI writes. */
static const char* const gp_default[ACT_COUNT] = {
    "", "", "", "",
    "", "", "", "",
    "a", "b", "x", "y",
    /* Z on the right shoulder and L on the left, as before.  R has no button
     * by default on purpose: it is the analog trigger's click, handled below,
     * which is what a GameCube R actually is. */
    "rightshoulder", "leftshoulder", "", "start",
    "dpup", "dpdown", "dpleft", "dpright",
};

#define PAD_MAX_KEYS 4
static SDL_Scancode kb_binding[2][ACT_COUNT][PAD_MAX_KEYS];
static int kb_binding_count[2][ACT_COUNT];
static int gp_binding[ACT_COUNT]; /* SDL_GamepadButton, or -1 */
static int pad_bindings_loaded;

static const char* pad_env(const char* fmt, int player, const char* key)
{
    char name[128];
    char upper[128];
    size_t i;
    snprintf(name, sizeof(name), fmt, player, key);
    for (i = 0; name[i] != '\0' && i + 1 < sizeof(upper); ++i) {
        upper[i] = (name[i] >= 'a' && name[i] <= 'z')
                       ? (char) (name[i] - 'a' + 'A')
                       : name[i];
    }
    upper[i] = '\0';
    return getenv(upper);
}

/* "Z" or "Return,Keypad Enter" -> scancodes.  Unknown names are reported
 * rather than dropped: a binding that silently does nothing is the worst
 * possible outcome for a remapping screen. */
static void pad_parse_keys(const char* spec, int player, PadAction act)
{
    const char* p = spec;
    kb_binding_count[player][act] = 0;
    while (*p != '\0' && kb_binding_count[player][act] < PAD_MAX_KEYS) {
        char name[64];
        size_t n = strcspn(p, ",");
        SDL_Scancode code;
        if (n == 0 || n >= sizeof(name)) {
            break;
        }
        memcpy(name, p, n);
        name[n] = '\0';
        code = SDL_GetScancodeFromName(name);
        if (code == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "[controls] p%d %s: unknown key \"%s\"\n",
                    player + 1, pad_actions[act].key, name);
        } else {
            kb_binding[player][act][kb_binding_count[player][act]++] = code;
        }
        p += n;
        if (*p == ',') {
            ++p;
        }
    }
}

static void pad_load_bindings(void)
{
    int player;
    int act;
    const char* dz;

    if (pad_bindings_loaded) {
        return;
    }
    pad_bindings_loaded = 1;

    for (player = 0; player < 2; ++player) {
        for (act = 0; act < ACT_COUNT; ++act) {
            const char* spec =
                pad_env("MELEE_CONTROLS_P%d_KEYBOARD_%s", player + 1,
                        pad_actions[act].key);
            if (spec == NULL) {
                spec = kb_default[player][act];
            }
            pad_parse_keys(spec, player, (PadAction) act);
        }
    }

    for (act = 0; act < ACT_COUNT; ++act) {
        const char* spec =
            pad_env("MELEE_CONTROLS_P%d_GAMEPAD_%s", 1, pad_actions[act].key);
        if (spec == NULL) {
            spec = gp_default[act];
        }
        gp_binding[act] = (spec[0] == '\0')
                              ? -1
                              : (int) SDL_GetGamepadButtonFromString(spec);
        if (spec[0] != '\0' && gp_binding[act] < 0) {
            fprintf(stderr, "[controls] gamepad %s: unknown button \"%s\"\n",
                    pad_actions[act].key, spec);
        }
    }

    dz = getenv("MELEE_CONTROLS_DEADZONE");
    if (dz != NULL && dz[0] != '\0') {
        int v = atoi(dz);
        /* A deadzone at or above full deflection is a dead stick, which
         * looks exactly like a broken build. */
        if (v >= 0 && v < 30000) {
            pad_deadzone = v;
        }
    }
}

/*
 * Print what the bindings actually resolved to.
 *
 * `--controls` runs this and exits, which is the only way to tell a binding
 * that did not apply from one that applied to a key you did not mean -- and it
 * needs no window, so it works over ssh and in a script.
 */
void frontend_print_bindings(void)
{
    int player;
    int act;

    pad_load_bindings();
    for (player = 0; player < 2; ++player) {
        for (act = 0; act < ACT_COUNT; ++act) {
            int i;
            if (kb_binding_count[player][act] == 0) {
                continue;
            }
            /* Comma-separated, and not space-separated, because scancode
             * names contain spaces ("Keypad Enter") and the launcher parses
             * this back. */
            fprintf(stderr, "[controls] p%d %s = ", player + 1,
                    pad_actions[act].key);
            for (i = 0; i < kb_binding_count[player][act]; ++i) {
                fprintf(stderr, "%s%s", i > 0 ? "," : "",
                        SDL_GetScancodeName(kb_binding[player][act][i]));
            }
            fprintf(stderr, "\n");
        }
    }
    for (act = 0; act < ACT_COUNT; ++act) {
        if (gp_binding[act] >= 0) {
            fprintf(stderr, "[controls] pad %s = %s\n",
                    pad_actions[act].key,
                    SDL_GetGamepadStringForButton(
                        (SDL_GamepadButton) gp_binding[act]));
        }
    }
    fprintf(stderr, "[controls] deadzone = %d\n", pad_deadzone);
}

static int pad_key_held(const bool* keys, int player, PadAction act)
{
    int i;
    for (i = 0; i < kb_binding_count[player][act]; ++i) {
        if (keys[kb_binding[player][act][i]]) {
            return 1;
        }
    }
    return 0;
}

static signed char pad_axis_to_stick(int axis)
{
    int v;

    if (axis > -pad_deadzone && axis < pad_deadzone) {
        return 0;
    }
    v = axis * PAD_STICK_RANGE / 32767;
    if (v > PAD_STICK_RANGE) {
        v = PAD_STICK_RANGE;
    }
    if (v < -PAD_STICK_RANGE) {
        v = -PAD_STICK_RANGE;
    }
    return (signed char) v;
}

static unsigned char pad_axis_to_trigger(Sint16 axis)
{
    int v = (int) axis * 255 / 32767;
    if (v < 0) {
        v = 0;
    }
    if (v > 255) {
        v = 255;
    }
    return (unsigned char) v;
}

static void pad_poll_gamepad(SDL_Gamepad* gp, PadInputFrame* out)
{
    Sint16 lt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    Sint16 rt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    int dx = 0;
    int dy = 0;

    /*
     * Only a *deflected* axis overwrites what the keyboard put here.  A pad
     * sitting at rest must not zero the arrow keys: someone with a controller
     * plugged in who reaches for the keyboard should still be able to play,
     * and on the menus that is often exactly what happens (P-854).  The same
     * goes for the triggers, which rest at zero.
     */
    {
        signed char v;
        /* SDL's y axis points down, the GameCube's points up. */
        v = pad_axis_to_stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX));
        if (v != 0 && out->stick_x == 0) {
            out->stick_x = v;
        }
        v = pad_axis_to_stick(
            -(int) SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY));
        if (v != 0 && out->stick_y == 0) {
            out->stick_y = v;
        }
        v = pad_axis_to_stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX));
        if (v != 0 && out->cstick_x == 0) {
            out->cstick_x = v;
        }
        v = pad_axis_to_stick(
            -(int) SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY));
        if (v != 0 && out->cstick_y == 0) {
            out->cstick_y = v;
        }
    }
    if (lt > 0 && out->trigger_l == 0) {
        out->trigger_l = pad_axis_to_trigger(lt);
    }
    if (rt > 0 && out->trigger_r == 0) {
        out->trigger_r = pad_axis_to_trigger(rt);
    }

    /* Buttons come from the binding table, so a remap reaches the pad and
     * the keyboard through the same list. */
    {
        int act;
        for (act = 0; act < ACT_COUNT; ++act) {
            if (gp_binding[act] < 0 || pad_actions[act].button == 0) {
                continue;
            }
            if (SDL_GetGamepadButton(gp, (SDL_GamepadButton) gp_binding[act])) {
                out->buttons |= pad_actions[act].button;
                if (act == ACT_DPAD_LEFT) {
                    dx = -1;
                } else if (act == ACT_DPAD_RIGHT) {
                    dx = 1;
                } else if (act == ACT_DPAD_DOWN) {
                    dy = -1;
                } else if (act == ACT_DPAD_UP) {
                    dy = 1;
                }
            }
        }
    }
    /* L and R also answer to their analog triggers, whatever else they are
     * bound to: a GameCube trigger is a button at the bottom of its travel,
     * and a player who presses it all the way expects the click. */
    if (lt >= PAD_TRIGGER_CLICK) {
        out->buttons |= PAD_TRIGGER_L;
    }
    if (rt >= PAD_TRIGGER_CLICK) {
        out->buttons |= PAD_TRIGGER_R;
    }
    if (dx != 0) {
        out->buttons |= dx < 0 ? PAD_BUTTON_LEFT : PAD_BUTTON_RIGHT;
    }
    if (dy != 0) {
        out->buttons |= dy < 0 ? PAD_BUTTON_DOWN : PAD_BUTTON_UP;
    }
}


void frontend_poll_live(void)
{
    const bool* keys = SDL_GetKeyboardState(NULL);
    PadInputFrame* live[2];
    int player;

    pad_load_bindings();
    live[0] = &match_view.live[0];
    live[1] = &match_view.live[1];
    memset(match_view.live, 0, sizeof(match_view.live));

    for (player = 0; player < 2; ++player) {
        PadInputFrame* out = live[player];
        int sx = 0, sy = 0, cx = 0, cy = 0;
        int act;

        for (act = 0; act < ACT_COUNT; ++act) {
            if (!pad_key_held(keys, player, (PadAction) act)) {
                continue;
            }
            if (pad_actions[act].button != 0) {
                out->buttons |= pad_actions[act].button;
                continue;
            }
            /* The analog directions. A key is full deflection, which is what
             * the hard-coded version did and what makes the keyboard usable
             * at all. */
            switch (act) {
            case ACT_STICK_UP:      sy += PAD_STICK_RANGE; break;
            case ACT_STICK_DOWN:    sy -= PAD_STICK_RANGE; break;
            case ACT_STICK_LEFT:    sx -= PAD_STICK_RANGE; break;
            case ACT_STICK_RIGHT:   sx += PAD_STICK_RANGE; break;
            case ACT_CSTICK_UP:     cy += PAD_STICK_RANGE; break;
            case ACT_CSTICK_DOWN:   cy -= PAD_STICK_RANGE; break;
            case ACT_CSTICK_LEFT:   cx -= PAD_STICK_RANGE; break;
            case ACT_CSTICK_RIGHT:  cx += PAD_STICK_RANGE; break;
            default: break;
            }
        }
        out->stick_x = (signed char) sx;
        out->stick_y = (signed char) sy;
        out->cstick_x = (signed char) cx;
        out->cstick_y = (signed char) cy;
    }

    /*
     * Gamepads last, and only into axes the keyboard left alone.
     *
     * **A held key beats an analog stick.**  The first version let any
     * deflected axis win, which is fine until the pad has drift: a worn stick
     * sitting outside the deadzone then owns the axis forever and the arrow
     * keys do nothing, with no way to tell from the outside that a controller
     * is the reason (P-855).  Explicit input is never ambiguous, so it wins,
     * and the pad fills in whatever the keyboard is not asking for.  Buttons
     * OR in from both, as before.
     */
    if (!pad_gamepad_scanned) {
        pad_gamepad_scanned = 1;
        pad_scan_gamepads();
    }
    {
        int i;
        for (i = 0; i < PAD_GAMEPAD_MAX; ++i) {
            if (pad_gamepads[i] != NULL &&
                SDL_GamepadConnected(pad_gamepads[i]))
            {
                pad_poll_gamepad(pad_gamepads[i], &match_view.live[i]);
            }
        }
    }
    /* MELEE_INPUT_TRACE=1 prints port 1 whenever it changes: "the key did
     * nothing" and "something else overwrote it" look identical from the
     * outside, and this tells them apart in one line. */
    {
        static int trace = -1;
        static PadInputFrame last;
        const PadInputFrame* now = &match_view.live[0];
        if (trace < 0) {
            trace = getenv("MELEE_INPUT_TRACE") != NULL;
        }
        if (trace && memcmp(now, &last, sizeof(last)) != 0) {
            last = *now;
            fprintf(stderr,
                    "[input] p1 stick=(%d,%d) c=(%d,%d) lr=(%u,%u) "
                    "buttons=%04x  pads=%d\n",
                    now->stick_x, now->stick_y, now->cstick_x, now->cstick_y,
                    now->trigger_l, now->trigger_r, now->buttons,
                    pad_gamepads[0] != NULL);
        }
    }
    pad_set_live_input(match_view.live, 4);
}

/* Hot-plug: the event loop calls this when SDL says the set changed. */
void frontend_gamepads_changed(void)
{
    pad_scan_gamepads();
}

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
static signed char pad_axis_to_stick(int axis)
{
    int v;

    if (axis > -PAD_STICK_DEADZONE && axis < PAD_STICK_DEADZONE) {
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

    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH)) {
        out->buttons |= PAD_BUTTON_A;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_EAST)) {
        out->buttons |= PAD_BUTTON_B;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST)) {
        out->buttons |= PAD_BUTTON_X;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_NORTH)) {
        out->buttons |= PAD_BUTTON_Y;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_START)) {
        out->buttons |= PAD_BUTTON_START;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        out->buttons |= PAD_TRIGGER_Z;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ||
        lt >= PAD_TRIGGER_CLICK)
    {
        out->buttons |= PAD_TRIGGER_L;
    }
    if (rt >= PAD_TRIGGER_CLICK) {
        out->buttons |= PAD_TRIGGER_R;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
        dx -= 1;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
        dx += 1;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
        dy -= 1;
    }
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
        dy += 1;
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
    PadInputFrame* p0 = &match_view.live[0];
    PadInputFrame* p1 = &match_view.live[1];
    int sx = 0;
    int sy = 0;

    memset(match_view.live, 0, sizeof(match_view.live));
    if (keys[SDL_SCANCODE_LEFT]) {
        sx -= 80;
    }
    if (keys[SDL_SCANCODE_RIGHT]) {
        sx += 80;
    }
    if (keys[SDL_SCANCODE_UP]) {
        sy += 80;
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        sy -= 80;
    }
    p0->stick_x = (signed char) sx;
    p0->stick_y = (signed char) sy;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]) {
        p0->buttons |= PAD_BUTTON_START;
    }
    if (keys[SDL_SCANCODE_Z]) {
        p0->buttons |= PAD_BUTTON_A;
    }
    if (keys[SDL_SCANCODE_X]) {
        p0->buttons |= PAD_BUTTON_B;
    }
    if (keys[SDL_SCANCODE_C]) {
        p0->buttons |= PAD_BUTTON_X;
    }
    if (keys[SDL_SCANCODE_V]) {
        p0->buttons |= PAD_BUTTON_Y;
    }
    if (keys[SDL_SCANCODE_A]) {
        p0->buttons |= PAD_TRIGGER_L;
    }
    if (keys[SDL_SCANCODE_S]) {
        p0->buttons |= PAD_TRIGGER_R;
    }
    if (keys[SDL_SCANCODE_Q]) {
        p0->buttons |= PAD_TRIGGER_Z;
    }
    if (keys[SDL_SCANCODE_J]) {
        p1->stick_x -= 80;
    }
    if (keys[SDL_SCANCODE_L]) {
        p1->stick_x += 80;
    }
    if (keys[SDL_SCANCODE_I]) {
        p1->stick_y += 80;
    }
    if (keys[SDL_SCANCODE_K]) {
        p1->stick_y -= 80;
    }
    if (keys[SDL_SCANCODE_F]) {
        p1->buttons |= PAD_BUTTON_A;
    }
    if (keys[SDL_SCANCODE_G]) {
        p1->buttons |= PAD_BUTTON_B;
    }
    if (keys[SDL_SCANCODE_T]) {
        p1->buttons |= PAD_BUTTON_START;
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

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
    pad_set_live_input(match_view.live, 4);
}

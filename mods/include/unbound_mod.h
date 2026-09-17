#ifndef UNBOUND_MOD_H
#define UNBOUND_MOD_H

/*
 * The mod-side SDK.  Include this, implement `unbound_mod_init`, and the
 * same source builds two ways:
 *
 *   wasm32 (desktop)   clang --target=wasm32 -nostdlib ...
 *                      -> mods/<id>/<id>.wasm, loaded by native/mod/mod_wasm.c
 *   native (browser)   compiled straight into the port, no loader
 *
 * Nothing below is conditional on which one you are building: the import and
 * export attributes are no-ops outside wasm, and the host implements the same
 * functions as ordinary C for the native binding.  A mod never knows which it
 * is.
 *
 * Only scalars cross the boundary.  A `const char*` is fine because a wasm
 * pointer is an i32 offset into the guest's own memory, which the host can
 * read after validating it -- but a pointer the *host* owns can never be
 * handed to a guest, which is why every engine object is an opaque handle.
 */

#include "unbound_abi.h"

#if defined(__wasm__)
#define UNBOUND_IMPORT(name)                                                  \
    __attribute__((import_module("unbound"), import_name(name)))
#define UNBOUND_EXPORT(name) __attribute__((export_name(name)))
#else
#define UNBOUND_IMPORT(name)
#define UNBOUND_EXPORT(name)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------- imports */

/*
 * Install this mod's handler for `hook`.  Only legal from inside
 * `unbound_mod_init`; the host builds its dispatch chains once and they are
 * immutable for the run, so a hook cannot appear or vanish mid-frame.
 */
UNBOUND_IMPORT("hook_enable")
void unbound_hook_enable(unsigned hook, int priority);

/* Current drawable size in pixels. */
UNBOUND_IMPORT("display_width") int unbound_display_width(void);
UNBOUND_IMPORT("display_height") int unbound_display_height(void);

/*
 * The aspect ratio the 640x480 EFB is presented at.  4:3 is the retail
 * value: the EFB is fitted to that aspect inside the window and centred, so
 * a differently shaped window gets bars rather than a stretched picture.
 * Raising it to the window's own aspect makes the image fill the window --
 * which is only correct if the projections have been widened to match, so a
 * mod that calls this is expected to handle UNBOUND_HOOK_CAMERA_SETUP too.
 */
UNBOUND_IMPORT("display_set_aspect")
void unbound_display_set_aspect(float aspect);
UNBOUND_IMPORT("display_get_aspect") float unbound_display_get_aspect(void);

/*
 * A setting for this mod.  Resolved by the host from the environment as
 * MELEE_MOD_<ID>_<KEY>, upper-cased, falling back to `fallback` when unset --
 * so `unbound_config_int("widescreen", 1)` is read from
 * MELEE_MOD_UNBOUND_WIDESCREEN.  Keys are string literals; the macro passes
 * the length so the host never has to trust a guest string to be terminated.
 */
UNBOUND_IMPORT("config_int")
int unbound_config_int_raw(const char* key, unsigned len, int fallback);
#define unbound_config_int(key, fallback)                                     \
    unbound_config_int_raw((key), (unsigned) (sizeof(key) - 1), (fallback))

/*
 * Overlay drawing.  Legal only from UNBOUND_HOOK_FRAME, where the host has a
 * drawing pass open; calls from anywhere else are dropped.  Coordinates are
 * the 640x480 space in the frame payload, y down, origin top-left.
 */
UNBOUND_IMPORT("draw_color")
void unbound_draw_color(float r, float g, float b, float a);
UNBOUND_IMPORT("draw_rect")
void unbound_draw_rect(float x, float y, float w, float h);
UNBOUND_IMPORT("draw_text")
void unbound_draw_text_raw(float x, float y, float scale, const char* text,
                           unsigned len);
#define unbound_draw_text(x, y, scale, text)                                  \
    unbound_draw_text_raw((x), (y), (scale), (text),                          \
                          (unsigned) (sizeof(text) - 1))

/*
 * Controller state for a port, 0-3.  `held` is the buttons down now;
 * `pressed` is the ones that went down this frame, which is what a menu
 * wants.  Masks are UNBOUND_BUTTON_*.
 */
UNBOUND_IMPORT("buttons_held") unsigned unbound_buttons_held(int port);
UNBOUND_IMPORT("buttons_pressed") unsigned unbound_buttons_pressed(int port);

/*
 * Non-zero once when this mod's main-menu entry has been chosen, and clears
 * on read -- so poll it from UNBOUND_HOOK_FRAME and act on the frame it
 * returns true.
 *
 * The entry itself is port machinery: adding one means writing to the game's
 * menu table and substituting a label texture, neither of which the ABI can
 * express yet.  This is the seam between the two, and what it should
 * eventually look like is P-833's question.
 */
UNBOUND_IMPORT("menu_activated") int unbound_menu_activated(void);

/*
 * Non-zero while this mod's main-menu entry is the hovered selection.  The
 * game leaves its description box empty for that entry, so a mod that adds
 * one is expected to draw its own description there.
 */
UNBOUND_IMPORT("menu_hovered") int unbound_menu_hovered(void);

/* What kind of screen the game is showing right now; see unbound_abi.h. */
UNBOUND_IMPORT("scene_kind") int unbound_scene_kind(void);

/* Diagnostics.  Goes to stderr with the mod id as a prefix. */
UNBOUND_IMPORT("log") void unbound_log(const char* msg, unsigned len);
#define unbound_logs(msg) unbound_log((msg), (unsigned) (sizeof(msg) - 1))

/* ------------------------------------------------------------- exports */

/*
 * Called once, after the host has accepted the mod's manifest.  Enable hooks
 * here and nowhere else.
 */
UNBOUND_EXPORT("unbound_mod_init") void unbound_mod_init(void);

/*
 * Called for each dispatch of a hook this mod enabled.  The payload has
 * already been copied into the buffer `unbound_mod_payload` returns; write
 * back into that same buffer to change an in/out field.
 */
UNBOUND_EXPORT("unbound_mod_on_hook") void unbound_mod_on_hook(unsigned hook);

/*
 * The mod's payload buffer, at least UNBOUND_PAYLOAD_MAX bytes.  The host
 * asks once at load and copies payloads in and out of it.  `UNBOUND_MOD_MAIN`
 * defines it for you.
 */
UNBOUND_EXPORT("unbound_mod_payload") void* unbound_mod_payload(void);

/*
 * Boilerplate a mod drops in exactly once.  Kept as a macro so the payload
 * buffer's size and alignment are the SDK's problem, not each mod's.
 */
#define UNBOUND_MOD_MAIN()                                                    \
    static union {                                                            \
        unsigned char bytes[UNBOUND_PAYLOAD_MAX];                             \
        float align_f;                                                        \
        unsigned align_u;                                                     \
    } unbound_payload_storage;                                                \
    UNBOUND_EXPORT("unbound_mod_payload") void* unbound_mod_payload(void)     \
    {                                                                         \
        return unbound_payload_storage.bytes;                                 \
    }

#ifdef __cplusplus
}
#endif

#endif /* UNBOUND_MOD_H */

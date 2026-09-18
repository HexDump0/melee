/*
 * Builtin mod binding: Unbound compiled into the host.
 *
 * The desktop build hands each mod's `.wasm` to WAMR (mod_wasm.c).  A browser
 * page has no loader to hand a second module to, and shipping one would mean
 * a wasm runtime inside a wasm runtime, so the browser build compiles Unbound
 * in and binds it here instead (ADR-0026).
 *
 * That is only possible because the registry never knew what a mod was made
 * of: `ModBinding` is four function pointers and `mod.h` already says `ctx`
 * may be "NULL for the native binding".  This file is what that sentence
 * meant -- the same hooks, the same payload contract, the same ordering, with
 * the calls going straight across instead of through a sandbox.
 *
 * **The sandbox is what is lost, and it is worth saying plainly.**  A wasm mod
 * cannot reach memory the host did not hand it; a builtin one is ordinary C
 * in the same address space.  That is acceptable for Unbound, which ships
 * with the port and is built from these sources, and it is not a precedent
 * for anybody else's mod: a stranger's mod must stay in the sandbox, which on
 * the web means it does not load at all.
 */
#include "mod/mod.h"

#include <unbound_mod.h>

/* ------------------------------------------------ the mod's host imports */

void unbound_hook_enable(unsigned hook, int priority)
{
    mod_host_hook_enable(hook, priority);
}

int unbound_config_int_raw(const char* key, unsigned len, int fallback)
{
    return mod_host_config_int(key, len, fallback);
}

void unbound_log(const char* msg, unsigned len) { mod_host_log(msg, len); }

int unbound_display_width(void) { return mod_host_display_width(); }
int unbound_display_height(void) { return mod_host_display_height(); }

void unbound_display_set_aspect(float aspect)
{
    mod_host_display_set_aspect(aspect);
}

float unbound_display_get_aspect(void)
{
    return mod_host_display_get_aspect();
}

void unbound_draw_color(float r, float g, float b, float a)
{
    mod_host_draw_color(r, g, b, a);
}

void unbound_draw_rect(float x, float y, float w, float h)
{
    mod_host_draw_rect(x, y, w, h);
}

void unbound_draw_text_raw(float x, float y, float scale, const char* text,
                           unsigned len)
{
    mod_host_draw_text(x, y, scale, text, len);
}

unsigned unbound_buttons_held(int port)
{
    return mod_engine_buttons_held(port);
}

unsigned unbound_buttons_pressed(int port)
{
    return mod_engine_buttons_pressed(port);
}

int unbound_menu_activated(void) { return mod_menu_take_activation(); }
int unbound_menu_hovered(void) { return mod_menu_entry_hovered(); }
int unbound_menu_page_open(void) { return mod_menu_page_open(); }
int unbound_scene_kind(void) { return mod_host_scene_kind(); }

/* ------------------------------------------------------------- the binding */

static void* builtin_payload(void* ctx)
{
    (void) ctx;
    return unbound_mod_payload();
}

static void builtin_init(void* ctx)
{
    (void) ctx;
    unbound_mod_init();
}

static void builtin_on_hook(void* ctx, unsigned hook)
{
    (void) ctx;
    unbound_mod_on_hook(hook);
}

static const ModBinding builtin_binding = { builtin_payload, builtin_init,
                                            builtin_on_hook, NULL };

/*
 * `mod.h` already declared this -- "Add the mods compiled into this binary.
 * Browser." -- and `mod.c` already called it behind
 * `MELEE_MOD_BINDING_NATIVE`.  The registry was waiting for an implementation
 * and a build that defines the flag; this is both.  There is nothing to scan:
 * the one mod that can be here is the one that was compiled in.
 */
void mod_native_scan(void)
{
    mod_add("unbound", "Melee Unbound", "0.1.0", 100, &builtin_binding, NULL);
}

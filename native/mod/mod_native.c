/*
 * The native binding: a mod's C compiled straight into the binary.
 *
 * This is how Unbound ships in the browser, where there is no loader, mods
 * are not supported, and Unbound is the only mod there will ever be.  It
 * compiles the *same sources* as mods/unbound/unbound.wasm -- the ABI lives
 * in a header precisely so that the two bindings cannot drift, and a feature
 * cannot quietly exist on the desktop and not in the browser.
 *
 * The import functions a mod calls are declared by mods/include/unbound_mod.h
 * with attributes that are no-ops off wasm, so here they are simply ordinary
 * C functions forwarding to the registry.  One mod per binary: the mod's
 * exported names are plain globals, so a second compiled-in mod would collide
 * at link time.  That is a real limit and an acceptable one -- if the browser
 * ever wants a second mod it wants the loader, not a second set of globals.
 */
#include "mod/mod.h"

#include "unbound_mod.h"

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

float unbound_display_get_aspect(void) { return mod_host_display_get_aspect(); }

int unbound_scene_kind(void) { return mod_host_scene_kind(); }

static void* native_payload(void* ctx)
{
    (void) ctx;
    return unbound_mod_payload();
}

static void native_init(void* ctx)
{
    (void) ctx;
    unbound_mod_init();
}

static void native_on_hook(void* ctx, unsigned hook)
{
    (void) ctx;
    unbound_mod_on_hook(hook);
}

static const ModBinding native_binding = { native_payload, native_init,
                                           native_on_hook, NULL };

void mod_native_scan(void)
{
    mod_add("unbound", "Melee Unbound", "0.1.0", UNBOUND_PRIORITY_EARLY,
            &native_binding, NULL);
}

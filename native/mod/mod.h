#ifndef MELEE_NATIVE_MOD_MOD_H
#define MELEE_NATIVE_MOD_MOD_H

/*
 * The mod registry, host side (ADR-0026).  The contract mods are written
 * against is mods/include/unbound_abi.h; this is the part that loads them,
 * orders them and dispatches to them.
 *
 * One registry, two bindings:
 *
 *   wasm   (native/mod/mod_wasm.c, desktop)  a `.wasm` in mods/, run under
 *          WAMR.  This is how every mod ships, Unbound's included.
 *   native (native/mod/mod_native.c, browser) the mod's C compiled straight
 *          into the binary.  The browser has no loader and Unbound is its
 *          only mod.
 *
 * Both bindings reach the same hook ids, the same payload structs and the
 * same ordering rules, because a feature that worked on one target and not
 * the other would be a bug nobody found until a player did.
 */

#include <stddef.h>

#include "unbound_abi.h"

typedef struct ModInstance ModInstance;

/*
 * How the registry talks to one mod.  `ctx` is the binding's own handle (a
 * wasm module instance, or NULL for the native binding).
 *
 * `payload` returns a host-addressable buffer of at least
 * UNBOUND_PAYLOAD_MAX bytes belonging to that mod -- for a wasm mod that is
 * its exported buffer translated out of guest address space, which is why
 * the registry can memcpy into it rather than marshalling field by field.
 */
typedef struct ModBinding {
    void* (*payload)(void* ctx);
    void (*init)(void* ctx);
    void (*on_hook)(void* ctx, unsigned hook);
    void (*destroy)(void* ctx);
} ModBinding;

/*
 * Add a mod to the list.  Called by a binding while scanning; the registry
 * has not run anybody's init yet at this point.  Returns NULL if the mod was
 * refused (duplicate id, disabled by the user, table full).
 */
ModInstance* mod_add(const char* id, const char* name, const char* version,
                     int priority, const ModBinding* binding, void* ctx);

/*
 * Build the mod list and initialise it.  Idempotent.
 *
 * MELEE_NO_MODS=1 loads nothing, which is the vanilla-parity configuration:
 * with it set the port must behave exactly as it did before the mod system
 * existed, and that is a test rather than an aspiration.
 * MELEE_MODS_DISABLE=a,b drops individual ids.
 * MELEE_MODS_DIR overrides where the wasm binding looks (default: ./mods).
 */
void mod_system_init(void);
void mod_system_shutdown(void);

/* True when at least one handler is installed.  Call before building a
 * payload so an unused hook costs one predictable branch. */
int mod_hook_active(unsigned hook);

/*
 * Run `hook`'s chain.  Each handler sees the previous one's output, in
 * ascending priority order, ties broken by load order -- never by whatever
 * order the filesystem returned.
 */
void mod_dispatch(unsigned hook, void* payload, unsigned size);

/* Whether `hook` can change what the game computes. */
int mod_hook_effect(unsigned hook);

/*
 * True when any loaded mod handles a UNBOUND_EFFECT_SIM hook.  Nothing
 * consumes this yet.  It is the one fact a future netplay handshake has to
 * agree on between two machines, and it is free to keep correct from the
 * start and expensive to reconstruct later.
 */
int mod_sim_affecting(void);

int mod_is_enabled(const char* id);
unsigned mod_count(void);

/*
 * The display, as the registry sees it.
 *
 * `melee_decomp_boot` runs headless and links no renderer at all, so the
 * registry cannot call into gx_gl directly without dragging the GL backend
 * into a target that deliberately has none.  Whoever owns a drawable installs
 * this; with nothing installed the host reports the bare 640x480 EFB at 4:3
 * and a display mod becomes a no-op, which is the right answer for a headless
 * run rather than a special case inside every mod.
 */
typedef struct ModDisplayBackend {
    void (*get_window_size)(int* width, int* height);
    void (*set_aspect)(float aspect);
    float (*get_aspect)(void);
    /* Overlay drawing, valid only between the host's begin/end pass.  NULL
     * on a target with no renderer, where a drawing mod becomes a no-op. */
    void (*draw_color)(float r, float g, float b, float a);
    void (*draw_text)(float x, float y, float scale, const char* text,
                      unsigned len);
} ModDisplayBackend;

void mod_set_display_backend(const ModDisplayBackend* backend);

/* ------------------------------------------- host API, called by mods */

void mod_host_hook_enable(unsigned hook, int priority);
int mod_host_config_int(const char* key, unsigned len, int fallback);
void mod_host_log(const char* msg, unsigned len);
int mod_host_display_width(void);
int mod_host_display_height(void);
void mod_host_display_set_aspect(float aspect);
void mod_host_draw_color(float r, float g, float b, float a);
void mod_host_draw_text(float x, float y, float scale, const char* text,
                        unsigned len);
/* Opened by whoever owns the drawable, around the frame hook. */
void mod_set_drawing(int open);
float mod_host_display_get_aspect(void);
int mod_host_scene_kind(void);

/* What the engine's own scene index says.  Implemented in mod_cobj.c. */
int mod_engine_scene_kind(void);

/* Controller state, straight off the engine's pad copy.  Implemented in
 * mod_cobj.c because it reads a decomp global. */
unsigned mod_engine_buttons_held(int port);
unsigned mod_engine_buttons_pressed(int port);

/*
 * Force the scene kind reported to mods.
 *
 * The `--match` harness jumps straight into a match without going through the
 * scene machinery, so the engine's scene index still reads whatever it was at
 * boot (GS_MENU, mode 24).  That is a property of the harness, not of the
 * game, and a mod should not have to know about it.  UNBOUND_SCENE_UNKNOWN
 * clears the override.
 */
void mod_set_scene_override(int kind);

/* --------------------------------------------------- binding entry points */

/* Scan MELEE_MODS_DIR for folders with a mod.toml and add them.  Desktop. */
void mod_wasm_scan(void);
void mod_wasm_shutdown(void);

/* Archive hook for the menu-label work (P-838); see mod_menu.c. */
void mod_menu_on_asset(const void* bytes, size_t size);

/* Add the mods compiled into this binary.  Browser. */
void mod_native_scan(void);

#endif /* MELEE_NATIVE_MOD_MOD_H */

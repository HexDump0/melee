/*
 * The wasm binding: mods as `.wasm` files dropped into mods/ (ADR-0026,
 * ADR-0027).  Desktop only -- the browser build has no loader and compiles
 * Unbound in through mod_native.c.
 *
 * Why wasm and not a shared object: a mod should be one file that works on
 * every desktop platform, that cannot take the game down with it, and that
 * does not need rebuilding when the decomp pin moves.  A .so is none of
 * those.  It also means a mod's execution is deterministic by specification,
 * which is the property a future netplay build would need and which no
 * natively-compiled plugin can promise across two machines' compilers.
 *
 * What the sandbox is and is not: it protects the player's machine -- no
 * filesystem, no network, no host memory -- and it deliberately does not try
 * to protect the *game's* state from a mod that asks for it. A mod that can
 * only read what the host thought to expose is a mod nobody can write.
 */
#include "mod/mod.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_export.h"

#define MOD_WASM_MAX 16
#define MOD_WASM_STACK 32768
#define MOD_WASM_HEAP 8192

typedef struct WasmMod {
    char id[32];
    unsigned char* bytes;
    wasm_module_t module;
    wasm_module_inst_t inst;
    wasm_exec_env_t env;
    wasm_function_inst_t fn_init;
    wasm_function_inst_t fn_on_hook;
    void* payload; /* host address of the guest's payload buffer */
} WasmMod;

static WasmMod wasm_mods[MOD_WASM_MAX];
static unsigned wasm_mod_count;
static int runtime_up;

/* ------------------------------------------------- host functions (imports) */

static void host_hook_enable(wasm_exec_env_t env, uint32_t hook,
                             int32_t priority)
{
    (void) env;
    mod_host_hook_enable(hook, priority);
}

static int32_t host_config_int(wasm_exec_env_t env, const char* key,
                               uint32_t len, int32_t fallback)
{
    (void) env;
    return mod_host_config_int(key, len, fallback);
}

static void host_log(wasm_exec_env_t env, const char* msg, uint32_t len)
{
    (void) env;
    mod_host_log(msg, len);
}

static int32_t host_display_width(wasm_exec_env_t env)
{
    (void) env;
    return mod_host_display_width();
}

static int32_t host_display_height(wasm_exec_env_t env)
{
    (void) env;
    return mod_host_display_height();
}

static void host_display_set_aspect(wasm_exec_env_t env, float aspect)
{
    (void) env;
    mod_host_display_set_aspect(aspect);
}

static float host_display_get_aspect(wasm_exec_env_t env)
{
    (void) env;
    return mod_host_display_get_aspect();
}

static int32_t host_menu_activated(wasm_exec_env_t env)
{
    (void) env;
    return mod_menu_take_activation();
}

static int32_t host_menu_hovered(wasm_exec_env_t env)
{
    (void) env;
    return mod_menu_entry_hovered();
}

static int32_t host_scene_kind(wasm_exec_env_t env)
{
    (void) env;
    return mod_host_scene_kind();
}

static void host_draw_color(wasm_exec_env_t env, float r, float g, float b,
                            float a)
{
    (void) env;
    mod_host_draw_color(r, g, b, a);
}

static void host_draw_text(wasm_exec_env_t env, float x, float y, float scale,
                           const char* text, uint32_t len)
{
    (void) env;
    mod_host_draw_text(x, y, scale, text, len);
}

static void host_draw_rect(wasm_exec_env_t env, float x, float y, float w,
                           float h)
{
    (void) env;
    mod_host_draw_rect(x, y, w, h);
}

static uint32_t host_buttons_held(wasm_exec_env_t env, int32_t port)
{
    (void) env;
    return mod_engine_buttons_held(port);
}

static uint32_t host_buttons_pressed(wasm_exec_env_t env, int32_t port)
{
    (void) env;
    return mod_engine_buttons_pressed(port);
}

/*
 * WAMR signature strings: `i` i32, `f` f32, `*~` a guest buffer plus its
 * length, which the runtime bounds-checks and hands over as a host pointer.
 * Using `*~` rather than `$` is deliberate -- it means a guest never has to
 * be trusted to NUL-terminate anything.
 */
/*
 * WAMR's NativeSymbol carries the implementation in a `void*`, so registering
 * one is a function-pointer-to-object conversion that ISO C forbids and
 * -Wpedantic reports.  It is the runtime's API, not a choice we get to make,
 * and it is confined to this table.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static NativeSymbol mod_natives[] = {
    { "hook_enable", (void*) host_hook_enable, "(ii)", NULL },
    { "config_int", (void*) host_config_int, "(*~i)i", NULL },
    { "log", (void*) host_log, "(*~)", NULL },
    { "display_width", (void*) host_display_width, "()i", NULL },
    { "display_height", (void*) host_display_height, "()i", NULL },
    { "display_set_aspect", (void*) host_display_set_aspect, "(f)", NULL },
    { "display_get_aspect", (void*) host_display_get_aspect, "()f", NULL },
    { "scene_kind", (void*) host_scene_kind, "()i", NULL },
    { "draw_color", (void*) host_draw_color, "(ffff)", NULL },
    { "draw_text", (void*) host_draw_text, "(fff*~)", NULL },
    { "draw_rect", (void*) host_draw_rect, "(ffff)", NULL },
    { "buttons_held", (void*) host_buttons_held, "(i)i", NULL },
    { "buttons_pressed", (void*) host_buttons_pressed, "(i)i", NULL },
    { "menu_activated", (void*) host_menu_activated, "()i", NULL },
    { "menu_hovered", (void*) host_menu_hovered, "()i", NULL }
};
#pragma GCC diagnostic pop

/* ------------------------------------------------------------- manifest */

typedef struct Manifest {
    char id[32];
    char name[64];
    char version[16];
    char module[64];
    int abi_version;
    int priority;
} Manifest;

static void trim(char* s)
{
    size_t n = strlen(s);
    size_t i = 0;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                     s[n - 1] == '\n' || s[n - 1] == '"'))
    {
        s[--n] = '\0';
    }
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '"') {
        ++i;
    }
    if (i > 0) {
        memmove(s, s + i, n - i + 1);
    }
}

/*
 * Deliberately not a TOML parser.  A manifest is a handful of flat
 * `key = value` lines; pulling in a parser for that would be a dependency we
 * would then have to keep, and a mod that needs richer configuration than
 * this should be telling us what the loader is missing.
 */
static int manifest_read(const char* path, Manifest* out)
{
    FILE* f = fopen(path, "r");
    char line[256];

    if (f == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->abi_version = -1;
    out->priority = UNBOUND_PRIORITY_NORMAL;

    while (fgets(line, sizeof(line), f) != NULL) {
        char* eq;
        char* key;
        char* value;
        char* hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        key = line;
        value = eq + 1;
        trim(key);
        trim(value);
        if (strcmp(key, "id") == 0) {
            snprintf(out->id, sizeof(out->id), "%s", value);
        } else if (strcmp(key, "name") == 0) {
            snprintf(out->name, sizeof(out->name), "%s", value);
        } else if (strcmp(key, "version") == 0) {
            snprintf(out->version, sizeof(out->version), "%s", value);
        } else if (strcmp(key, "module") == 0) {
            snprintf(out->module, sizeof(out->module), "%s", value);
        } else if (strcmp(key, "abi_version") == 0) {
            out->abi_version = (int) strtol(value, NULL, 10);
        } else if (strcmp(key, "priority") == 0) {
            out->priority = (int) strtol(value, NULL, 10);
        }
    }
    fclose(f);
    return out->id[0] != '\0';
}

/* ------------------------------------------------------------- binding */

static void* wasm_payload(void* ctx) { return ((WasmMod*) ctx)->payload; }

static void wasm_init(void* ctx)
{
    WasmMod* m = (WasmMod*) ctx;
    if (m->fn_init == NULL) {
        return;
    }
    if (!wasm_runtime_call_wasm(m->env, m->fn_init, 0, NULL)) {
        fprintf(stderr, "[mod] %s: init trapped: %s\n", m->id,
                wasm_runtime_get_exception(m->inst));
        wasm_runtime_clear_exception(m->inst);
    }
}

static void wasm_on_hook(void* ctx, unsigned hook)
{
    WasmMod* m = (WasmMod*) ctx;
    uint32_t argv[1];

    if (m->fn_on_hook == NULL) {
        return;
    }
    argv[0] = (uint32_t) hook;
    if (!wasm_runtime_call_wasm(m->env, m->fn_on_hook, 1, argv)) {
        /*
         * A trapping mod is disabled rather than allowed to trap every frame:
         * the exception has to be cleared before the instance is usable
         * again, and a mod that faults once will fault again with the same
         * inputs.
         */
        fprintf(stderr, "[mod] %s: hook %u trapped: %s -- disabling\n", m->id,
                hook, wasm_runtime_get_exception(m->inst));
        wasm_runtime_clear_exception(m->inst);
        m->fn_on_hook = NULL;
    }
}

static void wasm_destroy(void* ctx)
{
    WasmMod* m = (WasmMod*) ctx;
    if (m->env != NULL) {
        wasm_runtime_destroy_exec_env(m->env);
        m->env = NULL;
    }
    if (m->inst != NULL) {
        wasm_runtime_deinstantiate(m->inst);
        m->inst = NULL;
    }
    if (m->module != NULL) {
        wasm_runtime_unload(m->module);
        m->module = NULL;
    }
    free(m->bytes);
    m->bytes = NULL;
}

static const ModBinding wasm_binding = { wasm_payload, wasm_init,
                                         wasm_on_hook, wasm_destroy };

/* --------------------------------------------------------------- loading */

static int runtime_start(void)
{
    RuntimeInitArgs args;

    if (runtime_up) {
        return 1;
    }
    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_System_Allocator;
    args.native_module_name = "unbound";
    args.native_symbols = mod_natives;
    args.n_native_symbols =
        (uint32_t) (sizeof(mod_natives) / sizeof(mod_natives[0]));
    if (!wasm_runtime_full_init(&args)) {
        fprintf(stderr, "[mod] wasm runtime init failed\n");
        return 0;
    }
    runtime_up = 1;
    return 1;
}

static unsigned char* read_file(const char* path, unsigned* size_out)
{
    FILE* f = fopen(path, "rb");
    unsigned char* buf;
    long size;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char*) malloc((size_t) size);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t) size, f) != (size_t) size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size_out = (unsigned) size;
    return buf;
}

static int wasm_load_one(const char* dir, const Manifest* man)
{
    char path[512];
    char error[192];
    WasmMod* m;
    unsigned size = 0;
    wasm_function_inst_t fn_payload;
    uint32_t argv[1];
    uint32_t offset;

    if (man->abi_version != (int) UNBOUND_ABI_VERSION) {
        fprintf(stderr,
                "[mod] %s: refused, built against ABI %d, this build is %u\n",
                man->id, man->abi_version, UNBOUND_ABI_VERSION);
        return 0;
    }
    if (man->module[0] == '\0') {
        fprintf(stderr, "[mod] %s: refused, manifest names no module\n",
                man->id);
        return 0;
    }
    if (wasm_mod_count >= MOD_WASM_MAX) {
        return 0;
    }

    m = &wasm_mods[wasm_mod_count];
    memset(m, 0, sizeof(*m));
    snprintf(m->id, sizeof(m->id), "%s", man->id);
    snprintf(path, sizeof(path), "%s/%s", dir, man->module);
    m->bytes = read_file(path, &size);
    if (m->bytes == NULL) {
        fprintf(stderr, "[mod] %s: cannot read %s\n", man->id, path);
        return 0;
    }

    error[0] = '\0';
    m->module = wasm_runtime_load(m->bytes, size, error, sizeof(error));
    if (m->module == NULL) {
        fprintf(stderr, "[mod] %s: load failed: %s\n", man->id, error);
        wasm_destroy(m);
        return 0;
    }
    m->inst = wasm_runtime_instantiate(m->module, MOD_WASM_STACK,
                                       MOD_WASM_HEAP, error, sizeof(error));
    if (m->inst == NULL) {
        fprintf(stderr, "[mod] %s: instantiate failed: %s\n", man->id, error);
        wasm_destroy(m);
        return 0;
    }
    m->env = wasm_runtime_create_exec_env(m->inst, MOD_WASM_STACK);
    if (m->env == NULL) {
        fprintf(stderr, "[mod] %s: exec env failed\n", man->id);
        wasm_destroy(m);
        return 0;
    }

    m->fn_init = wasm_runtime_lookup_function(m->inst, "unbound_mod_init");
    m->fn_on_hook =
        wasm_runtime_lookup_function(m->inst, "unbound_mod_on_hook");
    fn_payload = wasm_runtime_lookup_function(m->inst, "unbound_mod_payload");
    if (m->fn_init == NULL || m->fn_on_hook == NULL || fn_payload == NULL) {
        fprintf(stderr,
                "[mod] %s: refused, missing an SDK export (init/on_hook/"
                "payload) -- built without UNBOUND_MOD_MAIN?\n",
                man->id);
        wasm_destroy(m);
        return 0;
    }

    if (!wasm_runtime_call_wasm(m->env, fn_payload, 0, argv)) {
        fprintf(stderr, "[mod] %s: payload query trapped: %s\n", man->id,
                wasm_runtime_get_exception(m->inst));
        wasm_destroy(m);
        return 0;
    }
    offset = argv[0];
    /*
     * Validate before translating.  The offset comes from guest code, so a
     * broken or hostile mod can return anything; this is the one place the
     * host would otherwise take a guest's word for an address.
     */
    if (!wasm_runtime_validate_app_addr(m->inst, offset,
                                        UNBOUND_PAYLOAD_MAX))
    {
        fprintf(stderr, "[mod] %s: refused, payload buffer out of bounds\n",
                man->id);
        wasm_destroy(m);
        return 0;
    }
    m->payload = wasm_runtime_addr_app_to_native(m->inst, offset);
    if (m->payload == NULL) {
        wasm_destroy(m);
        return 0;
    }

    if (mod_add(man->id, man->name, man->version, man->priority,
                &wasm_binding, m) == NULL)
    {
        wasm_destroy(m);
        return 0;
    }
    wasm_mod_count++;
    return 1;
}

void mod_wasm_scan(void)
{
    const char* root = getenv("MELEE_MODS_DIR");
    DIR* d;
    struct dirent* entry;

    if (root == NULL || root[0] == '\0') {
        root = "mods";
    }
    d = opendir(root);
    if (d == NULL) {
        /* No mods folder is not an error: it is the vanilla port. */
        return;
    }
    if (!runtime_start()) {
        closedir(d);
        return;
    }
    while ((entry = readdir(d)) != NULL) {
        char manifest_path[512];
        char dir[448];
        Manifest man;

        if (entry->d_name[0] == '.') {
            continue;
        }
        snprintf(dir, sizeof(dir), "%s/%s", root, entry->d_name);
        snprintf(manifest_path, sizeof(manifest_path), "%s/mod.toml", dir);
        if (!manifest_read(manifest_path, &man)) {
            continue;
        }
        wasm_load_one(dir, &man);
    }
    closedir(d);
}

void mod_wasm_shutdown(void)
{
    wasm_mod_count = 0;
    if (runtime_up) {
        wasm_runtime_destroy();
        runtime_up = 0;
    }
}

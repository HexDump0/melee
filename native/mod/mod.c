/*
 * Mod registry: the list, the ordering, the dispatch, and the host functions
 * mods call.  Binding-agnostic -- everything wasm-specific lives in
 * mod_wasm.c and everything engine-specific in mod_cobj.c.
 */
#include "mod/mod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD_MAX 16
#define MOD_HANDLERS_MAX 32

struct ModInstance {
    char id[32];
    char name[64];
    char version[16];
    int priority;
    unsigned seq;
    const ModBinding* binding;
    void* ctx;
};

typedef struct ModHandler {
    ModInstance* mod;
    int priority;
    unsigned seq;
} ModHandler;

/*
 * Every hook is classified where it is declared, not where it is used.  See
 * unbound_abi.h: PRESENT means two machines may differ, SIM means they may
 * not.  Keep this table in step with the enum; the assert below is what
 * catches a hook added without a decision being made about it.
 */
static const int hook_effect[UNBOUND_HOOK_COUNT] = {
    UNBOUND_EFFECT_PRESENT, /* UNBOUND_HOOK_CAMERA_SETUP */
    UNBOUND_EFFECT_PRESENT  /* UNBOUND_HOOK_DISPLAY_RESIZED */
};

static ModInstance mods[MOD_MAX];
static unsigned mod_total;
static ModHandler handlers[UNBOUND_HOOK_COUNT][MOD_HANDLERS_MAX];
static unsigned handler_count[UNBOUND_HOOK_COUNT];
static int initialised;
static int sim_affecting;

/*
 * Who the registry is currently talking to.  Set around init and around each
 * dispatch, so the host functions a mod calls know which mod is asking
 * without every one of them taking a handle.  Single-threaded by
 * construction: mods are initialised once at startup and dispatched from the
 * render path.
 */
static ModInstance* current;

static void str_copy(char* dst, size_t cap, const char* src)
{
    size_t i;
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1 < cap && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int id_listed(const char* list, const char* id)
{
    const char* p = list;
    size_t len = strlen(id);
    while (p != NULL && *p != '\0') {
        const char* end = strchr(p, ',');
        size_t n = (end != NULL) ? (size_t) (end - p) : strlen(p);
        if (n == len && strncmp(p, id, n) == 0) {
            return 1;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return 0;
}

static int mod_disabled(const char* id)
{
    const char* disable;
    if (getenv("MELEE_NO_MODS") != NULL) {
        return 1;
    }
    disable = getenv("MELEE_MODS_DISABLE");
    if (disable != NULL && id_listed(disable, id)) {
        return 1;
    }
    return 0;
}

ModInstance* mod_add(const char* id, const char* name, const char* version,
                     int priority, const ModBinding* binding, void* ctx)
{
    ModInstance* mod;
    unsigned i;

    if (id == NULL || id[0] == '\0' || binding == NULL) {
        return NULL;
    }
    if (mod_disabled(id)) {
        fprintf(stderr, "[mod] %s: disabled\n", id);
        return NULL;
    }
    if (mod_total >= MOD_MAX) {
        fprintf(stderr, "[mod] %s: refused, at the %d-mod limit\n", id,
                MOD_MAX);
        return NULL;
    }
    for (i = 0; i < mod_total; ++i) {
        if (strcmp(mods[i].id, id) == 0) {
            fprintf(stderr, "[mod] %s: refused, id already loaded\n", id);
            return NULL;
        }
    }

    mod = &mods[mod_total];
    memset(mod, 0, sizeof(*mod));
    str_copy(mod->id, sizeof(mod->id), id);
    str_copy(mod->name, sizeof(mod->name), name != NULL ? name : id);
    str_copy(mod->version, sizeof(mod->version),
             version != NULL ? version : "0");
    mod->priority = priority;
    mod->seq = mod_total;
    mod->binding = binding;
    mod->ctx = ctx;
    mod_total++;
    return mod;
}

void mod_host_hook_enable(unsigned hook, int priority)
{
    ModHandler* h;

    if (current == NULL) {
        fprintf(stderr, "[mod] hook_enable outside init, ignored\n");
        return;
    }
    if (hook >= UNBOUND_HOOK_COUNT) {
        fprintf(stderr, "[mod] %s: unknown hook %u, ignored\n", current->id,
                hook);
        return;
    }
    if (handler_count[hook] >= MOD_HANDLERS_MAX) {
        fprintf(stderr, "[mod] %s: hook %u full, ignored\n", current->id,
                hook);
        return;
    }
    h = &handlers[hook][handler_count[hook]++];
    h->mod = current;
    h->priority = priority;
    h->seq = current->seq;
    if (hook_effect[hook] == UNBOUND_EFFECT_SIM) {
        sim_affecting = 1;
    }
}

int mod_host_config_int(const char* key, unsigned len, int fallback)
{
    char name[128];
    const char* value;
    size_t n = 0;
    size_t i;

    if (current == NULL || key == NULL) {
        return fallback;
    }
    /* MELEE_MOD_<ID>_<KEY>, upper-cased. */
    n += (size_t) snprintf(name, sizeof(name), "MELEE_MOD_%s_", current->id);
    for (i = 0; i < len && n + 1 < sizeof(name); ++i, ++n) {
        char c = key[i];
        name[n] = (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
    }
    if (n >= sizeof(name)) {
        return fallback;
    }
    name[n] = '\0';
    for (i = 0; name[i] != '\0'; ++i) {
        if (name[i] >= 'a' && name[i] <= 'z') {
            name[i] = (char) (name[i] - 'a' + 'A');
        }
    }
    value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    return (int) strtol(value, NULL, 10);
}

void mod_host_log(const char* msg, unsigned len)
{
    fprintf(stderr, "[mod:%s] %.*s\n",
            current != NULL ? current->id : "?", (int) len,
            msg != NULL ? msg : "");
}

static const ModDisplayBackend* display;

void mod_set_display_backend(const ModDisplayBackend* backend)
{
    display = backend;
}

int mod_host_display_width(void)
{
    int w = 640;
    int h = 480;
    if (display != NULL && display->get_window_size != NULL) {
        display->get_window_size(&w, &h);
    }
    return w;
}

int mod_host_display_height(void)
{
    int w = 640;
    int h = 480;
    if (display != NULL && display->get_window_size != NULL) {
        display->get_window_size(&w, &h);
    }
    return h;
}

void mod_host_display_set_aspect(float aspect)
{
    if (display != NULL && display->set_aspect != NULL) {
        display->set_aspect(aspect);
    }
}

float mod_host_display_get_aspect(void)
{
    if (display != NULL && display->get_aspect != NULL) {
        return display->get_aspect();
    }
    return 4.0f / 3.0f;
}

static int handler_before(const ModHandler* a, const ModHandler* b)
{
    if (a->priority != b->priority) {
        return a->priority < b->priority;
    }
    return a->seq < b->seq;
}

static void sort_handlers(unsigned hook)
{
    unsigned i;
    unsigned j;
    /* Insertion sort: at most MOD_HANDLERS_MAX entries, and it is stable,
     * which is what makes "ties break on load order" true rather than
     * approximately true. */
    for (i = 1; i < handler_count[hook]; ++i) {
        ModHandler key = handlers[hook][i];
        j = i;
        while (j > 0 && handler_before(&key, &handlers[hook][j - 1])) {
            handlers[hook][j] = handlers[hook][j - 1];
            --j;
        }
        handlers[hook][j] = key;
    }
}

static int mod_before(const ModInstance* a, const ModInstance* b)
{
    if (a->priority != b->priority) {
        return a->priority < b->priority;
    }
    return a->seq < b->seq;
}

void mod_system_init(void)
{
    unsigned i;
    unsigned j;

    if (initialised) {
        return;
    }
    initialised = 1;

    if (getenv("MELEE_NO_MODS") != NULL) {
        fprintf(stderr, "[mod] MELEE_NO_MODS is set; running vanilla\n");
        return;
    }

#if defined(MELEE_MOD_BINDING_WASM)
    mod_wasm_scan();
#endif
#if defined(MELEE_MOD_BINDING_NATIVE)
    mod_native_scan();
#endif

    /* Declared order, never discovered order. */
    for (i = 1; i < mod_total; ++i) {
        ModInstance key = mods[i];
        j = i;
        while (j > 0 && mod_before(&key, &mods[j - 1])) {
            mods[j] = mods[j - 1];
            --j;
        }
        mods[j] = key;
    }
    for (i = 0; i < mod_total; ++i) {
        mods[i].seq = i;
    }

    for (i = 0; i < mod_total; ++i) {
        current = &mods[i];
        if (mods[i].binding->init != NULL) {
            mods[i].binding->init(mods[i].ctx);
        }
        current = NULL;
    }

    for (i = 0; i < UNBOUND_HOOK_COUNT; ++i) {
        sort_handlers(i);
    }

    for (i = 0; i < mod_total; ++i) {
        fprintf(stderr, "[mod] loaded %s (%s %s) priority %d\n", mods[i].id,
                mods[i].name, mods[i].version, mods[i].priority);
    }
    fprintf(stderr, "[mod] %u loaded, simulation-affecting: %s\n", mod_total,
            sim_affecting ? "yes" : "no");
}

void mod_system_shutdown(void)
{
    unsigned i;
    for (i = 0; i < mod_total; ++i) {
        if (mods[i].binding != NULL && mods[i].binding->destroy != NULL) {
            mods[i].binding->destroy(mods[i].ctx);
        }
    }
    memset(mods, 0, sizeof(mods));
    memset(handlers, 0, sizeof(handlers));
    memset(handler_count, 0, sizeof(handler_count));
    mod_total = 0;
    sim_affecting = 0;
    initialised = 0;
#if defined(MELEE_MOD_BINDING_WASM)
    mod_wasm_shutdown();
#endif
}

int mod_hook_active(unsigned hook)
{
    return hook < UNBOUND_HOOK_COUNT && handler_count[hook] > 0;
}

void mod_dispatch(unsigned hook, void* payload, unsigned size)
{
    unsigned i;

    if (hook >= UNBOUND_HOOK_COUNT || payload == NULL ||
        size > UNBOUND_PAYLOAD_MAX)
    {
        return;
    }
    for (i = 0; i < handler_count[hook]; ++i) {
        ModInstance* mod = handlers[hook][i].mod;
        void* buf = mod->binding->payload(mod->ctx);
        if (buf == NULL) {
            continue;
        }
        /*
         * Copy in and back out around every handler rather than once around
         * the chain: each mod owns its own buffer (a wasm mod's lives in its
         * own linear memory and no other mod can reach it), so chaining is
         * exactly this copy.
         */
        memcpy(buf, payload, size);
        current = mod;
        mod->binding->on_hook(mod->ctx, hook);
        current = NULL;
        memcpy(payload, buf, size);
    }
}

int mod_hook_effect(unsigned hook)
{
    if (hook >= UNBOUND_HOOK_COUNT) {
        return UNBOUND_EFFECT_PRESENT;
    }
    return hook_effect[hook];
}

int mod_sim_affecting(void) { return sim_affecting; }

int mod_is_enabled(const char* id)
{
    unsigned i;
    if (id == NULL) {
        return 0;
    }
    for (i = 0; i < mod_total; ++i) {
        if (strcmp(mods[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

unsigned mod_count(void) { return mod_total; }

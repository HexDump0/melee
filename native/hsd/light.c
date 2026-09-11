#include "hsd/light.h"

#include <stdio.h>
#include <string.h>

#include "platform/disc.h"

#define DATA_BASE 0x20u
#define HSD_MAX_CHAIN 16

typedef struct FindCtx {
    unsigned int offset;
    int found;
    const char *name;
} FindCtx;

static uint32_t be32(const uint8_t *d, size_t n, size_t o)
{
    if (o > n - 4) {
        return 0;
    }
    return ((uint32_t) d[o] << 24) | ((uint32_t) d[o + 1] << 16) |
           ((uint32_t) d[o + 2] << 8) | d[o + 3];
}

static uint16_t be16(const uint8_t *d, size_t n, size_t o)
{
    if (o > n - 2) {
        return 0;
    }
    return (uint16_t) (((uint16_t) d[o] << 8) | d[o + 1]);
}

static float bf32(const uint8_t *d, size_t n, size_t o)
{
    uint32_t bits = be32(d, n, o);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int range_ok(size_t o, size_t bytes, size_t n)
{
    return o <= n && bytes <= n - o;
}

/* NULL-aware data-relative pointer (0 means NULL). */
static size_t rptr(const uint8_t *d, size_t n, size_t field)
{
    uint32_t value;
    if (!range_ok(field, 4, n)) {
        return SIZE_MAX;
    }
    value = be32(d, n, field);
    if (value == 0 || value > n - DATA_BASE) {
        return SIZE_MAX;
    }
    return (size_t) value + DATA_BASE;
}

static int find_symbol(const char *name, unsigned int offset, void *user)
{
    FindCtx *ctx = (FindCtx *) user;
    if (!ctx->found && strcmp(name, ctx->name) == 0) {
        ctx->offset = offset;
        ctx->found = 1;
    }
    return 0;
}

static void read_wobj_pos(const uint8_t *d, size_t n, size_t wobj,
                          uint8_t *present, float out[3])
{
    if (wobj == SIZE_MAX || !range_ok(wobj, 0x10, n)) {
        return;
    }
    out[0] = bf32(d, n, wobj + 4);
    out[1] = bf32(d, n, wobj + 8);
    out[2] = bf32(d, n, wobj + 0xc);
    *present = 1;
}

/*
 * HSD_LightDesc (lobj.h): flags +8, attnflags +0xA, color +0xC,
 * position +0x10, interest +0x14, union +0x18.  The union interpretation
 * follows LObjLoad (lobj.c:951).
 */
static void parse_light_desc(const uint8_t *d, size_t n, size_t desc,
                             SceneLight *out)
{
    size_t position;
    size_t interest;
    size_t u;
    memset(out, 0, sizeof(*out));
    if (desc == SIZE_MAX || !range_ok(desc, 0x1c, n)) {
        return;
    }
    out->flags = be16(d, n, desc + 8);
    out->attnflags = be16(d, n, desc + 0xa);
    memcpy(out->color, d + desc + 0xc, 4);
    out->type = (uint8_t) (out->flags & 3u);
    position = rptr(d, n, desc + 0x10);
    interest = rptr(d, n, desc + 0x14);
    read_wobj_pos(d, n, position, &out->has_position, out->position);
    read_wobj_pos(d, n, interest, &out->has_interest, out->interest);
    u = rptr(d, n, desc + 0x18);
    if (u == SIZE_MAX || !range_ok(u, 0x18, n)) {
        return;
    }
    if (out->type == LOBJ_POINT) {
        if (out->attnflags & 1u) { /* LOBJ_LIGHT_ATTN: raw HSD_LightAttn */
            out->attn_a0 = bf32(d, n, u + 0);
            out->attn_a1 = bf32(d, n, u + 4);
            out->attn_a2 = bf32(d, n, u + 8);
            out->attn_k0 = bf32(d, n, u + 12);
            out->attn_k1 = bf32(d, n, u + 16);
            out->attn_k2 = bf32(d, n, u + 20);
        } else { /* HSD_LightPointDesc: ref_br, ref_dist, dist_func */
            out->ref_br = bf32(d, n, u + 0);
            out->ref_dist = bf32(d, n, u + 4);
            out->dist_func = be32(d, n, u + 8);
        }
    } else if (out->type == LOBJ_SPOT) {
        if (out->attnflags != 0) { /* raw HSD_LightAttn */
            out->attn_a0 = bf32(d, n, u + 0);
            out->attn_a1 = bf32(d, n, u + 4);
            out->attn_a2 = bf32(d, n, u + 8);
            out->attn_k0 = bf32(d, n, u + 12);
            out->attn_k1 = bf32(d, n, u + 16);
            out->attn_k2 = bf32(d, n, u + 20);
        } else { /* HSD_LightSpotDesc: cutoff, spot_func, ref_br, ref_dist, dist_func */
            out->cutoff = bf32(d, n, u + 0);
            out->spot_func = be32(d, n, u + 4);
            out->ref_br = bf32(d, n, u + 8);
            out->ref_dist = bf32(d, n, u + 12);
            out->dist_func = be32(d, n, u + 16);
        }
    }
}

static void parse_light_chain(const uint8_t *d, size_t n, size_t desc,
                              SceneLights *set)
{
    int i;
    for (i = 0; i < HSD_MAX_CHAIN && desc != SIZE_MAX &&
                set->count < MAX_LOBS;
         ++i)
    {
        size_t next;
        if (!range_ok(desc, 0x1c, n)) {
            break;
        }
        parse_light_desc(d, n, desc, &set->lights[set->count]);
        set->count++;
        next = rptr(d, n, desc + 4);
        desc = next;
    }
}

static void parse_fog_desc(const uint8_t *d, size_t n, size_t fog,
                           SceneFog *out)
{
    memset(out, 0, sizeof(*out));
    if (fog == SIZE_MAX || !range_ok(fog, 0x14, n)) {
        return;
    }
    out->present = 1;
    out->type = be32(d, n, fog + 0);
    out->start = bf32(d, n, fog + 8);
    out->end = bf32(d, n, fog + 0xc);
    memcpy(out->color, d + fog + 0x10, 4);
}

static int load_from_archive(const char *disc, const char *name,
                             SceneLights *set, char *error, size_t error_size)
{
    DiscFile asset = {0};
    FindCtx ctx;
    const uint8_t *d;
    size_t n;
    size_t table;

    if (disc_load(disc, name, &asset, error, error_size) !=
        DISC_OK) {
        return 0;
    }
    d = (const uint8_t *) asset.data;
    n = asset.size;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = "MnSelectChrDataTable";
    if (disc_enumerate_public_symbols(&asset, find_symbol, &ctx, error,
                                            error_size) != DISC_OK ||
        !ctx.found)
    {
        disc_free(&asset);
        return 0;
    }
    table = DATA_BASE + (size_t) ctx.offset;
    /* MnSelectChrDataTable: cam +0, light0 +4, light1 +8, fog +0xC. */
    if (range_ok(table, 0x10, n)) {
        parse_light_chain(d, n, rptr(d, n, table + 4), set);
        parse_light_chain(d, n, rptr(d, n, table + 8), set);
        parse_fog_desc(d, n, rptr(d, n, table + 0xc), &set->fog);
    }
    disc_free(&asset);
    return set->count != 0;
}

int lights_load(const char *disc_image, SceneLights *set, char *error,
                     size_t error_size)
{
    static const char *candidates[] = { "MnSlChr.usd", "MnSlChr.dat" };
    size_t i;
    if (disc_image == NULL || set == NULL) {
        return -1;
    }
    memset(set, 0, sizeof(*set));
    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (load_from_archive(disc_image, candidates[i], set, error,
                              error_size)) {
            return 0;
        }
    }
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "no MnSlChr archive with lights");
    }
    return 1;
}

void lights_dump(const SceneLights *set)
{
    size_t i;
    if (set == NULL) {
        return;
    }
    printf("lights: %zu\n", set->count);
    for (i = 0; i < set->count; ++i) {
        const SceneLight *l = &set->lights[i];
        static const char *names[4] = { "AMBIENT", "INFINITE", "POINT",
                                        "SPOT" };
        printf("  [%zu] %-8s flags=%#06x attn=%u color=%u,%u,%u,%u pos=[%.3f %.3f %.3f]%s", i,
               names[l->type & 3u], l->flags, l->attnflags,
               l->color[0], l->color[1], l->color[2], l->color[3],
               (double)l->position[0], (double)l->position[1],
               (double)l->position[2], l->has_position ? "" : " (none)");
        if (l->type == LOBJ_POINT || l->type == LOBJ_SPOT) {
            printf(" ref_dist=%.3f ref_br=%.3f dist_func=%u",
                   (double)l->ref_dist, (double)l->ref_br,
                   (unsigned)l->dist_func);
        }
        if (l->type == LOBJ_SPOT) {
            printf(" cutoff=%.3f spot_func=%u", (double)l->cutoff,
                   (unsigned)l->spot_func);
        }
        printf("\n");
    }
    if (set->fog.present) {
        printf("fog: type=%u start=%.2f end=%.2f color=%u,%u,%u,%u\n",
               (unsigned)set->fog.type, (double)set->fog.start,
               (double)set->fog.end, set->fog.color[0], set->fog.color[1],
               set->fog.color[2], set->fog.color[3]);
    } else {
        printf("fog: none\n");
    }
}

void lights_ambient(const SceneLights *set, float out[3])
{
    size_t i;
    out[0] = out[1] = out[2] = 0.0f;
    if (set == NULL) {
        return;
    }
    for (i = 0; i < set->count; ++i) {
        const SceneLight *l = &set->lights[i];
        if ((l->flags & 3u) == LOBJ_AMBIENT && !(l->flags & LOBJ_HIDDEN)) {
            /* HSD_SetupChannelMode case 4 only uses it with LOBJ_DIFFUSE. */
            if (l->flags & LOBJ_DIFFUSE) {
                out[0] = l->color[0] / 255.0f;
                out[1] = l->color[1] / 255.0f;
                out[2] = l->color[2] / 255.0f;
            }
            return;
        }
    }
}

size_t lights_count(const SceneLights *set, uint16_t mask)
{
    size_t i;
    size_t count = 0;
    if (set == NULL) {
        return 0;
    }
    for (i = 0; i < set->count; ++i) {
        const SceneLight *l = &set->lights[i];
        if (l->flags & LOBJ_HIDDEN) {
            continue;
        }
        if ((l->flags & 3u) == LOBJ_AMBIENT) {
            continue;
        }
        if (l->flags & mask) {
            count++;
        }
    }
    return count;
}

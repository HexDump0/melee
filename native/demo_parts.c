#include "demo_parts.h"

#include <stdio.h>
#include <string.h>

#include "demo_assets.h"

/*
 * ftData<Char> part visibility.
 *
 * Layout (src/melee/ft/types.h, verified against PlMr.dat's reloc table):
 *   ftData +0x08 -> ftData_x8 (data-relative; 0 means the start of the data
 *                   section, which is where Mario's descriptor lives)
 *   ftData_x8 +0x00 = FtPartsDesc { u32 model_num; void* (*vis_table)[4]; }
 *   vis_table[costume][slot] -> FtPartsVisLookup[model_num]
 *   FtPartsVisLookup { int variant_count; TempS* variants; }
 *   TempS { int count; u8* dobj_indices; }
 *
 * The game hides every DObj listed in any slot at load, then animation selects
 * one variant per model.  Variant 0 is the neutral pose.
 */

#define DATA_BASE 0x20u
#define MAX_MODEL_NUM 16

typedef struct FindCtx {
    unsigned int offset;
    int found;
} FindCtx;

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

static int range_ok(size_t o, size_t bytes, size_t n)
{
    return o <= n && bytes <= n - o;
}

/* PlMrNr.dat -> PlMr.dat */
static int ftdata_name(const char *model_file, char *out, size_t out_size)
{
    if (model_file == NULL || strlen(model_file) < 6 ||
        strncmp(model_file, "Pl", 2) != 0) {
        return 0;
    }
    return snprintf(out, out_size, "%.4s.dat", model_file) > 0;
}

static int find_ftdata(const char *name, unsigned int offset, void *user)
{
    FindCtx *ctx = (FindCtx *) user;
    if (!ctx->found && strncmp(name, "ftData", 6) == 0) {
        ctx->offset = offset;
        ctx->found = 1;
    }
    return 0;
}

/* Applies one variant's DObj list. hidden = 1 to hide, 0 to show. */
static void apply_variant(DemoModel *model, const uint8_t *d, size_t n,
                          size_t variant, int hidden)
{
    uint32_t count;
    uint32_t indices;
    size_t list;
    uint32_t k;
    if (!range_ok(variant, 8, n)) {
        return;
    }
    count = be32(d + variant);
    indices = be32(d + variant + 4);
    if (indices == 0 || indices > n - DATA_BASE) {
        return;
    }
    list = DATA_BASE + indices;
    for (k = 0; k < count; ++k) {
        unsigned int idx;
        if (!range_ok(list + k, 1, n)) {
            break;
        }
        idx = d[list + k];
        if (idx < DEMO_MAX_DOBJS) {
            model->dobj_hidden[idx] = (uint8_t) hidden;
        }
    }
}

static void hide_slot(DemoModel *model, const uint8_t *d, size_t n,
                      size_t slot_ptr, size_t model_num)
{
    size_t lookup;
    size_t i;
    if (slot_ptr == 0 || slot_ptr > n - DATA_BASE) {
        return;
    }
    lookup = DATA_BASE + slot_ptr;
    for (i = 0; i < model_num; ++i) {
        uint32_t variants;
        size_t variants_off;
        uint32_t j;
        if (!range_ok(lookup + i * 8, 8, n)) {
            break;
        }
        variants = be32(d + lookup + i * 8);
        {
            uint32_t variant_ptr = be32(d + lookup + i * 8 + 4);
            if (variant_ptr == 0 || variant_ptr > n - DATA_BASE) {
                continue;
            }
            variants_off = DATA_BASE + variant_ptr;
        }
        for (j = 0; j < variants; ++j) {
            apply_variant(model, d, n, variants_off + j * 8, 1);
        }
    }
}

/* Shows variant 0 for every model in a slot (the neutral expression). */
static void show_neutral(DemoModel *model, const uint8_t *d, size_t n,
                         size_t slot_ptr, size_t model_num)
{
    size_t lookup;
    size_t i;
    if (slot_ptr == 0 || slot_ptr > n - DATA_BASE) {
        return;
    }
    lookup = DATA_BASE + slot_ptr;
    for (i = 0; i < model_num; ++i) {
        uint32_t variant_ptr;
        if (!range_ok(lookup + i * 8, 8, n)) {
            break;
        }
        variant_ptr = be32(d + lookup + i * 8 + 4);
        if (variant_ptr == 0 || variant_ptr > n - DATA_BASE) {
            continue;
        }
        apply_variant(model, d, n, DATA_BASE + variant_ptr, 0);
    }
}

int demo_parts_apply(const char *disc_image, const char *model_file,
                     DemoModel *model, char *error, size_t error_size)
{
    char ft_name[32];
    DemoAsset asset = {0};
    FindCtx ctx = {0, 0};
    const uint8_t *d;
    size_t n;
    size_t ft;
    uint32_t desc_ptr;
    size_t desc;
    uint32_t model_num;
    uint32_t vis_table;
    size_t vis;
    size_t slot;

    if (disc_image == NULL || model_file == NULL || model == NULL) {
        return -1;
    }
    if (!ftdata_name(model_file, ft_name, sizeof(ft_name))) {
        return 1;
    }
    if (demo_asset_load(disc_image, ft_name, &asset, error, error_size) !=
        DEMO_ASSET_OK) {
        return 1; /* Visibility is optional. */
    }
    d = (const uint8_t *) asset.data;
    n = asset.size;
    if (demo_asset_enumerate_public_symbols(&asset, find_ftdata, &ctx, error,
                                            error_size) != DEMO_ASSET_OK ||
        !ctx.found) {
        demo_asset_free(&asset);
        return 1;
    }
    ft = DATA_BASE + (size_t) ctx.offset;
    if (!range_ok(ft, 0x60, n)) {
        demo_asset_free(&asset);
        return -1;
    }
    desc_ptr = be32(d + ft + 8);
    if (desc_ptr > n - DATA_BASE) {
        demo_asset_free(&asset);
        return -1;
    }
    desc = DATA_BASE + desc_ptr; /* 0 means the start of the data section */
    if (!range_ok(desc, 8, n)) {
        demo_asset_free(&asset);
        return -1;
    }
    model_num = be32(d + desc);
    vis_table = be32(d + desc + 4);
    if (model_num == 0 || model_num > MAX_MODEL_NUM ||
        vis_table > n - DATA_BASE) {
        demo_asset_free(&asset);
        return 1;
    }
    vis = DATA_BASE + vis_table;
    /* Hide everything the tables mention, like ftParts_800749CC does. */
    for (slot = 0; slot < 4; ++slot) {
        uint32_t slot_ptr;
        if (!range_ok(vis + slot * 4, 4, n)) {
            break;
        }
        slot_ptr = be32(d + vis + slot * 4);
        hide_slot(model, d, n, slot_ptr, model_num);
    }
    /* Show the neutral variant (slot 0, variant 0). */
    {
        uint32_t slot0 = be32(d + vis);
        show_neutral(model, d, n, slot0, model_num);
    }
    demo_asset_free(&asset);
    return 0;
}

void demo_parts_show_all(DemoModel *model)
{
    if (model != NULL) {
        memset(model->dobj_hidden, 0, sizeof(model->dobj_hidden));
    }
}

size_t demo_parts_hidden_count(const DemoModel *model)
{
    size_t i;
    size_t hidden = 0;
    if (model == NULL) {
        return 0;
    }
    for (i = 0; i < model->dobj_count && i < DEMO_MAX_DOBJS; ++i) {
        if (model->dobj_hidden[i]) {
            hidden++;
        }
    }
    return hidden;
}

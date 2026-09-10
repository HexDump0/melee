#include "demo_attributes.h"

#include "demo_assets.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int finite_float_at(const unsigned char *data, size_t size,
                           size_t offset, float *out)
{
    uint32_t bits;
    float value;
    if (offset > size || size - offset < sizeof(bits)) return 0;
    bits = be32(data + offset);
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value)) return 0;
    *out = value;
    return 1;
}

typedef struct MarioRoot {
    const DemoAsset *asset;
    size_t offset;
    int found;
} MarioRoot;

static int find_mario_root(const char *name, unsigned int offset, void *user)
{
    MarioRoot *root = (MarioRoot *)user;
    if (!root->found && strcmp(name, "ftDataMario") == 0) {
        root->offset = offset;
        root->found = 1;
    }
    return 0;
}

static void attr_error(char *error, size_t error_size, const char *text)
{
    if (error != NULL && error_size != 0) {
        (void)snprintf(error, error_size, "%s", text);
    }
}

int demo_load_mario_attrs(const char *disc_image, DemoPhysicsAttrs *attrs,
                          char *error, size_t error_size)
{
    DemoAsset asset = {0};
    MarioRoot root = {0};
    const unsigned char *data;
    size_t attr_offset;
    size_t data_base = 0x20;
    int rc;

    if (attrs == NULL || disc_image == NULL) {
        attr_error(error, error_size, "bad argument");
        return DEMO_ASSET_BAD_ARGUMENT;
    }
    rc = demo_asset_load(disc_image, "PlMr.dat", &asset, error, error_size);
    if (rc != DEMO_ASSET_OK) return rc;
    rc = demo_asset_enumerate_public_symbols(&asset, find_mario_root, &root,
                                             error, error_size);
    if (rc != DEMO_ASSET_OK || !root.found) {
        demo_asset_free(&asset);
        attr_error(error, error_size, "ftDataMario symbol not found");
        return DEMO_ASSET_CORRUPT_ARCHIVE;
    }
    data = (const unsigned char *)asset.data;
    if (data_base > asset.size || root.offset > asset.size - data_base ||
        asset.size - data_base - root.offset < 4) {
        demo_asset_free(&asset);
        attr_error(error, error_size, "ftDataMario root is out of bounds");
        return DEMO_ASSET_CORRUPT_ARCHIVE;
    }
    /* HSD relocation entries encode pointers as offsets from archive data;
     * zero is therefore a valid pointer to the first data byte. */
    attr_offset = be32(data + data_base + root.offset);
    if (attr_offset > asset.size - data_base ||
        asset.size - data_base - attr_offset < 0x7c) {
        demo_asset_free(&asset);
        attr_error(error, error_size, "Mario attributes are out of bounds");
        return DEMO_ASSET_CORRUPT_ARCHIVE;
    }
    attr_offset += data_base;
    {
        float dash_accel_mul;
        float aerial_drift_base;
        if (!finite_float_at(data, asset.size, attr_offset + 0x20,
                             &dash_accel_mul) ||
            !finite_float_at(data, asset.size, attr_offset + 0x24,
                             &attrs->ground_accel) ||
            !finite_float_at(data, asset.size, attr_offset + 0x68,
                             &aerial_drift_base)) {
            demo_asset_free(&asset);
            attr_error(error, error_size, "Mario attributes contain non-finite data");
            return DEMO_ASSET_CORRUPT_ARCHIVE;
        }
        /* Demo physics treats full stick as one acceleration value. The
         * game stores dash acceleration as base + multiplier * stick. */
        attrs->ground_accel += dash_accel_mul;
        /* Aerial acceleration is stick multiplier plus the drift base. */
        if (!finite_float_at(data, asset.size, attr_offset + 0x64,
                             &dash_accel_mul)) {
            demo_asset_free(&asset);
            attr_error(error, error_size, "Mario attributes contain non-finite data");
            return DEMO_ASSET_CORRUPT_ARCHIVE;
        }
        attrs->air_accel = dash_accel_mul + aerial_drift_base;
    }
    if (!finite_float_at(data, asset.size, attr_offset + 0x18,
                         &attrs->ground_friction) ||
        !finite_float_at(data, asset.size, attr_offset + 0x28,
                         &attrs->ground_max_speed) ||
        !finite_float_at(data, asset.size, attr_offset + 0x5c,
                         &attrs->gravity) ||
        !finite_float_at(data, asset.size, attr_offset + 0x60,
                         &attrs->terminal_velocity) ||
        !finite_float_at(data, asset.size, attr_offset + 0x70,
                         &attrs->air_friction) ||
        !finite_float_at(data, asset.size, attr_offset + 0x6c,
                         &attrs->air_max_speed) ||
        !finite_float_at(data, asset.size, attr_offset + 0x40,
                         &attrs->jump_velocity)) {
        demo_asset_free(&asset);
        attr_error(error, error_size, "Mario attributes contain non-finite data");
        return DEMO_ASSET_CORRUPT_ARCHIVE;
    }
    /* Every field is used as a positive movement parameter by the sandbox. */
    if (!(attrs->ground_accel > 0.0f && attrs->ground_friction > 0.0f &&
          attrs->ground_max_speed > 0.0f && attrs->gravity > 0.0f &&
          attrs->terminal_velocity > 0.0f && attrs->air_accel > 0.0f &&
          attrs->air_friction > 0.0f && attrs->air_max_speed > 0.0f &&
          attrs->jump_velocity > 0.0f) ||
        attrs->ground_accel > 100.0f || attrs->ground_max_speed > 100.0f ||
        attrs->gravity > 100.0f || attrs->terminal_velocity > 100.0f ||
        attrs->air_accel > 100.0f || attrs->air_max_speed > 100.0f ||
        attrs->jump_velocity > 100.0f) {
        demo_asset_free(&asset);
        attr_error(error, error_size, "Mario movement attributes are unreasonable");
        return DEMO_ASSET_CORRUPT_ARCHIVE;
    }
    if (attr_offset > asset.size - 0x5c) {
        demo_asset_free(&asset);
        attr_error(error, error_size, "Mario jump count is out of bounds");
        return DEMO_ASSET_CORRUPT_ARCHIVE;
    }
    attrs->max_jumps = (int)be32(data + attr_offset + 0x58);
    if (attrs->max_jumps < 1 || attrs->max_jumps > 10) {
        demo_asset_free(&asset);
        attr_error(error, error_size, "Mario jump count is invalid");
        return DEMO_ASSET_CORRUPT_ARCHIVE;
    }
    demo_asset_free(&asset);
    attr_error(error, error_size, "ok");
    return DEMO_ASSET_OK;
}

#ifndef MELEE_DEMO_ASSETS_H
#define MELEE_DEMO_ASSETS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DemoAsset {
    void *data;
    size_t size;
} DemoAsset;

enum DemoAssetError {
    DEMO_ASSET_OK = 0,
    DEMO_ASSET_BAD_ARGUMENT = 1,
    DEMO_ASSET_OPEN_FAILED,
    DEMO_ASSET_NOT_A_DISC,
    DEMO_ASSET_FILE_NOT_FOUND,
    DEMO_ASSET_CORRUPT_DISC,
    DEMO_ASSET_OUT_OF_MEMORY,
    DEMO_ASSET_IO_ERROR,
    DEMO_ASSET_CORRUPT_ARCHIVE
};

/* image_path may be a CISO, ISO, or GCM image. disc_path is an FST path,
 * for example "PlMr.dat" or "PlMrNr.dat". */
int demo_asset_load(const char *image_path, const char *disc_path,
                    DemoAsset *out, char *error, size_t error_size);

/* Convenience wrapper for the Mario costume model archive. */
int demo_asset_load_default(const char *image_path, DemoAsset *out,
                            char *error, size_t error_size);

void demo_asset_free(DemoAsset *asset);

typedef int (*DemoAssetSymbolFn)(const char *name, unsigned int data_offset,
                                 void *user);

/* Enumerate HSD DAT public symbols in an already loaded asset. Returns zero
 * on success; callback may return non-zero to stop early. */
int demo_asset_enumerate_public_symbols(const DemoAsset *asset,
                                        DemoAssetSymbolFn callback,
                                        void *user, char *error,
                                        size_t error_size);

const char *demo_asset_error_string(int error_code);

#ifdef __cplusplus
}
#endif
#endif

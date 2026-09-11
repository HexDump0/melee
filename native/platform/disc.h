#ifndef MELEE_NATIVE_PLATFORM_DISC_H
#define MELEE_NATIVE_PLATFORM_DISC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DiscFile {
    void *data;
    size_t size;
} DiscFile;

typedef struct DiscFileList {
    char **names;
    size_t count;
    size_t capacity;
} DiscFileList;

enum DiscError {
    DISC_OK = 0,
    DISC_BAD_ARGUMENT = 1,
    DISC_OPEN_FAILED,
    DISC_NOT_A_DISC,
    DISC_FILE_NOT_FOUND,
    DISC_CORRUPT_DISC,
    DISC_OUT_OF_MEMORY,
    DISC_IO_ERROR,
    DISC_CORRUPT_ARCHIVE
};

/* image_path may be a CISO, ISO, or GCM image. disc_path is an FST path,
 * for example "PlMr.dat" or "PlMrNr.dat". */
int disc_load(const char *image_path, const char *disc_path,
                    DiscFile *out, char *error, size_t error_size);

/* Convenience wrapper for the Mario costume model archive. */
int disc_load_default(const char *image_path, DiscFile *out,
                            char *error, size_t error_size);

/* Enumerates file entries on the disc whose names begin with prefix and end
 * with suffix (either may be NULL), sorted alphabetically. */
int disc_list(const char *image_path, const char *prefix,
                    const char *suffix, DiscFileList *out, char *error,
                    size_t error_size);

void disc_list_free(DiscFileList *list);

void disc_free(DiscFile *asset);

typedef int (*DiscSymbolFn)(const char *name, unsigned int data_offset,
                                 void *user);

/* Enumerate HSD DAT public symbols in an already loaded asset. Returns zero
 * on success; callback may return non-zero to stop early. */
int disc_enumerate_public_symbols(const DiscFile *asset,
                                        DiscSymbolFn callback,
                                        void *user, char *error,
                                        size_t error_size);

const char *disc_error_string(int error_code);

#ifdef __cplusplus
}
#endif
#endif

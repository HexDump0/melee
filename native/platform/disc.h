#ifndef MELEE_NATIVE_PLATFORM_DISC_H
#define MELEE_NATIVE_PLATFORM_DISC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DiscFile {
    void *data;
    size_t size;
} DiscFile;

/* Mounted disc image for the platform DVD backend (S3).  The handle owns the
 * open FILE* and the CISO block map; it is not thread safe. */
typedef struct DiscImage DiscImage;

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

/* Mounts image_path and keeps it open for random reads (the DVD backend).
 * Returns DISC_OK and stores the handle in *out, or an error. */
int disc_mount(const char *image_path, DiscImage **out, char *error,
               size_t error_size);

/* Reads size bytes at a disc byte offset, CISO-aware. */
int disc_image_read(const DiscImage *image, uint64_t offset, void *dst,
                    size_t size);

/* Total logical disc size in bytes. */
uint64_t disc_image_size(const DiscImage *image);

void disc_unmount(DiscImage *image);

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

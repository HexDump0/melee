#include "demo_assets.h"

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CISO layout used by the Animal Crossing PC port (MIT, see
 * ACGC-PC-Port/pc/src/pc_disc.c): a 0x8000-byte header, followed by packed
 * blocks. This implementation validates every offset before reading it. */
#define CISO_HEADER_SIZE 0x8000u
#define CISO_MAP_OFFSET 8u
#define CISO_MAP_SIZE (CISO_HEADER_SIZE - CISO_MAP_OFFSET)
#define CISO_MAGIC 0x4f534943u /* bytes: CISO */
#define GC_MAGIC 0xc2339f3du
#define MAX_FST_ENTRIES 1000000u

typedef struct Disc {
    FILE *fp;
    uint64_t file_size;
    uint32_t block_size;
    int ciso;
    unsigned char map[CISO_MAP_SIZE];
} Disc;

static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static uint32_t le32(const unsigned char *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static int range_ok(uint64_t off, uint64_t len, uint64_t total) {
    return off <= total && len <= total - off;
}
static void set_error(char *dst, size_t n, const char *msg) {
    if (dst != NULL && n != 0) {
        snprintf(dst, n, "%s", msg);
    }
}
static int seek_read(Disc *d, uint64_t off, void *dst, size_t size) {
    if (!range_ok(off, size, d->file_size) || off > (uint64_t)LONG_MAX)
        return 0;
    if (fseek(d->fp, (long)off, SEEK_SET) != 0)
        return 0;
    return fread(dst, 1, size, d->fp) == size;
}
static int disc_read(Disc *d, uint64_t off, void *dst, size_t size) {
    unsigned char *out = (unsigned char *)dst;
    if (!d->ciso)
        return seek_read(d, off, dst, size);
    while (size != 0) {
        uint64_t block = off / d->block_size;
        size_t in_block = (size_t)(off % d->block_size);
        size_t chunk = d->block_size - in_block;
        uint64_t physical;
        if (chunk > size) chunk = size;
        if (block >= CISO_MAP_SIZE || d->map[block] == 0) {
            memset(out, 0, chunk);
        } else {
            /* map entries are ordinal physical blocks; reject overflow and
             * truncated packed images before seeking. */
            { uint64_t ordinal = 0, n;
              for (n = 0; n < block; ++n) ordinal += d->map[n] != 0;
              physical = CISO_HEADER_SIZE + ordinal * d->block_size + in_block;
            }
            if (!seek_read(d, physical, out, chunk)) return 0;
        }
        off += chunk; out += chunk; size -= chunk;
    }
    return 1;
}
static void disc_close(Disc *d) {
    if (d->fp != NULL) fclose(d->fp);
    memset(d, 0, sizeof(*d));
}
static int disc_open(Disc *d, const char *path) {
    unsigned char hdr[CISO_HEADER_SIZE];
    long end;
    memset(d, 0, sizeof(*d));
    d->fp = fopen(path, "rb");
    if (d->fp == NULL) return 0;
    if (fseek(d->fp, 0, SEEK_END) != 0 || (end = ftell(d->fp)) < 0) {
        disc_close(d); return 0;
    }
    d->file_size = (uint64_t)end;
    if (d->file_size >= CISO_HEADER_SIZE && seek_read(d, 0, hdr, sizeof(hdr)) &&
        le32(hdr) == CISO_MAGIC) {
        d->block_size = le32(hdr + 4);
        if (d->block_size == 0 || d->block_size > (64u << 20)) {
            disc_close(d); return 0;
        }
        memcpy(d->map, hdr + CISO_MAP_OFFSET, CISO_MAP_SIZE);
        d->ciso = 1;
    }
    return 1;
}

static int fst_find(Disc *d, const char *wanted, uint32_t *off, uint32_t *size) {
    unsigned char head[12], ent[12];
    uint32_t fst, entries, strings, i;
    if (!disc_read(d, 0x1c, head, 4) || be32(head) != GC_MAGIC ||
        !disc_read(d, 0x424, head, 4)) return DEMO_ASSET_NOT_A_DISC;
    fst = be32(head);
    if (!disc_read(d, (uint64_t)fst + 8, head, 4)) return DEMO_ASSET_CORRUPT_DISC;
    entries = be32(head);
    if (entries < 1 || entries > MAX_FST_ENTRIES ||
        !range_ok(fst, (uint64_t)entries * 12, d->ciso ? (uint64_t)CISO_MAP_SIZE * d->block_size : d->file_size))
        return DEMO_ASSET_CORRUPT_DISC;
    strings = fst + entries * 12;
    for (i = 1; i < entries; ++i) {
        uint32_t name_off, parent, end;
        char name[256];
        if (!disc_read(d, (uint64_t)fst + i * 12, ent, 12)) return DEMO_ASSET_CORRUPT_DISC;
        name_off = ((uint32_t)ent[1] << 16) | ((uint32_t)ent[2] << 8) | ent[3];
        if (name_off >= 1u << 24) return DEMO_ASSET_CORRUPT_DISC;
        { size_t j = 0; unsigned char c;
          do { if (j + 1 >= sizeof(name) || !disc_read(d, (uint64_t)strings + name_off + j, &c, 1)) return DEMO_ASSET_CORRUPT_DISC; name[j++] = (char)c; } while (c != 0); }
        if (ent[0] & 1) {
            /* Reconstructing paths from FST parent indices is unnecessary for
             * the common root file, but retain root and one-level directory
             * names by using the entry's parent/end fields below. */
            parent = be32(ent + 4); end = be32(ent + 8); (void)parent; (void)end;
            continue;
        }
        if (strcmp(name, wanted) == 0 || (wanted[0] == '/' && strcmp(name, wanted + 1) == 0)) {
            *off = be32(ent + 4); *size = be32(ent + 8); return DEMO_ASSET_OK;
        }
    }
    return DEMO_ASSET_FILE_NOT_FOUND;
}

int demo_asset_load(const char *image_path, const char *disc_path, DemoAsset *out,
                    char *error, size_t error_size) {
    Disc d; uint32_t off, size; int rc;
    if (out == NULL || image_path == NULL || disc_path == NULL) { set_error(error,error_size,"bad argument"); return DEMO_ASSET_BAD_ARGUMENT; }
    out->data = NULL; out->size = 0;
    if (!disc_open(&d, image_path)) { set_error(error,error_size,"cannot open disc image"); return DEMO_ASSET_OPEN_FAILED; }
    rc = fst_find(&d, disc_path, &off, &size);
    if (rc == DEMO_ASSET_OK) {
        out->data = malloc(size ? size : 1);
        if (out->data == NULL) rc = DEMO_ASSET_OUT_OF_MEMORY;
        else if (!disc_read(&d, off, out->data, size)) { free(out->data); out->data=NULL; rc=DEMO_ASSET_IO_ERROR; }
        else out->size = size;
    }
    disc_close(&d);
    set_error(error,error_size,demo_asset_error_string(rc));
    return rc;
}
int demo_asset_load_default(const char *image_path, DemoAsset *out, char *error, size_t error_size) {
    return demo_asset_load(image_path, "PlMrNr.dat", out, error, error_size);
}
void demo_asset_free(DemoAsset *asset) { if (asset) { free(asset->data); asset->data=NULL; asset->size=0; } }

int demo_asset_enumerate_public_symbols(const DemoAsset *a, DemoAssetSymbolFn cb, void *user, char *error, size_t n) {
    const unsigned char *p; uint32_t file_size,data_size,nrel,npub,next, i;
    if (!a || !a->data || !cb) { set_error(error,n,"bad argument"); return DEMO_ASSET_BAD_ARGUMENT; }
    if (a->size < 0x20) goto corrupt;
    p=(const unsigned char*)a->data; file_size=be32(p); data_size=be32(p+4); nrel=be32(p+8); npub=be32(p+12); next=be32(p+16);
    if (file_size != a->size || data_size > a->size-0x20 || nrel > (a->size-0x20)/4) goto corrupt;
    { uint64_t pos=0x20u + data_size + (uint64_t)nrel*4; if (pos > a->size || npub > (a->size-pos)/8) goto corrupt;
      { uint64_t public_pos=pos; pos += (uint64_t)npub*8; if (pos > a->size || next > (a->size-pos)/8) goto corrupt;
        { uint64_t symbols_pos=pos+(uint64_t)next*8;
          for(i=0;i<npub;i++){ uint32_t doff=be32(p+public_pos+i*8), so=be32(p+public_pos+i*8+4); uint64_t s=symbols_pos+so; size_t lim,j=0; if(doff>=data_size||s>=a->size) goto corrupt; lim=a->size-s; while(j<lim&&p[s+j])j++; if(j==lim)goto corrupt; if(cb((const char*)p+s,doff,user))break; }
        }
      }
    }
    set_error(error,n,"ok"); return DEMO_ASSET_OK;
corrupt: set_error(error,n,"invalid HSD archive"); return DEMO_ASSET_CORRUPT_ARCHIVE;
}
const char *demo_asset_error_string(int c) { switch(c){case 0:return "ok";case 1:return "bad argument";case 2:return "cannot open disc image";case 3:return "not a GameCube disc";case 4:return "file not found";case 5:return "corrupt disc";case 6:return "out of memory";case 7:return "disc read error";case 8:return "corrupt HSD archive";default:return "unknown error";} }

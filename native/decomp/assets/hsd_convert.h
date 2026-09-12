#ifndef MELEE_NATIVE_DECOMP_ASSETS_HSD_CONVERT_H
#define MELEE_NATIVE_DECOMP_ASSETS_HSD_CONVERT_H

#include <stddef.h>

/*
 * Host-endian conversion of HSD archive images (S3).
 *
 * The compiled decompilation reads archive data as big-endian GameCube
 * structs.  `hsd_asset_convert` converts an archive image in place to host
 * order before `HSD_ArchiveParse` runs:
 *
 *   1. the header and the relocation/public/extern tables (u32 words),
 *   2. every pointer field listed in the relocation table (authoritative:
 *      `Locate` will add the host data base to each of them),
 *   3. the numeric fields of the descriptor classes the port knows
 *      (joints, DObj/MObj/PObj/TObj, textures, LUTs, animation trees,
 *      RObs, FigaTrees, ...).
 *
 * Byte-defined ranges (FObj `ad` streams, GX display lists, vertex arrays,
 * texture/TLUT bits, FigaTree node bytes, strings) are never touched:
 * those stay big-endian and the GX HLE and the FObj player read them as
 * bytes.
 *
 * Conversion is cacheable: a content hash + converter version keys a disk
 * cache under $MELEE_ASSET_CACHE, $XDG_CACHE_HOME/melee/assets or
 * ~/.cache/melee/assets.  Set MELEE_NO_ASSET_CACHE=1 to disable it.
 */

typedef struct HsdConvertStats {
    unsigned public_symbols;
    unsigned roots_joint;
    unsigned roots_anim;
    unsigned roots_figatree;
    unsigned roots_unknown;
    unsigned joints;
    unsigned dobjs;
    unsigned mobjs;
    unsigned pobjs;
    unsigned tobjs;
    unsigned anim_joints;
    unsigned aobjs;
    unsigned fobjs;
    unsigned figatrees;
    unsigned robjdescs;
    unsigned scene_descs;
    unsigned stage_maps;
    unsigned ground_params;
    unsigned yakumono_params;
    unsigned itemdata;
    unsigned scripts;
    unsigned reloc_total;
    unsigned reloc_valid;
    int ok; /* header/tables valid and every relocation target converted */
} HsdConvertStats;

/* Returns 1 when the buffer was (or already is) a converted HSD archive,
 * 0 when the bytes do not look like an HSD archive.  `stats` may be NULL.
 * The buffer is converted in place; a cache hit copies the cached image. */
int hsd_asset_convert(unsigned char* data, size_t size,
                      HsdConvertStats* stats);

/*
 * Optional per-archive hook, called by `melee_port_HSD_ArchiveParse` for every
 * archive (including the mini-archives).  The match viewer installs
 * `gx_hle_register_asset` here so the GX HLE can decode display lists/vertex
 * arrays that point into any loaded archive.
 */
void hsd_asset_set_register_hook(void (*fn)(const void*, size_t));

#endif

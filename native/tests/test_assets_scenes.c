/*
 * Asset checks for scene, menu and trophy archives: IfAll, GmRg, MnSl and the
 * Ty tables.
 *
 * Split out of test_decomp_assets.c; see asset_common.h.
 */
#include "asset_common.h"

/* P-696: three IfAll HUD model sets have names that match no converter rule
 * (`lupe`, `tdsce`, `Stc_rarwmdls`), so their whole sub-graph -- joints, TObjs
 * and the HSD_ImageDesc the magnifier copies the EFB into -- stayed
 * big-endian.  ifMagnify_802FBBDC then asked for a 0x4000 x 0x4000 EFB copy
 * (64 x 64 byte-swapped) every frame a player was off-camera, which cost
 * ~400 ms a frame in the copy encoder (G-146).  Reading the converted word as
 * host-endian must give the same value as reading the raw word as big-endian.
 */
int check_ifall_hud_modelsets(const char* image)
{
    static const char* const names[] = { "lupe", "tdsce", "Stc_rarwmdls" };
    char error[256];
    size_t size = 0;
    unsigned char* buffer =
        load_archive(image, "IfAll.dat", NULL, &size, error, sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned checked = 0;
    int failed = 0;
    size_t n;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: IfAll.dat: %s\n", error);
        return 1;
    }
    raw = malloc(size);
    if (raw == NULL) {
        free(buffer);
        return 1;
    }
    memcpy(raw, buffer, size);
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: IfAll.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    for (n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        unsigned char* slot =
            HSD_ArchiveGetPublicAddress(&archive, names[n]);
        unsigned char* desc;
        unsigned char* joint;
        uint32_t off;
        uint32_t host;
        uint32_t want;

        if (slot == NULL || !ptr_in_buffer(slot, buffer, size)) {
            fprintf(stderr, "decomp_assets: IfAll.dat has no `%s`\n",
                    names[n]);
            failed++;
            continue;
        }
        desc = read_host_ptr(slot);
        if (desc == NULL || !ptr_in_buffer(desc + 0x10, buffer, size)) {
            fprintf(stderr, "decomp_assets: `%s` desc out of range\n",
                    names[n]);
            failed++;
            continue;
        }
        joint = read_host_ptr(desc);
        if (joint == NULL || !ptr_in_buffer(joint + 0x40, buffer, size)) {
            fprintf(stderr, "decomp_assets: `%s` joint out of range\n",
                    names[n]);
            failed++;
            continue;
        }
        off = (uint32_t) (joint - (buffer + 0x20));
        host = read_host_u32(buffer + 0x20 + off + 0x04);
        want = read_be_u32(raw + 0x20 + off + 0x04);
        if (host != want) {
            fprintf(stderr,
                    "decomp_assets: IfAll `%s` joint flags=%08x want=%08x "
                    "(not converted?)\n",
                    names[n], host, want);
            failed++;
            continue;
        }
        checked++;
    }
    if (failed == 0) {
        printf("decomp_assets: IfAll.dat hud modelsets=%u ok\n", checked);
    }
    free(raw);
    free(buffer);
    return failed != 0 ? 1 : 0;
}

int check_staffroll_modelset(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "GmStRoll.dat", NULL, &size,
                                         error, sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* descs;
    unsigned checked = 0;
    int failed = 0;
    int i;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: GmStRoll.dat: %s\n", error);
        return 1;
    }
    raw = malloc(size);
    if (raw == NULL) {
        free(buffer);
        return 1;
    }
    memcpy(raw, buffer, size);
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: GmStRoll.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    descs = HSD_ArchiveGetPublicAddress(
        &archive, "ScGamRegStaffrollNames_scene_modelset");
    if (descs == NULL || !ptr_in_buffer(descs, buffer, size)) {
        fprintf(stderr, "decomp_assets: GmStRoll.dat modelset missing\n");
        free(raw);
        free(buffer);
        return 1;
    }
    for (i = 0; i < 10; i++) {
        unsigned char* slot = descs + i * sizeof(void*);
        unsigned char* desc;
        unsigned char* joint;
        uint32_t off;
        uint32_t host;
        uint32_t want;
        if (!ptr_in_buffer(slot, buffer, size)) {
            break;
        }
        desc = read_host_ptr(slot);
        if (desc == NULL || !ptr_in_buffer(desc + 0x10, buffer, size)) {
            continue;
        }
        joint = read_host_ptr(desc);
        if (joint == NULL) {
            continue;
        }
        if (!ptr_in_buffer(joint + 0x40, buffer, size)) {
            failed++;
            continue;
        }
        off = (uint32_t) (joint - (buffer + 0x20));
        host = read_host_u32(buffer + 0x20 + off + 0x04);
        want = read_be_u32(raw + 0x20 + off + 0x04);
        if (host != want) {
            if (failed == 0) {
                fprintf(stderr,
                        "decomp_assets: GmStRoll modelset[%d] joint flags=%08x "
                        "want=%08x (not converted?)\n",
                        i, host, want);
            }
            failed++;
        }
        checked++;
    }
    if (failed == 0) {
        printf("decomp_assets: GmStRoll.dat modelset=%u ok\n", checked);
    }
    free(raw);
    free(buffer);
    return failed != 0 ? 1 : 0;
}

/* GmKumite.dat Stadium spawn tables (`RegClearSpawnEntry[]`, 0x10-byte rows
 * terminated by kind 0x3E7).  gm_80182174 copies x0/x8/xC straight into the
 * runtime table, so an unconverted table makes every spawn entry absurd. */
int check_kumite_tables(const char* image)
{
    static const char* const names[] = {
        "gmKumiteSystemTable10man",   "gmKumiteSystemTable100man",
        "gmKumiteSystemTable10min",   "gmKumiteSystemTable60min",
        "gmKumiteSystemTableEndless", "gmKumiteSystemTableMercilessly",
    };
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "GmKumite.dat", NULL, &size,
                                         error, sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned checked = 0;
    int failed = 0;
    size_t n;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: GmKumite.dat: %s\n", error);
        return 1;
    }
    raw = malloc(size);
    if (raw == NULL) {
        free(buffer);
        return 1;
    }
    memcpy(raw, buffer, size);
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: GmKumite.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    for (n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        unsigned char* table =
            HSD_ArchiveGetPublicAddress(&archive, names[n]);
        uint32_t off;
        uint32_t i;
        if (table == NULL || !ptr_in_buffer(table, buffer, size)) {
            fprintf(stderr, "decomp_assets: GmKumite.dat missing %s\n",
                    names[n]);
            failed++;
            continue;
        }
        off = (uint32_t) (table - (buffer + 0x20));
        for (i = 0; i < 512; i++) {
            uint32_t e = off + i * 0x10;
            uint32_t kind;
            if ((size_t) e + 0x10 > size - 0x20) {
                break;
            }
            kind = read_host_u32(buffer + 0x20 + e);
            if (read_host_u32(buffer + 0x20 + e) !=
                    read_be_u32(raw + 0x20 + e) ||
                read_host_u32(buffer + 0x20 + e + 0x08) !=
                    read_be_u32(raw + 0x20 + e + 0x08) ||
                read_host_u32(buffer + 0x20 + e + 0x0C) !=
                    read_be_u32(raw + 0x20 + e + 0x0C))
            {
                if (failed == 0) {
                    fprintf(stderr,
                            "decomp_assets: GmKumite %s[%u] not converted\n",
                            names[n], i);
                }
                failed++;
                break;
            }
            checked++;
            if (kind == 0x3E7) {
                break;
            }
        }
    }
    if (failed == 0) {
        printf("decomp_assets: GmKumite.dat spawn rows=%u ok\n", checked);
    }
    free(raw);
    free(buffer);
    return failed != 0 ? 1 : 0;
}

/* Converter safety sweep over every HSD archive on the disc:
 *   1. after convert+Locate, every relocation field must equal its raw
 *      big-endian value plus the data base (a walker that writes into a
 *      pointer field is caught, P-652 class);
 *   2. for Ef*Data.dat effect tables, the EF_EffectDesc run ends at the
 *      first entry with no relocation-backed model pointer, and the words
 *      at and after that entry must be untouched (conv_ef_dat used to walk
 *      up to 1024 entries into unrelated data).
 * Both fail before the converter hardening (167 corrupted reloc fields and
 * a heap overflow on the old code path). */

/* P-658: the remaining unwalked public roots.  Each check loads the archive,
 * converts it, parses it and compares a numeric field inside the walked data
 * against the raw archive, so a missing branch fails on the first value. */
int check_scene_root(const char* image, const char* path,
                            const char* symbol)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer =
        load_archive(image, path, NULL, &size, error, sizeof(error));
    unsigned char* raw;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* scene;
    unsigned char* cameras;
    unsigned char* desc;
    uint32_t off;
    int failed = 0;

    if (buffer == NULL) {
        printf("decomp_assets: %s SKIP (%s)\n", path, error);
        return 0;
    }
    raw = malloc(size);
    if (raw == NULL) {
        free(buffer);
        return 1;
    }
    memcpy(raw, buffer, size);
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        printf("decomp_assets: %s conversion failed\n", path);
        free(raw);
        free(buffer);
        return 1;
    }
    scene = HSD_ArchiveGetPublicAddress(&archive, symbol);
    if (scene == NULL || !ptr_in_buffer(scene, buffer, size)) {
        printf("decomp_assets: %s %s missing\n", path, symbol);
        free(raw);
        free(buffer);
        return 1;
    }
    cameras = read_host_ptr(scene + 0x04);
    desc = cameras != NULL && ptr_in_buffer(cameras, buffer, size)
               ? read_host_ptr(cameras)
               : NULL;
    if (desc == NULL || !ptr_in_buffer(desc, buffer, size)) {
        printf("decomp_assets: %s %s camera desc missing\n", path, symbol);
        free(raw);
        free(buffer);
        return 1;
    }
    off = (uint32_t) (desc - (buffer + 0x20));
    /* HSD_CameraDescCommon: nnear at +0x28, ffar at +0x2C. */
    if (read_host_u32(desc + 0x28) != read_be_u32(raw + 0x20 + off + 0x28) ||
        read_host_u32(desc + 0x2C) != read_be_u32(raw + 0x20 + off + 0x2C))
    {
        printf("decomp_assets: %s %s camera near/far not converted\n", path,
               symbol);
        failed = 1;
    } else if (read_host_f32(desc + 0x28) <= 0.0f) {
        printf("decomp_assets: %s %s camera near=%.3g is not sane\n", path,
               symbol, (double) read_host_f32(desc + 0x28));
        failed = 1;
    } else {
        printf("decomp_assets: %s %s near=%.3g ok\n", path, symbol,
               (double) read_host_f32(desc + 0x28));
    }
    free(raw);
    free(buffer);
    return failed;
}

int check_intro_easy(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer =
        load_archive(image, "GmIntEz.dat", NULL, &size, error, sizeof(error));
    unsigned char* raw;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* table;
    uint32_t off;
    int failed = 0;

    if (buffer == NULL) {
        printf("decomp_assets: GmIntEz.dat SKIP (%s)\n", error);
        return 0;
    }
    raw = malloc(size);
    if (raw == NULL) {
        free(buffer);
        return 1;
    }
    memcpy(raw, buffer, size);
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        printf("decomp_assets: GmIntEz.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    table = HSD_ArchiveGetPublicAddress(&archive, "gmIntroEasyTable");
    if (table == NULL || !ptr_in_buffer(table, buffer, size)) {
        printf("decomp_assets: GmIntEz.dat gmIntroEasyTable missing\n");
        free(raw);
        free(buffer);
        return 1;
    }
    off = (uint32_t) (table - (buffer + 0x20));
    /* x00[0].vals = {0.0, 99.0, 99.0}; x6C[0] layout starts at -2.0. */
    if (read_host_f32(table + 0x00) != 0.0f ||
        read_host_f32(table + 0x04) != 99.0f ||
        read_host_f32(table + 0x08) != 99.0f ||
        read_host_u32(table + 0x6C) != read_be_u32(raw + 0x20 + off + 0x6C))
    {
        printf("decomp_assets: GmIntEz.dat gmIntroEasyTable not converted "
               "(x00=%.3g x04=%.3g x08=%.3g x6C=%u want=%u)\n",
               (double) read_host_f32(table + 0x00),
               (double) read_host_f32(table + 0x04),
               (double) read_host_f32(table + 0x08),
               read_host_u32(table + 0x6C),
               read_be_u32(raw + 0x20 + off + 0x6C));
        failed = 1;
    } else {
        printf("decomp_assets: GmIntEz.dat gmIntroEasyTable x00={%.3g,%.3g,"
               "%.3g} ok\n",
               (double) read_host_f32(table + 0x00),
               (double) read_host_f32(table + 0x04),
               (double) read_host_f32(table + 0x08));
    }
    free(raw);
    free(buffer);
    return failed;
}

int check_event_levels(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer =
        load_archive(image, "GmEvent.dat", NULL, &size, error, sizeof(error));
    unsigned char* raw;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char** table;
    unsigned char* entry;
    unsigned char* evinit;
    int failed = 0;

    if (buffer == NULL) {
        printf("decomp_assets: GmEvent.dat SKIP (%s)\n", error);
        return 0;
    }
    raw = malloc(size);
    if (raw == NULL) {
        free(buffer);
        return 1;
    }
    memcpy(raw, buffer, size);
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        printf("decomp_assets: GmEvent.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    table = HSD_ArchiveGetPublicAddress(&archive, "sqEventInitDataLevelTbl");
    if (table == NULL || !ptr_in_buffer(table, buffer, size)) {
        printf("decomp_assets: GmEvent.dat sqEventInitDataLevelTbl missing\n");
        free(raw);
        free(buffer);
        return 1;
    }
    entry = read_host_ptr((const unsigned char*) table);
    evinit = entry != NULL && ptr_in_buffer(entry, buffer, size)
                 ? read_host_ptr(entry + 0x08)
                 : NULL;
    if (evinit == NULL || !ptr_in_buffer(evinit, buffer, size)) {
        printf("decomp_assets: GmEvent.dat level 0 evinit missing\n");
        free(raw);
        free(buffer);
        return 1;
    }
    /* Level 0 flags are the console word 0x2b800102: after the MSB-first to
     * LSB-first repack the host bytes are 0xD1 (x0_0=1, x0_3=2, x0_6=1,
     * x0_7=1) and 0x01 (x1_0=1); unk24 stays 1.0f. */
    if (evinit[0x00] != 0xD1 || evinit[0x01] != 0x01 ||
        read_host_f32(evinit + 0x24) != 1.0f)
    {
        printf("decomp_assets: GmEvent.dat evinit flags=%02x/%02x unk24=%.3g "
               "(not repacked?)\n",
               evinit[0x00], evinit[0x01],
               (double) read_host_f32(evinit + 0x24));
        failed = 1;
    } else {
        printf("decomp_assets: GmEvent.dat level 0 flags=%02x/%02x ok\n",
               evinit[0x00], evinit[0x01]);
    }
    free(raw);
    free(buffer);
    return failed;
}

/* P-727: TyDatai's trophy tables are raw arrays of small integers, so no
 * branch of the converter's name dispatch claimed them and they stayed
 * big-endian -- silently, because a byte-swapped small integer is another
 * plausible small integer.
 *
 * `tyModelSortTbl` is ToyNameData[293]; `_Toy_803064B8` reads entry.x0 as the
 * trophy id and `Toy_8030813C` looks it up in TyDataf's `tyModelFileTbl`.
 * Trophy 268 (0x010C) read the wrong way round is 0x0C01 = 3073, which is in
 * no table, so the Trophy Gallery panicked building its list.  Assert the
 * invariant that actually broke: every sort-table id resolves in the
 * model-file table. */

/* P-728: TyMnBg.dat's ToyFigureBg*_sobjdesc roots were unclaimed by the
 * converter's name dispatch, so the HSD_ImageDescs behind them stayed
 * big-endian.  The trophy screen then asked GX to draw a 320x240 RGBA8
 * background as 16385x61440 format 0x06000000 and the decoder rejected every
 * one of them.  Check the descriptors the sprite path actually reads. */
int check_ty_sobj_backgrounds(const char* image)
{
    static const char* roots[] = { "ToyFigureBg3_sobjdesc",
                                   "ToyFigureBg5_sobjdesc",
                                   "ToyFigureBg6_sobjdesc" };
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "TyMnBg.dat", NULL, &size,
                                         error, sizeof(error));
    HSD_Archive archive;
    HsdConvertStats stats;
    size_t k;
    int failed = 0;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: TyMnBg.dat: %s\n", error);
        return 1;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: TyMnBg.dat conversion failed\n");
        free(buffer);
        return 1;
    }
    for (k = 0; k < sizeof(roots) / sizeof(roots[0]); k++) {
        unsigned char* desc =
            HSD_ArchiveGetPublicAddress(&archive, roots[k]);
        unsigned char* img;
        unsigned width;
        unsigned height;
        unsigned format;

        if (desc == NULL || !ptr_in_buffer(desc, buffer, size)) {
            fprintf(stderr, "decomp_assets: TyMnBg.dat missing %s\n",
                    roots[k]);
            failed++;
            continue;
        }
        img = (unsigned char*) read_host_ptr(desc + 0x00);
        if (img == NULL || !ptr_in_buffer(img, buffer, size)) {
            fprintf(stderr, "decomp_assets: %s image pointer is bad\n",
                    roots[k]);
            failed++;
            continue;
        }
        width = read_host_u16(img + 0x04);
        height = read_host_u16(img + 0x06);
        format = read_host_u32(img + 0x08);
        /* An unconverted descriptor reads as a huge dimension and a format
         * shifted into the top byte; a converted one is a real texture. */
        if (width == 0 || width > 1024 || height == 0 || height > 1024 ||
            format > 14)
        {
            fprintf(stderr,
                    "decomp_assets: %s image %ux%u fmt=%u (not converted?)\n",
                    roots[k], width, height, format);
            failed++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: TyMnBg.dat sobjdesc images ok\n");
    }
    free(buffer);
    return failed != 0 ? 1 : 0;
}

int check_ty_datai_tables(const char* image)
{

    char error[256];
    size_t isize = 0;
    unsigned char* ibuf = NULL;
    unsigned char* raw = NULL;
    unsigned char* sort = NULL;
    HSD_Archive iarch;
    HsdConvertStats stats;
    unsigned i;
    unsigned checked = 0;
    int failed = 0;

    ibuf = load_archive(image, "TyDatai.usd", NULL, &isize, error,
                        sizeof(error));
    if (ibuf == NULL) {
        ibuf = load_archive(image, "TyDatai.dat", NULL, &isize, error,
                            sizeof(error));
    }
    if (ibuf == NULL) {
        fprintf(stderr, "decomp_assets: TyDatai: %s\n", error);
        return 1;
    }
    raw = malloc(isize);
    if (raw == NULL) {
        free(ibuf);
        return 1;
    }
    memcpy(raw, ibuf, isize);
    if (!hsd_asset_convert(ibuf, isize, &stats) ||
        HSD_ArchiveParse(&iarch, ibuf, isize) != 0)
    {
        fprintf(stderr, "decomp_assets: TyDatai conversion failed\n");
        free(raw);
        free(ibuf);
        return 1;
    }
    sort = HSD_ArchiveGetPublicAddress(&iarch, "tyModelSortTbl");
    if (sort == NULL || !ptr_in_buffer(sort, ibuf, isize)) {
        fprintf(stderr, "decomp_assets: TyDatai missing tyModelSortTbl\n");
        free(raw);
        free(ibuf);
        return 1;
    }
    /* Every field is an s16, so each one must read back as the big-endian
     * value the console sees.  Comparing against the raw bytes catches both
     * failure modes: a table left unconverted, and a table converted at u32
     * granularity by a neighbour that overran it (which transposes each pair
     * and is invisible to a plausibility check, since both halves are small
     * integers either way). */
    for (i = 0; i < 293 * 6; i++) {
        size_t off = (size_t) (sort - ibuf) + (size_t) i * 2;
        uint16_t host;
        uint16_t want;

        if (off + 2 > isize) {
            break;
        }
        host = read_host_u16(ibuf + off);
        want = read_be_u16(raw + off);
        if (host != want) {
            if (failed == 0) {
                fprintf(stderr,
                        "decomp_assets: tyModelSortTbl u16[%u] = %u, want %u "
                        "(byte order / wrong granularity)\n",
                        i, host, want);
            }
            failed++;
        }
        checked++;
    }
    if (failed == 0) {
        printf("decomp_assets: TyDatai.dat sort fields=%u ok\n", checked);
    }
    free(raw);
    free(ibuf);
    return failed != 0 ? 1 : 0;
}

/* P-645: TyDataf's trophy tables are 0x54-byte entries { s32 id; char
 * name[0x20]; char model[0x2c] }.  `Toy_8030813C` matches the id against the
 * table and `Toy_80308250` then hands out `entry + 4` (name) and `entry +
 * 0x24` (model symbol), so an unconverted big-endian id makes every lookup
 * miss and the results screen reads whatever the first entry says.  The
 * converter's symbol dispatch tested name lengths 15/17 for the 14/16-char
 * "tyModelFileTbl"/"tyModelFileUsTbl", so neither table was ever walked. */
int check_ty_data_tables(const char* image)
{
    static const struct {
        const char* symbol;
        unsigned count;
    } tables[] = {
        { "tyModelFileTbl", 293 },
        { "tyModelFileUsTbl", 5 },
    };
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "TyDataf.dat", NULL, &size,
                                         error, sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned checked = 0;
    int failed = 0;
    size_t t;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: TyDataf.dat: %s\n", error);
        return 1;
    }
    raw = malloc(size);
    if (raw == NULL) {
        free(buffer);
        return 1;
    }
    memcpy(raw, buffer, size);
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: TyDataf.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    for (t = 0; t < sizeof(tables) / sizeof(tables[0]); t++) {
        unsigned char* table =
            HSD_ArchiveGetPublicAddress(&archive, tables[t].symbol);
        uint32_t off;
        unsigned i;
        if (table == NULL || !ptr_in_buffer(table, buffer, size)) {
            fprintf(stderr, "decomp_assets: TyDataf.dat missing %s\n",
                    tables[t].symbol);
            failed++;
            continue;
        }
        off = (uint32_t) (table - (buffer + 0x20));
        for (i = 0; i < tables[t].count; i++) {
            uint32_t e = off + i * 0x54;
            uint32_t host;
            uint32_t want;
            if ((size_t) e + 0x54 > size - 0x20) {
                break;
            }
            host = read_host_u32(buffer + 0x20 + e);
            want = read_be_u32(raw + 0x20 + e);
            if (host != want) {
                if (failed == 0) {
                    fprintf(stderr,
                            "decomp_assets: TyDataf.dat %s[%u] id=%d "
                            "want=%d (not converted?)\n",
                            tables[t].symbol, i, (int) host, (int) want);
                }
                failed++;
            } else if (host > 0x125) {
                if (failed == 0) {
                    fprintf(stderr,
                            "decomp_assets: TyDataf.dat %s[%u] id=%d "
                            "out of range\n",
                            tables[t].symbol, i, (int) host);
                }
                failed++;
            }
            checked++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: TyDataf.dat trophy ids=%u ok\n", checked);
    }
    free(raw);
    free(buffer);
    return failed != 0 ? 1 : 0;
}

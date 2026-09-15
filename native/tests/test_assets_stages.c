/*
 * Asset checks for stage archives (Gr*.dat): yakumono params, dynamics, stage material animation.
 *
 * Split out of test_decomp_assets.c; see asset_common.h.
 */
#include "asset_common.h"

int check_kraid_param(const char* image)
{
    static const uint32_t want[3] = { 150, 240, 180 };
    static const float want_pos[6] = { -60.0f, -30.0f, 0.0f,
                                       30.0f,  60.0f,  0.0f };
    char error[256];
    size_t size = 0;
    unsigned char* buffer =
        load_archive(image, "GrKr.dat", NULL, &size, error, sizeof(error));
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* p;
    int failed = 0;
    int i;

    if (buffer == NULL) {
        printf("decomp_assets: GrKr.dat SKIP (%s)\n", error);
        return 0;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: GrKr.dat conversion failed\n");
        free(buffer);
        return 1;
    }
    p = HSD_ArchiveGetPublicAddress(&archive, "yakumono_param");
    if (p == NULL || !ptr_in_buffer(p, buffer, size)) {
        fprintf(stderr, "decomp_assets: GrKr.dat yakumono_param missing\n");
        free(buffer);
        return 1;
    }
    for (i = 0; i < 3; i++) {
        uint32_t got = read_host_u32(p + i * 4);
        if (got != want[i]) {
            fprintf(stderr, "decomp_assets: GrKr.dat param+0x%02X=%u want=%u\n",
                    (unsigned) (i * 4), got, want[i]);
            failed++;
        }
    }
    for (i = 0; i < 6; i++) {
        uint32_t bits = read_host_u32(p + 0x1C + (uint32_t) i * 4);
        float got;
        memcpy(&got, &bits, sizeof(got));
        if (got != want_pos[i]) {
            fprintf(stderr,
                    "decomp_assets: GrKr.dat kraid_pos_x[%d]=%g want=%g\n", i,
                    (double) got, (double) want_pos[i]);
            failed++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: GrKr.dat yakumono_param times=150/240/180 "
               "kraid_pos_x=-60..60 ok\n");
    }
    free(buffer);
    return failed != 0 ? 1 : 0;
}

/* GrYt.dat (Yoshi's Story) `yakumono_param` is YorsterParams: four f32 then
 * four s32 read directly by grYorster_802024F0/grYorster_8020266C.  Left
 * big-endian, the block bump threshold x00 is a huge negative float (always
 * passes) and the bump velocity x10 is a denormal ~0, so hitting a Lucky
 * Block from below stops the fighter mid-air. */
int check_yorster_param(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "GrYt.dat", NULL, &size,
                                         error, sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* param;
    uint32_t off;
    unsigned i;
    int failed = 0;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: GrYt.dat: %s\n", error);
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
        fprintf(stderr, "decomp_assets: GrYt.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    param = HSD_ArchiveGetPublicAddress(&archive, "yakumono_param");
    if (param == NULL || !ptr_in_buffer(param, buffer, size)) {
        fprintf(stderr, "decomp_assets: GrYt.dat yakumono_param missing\n");
        free(raw);
        free(buffer);
        return 1;
    }
    off = (uint32_t) (param - (buffer + 0x20));
    for (i = 0; i < 8; i++) {
        uint32_t host = read_host_u32(buffer + 0x20 + off + i * 4);
        uint32_t want = read_be_u32(raw + 0x20 + off + i * 4);
        if (host != want) {
            if (failed == 0) {
                fprintf(stderr,
                        "decomp_assets: GrYt yakumono_param[%u]=%u want=%u "
                        "(not converted?)\n",
                        i, host, want);
            }
            failed++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: GrYt.dat yakumono_param x00=%.3g x10=%d "
               "x14=%d x1C=%d ok\n",
               (double) read_host_f32(buffer + 0x20 + off),
               (int) read_host_u32(buffer + 0x20 + off + 0x10),
               (int) read_host_u32(buffer + 0x20 + off + 0x14),
               (int) read_host_u32(buffer + 0x20 + off + 0x1C));
    }
    free(raw);
    free(buffer);
    return failed != 0 ? 1 : 0;
}

/* P-662: the per-stage `yakumono_param` layouts the converter v77 selects by
 * the archive's own `Grd<Stage>*` publics.  Each range is a field run the
 * layout claims (size 4 = f32/s32, size 2 = s16/u16); the regression requires
 * every field to equal its byte-swapped raw value, which fails on the first
 * word for an unconverted archive and catches an over-broad layout that
 * rewrites a neighbouring field. */
typedef struct ParamRange {
    uint16_t off;
    uint16_t count;
    uint8_t size;
} ParamRange;

typedef struct StageParamCase {
    const char* path;
    const ParamRange* ranges;
    unsigned count;
} StageParamCase;

static const ParamRange corneria_ranges[] = {
    { 0x00, 20, 4 }, { 0x68, 1, 4 }, { 0x70, 1, 4 },
    { 0x74, 4, 4 },  { 0x88, 1, 4 },
};

static const ParamRange izumi_ranges[] = {
    { 0x00, 21, 4 },
};

static const ParamRange kongo_ranges[] = {
    { 0x00, 17, 4 }, { 0x44, 8, 2 }, { 0x54, 4, 4 },
    { 0x64, 2, 4 },  { 0x6C, 6, 4 }, { 0x88, 13, 4 },
};

static const ParamRange story_ranges[] = {
    { 0x00, 9, 4 },
};

static const ParamRange venom_ranges[] = {
    { 0x00, 5, 4 }, { 0x2C, 1, 4 }, { 0x34, 1, 4 },
};

static const ParamRange onett_ranges[] = {
    { 0x00, 26, 4 },
};

static const ParamRange inishie1_ranges[] = {
    { 0x00, 5, 4 }, { 0x14, 6, 2 }, { 0x20, 3, 4 },
    { 0x2C, 6, 4 }, { 0x44, 4, 4 },
};

/* P-707: Peach's Castle (GrCs) `grCastle_YakumonoParam` (grcastle.c:121).
 * Nine `entries[]` of { s16 timer; f32 speed; Vec3 rot } at +0x5C stride 0x14. */
static const ParamRange castle_ranges[] = {
    { 0x00, 8, 2 },  { 0x10, 3, 4 },  { 0x20, 8, 4 },
    { 0x40, 3, 2 },  { 0x48, 3, 4 },  { 0x54, 1, 2 }, { 0x58, 1, 2 },
    { 0x5C, 1, 2 },  { 0x60, 4, 4 },
    { 0x70, 1, 2 },  { 0x74, 4, 4 },
    { 0x84, 1, 2 },  { 0x88, 4, 4 },
    { 0x98, 1, 2 },  { 0x9C, 4, 4 },
    { 0xAC, 1, 2 },  { 0xB0, 4, 4 },
    { 0xC0, 1, 2 },  { 0xC4, 4, 4 },
    { 0xD4, 1, 2 },  { 0xD8, 4, 4 },
    { 0xE8, 1, 2 },  { 0xEC, 4, 4 },
    { 0xFC, 1, 2 },  { 0x100, 4, 4 },
    { 0x110, 1, 4 }, { 0x118, 4, 4 },
    { 0x12C, 4, 2 }, { 0x134, 4, 4 },
};

/* P-741: Pokemon Stadium (GrPs/GrPs3) `grPStadium_YakumonoParam`
 * (grpstadium.c:40).  +0x1C is the { u8 r, g, b } monitor tint and is checked
 * separately by `check_pstadium_param`, since a byte triple has no endianness
 * and a range entry would wrongly demand one. */
static const ParamRange pstadium_ranges[] = {
    { 0x00, 7, 4 }, { 0x20, 10, 4 }, { 0x48, 5, 2 },
};

/* P-746: the three P-708 priority stages -- the ones whose intro countdown
 * drives the looping ambient, the shape that made Peach's Castle drone
 * (P-707) and Pokemon Stadium show noise (P-738).  Every field in all three
 * is 4-byte; Mute City's first four words are pointers, which the relocation
 * pass owns and `conv_u32` skips. */
static const ParamRange kraid_ranges[] = {
    { 0x00, 13, 4 },
};

static const ParamRange mutecity_ranges[] = {
    { 0x10, 16, 4 },
};

static const ParamRange bigblue_ranges[] = {
    { 0x00, 0x144 / 4, 4 },
};

static const StageParamCase stage_param_cases[] = {
    { "GrCn.dat", corneria_ranges,
      (unsigned) (sizeof(corneria_ranges) / sizeof(corneria_ranges[0])) },
    { "GrIz.dat", izumi_ranges,
      (unsigned) (sizeof(izumi_ranges) / sizeof(izumi_ranges[0])) },
    { "GrKg.dat", kongo_ranges,
      (unsigned) (sizeof(kongo_ranges) / sizeof(kongo_ranges[0])) },
    { "GrSt.dat", story_ranges,
      (unsigned) (sizeof(story_ranges) / sizeof(story_ranges[0])) },
    { "GrVe.dat", venom_ranges,
      (unsigned) (sizeof(venom_ranges) / sizeof(venom_ranges[0])) },
    { "GrOt.dat", onett_ranges,
      (unsigned) (sizeof(onett_ranges) / sizeof(onett_ranges[0])) },
    { "GrI1.dat", inishie1_ranges,
      (unsigned) (sizeof(inishie1_ranges) / sizeof(inishie1_ranges[0])) },
    { "GrCs.dat", castle_ranges,
      (unsigned) (sizeof(castle_ranges) / sizeof(castle_ranges[0])) },
    { "GrPs.dat", pstadium_ranges,
      (unsigned) (sizeof(pstadium_ranges) / sizeof(pstadium_ranges[0])) },
    { "GrPs3.dat", pstadium_ranges,
      (unsigned) (sizeof(pstadium_ranges) / sizeof(pstadium_ranges[0])) },
    { "GrKr.dat", kraid_ranges,
      (unsigned) (sizeof(kraid_ranges) / sizeof(kraid_ranges[0])) },
    { "GrMc.dat", mutecity_ranges,
      (unsigned) (sizeof(mutecity_ranges) / sizeof(mutecity_ranges[0])) },
    { "GrBb.dat", bigblue_ranges,
      (unsigned) (sizeof(bigblue_ranges) / sizeof(bigblue_ranges[0])) },
};

int check_stage_params(const char* image)
{
    /* Stages whose `yakumono_param` is packed data (offsets, bytes, mixed
     * s16 records) must stay untouched: a layout applied to them would
     * corrupt live fields.  These three are the counter-examples named in
     * P-662. */
    static const char* untouched[] = { "GrNBa.dat", "GrFs.dat", "GrFz.dat" };
    unsigned c;
    int failed = 0;

    for (c = 0; c < sizeof(untouched) / sizeof(untouched[0]); c++) {
        char error[256];
        size_t size = 0;
        unsigned char* buffer =
            load_archive(image, untouched[c], NULL, &size, error,
                         sizeof(error));
        unsigned char* raw;
        HSD_Archive archive;
        HsdConvertStats stats;
        unsigned char* param;
        uint32_t off;
        int i;
        int case_failed = 0;

        if (buffer == NULL) {
            printf("decomp_assets: %s SKIP (%s)\n", untouched[c], error);
            continue;
        }
        raw = malloc(size);
        if (raw == NULL) {
            free(buffer);
            return failed + 1;
        }
        memcpy(raw, buffer, size);
        if (!hsd_asset_convert(buffer, size, &stats) ||
            HSD_ArchiveParse(&archive, buffer, size) != 0)
        {
            printf("decomp_assets: %s conversion failed\n", untouched[c]);
            failed++;
            free(raw);
            free(buffer);
            continue;
        }
        param = HSD_ArchiveGetPublicAddress(&archive, "yakumono_param");
        off = param != NULL ? (uint32_t) (param - (buffer + 0x20)) : 0;
        if (param == NULL || !ptr_in_buffer(param, buffer, size) ||
            off + 4 > size - 0x20)
        {
            printf("decomp_assets: %s yakumono_param missing\n", untouched[c]);
            failed++;
            free(raw);
            free(buffer);
            continue;
        }
        for (i = 0; i < 4; i++) {
            /* Pointers are byte-swapped by the relocation pass for every
             * archive; only non-pointer words prove a numeric layout did or
             * did not run. */
            if (archive_has_reloc(&archive, buffer + 0x20 + off +
                                               (uint32_t) i * 4))
            {
                continue;
            }
            if (memcmp(buffer + 0x20 + off + (uint32_t) i * 4,
                       raw + 0x20 + off + (uint32_t) i * 4, 4) != 0)
            {
                printf("decomp_assets: %s yakumono_param+%d was converted "
                       "(packed layout must stay raw)\n",
                       untouched[c], i * 4);
                failed++;
                case_failed = 1;
                break;
            }
        }
        if (!case_failed) {
            printf("decomp_assets: %s yakumono_param left raw ok\n",
                   untouched[c]);
        }
        free(raw);
        free(buffer);
    }

    for (c = 0; c < sizeof(stage_param_cases) / sizeof(stage_param_cases[0]);
         c++)
    {
        const StageParamCase* tc = &stage_param_cases[c];
        char error[256];
        size_t size = 0;
        unsigned char* buffer =
            load_archive(image, tc->path, NULL, &size, error, sizeof(error));
        unsigned char* raw;
        HSD_Archive archive;
        HsdConvertStats stats;
        unsigned char* param;
        uint32_t off;
        unsigned r;
        int case_failed = 0;

        if (buffer == NULL) {
            printf("decomp_assets: %s SKIP (%s)\n", tc->path, error);
            continue;
        }
        raw = malloc(size);
        if (raw == NULL) {
            free(buffer);
            return failed + 1;
        }
        memcpy(raw, buffer, size);
        if (!hsd_asset_convert(buffer, size, &stats) ||
            HSD_ArchiveParse(&archive, buffer, size) != 0)
        {
            printf("decomp_assets: %s conversion failed\n", tc->path);
            free(raw);
            free(buffer);
            failed++;
            continue;
        }
        param = HSD_ArchiveGetPublicAddress(&archive, "yakumono_param");
        if (param == NULL || !ptr_in_buffer(param, buffer, size)) {
            printf("decomp_assets: %s yakumono_param missing\n", tc->path);
            free(raw);
            free(buffer);
            failed++;
            continue;
        }
        off = (uint32_t) (param - (buffer + 0x20));
        for (r = 0; r < tc->count && !case_failed; r++) {
            const ParamRange* range = &tc->ranges[r];
            unsigned i;
            for (i = 0; i < range->count; i++) {
                uint32_t field = off + range->off +
                                 (range->size == 2 ? i * 2u : i * 4u);
                if (range->size == 2) {
                    uint16_t host = read_host_u16(buffer + 0x20 + field);
                    uint16_t want =
                        (uint16_t) ((raw[0x20 + field] << 8) |
                                    raw[0x21 + field]);
                    if (host != want) {
                        if (case_failed == 0) {
                            printf("decomp_assets: %s yakumono_param+%#x "
                                   "u16=%u want=%u (not converted?)\n",
                                   tc->path, range->off + i * 2u, host, want);
                        }
                        case_failed = 1;
                        break;
                    }
                } else {
                    uint32_t host = read_host_u32(buffer + 0x20 + field);
                    uint32_t want = read_be_u32(raw + 0x20 + field);
                    if (host != want) {
                        if (case_failed == 0) {
                            printf("decomp_assets: %s yakumono_param+%#x "
                                   "=%u want=%u (not converted?)\n",
                                   tc->path, range->off + i * 4u, host, want);
                        }
                        case_failed = 1;
                        break;
                    }
                }
            }
        }
        if (case_failed) {
            failed++;
        } else if (tc->ranges[0].size == 2) {
            printf("decomp_assets: %s yakumono_param x00=%u layout ok\n",
                   tc->path, read_host_u16(buffer + 0x20 + off));
        } else {
            printf("decomp_assets: %s yakumono_param x00=%.4g layout ok\n",
                   tc->path,
                   (double) read_host_f32(buffer + 0x20 + off));
        }
        free(raw);
        free(buffer);
    }
    return failed;
}

/* P-661 follow-up (GrCs/GrRc): `dynamicsdata_*` publics are source
 * DynamicsDesc blocks whose `data` points at `count` 0x3C-byte records of
 * floats; `lb_80011710` copies them into the runtime dynamics list.  Left
 * big-endian, `count` reads 0x0n000000 and `lb_8000FD48` exhausts the pool
 * (Princess Peach's Castle crashed on entry, grCastle_801CD658). */
int check_castle_dynamics(const char* image)
{
    static const char* names[] = { "dynamicsdata_flag3",
                                   "dynamicsdata_flag4",
                                   "dynamicsdata_flag6" };
    static const int counts[] = { 3, 4, 6 };
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "GrCs.dat", NULL, &size,
                                         error, sizeof(error));
    unsigned char* raw;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned i;
    int failed = 0;

    if (buffer == NULL) {
        printf("decomp_assets: GrCs.dat SKIP (%s)\n", error);
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
        printf("decomp_assets: GrCs.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        unsigned char* desc =
            HSD_ArchiveGetPublicAddress(&archive, names[i]);
        uint32_t count;
        unsigned char* data;
        int w;

        if (desc == NULL || !ptr_in_buffer(desc, buffer, size)) {
            printf("decomp_assets: GrCs.dat %s missing\n", names[i]);
            failed++;
            continue;
        }
        count = read_host_u32(desc + 0x04);
        if ((int) count != counts[i]) {
            printf("decomp_assets: GrCs.dat %s count=%u want=%d "
                   "(not converted?)\n",
                   names[i], count, counts[i]);
            failed++;
            continue;
        }
        data = read_host_ptr(desc + 0x00);
        if (data == NULL || !ptr_in_buffer(data, buffer, size)) {
            printf("decomp_assets: GrCs.dat %s data pointer bad\n", names[i]);
            failed++;
            continue;
        }
        {
            uint32_t data_off = (uint32_t) (data - (buffer + 0x20));
            for (w = 0; w < 15; w++) {
                uint32_t host = read_host_u32(data + (uint32_t) w * 4);
                uint32_t want =
                    read_be_u32(raw + 0x20 + data_off + (uint32_t) w * 4);
                if (host != want) {
                    printf("decomp_assets: GrCs.dat %s record[0][%d]=%u "
                           "want=%u\n",
                           names[i], w, host, want);
                    failed++;
                    break;
                }
            }
        }
    }
    if (failed == 0) {
        printf("decomp_assets: GrCs.dat dynamicsdata counts=3/4/6 records ok\n");
    }
    free(raw);
    free(buffer);
    return failed != 0 ? 1 : 0;
}

/* P-707: `grCastle_801CE260` copies `yakumono_param->entries[map_id - 8].x0`
 * into the Ground's intro timer and `grCastle_801CE578` counts it down before
 * running the castle animation; completing that animation is what stops the
 * looping `castle.ssm` 0x53025 ambient via `Ground_801C5544`.  Left
 * big-endian the timers read negative (map 8: -27391) or tens of thousands of
 * frames (map 9: 22530), so the intro never runs and the loud ambient loops
 * for the whole match. */
int check_castle_param(const char* image)
{
    static const int timers[9] = { 405, 600, 600, 720, 575,
                                   720, 575, 600, 600 };
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "GrCs.dat", NULL, &size,
                                         error, sizeof(error));
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* param;
    uint32_t off;
    int i;
    int failed = 0;

    if (buffer == NULL) {
        printf("decomp_assets: GrCs.dat SKIP (%s)\n", error);
        return 0;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: GrCs.dat conversion failed\n");
        free(buffer);
        return 1;
    }
    param = HSD_ArchiveGetPublicAddress(&archive, "yakumono_param");
    if (param == NULL || !ptr_in_buffer(param, buffer, size)) {
        fprintf(stderr, "decomp_assets: GrCs.dat yakumono_param missing\n");
        free(buffer);
        return 1;
    }
    off = (uint32_t) (param - (buffer + 0x20));
    for (i = 0; i < 9; i++) {
        int host = (int) read_host_u16(buffer + 0x20 + off + 0x5C +
                                       (uint32_t) i * 0x14);
        if (host != timers[i]) {
            fprintf(stderr,
                    "decomp_assets: GrCs.dat entries[%d].x0=%d want=%d "
                    "(intro timer not converted?)\n",
                    i, host, timers[i]);
            failed++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: GrCs.dat yakumono_param entries[0..8].x0="
               "405/600/600/720/575/720/575/600/600 ok\n");
    }
    free(buffer);
    return failed != 0 ? 1 : 0;
}

/* P-741: the Pokemon Stadium jumbotron.  `grStadium_801D2528` seeds the
 * display countdown `gp->u.display.xE0` from this block -- state 7 (the
 * 640x406 live feed) from `randi_between_2(x38, x3C)`, state 8 (the 124x80
 * close-up) from `randi_between(x30, x34)`.  Left big-endian, 600 and 1200
 * read as 0x58020000 and 0xB0040000, so the countdown starts at a garbage
 * negative and `grStadium_801D2344`'s `xE0-- < 0` branch fires on the first
 * frame.  That branch picks a new state and breaks **before** clearing the
 * feed wrapper's flag, so the capture never runs and the monitor samples
 * uninitialised heap -- the noise in B-23.
 *
 * +0x1C is checked too, from the other direction: it is three colour bytes
 * and must come back unswapped, or the walk has run off its layout. */
int check_pstadium_param(const char* image, const char* file)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, file, NULL, &size, error,
                                         sizeof(error));
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* param;
    unsigned char* p;
    int failed = 0;
    unsigned i;
    /* x00..x18, then x20..x44. */
    static const uint32_t want_head[7] = { 3600, 3800, 1200, 1800,
                                           300,  120,  60 };
    static const uint32_t want_dwell[10] = { 600, 240, 600,  300, 600,
                                             1200, 600, 1200, 600, 800 };
    static const int want_s16[5] = { 5, 2, 2, 0, 7 };

    if (buffer == NULL) {
        printf("decomp_assets: %s SKIP (%s)\n", file, error);
        return 0;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: %s conversion failed\n", file);
        free(buffer);
        return 1;
    }
    param = HSD_ArchiveGetPublicAddress(&archive, "yakumono_param");
    if (param == NULL || !ptr_in_buffer(param, buffer, size)) {
        fprintf(stderr, "decomp_assets: %s yakumono_param missing\n", file);
        free(buffer);
        return 1;
    }
    p = param;
    for (i = 0; i < 7; i++) {
        uint32_t host = read_host_u32(p + i * 4);
        if (host != want_head[i]) {
            fprintf(stderr, "decomp_assets: %s param+0x%02X=%u want=%u\n",
                    file, (unsigned) (i * 4), host, want_head[i]);
            failed++;
        }
    }
    if (p[0x1C] != 150 || p[0x1D] != 180 || p[0x1E] != 160) {
        fprintf(stderr,
                "decomp_assets: %s monitor tint=%u,%u,%u want=150,180,160 "
                "(colour bytes must not be swapped)\n",
                file, p[0x1C], p[0x1D], p[0x1E]);
        failed++;
    }
    for (i = 0; i < 10; i++) {
        uint32_t host = read_host_u32(p + 0x20 + i * 4);
        if (host != want_dwell[i]) {
            fprintf(stderr, "decomp_assets: %s param+0x%02X=%u want=%u\n",
                    file, (unsigned) (0x20 + i * 4), host, want_dwell[i]);
            failed++;
        }
    }
    for (i = 0; i < 5; i++) {
        int host = (int) (int16_t) read_host_u16(p + 0x48 + i * 2);
        if (host != want_s16[i]) {
            fprintf(stderr, "decomp_assets: %s param+0x%02X=%d want=%d\n",
                    file, (unsigned) (0x48 + i * 2), host, want_s16[i]);
            failed++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: %s yakumono_param feed dwell x38/x3C=600/1200,"
               " tint=150,180,160 ok\n",
               file);
    }
    free(buffer);
    return failed != 0 ? 1 : 0;
}

/* The compiled stage code (grAnime_801C7C1C -> grAnime_801C6C0C) reads the
 * map's MatAnimJoint/ShapeAnimJoint pointer arrays and loads their AObjDescs
 * at stage load.  Unconverted (big-endian) AObjDesc fields give the runtime
 * bogus end_frame/flags, so material/TEV animation never fades.  Check that the
 * converter walked the arrays and that the first reachable aobjdesc has a sane
 * host-order end_frame. */
int check_stage_matanims(const char* image, const char* name)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, name, NULL, &size,
                                         error, sizeof(error));
    HsdConvertStats stats;
    HSD_Archive archive;
    unsigned char* stage;
    unsigned char* maps;
    unsigned char* arr;
    uint32_t map_count;
    float first_end = -1.0f;
    int found = 0;
    int failed = 0;
    int i;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: %s: %s\n", name, error);
        return 1;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: %s conversion failed\n", name);
        free(buffer);
        return 1;
    }
    if (stats.stage_matanims == 0) {
        fprintf(stderr,
                "decomp_assets: %s stage matanims not converted (matanims=%u)\n",
                name, stats.stage_matanims);
        failed = 1;
    }
    stage = HSD_ArchiveGetPublicAddress(&archive, "map_head");
    maps = stage != NULL ? read_host_ptr(stage + 0x08) : NULL;
    map_count = stage != NULL ? read_host_u32(stage + 0x0C) : 0;
    if (maps == NULL || map_count == 0 || map_count > 256 ||
        !ptr_in_buffer(maps, buffer, size))
    {
        fprintf(stderr, "decomp_assets: %s has no map entry\n", name);
        failed = 1;
    } else {
        uint32_t m;
        int bad = 0;
        int seen = 0;
        for (m = 0; m < map_count; m++) {
            arr = read_host_ptr(maps + m * 0x34 + 0x08);
            for (i = 0;
                 i < 32 && arr != NULL && ptr_in_buffer(arr, buffer, size); i++)
            {
                unsigned char* mj = ((unsigned char**) arr)[i];
                unsigned char* ma;
                unsigned char* aobj;
                float end_frame;
                if (mj == NULL) {
                    continue;
                }
                if (!ptr_in_buffer(mj, buffer, size)) {
                    break;
                }
                ma = read_host_ptr(mj + 0x08); /* MatAnimJoint.matanim */
                if (ma == NULL || !ptr_in_buffer(ma, buffer, size)) {
                    continue;
                }
                aobj = read_host_ptr(ma + 0x04); /* MatAnim.aobjdesc */
                if (aobj == NULL || !ptr_in_buffer(aobj, buffer, size)) {
                    continue;
                }
                end_frame = read_host_f32(aobj + 0x04);
                seen++;
                /* Big-endian data reads back as denormals/absurd values, so a
                 * host-order duration is either exactly 0 or a normal float. */
                if (!isfinite(end_frame) ||
                    (end_frame != 0.0f &&
                     (end_frame < 0.01f || end_frame > 100000.0f)))
                {
                    if (bad == 0) {
                        first_end = end_frame;
                    }
                    bad++;
                } else if (!found && end_frame >= 0.01f) {
                    first_end = end_frame;
                    found = 1;
                }
            }
        }
        if (bad != 0) {
            fprintf(stderr,
                    "decomp_assets: %s matanim end_frame=%g "
                    "(not host order? %d bad of %d)\n",
                    name, (double) first_end, bad, seen);
            failed = 1;
        }
    }
    printf("decomp_assets: %s maps=%u stage_matanims=%u "
           "first_end_frame=%.1f\n",
           name, map_count, stats.stage_matanims, (double) first_end);
    free(buffer);
    return failed;
}

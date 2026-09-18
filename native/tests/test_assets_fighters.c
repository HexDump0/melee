/*
 * Asset checks for fighter archives (Pl*.dat): part animations, ftData tables, hidden parts, CPU attack lists.
 *
 * Split out of test_decomp_assets.c; see asset_common.h.
 */
#include "asset_common.h"

/* P-631: ftDataLink's cap chain copies packed 0x3C-byte solver parameters
 * through lb_80011710.  The relocation table covers the pointer, but these
 * numeric pointees also have to be converted. */
int check_link_dynamics(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "PlLk.dat", NULL, &size,
                                         error, sizeof(error));
    HsdConvertStats stats;
    HSD_Archive archive;
    unsigned char* ft_data;
    unsigned char* dynamics;
    unsigned char* bones;
    unsigned char* params;
    uint32_t dynamics_num;
    uint32_t count;
    float enabled;
    float angle_limit;
    int failed = 0;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: PlLk.dat: %s\n", error);
        return 1;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: PlLk.dat conversion failed\n");
        free(buffer);
        return 1;
    }
    ft_data = HSD_ArchiveGetPublicAddress(&archive, "ftDataLink");
    dynamics = ft_data != NULL ? read_host_ptr(ft_data + 0x2C) : NULL;
    dynamics_num = dynamics != NULL ? read_host_u32(dynamics) : 0;
    bones = dynamics != NULL ? read_host_ptr(dynamics + 0x04) : NULL;
    count = bones != NULL ? read_host_u32(bones + 0x08) : 0;
    params = bones != NULL ? read_host_ptr(bones + 0x04) : NULL;
    enabled = params != NULL ? read_host_f32(params + 0x00) : 0.0f;
    angle_limit = params != NULL ? read_host_f32(params + 0x18) : 0.0f;

    if (dynamics_num != 1 || count != 4 ||
        !isfinite(enabled) || fabsf(enabled - 1.0f) > 1e-6f ||
        !isfinite(angle_limit) || fabsf(angle_limit - 0.6981317f) > 1e-5f)
    {
        fprintf(stderr,
                "decomp_assets: Link dynamics invalid: sets=%u count=%u "
                "enabled=%g angle=%g\n",
                dynamics_num, count, enabled, angle_limit);
        failed = 1;
    } else {
        printf("decomp_assets: PlLk.dat dynamics=%u cap_nodes=%u params=ok\n",
               dynamics_num, count);
    }
    free(buffer);
    return failed;
}

/* Fighter.x8B0 has five part-animation slots.  Each ftData->x1C pointer
 * names a { u16 first_part, u16 part_count, u8*, AnimJoint** } descriptor;
 * the two u16 fields are runtime indices/counts, not byte data. */
int check_ft_part_anims(const char* image, const char* path,
                               const char* symbol)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, path, NULL, &size, error,
                                         sizeof(error));
    unsigned char* raw = NULL;
    HsdConvertStats stats;
    HSD_Archive archive;
    unsigned char* ft_data;
    unsigned char* table;
    unsigned checked = 0;
    unsigned x48_articles = 0;
    int failed = 0;
    int i;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: %s: %s\n", path, error);
        return 1;
    }
    raw = malloc(size);
    if (raw != NULL) {
        memcpy(raw, buffer, size);
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: %s conversion failed\n", path);
        free(raw);
        free(buffer);
        return 1;
    }
    if (check_reloc_integrity(path, raw, buffer, &archive)) {
        failed = 1;
    }
    free(raw);
    ft_data = HSD_ArchiveGetPublicAddress(&archive, symbol);
    table = ft_data != NULL ? read_host_ptr(ft_data + 0x1C) : NULL;
    if (table == NULL || !ptr_in_buffer(table, buffer, size)) {
        fprintf(stderr, "decomp_assets: %s part-animation table missing\n",
                path);
        free(buffer);
        return 1;
    }
    for (i = 0; i < 5; i++) {
        unsigned char* slot = table + i * sizeof(void*);
        unsigned char* entry;
        uint16_t first_part;
        uint16_t part_count;
        unsigned char* parts;

        if (!archive_has_reloc(&archive, slot)) {
            break;
        }
        entry = read_host_ptr(slot);
        if (!ptr_in_buffer(entry, buffer, size) ||
            !ptr_in_buffer(entry + 0x0B, buffer, size))
        {
            fprintf(stderr,
                    "decomp_assets: %s part-animation slot %d is outside "
                    "the archive\n",
                    path, i);
            failed = 1;
            continue;
        }
        first_part = read_host_u16(entry + 0x00);
        part_count = read_host_u16(entry + 0x02);
        parts = read_host_ptr(entry + 0x04);
        if (first_part > 109 || part_count > 109 ||
            (part_count != 0 &&
             (parts == NULL || !ptr_in_buffer(parts, buffer, size) ||
              !ptr_in_buffer(parts + part_count - 1, buffer, size))))
        {
            fprintf(stderr,
                    "decomp_assets: %s part-animation slot %d invalid: "
                    "first=%u count=%u\n",
                    path, i, first_part, part_count);
            failed = 1;
        } else {
            checked++;
        }
    }
    if (checked == 0) {
        fprintf(stderr, "decomp_assets: %s has no part-animation slots\n",
                path);
        failed = 1;
    } else {
        printf("decomp_assets: %s part-animation slots=%u ok\n", path,
               checked);
    }

    /* Every relocation-backed x8 element must be an HSD_AnimJoint tree.  A
     * converter walk that runs past a structure it does not own can overwrite
     * these pointers (Fox's landing crash, P-652): the corrupted value either
     * falls outside the archive or points at a node whose child/next is a raw
     * big-endian word.  The array has no stored length, so invalid entries are
     * only acceptable as a trailing run after the last real tree. */
    {
        unsigned anims_checked = 0;
        int failed_order = 0;

        for (i = 0; i < 5; i++) {
            unsigned char* slot = table + i * sizeof(void*);
            unsigned char* entry;
            unsigned char* anims;
            int anim;
            int invalid_run = 0;

            if (!archive_has_reloc(&archive, slot)) {
                break;
            }
            entry = read_host_ptr(slot);
            if (!ptr_in_buffer(entry, buffer, size)) {
                continue;
            }
            anims = read_host_ptr(entry + 8);
            for (anim = 0; anim < 32; anim++) {
                unsigned char* aslot = anims + anim * 4;
                unsigned char* stack[256];
                unsigned char* node;
                int sp = 0;
                int nodes = 0;
                int valid = 1;

                if (!archive_has_reloc(&archive, aslot)) {
                    break;
                }
                node = read_host_ptr(aslot);
                if (node == NULL) {
                    continue;
                }
                stack[sp++] = node;
                while (sp > 0 && nodes < 4096) {
                    unsigned char* n = stack[--sp];
                    unsigned char* child;
                    unsigned char* next;
                    unsigned char* aobj;
                    nodes++;
                    if (!ptr_in_buffer(n, buffer, size) ||
                        !ptr_in_buffer(n + 0x13, buffer, size))
                    {
                        valid = 0;
                        break;
                    }
                    child = read_host_ptr(n);
                    next = read_host_ptr(n + 4);
                    aobj = read_host_ptr(n + 8);
                    if ((child != NULL && !ptr_in_buffer(child, buffer, size)) ||
                        (next != NULL && !ptr_in_buffer(next, buffer, size)) ||
                        (aobj != NULL && !ptr_in_buffer(aobj, buffer, size)))
                    {
                        valid = 0;
                        break;
                    }
                    if (child != NULL && sp < 256) {
                        stack[sp++] = child;
                    }
                    if (next != NULL && sp < 256) {
                        stack[sp++] = next;
                    }
                }
                if (sp > 0 || nodes >= 4096) {
                    valid = 0;
                }
                if (valid) {
                    if (invalid_run) {
                        failed_order = 1;
                        fprintf(stderr,
                                "decomp_assets: %s part-animation slot %d "
                                "anim %d valid after invalid entries\n",
                                path, i, anim);
                    }
                    anims_checked++;
                } else {
                    invalid_run = 1;
                }
            }
        }
        if (failed_order) {
            failed = 1;
        } else if (anims_checked == 0) {
            fprintf(stderr,
                    "decomp_assets: %s has no part-animation trees\n", path);
            failed = 1;
        } else {
            printf("decomp_assets: %s part-animation trees=%u ok\n", path,
                   anims_checked);
        }
    }
    free(buffer);
    return failed;
}

/* P-655: ftData->x40 (itPickup: twelve grab-offset floats) and x4C_sfx
 * (FtSFX: twelve s32 sound ids plus two FtSFXArr {num, s32* ids}) are
 * numeric pointees that the converter used to leave big-endian.  A
 * byte-swapped pickup offset is a denormal near the origin (the grab volume
 * sits at the world origin) and a byte-swapped SFX id is either silent or
 * the wrong sound, so compare every field against the raw archive. */
int check_ft_data_tables(const char* image, const char* path,
                                const char* symbol)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, path, NULL, &size, error,
                                         sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* ft_data;
    uint32_t ft_off;
    const unsigned char* rdata;
    unsigned char* cdata;
    uint32_t raw_x40;
    uint32_t raw_sfx;
    unsigned checked = 0;
    unsigned x48_articles = 0;
    int failed = 0;
    int i;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: %s: %s\n", path, error);
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
        fprintf(stderr, "decomp_assets: %s conversion failed\n", path);
        free(raw);
        free(buffer);
        return 1;
    }
    ft_data = HSD_ArchiveGetPublicAddress(&archive, symbol);
    if (ft_data == NULL || !ptr_in_buffer(ft_data, buffer, size) ||
        ft_data < buffer + 0x20 || ft_data + 0x60 > buffer + size)
    {
        fprintf(stderr, "decomp_assets: %s missing %s\n", path, symbol);
        free(raw);
        free(buffer);
        return 1;
    }
    ft_off = (uint32_t) (ft_data - (buffer + 0x20));
    rdata = raw + 0x20;
    cdata = buffer + 0x20;

    raw_x40 = read_be_u32(raw + 0x20 + ft_off + 0x40);
    if (raw_x40 != 0) {
        if (raw_x40 + 0x30 > size - 0x20) {
            fprintf(stderr, "decomp_assets: %s x40 out of range\n", path);
            failed = 1;
        } else {
            for (i = 0; i < 12; i++) {
                const unsigned char* rp = rdata + raw_x40 + i * 4;
                const unsigned char* cp = cdata + raw_x40 + i * 4;
                uint32_t host = read_host_u32(cp);
                uint32_t want = read_be_u32(rp);
                if (host != want) {
                    if (failed == 0) {
                        fprintf(stderr,
                                "decomp_assets: %s x40[%d]=%g want=%g "
                                "(not converted?)\n",
                                path, i, (double) read_host_f32(cp),
                                (double) read_be_f32(rp));
                    }
                    failed++;
                }
            }
            checked++;
        }
    }
    {
        /* x54: per-costume part table (five ints) read by ftCo_8009F834 when
         * a command's bone id is 0x8D; it is a relocation-backed pointer, so
         * only the converter walk can byte-swap the entries (P-685). */
        uint32_t raw_parts = read_be_u32(raw + 0x20 + ft_off + 0x54);
        if (raw_parts != 0 && raw_parts + 5 * 4 <= size - 0x20) {
            for (i = 0; i < 5; i++) {
                const unsigned char* rp = rdata + raw_parts + i * 4;
                const unsigned char* cp = cdata + raw_parts + i * 4;
                if (read_host_u32(cp) != read_be_u32(rp)) {
                    if (failed == 0) {
                        fprintf(stderr,
                                "decomp_assets: %s x54[%d]=%u want=%u "
                                "(not converted?)\n",
                                path, i, read_host_u32(cp), read_be_u32(rp));
                    }
                    failed++;
                }
            }
            checked++;
        }
    }
    raw_sfx = read_be_u32(raw + 0x20 + ft_off + 0x4C);
    if (raw_sfx != 0) {
        if (raw_sfx + 0x38 > size - 0x20) {
            fprintf(stderr, "decomp_assets: %s x4C out of range\n", path);
            failed = 1;
        } else {
            static const int sfx_fields[] = { 0x04, 0x08, 0x0C, 0x10, 0x14,
                                              0x18, 0x24, 0x28, 0x2C, 0x30,
                                              0x34 };
            unsigned f;
            for (f = 0;
                 f < sizeof(sfx_fields) / sizeof(sfx_fields[0]); f++)
            {
                const unsigned char* rp = rdata + raw_sfx + sfx_fields[f];
                const unsigned char* cp = cdata + raw_sfx + sfx_fields[f];
                if (read_host_u32(cp) != read_be_u32(rp)) {
                    if (failed == 0) {
                        fprintf(stderr,
                                "decomp_assets: %s x4C+%x=%u want=%u "
                                "(not converted?)\n",
                                path, sfx_fields[f], read_host_u32(cp),
                                read_be_u32(rp));
                    }
                    failed++;
                }
            }
            for (i = 0; i < 3; i++) {
                static const uint32_t arr_fields[] = { 0x00, 0x1C, 0x20 };
                uint32_t at = arr_fields[i];
                uint32_t raw_arr;
                uint32_t num;
                int j;
                if (at == 0x1C &&
                    !archive_has_reloc(&archive,
                                       (unsigned char*) cdata + raw_sfx + at))
                {
                    continue; /* an s32 sound id, not an array pointer */
                }
                raw_arr = read_be_u32(rdata + raw_sfx + at);
                if (raw_arr == 0 || raw_arr + 8 > size - 0x20) {
                    continue;
                }
                num = read_be_u32(rdata + raw_arr);
                if (read_host_u32(cdata + raw_arr) != num) {
                    if (failed == 0) {
                        fprintf(stderr,
                                "decomp_assets: %s x4C+%x array num=%u "
                                "want=%u\n",
                                path, at, read_host_u32(cdata + raw_arr), num);
                    }
                    failed++;
                }
                if (num > 64) {
                    continue;
                }
                {
                    uint32_t raw_ids = read_be_u32(rdata + raw_arr + 4);
                    for (j = 0; j < (int) num; j++) {
                        if (raw_ids + (uint32_t) (j + 1) * 4 > size - 0x20) {
                            break;
                        }
                        if (read_host_u32(cdata + raw_ids + (uint32_t) j * 4) !=
                            read_be_u32(rdata + raw_ids + (uint32_t) j * 4))
                        {
                            if (failed == 0) {
                                fprintf(stderr,
                                        "decomp_assets: %s x4C+%x array id[%d]"
                                        "=%u want=%u\n",
                                        path, at, j,
                                        read_host_u32(cdata + raw_ids +
                                                      (uint32_t) j * 4),
                                        read_be_u32(rdata + raw_ids +
                                                    (uint32_t) j * 4));
                            }
                            failed++;
                        }
                    }
                }
            }
            checked++;
        }
    }
    /* x48_items: the leading Article run.  Each accepted entry's ItemAttr
     * (31 words at +0x04..+0x80) must be host order; holes (zero slots) are
     * legal, and the run ends at the first non-relocated non-NULL slot. */
    {
        uint32_t raw_items = read_be_u32(raw + 0x20 + ft_off + 0x48);
        int k;
        int stop = 0;
        int articles = 0;

        if (raw_items != 0) {
            for (k = 0; k < 32 && !stop; k++) {
                uint32_t slot = raw_items + (uint32_t) k * 4;
                uint32_t article;
                uint32_t attr;
                uint32_t states;
                float f4;
                float sc;
                unsigned w;

                if (slot + 4 > size - 0x20) {
                    break;
                }
                article = read_be_u32(rdata + slot);
                if (article == 0) {
                    continue;
                }
                if (!archive_has_reloc(&archive,
                                       (unsigned char*) cdata + slot) ||
                    article + 0x18 > size - 0x20)
                {
                    break;
                }
                attr = read_be_u32(rdata + article);
                if (attr == 0 || attr + 0x84 > size - 0x20 ||
                    archive_has_reloc(&archive, (unsigned char*) cdata + attr))
                {
                    break;
                }
                f4 = read_be_f32(rdata + attr + 0x04);
                sc = read_be_f32(rdata + attr + 0x60);
                if (!(f4 > 0.01f && f4 < 1000.0f) ||
                    !(sc > 0.01f && sc < 1000.0f))
                {
                    break;
                }
                states = read_be_u32(rdata + article + 0x0C);
                if (states != 0 && states >= article) {
                    break;
                }
                for (w = 0x04; w <= 0x80; w += 4) {
                    uint32_t host = read_host_u32(cdata + attr + w);
                    uint32_t want = read_be_u32(rdata + attr + w);
                    if (host != want) {
                        if (failed == 0) {
                            fprintf(stderr,
                                    "decomp_assets: %s x48[%d] attr+%x=%u "
                                    "want=%u (not converted?)\n",
                                    path, k, w, host, want);
                        }
                        failed++;
                    }
                }
                articles++;
            }
            x48_articles = (unsigned) articles;
            if (articles != 0) {
                checked++;
            }
        }
    }
    if (failed == 0) {
        if (checked == 0) {
            printf("decomp_assets: %s %s tables=none\n", path, symbol);
        } else {
            printf("decomp_assets: %s %s x40/x4C/x48(%u) ok\n", path, symbol,
                   x48_articles);
        }
    } else {
        fprintf(stderr, "decomp_assets: %s %s %d ftData field mismatches\n",
                path, symbol, failed);
    }
    free(raw);
    free(buffer);
    return failed != 0;
}

/* P-746: Brinstar Depths' `yakumono_param` drives how often the stage rotates
 * and how long Kraid waits between roars.  Left big-endian, `map_time_min`
 * reads 0x96000000 rather than 150.  Pin the actual values, the way
 * `check_castle_param` and `check_pstadium_param` do, so a layout that merely
 * "looks converted" still fails. */
/* P-753: ItCo.usd holds material-animation trees that no descriptor walk
 * reaches -- their owning Article is named by nothing in the archive -- so
 * they stayed big-endian and HSD_TObjAddAnim read n_tluttbl as 768 instead of
 * 3, walking 765 entries past the end of the table.  The relocation-target
 * scan finds them by shape; assert it still finds them, because the failure
 * mode is silence: with the scan removed the file converts "successfully" and
 * the crash only appears in a match where those animations are played. */

/* P-754: Fighter_804D6540 (PlCo.dat pData[5]) is indexed by fighter kind, and
 * has one slot past the fighter kinds at Ft_Kind_None (33).  It is not
 * padding -- a fighter animating another fighter's tree (Kirby with a copy
 * ability) reaches ftAnim_8006FCE4 with that kind and indexes it -- so its
 * count has to be converted like every other slot's.  Walking only 33 entries
 * left it big-endian and `for (i = 0; i < temp_r3->x4; i++)` ran 16,777,216
 * times off the end of memory.  Assert the count is sane, since the failure
 * is invisible until a match plays that animation. */
int check_hidden_parts_none_slot(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "PlCo.dat", NULL, &size, error,
                                         sizeof(error));
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* ft_data;
    unsigned char* table;
    unsigned char* entry;
    uint32_t count;
    int failed = 0;

    if (buffer == NULL) {
        printf("decomp_assets: PlCo.dat SKIP (%s)\n", error);
        return 0;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: PlCo.dat conversion failed\n");
        free(buffer);
        return 1;
    }
    ft_data = HSD_ArchiveGetPublicAddress(&archive, "ftLoadCommonData");
    table = ft_data != NULL ? read_host_ptr(ft_data + 5 * 4) : NULL;
    entry = table != NULL && ptr_in_buffer(table, buffer, size)
                ? read_host_ptr(table + 33 * 4)
                : NULL;
    if (entry == NULL || !ptr_in_buffer(entry, buffer, size)) {
        fprintf(stderr,
                "decomp_assets: PlCo.dat Fighter_804D6540[Ft_Kind_None] "
                "missing\n");
        free(buffer);
        return 1;
    }
    count = read_host_u32(entry + 0x04);
    if (count == 0 || count > 64) {
        fprintf(stderr,
                "decomp_assets: PlCo.dat Fighter_804D6540[Ft_Kind_None] count "
                "= %u (unconverted? P-754)\n",
                (unsigned) count);
        failed = 1;
    } else {
        printf("decomp_assets: PlCo.dat hidden-parts none-slot count = %u\n",
               (unsigned) count);
    }
    free(buffer);
    return failed;
}

/* P-654: ItemAttr's two flag bytes are MSB-first on the console.  Retail
 * `itIsHeavy` is `lbz` + `extrwi r0,r0,1,24` (bit 0x80), `it_8026B30C` is
 * `extrwi r3,r3,4,25` (bits 0x78) and `itGetHoldKind` is `clrlwi r3,r3,29`
 * (bits 0x07); byte 1 is x1_1=0xB0, x1_3=0x20, x1_4=0x10, x1_5=0x08,
 * x1_67_cam_kind=0x06, x1_8=0x01.  GCC allocates bitfields LSB-first, so
 * without the PORT_PC ordering in `melee/it/types.h` every compiled read
 * below sees the wrong bits and item behavior (heavy/hold/camera flags) is
 * wrong.  Compare the compiled struct against the raw article bytes. */
int check_item_attr_bits(unsigned char** articles, unsigned count)
{
    unsigned i;
    int failed = 0;
    static const char* const names[] = {
        "x0_is_heavy", "x0_78", "x0_hold_kind", "x1_1", "x1_3",
        "x1_4",         "x1_5",  "x1_67_cam_kind", "x1_8",
    };

    if (sizeof(ItemAttr) != 0x84) {
        fprintf(stderr, "decomp_assets: ItemAttr size=%zu (want 0x84)\n",
                sizeof(ItemAttr));
        return 1;
    }
    for (i = 0; i < count; i++) {
        unsigned char* article = articles[i];
        unsigned char* attr;
        ItemAttr* a;
        const u8* b;
        u32 got[9];
        u32 want[9];
        unsigned k;

        if (article == NULL) {
            continue;
        }
        attr = read_host_ptr(article + 0x00);
        if (attr == NULL) {
            continue;
        }
        a = (ItemAttr*) attr;
        b = (const u8*) attr;
        got[0] = a->x0_is_heavy;
        got[1] = a->x0_78;
        got[2] = a->x0_hold_kind;
        got[3] = a->x1_1;
        got[4] = a->x1_3;
        got[5] = a->x1_4;
        got[6] = a->x1_5;
        got[7] = a->x1_67_cam_kind;
        got[8] = a->x1_8;
        want[0] = (b[0] >> 7) & 1;
        want[1] = (b[0] >> 3) & 0xF;
        want[2] = b[0] & 7;
        want[3] = (b[1] >> 6) & 3;
        want[4] = (b[1] >> 5) & 1;
        want[5] = (b[1] >> 4) & 1;
        want[6] = (b[1] >> 3) & 1;
        want[7] = (b[1] >> 1) & 3;
        want[8] = b[1] & 1;
        for (k = 0; k < 9; k++) {
            if (got[k] == want[k]) {
                continue;
            }
            if (failed == 0) {
                fprintf(stderr,
                        "decomp_assets: item attr kind %u byte0=%02x "
                        "byte1=%02x: %s=%u want=%u\n",
                        i, b[0], b[1], names[k], got[k], want[k]);
            }
            failed++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: item attr bits ok (" "%u" " articles)\n",
               count);
    } else {
        fprintf(stderr, "decomp_assets: %d item attr bit mismatches\n",
                failed);
    }
    return failed != 0;
}

/* GmStRoll.dat `ScGamRegStaffrollNames_scene_modelset` is a
 * `DynamicModelDesc**` of ten credits name models (gmstaffroll.c:84);
 * without the converter's `_modelset` walk the joint flags and each anim
 * joint's flags stay big-endian. */

/* PlCo.dat pData[8] (`Fighter_804D6534`) is the respawn-platform
 * {joint, anim} pair read by ft_0D4D.c:139/148; pData[16] is the entry/trophy
 * platform (ft_0C31.c:101) the converter already walks, so it is the control
 * that the mechanics work. */
int check_respawn_platform(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "PlCo.dat", NULL, &size, error,
                                         sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* ft_data;
    uint32_t ft_off;
    const unsigned char* rdata;
    unsigned char* cdata;
    uint32_t host_base;
    int failed = 0;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: PlCo.dat: %s\n", error);
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
        fprintf(stderr, "decomp_assets: PlCo.dat conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    if (check_reloc_integrity("PlCo.dat", raw, buffer, &archive)) {
        failed = 1;
    }
    ft_data = HSD_ArchiveGetPublicAddress(&archive, "ftLoadCommonData");
    if (ft_data == NULL || !ptr_in_buffer(ft_data, buffer, size)) {
        fprintf(stderr, "decomp_assets: PlCo.dat missing ftLoadCommonData\n");
        free(raw);
        free(buffer);
        return 1;
    }
    ft_off = (uint32_t) (ft_data - (buffer + 0x20));
    rdata = raw + 0x20;
    cdata = buffer + 0x20;
    host_base = (uint32_t) (uintptr_t) (buffer + 0x20);

    /* Control: pData[16] is a direct joint and must stay converted. */
    failed += check_converted_joint("PlCo.dat pData[16]", rdata, cdata,
                                    host_base, ft_off + 16 * 4,
                                    read_be_u32(rdata + ft_off + 16 * 4));

    {
        uint32_t pair_field = ft_off + 8 * 4;
        uint32_t raw_pair = read_be_u32(rdata + pair_field);
        uint32_t host_pair = read_host_u32(cdata + pair_field);
        uint32_t raw_anim;

        if (raw_pair == 0 || host_pair != raw_pair + host_base ||
            raw_pair + 8 > size - 0x20)
        {
            fprintf(stderr,
                    "decomp_assets: PlCo.dat pData[8] pair missing/misplaced "
                    "(host=%08x raw=%08x)\n",
                    host_pair, raw_pair);
            free(raw);
            free(buffer);
            return 1;
        }
        failed += check_converted_joint("PlCo.dat pData[8] joint", rdata,
                                        cdata, host_base, raw_pair,
                                        read_be_u32(rdata + raw_pair));

        raw_anim = read_be_u32(rdata + raw_pair + 4);
        if (raw_anim == 0 || raw_anim + 0x14 > size - 0x20 ||
            read_host_u32(cdata + raw_pair + 4) != raw_anim + host_base)
        {
            fprintf(stderr,
                    "decomp_assets: PlCo.dat pData[8] anim pointer invalid "
                    "(raw=%08x host=%08x)\n",
                    raw_anim, read_host_u32(cdata + raw_pair + 4));
            failed = 1;
        } else if (read_host_u32(cdata + raw_anim + 0x10) !=
                       read_be_u32(rdata + raw_anim + 0x10) ||
                   read_host_u32(cdata + raw_anim + 0x10) == 0)
        {
            fprintf(stderr,
                    "decomp_assets: PlCo.dat pData[8] anim not converted "
                    "(raw=%08x flags=%08x want %08x)\n",
                    raw_anim, read_host_u32(cdata + raw_anim + 0x10),
                    read_be_u32(rdata + raw_anim + 0x10));
            failed = 1;
        }
        if (failed == 0) {
            printf("decomp_assets: PlCo.dat respawn platform joint+anim ok\n");
        }
    }
    free(raw);
    free(buffer);
    return failed;
}

/* P-705: PlCo.dat pData[22] (`Fighter_804D64FC`) owns the CPU command
 * scripts and seven per-fighter attack-selection tables.  The scripts are
 * bytes, but each 0x24-byte attack entry and the two reach tables are numeric
 * data.  Leaving those words big-endian makes weights look like denormals,
 * so CPU fighters approach their target but never select an attack. */
int check_cpu_attack_tables(const char* image)
{
    static const uint32_t attack_fields[] = {
        0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C,
    };
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "PlCo.dat", NULL, &size,
                                         error, sizeof(error));
    unsigned char* raw = NULL;
    HSD_Archive archive;
    HsdConvertStats stats;
    unsigned char* ft_data;
    const unsigned char* rdata;
    unsigned char* cdata;
    uint32_t ft_off;
    uint32_t cpu_off;
    uint32_t host_base;
    unsigned lists = 0;
    unsigned entries = 0;
    int failed = 0;
    size_t fi;

    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: PlCo.dat: %s\n", error);
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
        fprintf(stderr, "decomp_assets: PlCo.dat CPU conversion failed\n");
        free(raw);
        free(buffer);
        return 1;
    }
    ft_data = HSD_ArchiveGetPublicAddress(&archive, "ftLoadCommonData");
    if (ft_data == NULL || !ptr_in_buffer(ft_data, buffer, size)) {
        fprintf(stderr, "decomp_assets: PlCo.dat missing ftLoadCommonData\n");
        free(raw);
        free(buffer);
        return 1;
    }
    rdata = raw + 0x20;
    cdata = buffer + 0x20;
    host_base = (uint32_t) (uintptr_t) cdata;
    ft_off = (uint32_t) (ft_data - cdata);
    cpu_off = read_be_u32(rdata + ft_off + 22 * 4);
    if (cpu_off == 0 || cpu_off + 0x28 > size - 0x20 ||
        read_host_u32(cdata + ft_off + 22 * 4) != cpu_off + host_base)
    {
        fprintf(stderr, "decomp_assets: PlCo.dat CPU root invalid\n");
        free(raw);
        free(buffer);
        return 1;
    }

    for (fi = 0; fi < sizeof(attack_fields) / sizeof(attack_fields[0]); fi++) {
        uint32_t field = attack_fields[fi];
        uint32_t table_off = read_be_u32(rdata + cpu_off + field);
        int kind;

        if (table_off == 0 || table_off + 33 * 4 > size - 0x20 ||
            read_host_u32(cdata + cpu_off + field) != table_off + host_base)
        {
            fprintf(stderr,
                    "decomp_assets: CPU attack table +%02x invalid\n",
                    field);
            failed = 1;
            continue;
        }
        for (kind = 0; kind < 33; kind++) {
            uint32_t slot = table_off + (uint32_t) kind * 4;
            uint32_t list_off = read_be_u32(rdata + slot);
            int i;

            /* Some per-kind pointer runs end before Ft_Kind_Max; the next
             * nonzero word belongs to adjacent numeric data.  Conversely, a
             * relocated zero is the valid data-base pointer used by Mario's
             * ground-attack list (G-023). */
            if (!archive_has_reloc(&archive, cdata + slot)) {
                if (list_off == 0) {
                    continue;
                }
                break;
            }
            if (list_off + 0x24 > size - 0x20 ||
                read_host_u32(cdata + slot) != list_off + host_base)
            {
                fprintf(stderr,
                        "decomp_assets: CPU table +%02x kind %d list invalid\n",
                        field, kind);
                failed = 1;
                continue;
            }
            lists++;
            for (i = 0; i < 256; i++) {
                uint32_t entry = list_off + (uint32_t) i * 0x24;
                uint32_t cmd;
                int word;

                if (entry + 0x24 > size - 0x20) {
                    failed = 1;
                    break;
                }
                cmd = read_be_u32(rdata + entry);
                for (word = 0; word < 9; word++) {
                    uint32_t at = entry + (uint32_t) word * 4;
                    uint32_t want = read_be_u32(rdata + at);
                    uint32_t got = read_host_u32(cdata + at);
                    if (got != want) {
                        if (!failed) {
                            fprintf(stderr,
                                    "decomp_assets: CPU entry +%02x kind %d "
                                    "word %d=%08x want=%08x\n",
                                    field, kind, word, got, want);
                        }
                        failed = 1;
                    }
                }
                if (cmd == 0) {
                    break;
                }
                entries++;
            }
            if (i == 256) {
                fprintf(stderr,
                        "decomp_assets: CPU table +%02x kind %d unterminated\n",
                        field, kind);
                failed = 1;
            }
        }
    }
    {
        static const struct {
            uint32_t field;
            unsigned count;
        } numeric_tables[] = {
            { 0x20, 33 },
            { 0x24, 6 },
        };
        size_t ti;
        for (ti = 0; ti < sizeof(numeric_tables) / sizeof(numeric_tables[0]);
             ti++)
        {
            uint32_t table = read_be_u32(rdata + cpu_off +
                                         numeric_tables[ti].field);
            unsigned i;
            for (i = 0; table != 0 && i < numeric_tables[ti].count; i++) {
                uint32_t at = table + i * 4;
                if (at + 4 > size - 0x20 ||
                    read_host_u32(cdata + at) != read_be_u32(rdata + at))
                {
                    if (!failed) {
                        fprintf(stderr,
                                "decomp_assets: CPU numeric table +%02x "
                                "entry %u is not host order\n",
                                numeric_tables[ti].field, i);
                    }
                    failed = 1;
                }
            }
        }
    }
    if (!failed) {
        printf("decomp_assets: PlCo.dat CPU attack tables %u lists/%u entries "
               "ok\n",
               lists, entries);
    }
    free(raw);
    free(buffer);
    return failed;
}

/* Every `PlKbCp*.dat` is a `KirbyHatStruct` in one of two layouts, and the
 * five that `LOAD_HAT` loads keep a second model in "`hat_dynamics[2]`"
 * (+0x14) that `ftKb_SpecialN_800EF438` hands to `HSD_DObjLoadDesc`.  Left
 * big-endian, `DObjLoad` falls through its three `case`s and calls
 * `HSD_Panic` the first time Kirby swallows that fighter (P-842, G-199).
 *
 * The layout is derived here rather than copied from the converter's table,
 * so the two are independent: a joint-first hat has a pointer at +0x00, a
 * parts-first hat has `model_num` there.  What is then checked is the panic's
 * own condition -- every `MObjDesc.rendermode` under every joint tree a hat
 * hands to the engine must land on one of `DObjLoad`'s three cases. */
static int check_hat_rendermodes(const char* path, unsigned char* joint,
                                 const unsigned char* buffer, size_t size,
                                 unsigned* mobjs, int depth)
{
    unsigned char* dobj;
    uint32_t flags;
    int failed = 0;

    if (joint == NULL || !ptr_in_buffer(joint, buffer, size) || depth > 64) {
        return 0;
    }
    flags = read_host_u32(joint + 0x04);
    /* HSD_JOBJ_PTCL | HSD_JOBJ_SPLINE: the +0x10 union is not a DObj then. */
    dobj = (flags & 0x60000000u) ? NULL : read_host_ptr(joint + 0x10);
    for (; dobj != NULL && ptr_in_buffer(dobj, buffer, size);
         dobj = read_host_ptr(dobj + 0x04))
    {
        unsigned char* mobj = read_host_ptr(dobj + 0x08);
        uint32_t rendermode;
        if (mobj == NULL || !ptr_in_buffer(mobj, buffer, size)) {
            continue;
        }
        rendermode = read_host_u32(mobj + 0x04);
        (*mobjs)++;
        if ((rendermode & 0x60000000u) == 0x20000000u) {
            fprintf(stderr,
                    "decomp_assets: %s rendermode 0x%08x would panic "
                    "DObjLoad\n",
                    path, rendermode);
            failed = 1;
        }
    }
    if (!(flags & 0x01000000u)) { /* HSD_JOBJ_INSTANCE */
        failed |= check_hat_rendermodes(path, read_host_ptr(joint + 0x08),
                                        buffer, size, mobjs, depth + 1);
    }
    failed |= check_hat_rendermodes(path, read_host_ptr(joint + 0x0C), buffer,
                                    size, mobjs, depth + 1);
    return failed;
}

/*
 * Every fighter's `ftData->x48_items` articles, checked the way P-842 checks
 * Kirby's hats: follow each article's model to its joint tree and assert every
 * `MObjDesc.rendermode` lands on one of `DObjLoad`'s cases.
 *
 * This is the check the Samus (P-815), Kirby, Yoshi, Sheik and Game & Watch
 * (P-765) bugs all needed and none of them had.  Each was found by a player
 * using a move, reported as a panic, and diagnosed one archive at a time,
 * because the `x48_items` walk stops at the first slot that fails its
 * heuristics and **a run that stops early looks exactly like a run that
 * finished** -- no statistic counts the articles it never reached (G-218).
 *
 * Relocation targets are already in host order for the whole archive, so the
 * pointer chain below is followable even through slots the converter's walk
 * never reached.  `rendermode` is not a pointer, so it is still big-endian in
 * exactly those slots, which is what makes it the detector.
 */
static int check_ft_articles_one(const char* path, unsigned char* ft_data,
                                 const unsigned char* buffer, size_t size,
                                 unsigned* articles, unsigned* mobjs)
{
    unsigned char* items = read_host_ptr(ft_data + 0x48);
    int failed = 0;
    int k;

    if (items == NULL || !ptr_in_buffer(items, buffer, size)) {
        return 0;
    }
    for (k = 0; k < 32; k++) {
        unsigned char* slot = items + (size_t) k * 4;
        unsigned char* article;
        unsigned char* model;
        unsigned char* joint;

        if (!ptr_in_buffer(slot, buffer, size)) {
            break;
        }
        article = read_host_ptr(slot);
        if (article == NULL) {
            continue; /* a legal hole: the table is indexed by item kind */
        }
        if (!ptr_in_buffer(article, buffer, size)) {
            break;
        }
        model = read_host_ptr(article + 0x10);
        if (model == NULL || !ptr_in_buffer(model, buffer, size)) {
            continue;
        }
        joint = read_host_ptr(model + 0x00);
        if (joint == NULL || !ptr_in_buffer(joint, buffer, size)) {
            continue;
        }
        (*articles)++;
        failed |= check_hat_rendermodes(path, joint, buffer, size, mobjs, 0);
    }
    return failed;
}

int check_fighter_articles(const char* image)
{
    DiscFileList list;
    char error[256];
    unsigned files = 0;
    unsigned articles = 0;
    unsigned mobjs = 0;
    size_t i;
    int failed = 0;

    if (disc_list(image, "Pl", ".dat", &list, error, sizeof(error)) != 0) {
        fprintf(stderr, "decomp_assets: Pl*.dat: %s\n", error);
        return 1;
    }
    for (i = 0; i < list.count; i++) {
        const char* path = list.names[i];
        size_t size = 0;
        unsigned char* buffer;
        HsdConvertStats stats;
        HSD_Archive archive;
        unsigned char* ft_data = NULL;
        int j;

        /* Kirby's copies carry `ftDataKirbyCopy` and are P-842's business. */
        if (strncmp(path, "PlKbCp", 6) == 0) {
            continue;
        }
        buffer = load_archive(image, path, NULL, &size, error, sizeof(error));
        if (buffer == NULL) {
            continue;
        }
        if (!hsd_asset_convert(buffer, size, &stats) ||
            HSD_ArchiveParse(&archive, buffer, size) != 0)
        {
            free(buffer);
            continue;
        }
        for (j = 0; (uint32_t) j < archive.header.nb_public && ft_data == NULL;
             j++)
        {
            const char* name = archive.symbols + archive.public_info[j].symbol;
            if (strncmp(name, "ftData", 6) == 0) {
                ft_data = HSD_ArchiveGetPublicAddress(&archive, name);
            }
        }
        if (ft_data != NULL) {
            files++;
            failed |= check_ft_articles_one(path, ft_data, buffer, size,
                                            &articles, &mobjs);
        }
        free(buffer);
    }
    disc_list_free(&list);
    if (files < 25) {
        fprintf(stderr, "decomp_assets: %u fighter data archives (want >=25)\n",
                files);
        failed = 1;
    }
    if (!failed) {
        printf("decomp_assets: fighter articles files=%u articles=%u "
               "mobjs=%u rendermodes ok\n",
               files, articles, mobjs);
    }
    return failed;
}

/*
 * The same check over `ItCo`'s three `Article*` tables -- 43 common, 118
 * character, 47 pokemon -- which is where the articles that are not a
 * fighter's own live.  The Ice Climbers' ice block is one of these
 * (`itclimbersice.c`, `It_Kind_IceClimber_Ice`), reached from Popo's neutral
 * special and from Kirby's copy of it.
 */
static int check_item_table(const char* path, unsigned char* table, int count,
                            const unsigned char* buffer, size_t size,
                            unsigned* articles, unsigned* mobjs)
{
    int failed = 0;
    int i;

    if (table == NULL || !ptr_in_buffer(table, buffer, size)) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        unsigned char* slot = table + (size_t) i * 4;
        unsigned char* article;
        unsigned char* model;
        unsigned char* joint;

        if (!ptr_in_buffer(slot, buffer, size)) {
            break;
        }
        article = read_host_ptr(slot);
        if (article == NULL || !ptr_in_buffer(article, buffer, size)) {
            continue;
        }
        model = read_host_ptr(article + 0x10);
        if (model == NULL || !ptr_in_buffer(model, buffer, size)) {
            continue;
        }
        joint = read_host_ptr(model + 0x00);
        if (joint == NULL || !ptr_in_buffer(joint, buffer, size)) {
            continue;
        }
        (*articles)++;
        failed |= check_hat_rendermodes(path, joint, buffer, size, mobjs, 0);
    }
    return failed;
}

int check_item_articles(const char* image)
{
    static const char* const names[] = { "ItCo.dat", "ItCo.usd" };
    char error[256];
    unsigned articles = 0;
    unsigned mobjs = 0;
    size_t n;
    int found = 0;
    int failed = 0;

    for (n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        size_t size = 0;
        unsigned char* buffer =
            load_archive(image, names[n], NULL, &size, error, sizeof(error));
        HsdConvertStats stats;
        HSD_Archive archive;
        unsigned char* pub;

        if (buffer == NULL) {
            continue; /* one of the two spellings is the one on this disc */
        }
        if (!hsd_asset_convert(buffer, size, &stats) ||
            HSD_ArchiveParse(&archive, buffer, size) != 0)
        {
            fprintf(stderr, "decomp_assets: %s conversion failed\n", names[n]);
            free(buffer);
            failed = 1;
            continue;
        }
        pub = HSD_ArchiveGetPublicAddress(&archive, "itPublicData");
        if (pub == NULL || !ptr_in_buffer(pub, buffer, size)) {
            free(buffer);
            continue;
        }
        found = 1;
        /* Counts are the item-kind enum spans (it/forward.h), the same three
         * the converter walks. */
        failed |= check_item_table(names[n], read_host_ptr(pub + 0x04), 43,
                                   buffer, size, &articles, &mobjs);
        failed |= check_item_table(names[n], read_host_ptr(pub + 0x08), 118,
                                   buffer, size, &articles, &mobjs);
        failed |= check_item_table(names[n], read_host_ptr(pub + 0x0C), 47,
                                   buffer, size, &articles, &mobjs);
        free(buffer);
    }
    if (!found) {
        fprintf(stderr, "decomp_assets: no ItCo archive with itPublicData\n");
        return 1;
    }
    if (!failed) {
        printf("decomp_assets: item articles=%u mobjs=%u rendermodes ok\n",
               articles, mobjs);
    }
    return failed;
}

/*
 * Every fighter's part-visibility lookups, checked for the one thing that
 * makes them fatal: a `count` left big-endian (P-873).
 *
 * `ftParts_80074D7C` reads each lookup's count and walks that many `TempS`
 * entries, each of which holds a `u8*` list of DObj indices.  A count read
 * from the wrong end is enormous -- big-endian 11 is 0x0B000000 -- so the walk
 * runs off the end of the list and dereferences a run of index bytes as a
 * pointer.  The owner's crash was `SIGSEGV at 0x15141312`, which is the bytes
 * 0x12 0x13 0x14 0x15: four consecutive DObj indices.
 *
 * The signature is unmistakable and that is what this tests.  A real count is
 * small; a small number read from the wrong end has its value in the **top**
 * byte and zeroes below it.  Nothing else in this data looks like that, so the
 * check is specific enough to assert on every archive.
 */
static int vis_count_is_byte_reversed(uint32_t count)
{
    return count > 64 && (count & 0x00FFFFFFu) == 0 &&
           (count >> 24) >= 1 && (count >> 24) <= 64;
}

int check_vis_lookups(const char* image)
{
    DiscFileList list;
    char error[256];
    unsigned files = 0;
    unsigned lookups = 0;
    unsigned entries = 0;
    size_t i;
    int failed = 0;

    if (disc_list(image, "Pl", ".dat", &list, error, sizeof(error)) != 0) {
        fprintf(stderr, "decomp_assets: Pl*.dat: %s\n", error);
        return 1;
    }
    for (i = 0; i < list.count; i++) {
        const char* path = list.names[i];
        size_t size = 0;
        unsigned char* buffer;
        HsdConvertStats stats;
        HSD_Archive archive;
        unsigned char* ft_data = NULL;
        unsigned char* parts;
        unsigned char* table;
        uint32_t model_num;
        int j;
        int costume;

        buffer = load_archive(image, path, NULL, &size, error, sizeof(error));
        if (buffer == NULL) {
            continue;
        }
        if (!hsd_asset_convert(buffer, size, &stats) ||
            HSD_ArchiveParse(&archive, buffer, size) != 0)
        {
            free(buffer);
            continue;
        }
        for (j = 0; (uint32_t) j < archive.header.nb_public && ft_data == NULL;
             j++)
        {
            const char* name = archive.symbols + archive.public_info[j].symbol;
            if (strncmp(name, "ftData", 6) == 0) {
                ft_data = HSD_ArchiveGetPublicAddress(&archive, name);
            }
        }
        if (ft_data == NULL) {
            free(buffer);
            continue;
        }
        /* ftData->x8 is the FtPartsDesc: { model_num, vis_table, ... }. */
        parts = read_host_ptr(ft_data + 0x08);
        if (parts == NULL || !ptr_in_buffer(parts, buffer, size)) {
            free(buffer);
            continue;
        }
        model_num = read_host_u32(parts + 0x00);
        table = read_host_ptr(parts + 0x04);
        if (model_num == 0 || model_num > 12 || table == NULL ||
            !ptr_in_buffer(table, buffer, size))
        {
            free(buffer);
            continue;
        }
        files++;

        /* Eight costumes of four, the span the converter walks. */
        for (costume = 0; costume < 8; costume++) {
            int col;
            for (col = 0; col < 4; col++) {
                unsigned char* slot =
                    table + ((size_t) costume * 4 + (size_t) col) * 4;
                unsigned char* lookup;
                uint32_t m;
                if (!ptr_in_buffer(slot, buffer, size)) {
                    continue;
                }
                lookup = read_host_ptr(slot);
                if (lookup == NULL || !ptr_in_buffer(lookup, buffer, size)) {
                    continue;
                }
                lookups++;
                for (m = 0; m < model_num; m++) {
                    unsigned char* entry = lookup + (size_t) m * 8;
                    uint32_t count;
                    if (!ptr_in_buffer(entry, buffer, size)) {
                        break;
                    }
                    count = read_host_u32(entry + 0x00);
                    entries++;
                    if (vis_count_is_byte_reversed(count)) {
                        fprintf(stderr,
                                "decomp_assets: %s vis lookup model %u count "
                                "0x%08x is big-endian (%u) -- "
                                "ftParts_80074D7C would run off the list\n",
                                path, m, count, count >> 24);
                        failed = 1;
                    }
                }
            }
        }
        free(buffer);
    }
    disc_list_free(&list);
    if (files < 25) {
        fprintf(stderr, "decomp_assets: %u fighters with a vis table "
                        "(want >=25)\n",
                files);
        failed = 1;
    }
    if (!failed) {
        printf("decomp_assets: vis lookups files=%u lookups=%u entries=%u "
               "counts ok\n",
               files, lookups, entries);
    }
    return failed;
}

int check_kirby_hats(const char* image)
{
    DiscFileList list;
    char error[256];
    unsigned hats = 0;
    unsigned models = 0;
    unsigned mobjs = 0;
    unsigned hat_articles = 0;
    size_t i;
    int failed = 0;

    if (disc_list(image, "PlKbCp", ".dat", &list, error, sizeof(error)) != 0) {
        fprintf(stderr, "decomp_assets: PlKbCp*.dat: %s\n", error);
        return 1;
    }
    for (i = 0; i < list.count; i++) {
        const char* path = list.names[i];
        size_t size = 0;
        unsigned char* buffer = load_archive(image, path, NULL, &size, error,
                                             sizeof(error));
        HsdConvertStats stats;
        HSD_Archive archive;
        unsigned char* hat = NULL;
        unsigned char* joint;
        uint32_t model_num;
        int parts_first;
        int j;

        if (buffer == NULL) {
            fprintf(stderr, "decomp_assets: %s: %s\n", path, error);
            failed = 1;
            continue;
        }
        if (!hsd_asset_convert(buffer, size, &stats) ||
            HSD_ArchiveParse(&archive, buffer, size) != 0)
        {
            fprintf(stderr, "decomp_assets: %s conversion failed\n", path);
            free(buffer);
            failed = 1;
            continue;
        }
        for (j = 0; (uint32_t) j < archive.header.nb_public && hat == NULL;
             j++)
        {
            const char* name = archive.symbols + archive.public_info[j].symbol;
            if (strncmp(name, "ftDataKirbyCopy", 15) == 0) {
                hat = HSD_ArchiveGetPublicAddress(&archive, name);
            }
        }
        if (hat == NULL) {
            fprintf(stderr, "decomp_assets: %s has no ftDataKirbyCopy root\n",
                    path);
            free(buffer);
            failed = 1;
            continue;
        }
        hats++;
        parts_first = !ptr_in_buffer(read_host_ptr(hat + 0x00), buffer, size);
        model_num = read_host_u32(hat + (parts_first ? 0x00 : 0x04));
        /* ftParts_8007487C reports "fighter parts model num over!" above 11;
         * every hat on disc carries exactly one model. */
        if (model_num != 1) {
            fprintf(stderr, "decomp_assets: %s model_num=%u (want 1)\n", path,
                    model_num);
            failed = 1;
        }
        joint = read_host_ptr(hat + (parts_first ? 0x14 : 0x00));
        if (joint != NULL && ptr_in_buffer(joint, buffer, size)) {
            models++;
            failed |= check_hat_rendermodes(path, joint, buffer, size, &mobjs,
                                            0);
        }
        /*
         * The `hat_dynamics[0..6]` slots at +0x0C, which P-842 taught the
         * converter to walk and then checked nothing about.  The hat's own
         * joint tree was the only thing validated, so an article slot that
         * the walk reached but mis-walked looked exactly like one it got
         * right -- and every item Kirby's copied specials spawn hangs off
         * these.  The owner's `0x3c001060` panic came out of one.
         *
         * Read as articles: `Article.x10_modelDesc->x0_joint`, the chain
         * `item.c:578` uses.  A slot holding something else (a joint, anim
         * joint, dynamics) will not produce an in-buffer joint through two
         * more indirections, so it is skipped rather than misread.
         */
        for (j = 0; j < 7; j++) {
            unsigned char* slot = hat + 0x0C + (size_t) j * 4;
            unsigned char* article;
            unsigned char* model;
            unsigned char* ajoint;

            if (!ptr_in_buffer(slot, buffer, size)) {
                break;
            }
            article = read_host_ptr(slot);
            if (article == NULL || !ptr_in_buffer(article, buffer, size)) {
                continue;
            }
            model = read_host_ptr(article + 0x10);
            if (model == NULL || !ptr_in_buffer(model, buffer, size)) {
                continue;
            }
            ajoint = read_host_ptr(model + 0x00);
            if (ajoint == NULL || !ptr_in_buffer(ajoint, buffer, size)) {
                continue;
            }
            hat_articles++;
            if (getenv("MELEE_KBHAT_TRACE") != NULL) {
                fprintf(stderr,
                        "[kbhat] %s slot=%d article=+0x%lx model=+0x%lx "
                        "joint=+0x%lx\n",
                        path, j, (unsigned long) (article - buffer),
                        (unsigned long) (model - buffer),
                        (unsigned long) (ajoint - buffer));
            }
            failed |= check_hat_rendermodes(path, ajoint, buffer, size, &mobjs,
                                            0);
        }
        free(buffer);
    }
    disc_list_free(&list);
    if (hats != 25) {
        fprintf(stderr, "decomp_assets: %u Kirby copy archives (want 25)\n",
                hats);
        failed = 1;
    }
    if (!failed) {
        printf("decomp_assets: Kirby hats=%u models=%u articles=%u mobjs=%u "
               "rendermodes ok\n",
               hats, models, hat_articles, mobjs);
    }
    return failed;
}

/*
 * S3 asset pipeline sweep.
 *
 * Loads every Pl*Nr character archive, all common-item models, one stage and
 * the common MnSlChr/IfAll assets through the compiled HSD path:
 *
 *   1. disc.c reads the archive from the user's image,
 *   2. native/decomp/assets/hsd_convert.c converts it to host order and
 *      reports whether every relocation target was converted,
 *   3. the compiled HSD_ArchiveParse/HSD_JObjLoadJoint load and pose it.
 *
 * The `ok` statistic is the desync check: if a loader meets a pointer field
 * the converter did not know about, `reloc_valid != reloc_total` and the test
 * fails.  Asset-aware: exits 0 with a SKIP message when the disc is absent
 * (ADR-0005).
 */
#include "asset_common.h"

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"

/* P-743/G-180: `UnkFlagStruct` is written as a whole byte and read as
 * individual bN bits across the codebase.  MWCC allocates the first bitfield
 * at the MSB, so retail's `byte = 1` sets b7 -- `item.c:719` does exactly
 * that to `Item::xDAA_byte`, and `it_8026EECC` tests b7 before drawing.  With
 * GCC's LSB-first layout it set b0 instead, b7 stayed clear, and every item
 * model was invisible while its hitbox still worked.  This is cheap to assert
 * and it needs no disc image, so it runs even when the archive checks skip. */
int check_unk_flag_bit_order(void)
{
    UnkFlagStruct f;
    int failed = 0;

    f.byte = 1;
    if (f.b7 != 1 || f.b0 != 0) {
        fprintf(stderr,
                "decomp_assets: UnkFlagStruct byte=1 gives b0=%u b7=%u "
                "(want b0=0 b7=1; bitfields must be MSB-first as MWCC packs "
                "them)\n",
                (unsigned) f.b0, (unsigned) f.b7);
        failed = 1;
    }
    f.byte = 0x80;
    if (f.b0 != 1 || f.b7 != 0) {
        fprintf(stderr,
                "decomp_assets: UnkFlagStruct byte=0x80 gives b0=%u b7=%u "
                "(want b0=1 b7=0)\n",
                (unsigned) f.b0, (unsigned) f.b7);
        failed = 1;
    }
    /* A group that does NOT fill its storage unit is the case hand-reversing
     * the field order gets wrong: reversed without five bits of padding, b0
     * lands at 0x04 instead of 0x80.  grCorneria_GroundVars::xC4 is exactly
     * that shape, and `grcorneria.c:501` writes the whole byte (G-181). */
    {
        struct grCorneria_GroundVars g;
        memset(&g, 0, sizeof(g));
        g.xC4.flags.b0 = 1;
        if (g.xC4.value != 0x80) {
            fprintf(stderr,
                    "decomp_assets: grCorneria xC4.b0 -> 0x%02x (want 0x80; "
                    "a partial bit-field group needs PORT_BF_BE, not a "
                    "reversed field order)\n",
                    (unsigned) g.xC4.value);
            failed = 1;
        }
    }
    if (!failed) {
        printf("decomp_assets: UnkFlagStruct bit order MSB-first ok\n");
    }
    return failed;
}

/* Descriptor-walk coverage floor, disc-wide (P-756).  Measured 2026-09-16 at
 * converter v119: 173337/209261 = 82.83%, up from 82.60% when `visual*Scene`
 * joined the SceneDesc roots and the `Vi*` family stopped being raw, and from
 * 82.40% when the Kirby
 * copy archives stopped being walked as `ftData` (P-755), and from 79.50%
 * before that when
 * `conv_orphan_anim_trees` started finding `HSD_AnimJoint` trees that no
 * walker reaches, the animation counterpart of `conv_orphan_matanim_trees`.
 * Before that, 79.50% at v104 when `conv_ft_part_anim` began following
 * `ftData_x1C.x8` (P-758's head item), and 73.60% at v99 when conv_itemdata
 * started following each stage's Article (P-762).
 *
 * **Read this number with the caveat in P-758's row.** Coverage counts
 * descriptors reached, not bytes the game reads: roughly half of the v114
 * gain is the unreachable prefix of the `PlXx.dat` part-animation trees, and
 * a converter fix that repairs real data can move it not at all (v107) or
 * even downward (v106, which removed a wrong walk).
 *
 * Ratchet it upward as walkers land; never lower it to make a change pass. */
#define MELEE_COVERAGE_FLOOR 82.83

int check_converter_sweep(const char* image)
{
    char error[256];
    DiscFileList list;
    size_t i;
    size_t size = 0;
    unsigned archives = 0;
    unsigned loaded = 0;
    unsigned converted = 0;
    unsigned parsed = 0;
    int failed = 0;
    /* P-756: MELEE_COVERAGE_JSON=<path> writes per-file descriptor-walk
     * coverage.  The relocation table enumerates every object the game can
     * reach, so "targets a walker visited / targets that exist" is the exact
     * size of the conversion bug class.  See AI/DECISIONS.md ADR-0013. */
    const char* cov_path = getenv("MELEE_COVERAGE_JSON");
    FILE* cov = cov_path != NULL ? fopen(cov_path, "w") : NULL;
    unsigned long cov_targets = 0;
    unsigned long cov_walked = 0;
    unsigned long cov_roots = 0;
    unsigned long cov_unhandled = 0;
    unsigned long cov_struct = 0;
    unsigned long cov_struct_walked = 0;
    unsigned long cov_sroots = 0;
    unsigned long cov_sroots_unhandled = 0;
    int cov_first = 1;

    if (cov != NULL) {
        fprintf(cov, "[\n");
    }
    if (disc_list(image, NULL, NULL, &list, error, sizeof(error)) != DISC_OK) {
        fprintf(stderr, "decomp_assets: sweep list: %s\n", error);
        return 1;
    }
    for (i = 0; i < list.count; i++) {
        DiscFile file;
        unsigned char* buffer;
        unsigned char* raw;
        HSD_Archive archive;
        HsdConvertStats stats;
        int is_ef;

        if (disc_load(image, list.names[i], &file, error, sizeof(error)) !=
            DISC_OK)
        {
            continue;
        }
        loaded++;
        if (file.size < 0x20) {
            disc_free(&file);
            continue;
        }
        size = file.size;
        raw = malloc(size);
        buffer = malloc(size);
        if (raw == NULL || buffer == NULL) {
            free(raw);
            free(buffer);
            disc_free(&file);
            continue;
        }
        memcpy(raw, file.data, size);
        memcpy(buffer, file.data, size);
        disc_free(&file);
        /* MELEE_UNWALKED prints each unwalked descriptor to stderr from
         * inside the converter, which does not know the archive's name.
         * Mark the boundary here so the output is attributable. */
        if (getenv("MELEE_UNWALKED") != NULL) {
            fprintf(stderr, "[unwalked-file] %s\n", list.names[i]);
        }
        {
            int cv = hsd_asset_convert(buffer, size, &stats);
            if (!cv) {
                free(raw);
                free(buffer);
                continue;
            }
        }
        converted++;
        if (HSD_ArchiveParse(&archive, buffer, size) != 0) {
            free(raw);
            free(buffer);
            continue;
        }
        parsed++;
        archives++;
        if (cov != NULL) {
            fprintf(cov,
                    "%s  {\"file\":\"%s\",\"bytes\":%u,\"targets\":%u,"
                    "\"walked\":%u,\"roots\":%u,\"unhandled\":%u,"
                    "\"orphans\":%u,\"structs\":%u,\"structs_walked\":%u,"
                    "\"sroots\":%u,\"sroots_unhandled\":%u}",
                    cov_first ? "" : ",\n", list.names[i], stats.data_size,
                    stats.reloc_targets, stats.reloc_targets_walked,
                    stats.roots_total, stats.roots_unhandled,
                    stats.orphan_matanims, stats.struct_targets,
                    stats.struct_targets_walked, stats.roots_struct,
                    stats.roots_struct_unhandled);
            cov_first = 0;
        }
        {
            cov_targets += stats.reloc_targets;
            cov_walked += stats.reloc_targets_walked;
            cov_roots += stats.roots_total;
            cov_unhandled += stats.roots_unhandled;
            cov_struct += stats.struct_targets;
            cov_struct_walked += stats.struct_targets_walked;
            cov_sroots += stats.roots_struct;
            cov_sroots_unhandled += stats.roots_struct_unhandled;
        }
        if (check_reloc_integrity(list.names[i], raw, buffer, &archive)) {
            failed++;
        }

        is_ef = strncmp(list.names[i], "Ef", 2) == 0 &&
                strstr(list.names[i], "Data") != NULL;
        if (is_ef && archive.header.nb_public != 0) {
            unsigned expect_descs = 0;
            const char* sym = archive.symbols + archive.public_info[0].symbol;
            unsigned char* table = HSD_ArchiveGetPublicAddress(&archive, sym);
            if (table != NULL && ptr_in_buffer(table, buffer, size) &&
                table + 0x20 <= buffer + size)
            {
                unsigned char* descs = table + 8;
                uint32_t base =
                    (uint32_t) (descs - (buffer + 0x20));
                uint32_t arch_base = (uint32_t) (uintptr_t) (buffer + 0x20);
                uint32_t end = (uint32_t) (size - 0x20);
                uint32_t cmd = read_host_u32(table);
                uint32_t tex = read_host_u32(table + 4);
                unsigned k;
                if (cmd >= arch_base && cmd < arch_base + size) {
                    cmd -= arch_base;
                }
                if (tex >= arch_base && tex < arch_base + size) {
                    tex -= arch_base;
                }
                /* The descriptor array ends at the first bank blob when the
                 * effect has particle banks (the same bound the converter
                 * uses), otherwise at the first non-relocated model slot. */
                if (cmd > 8 && cmd < end) {
                    end = cmd;
                }
                if (tex > 8 && tex < end) {
                    end = tex;
                }
                for (k = 0; k < 1024; k++) {
                    uint32_t e = base + k * 0x14;
                    unsigned f;
                    int any_reloc = 0;
                    if ((size_t) e + 0x14 > end) {
                        break;
                    }
                    for (f = 0; f < 4; f++) {
                        if (archive_has_reloc(
                                &archive,
                                (unsigned char*) buffer + 0x20 + e + 4 +
                                    f * 4))
                        {
                            any_reloc = 1;
                        }
                    }
                    if (any_reloc) {
                        expect_descs++;
                    }
                    if (!any_reloc) {
                        /* The descriptor run ends here.  The lifetime word of
                         * this first non-descriptor slot is what the old
                         * 1024-entry walk overwrote; later words can belong
                         * to model trees reached from the real descriptors. */
                        if ((size_t) e + 4 <= end &&
                            read_host_u32(buffer + 0x20 + e) !=
                                read_be_u32(raw + 0x20 + e))
                        {
                            if (failed == 0) {
                                fprintf(stderr,
                                        "decomp_assets: %s effect desc "
                                        "tail[%u] converted\n",
                                        list.names[i], k);
                            }
                            failed++;
                        }
                        break;
                    }
                }
                if (stats.effect_descs != expect_descs) {
                    if (failed == 0) {
                        fprintf(stderr,
                                "decomp_assets: %s effect descs=%u want=%u\n",
                                list.names[i], stats.effect_descs,
                                expect_descs);
                    }
                    failed++;
                }
            }
        }
        free(raw);
        free(buffer);
    }
    disc_list_free(&list);
    if (cov != NULL) {
        fprintf(cov, "\n]\n");
        fclose(cov);
    }
    {
        printf("decomp_assets: coverage archives=%u targets=%lu walked=%lu "
               "(%.2f%%) roots=%lu unhandled=%lu\n",
               archives, cov_targets, cov_walked,
               cov_targets != 0 ? 100.0 * (double) cov_walked /
                                      (double) cov_targets
                                : 0.0,
               cov_roots, cov_unhandled);
        printf("decomp_assets: coverage descriptors=%lu walked=%lu (%.2f%%) "
               "struct-roots=%lu unhandled=%lu\n",
               cov_struct, cov_struct_walked,
               cov_struct != 0
                   ? 100.0 * (double) cov_struct_walked / (double) cov_struct
                   : 0.0,
               cov_sroots, cov_sroots_unhandled);
    }
    if (cov_struct != 0) {
        /* P-756: a ratchet.  Descriptor coverage is the measurable size of
         * the conversion bug class, so it may go up and must never go down:
         * a walker deleted or a root rule broken shows up here instead of in
         * a player's crash log.  Raise the floor when it climbs. */
        double pct =
            100.0 * (double) cov_struct_walked / (double) cov_struct;
        if (pct + 0.05 < MELEE_COVERAGE_FLOOR) {
            fprintf(stderr,
                    "decomp_assets: descriptor coverage %.2f%% below floor "
                    "%.2f%% (P-756)\n",
                    pct, MELEE_COVERAGE_FLOOR);
            failed++;
        }
    }
    if (failed == 0) {
        printf("decomp_assets: converter sweep archives=%u (loaded=%u converted=%u parsed=%u) ok\n",
               archives, loaded, converted, parsed);
    }
    return failed != 0 ? 1 : 0;
}

int main(int argc, char** argv)
{
    const char* image = argc > 1 ? argv[1] : DEFAULT_DISC;
    char error[256];
    DiscFileList list;
    size_t i;
    int models = 0;
    int failures = 0;
    int cache_failures = 0;
    void* arena = malloc(16 * 1024 * 1024);

    if (arena == NULL) {
        fprintf(stderr, "decomp_assets: out of memory\n");
        return 1;
    }
    HSD_ObjSetHeap(16 * 1024 * 1024, arena);
    HSD_ListInitAllocData();
    HSD_AObjInitAllocData();
    HSD_VecInitAllocData();
    HSD_MtxInitAllocData();
    HSD_RObjInitAllocData();
    HSD_IDInitAllocData();
    JObjInfoInit();

    if (disc_list(image, "Pl", "Nr.dat", &list, error, sizeof(error)) !=
        DISC_OK) {
        printf("decomp_assets: SKIP (%s: %s)\n", image, error);
        return 0;
    }
    printf("decomp_assets: %u Pl*Nr.dat archives\n", (unsigned) list.count);
    for (i = 0; i < list.count; i++) {
        ModelResult result;
        if (check_archive(image, list.names[i], &result, 1) != 0) {
            failures++;
        } else {
            models++;
            printf("decomp_assets: %-14s joints=%-3u pub=%-2u reloc=%u/%u "
                   "ok\n",
                   result.name, result.joints, result.stats.public_symbols,
                   result.stats.reloc_valid, result.stats.reloc_total);
        }
        cache_failures += cache_check(image, list.names[i]);
    }
    disc_list_free(&list);
    if (models < 26) {
        fprintf(stderr, "decomp_assets: only %d/26 character archives "
                        "loaded\n",
                models);
        failures++;
    }
    failures += check_link_dynamics(image);
    failures += check_respawn_platform(image);
    failures += check_cpu_attack_tables(image);
    failures += check_ft_part_anims(image, "PlMr.dat", "ftDataMario");
    failures += check_ft_part_anims(image, "PlLk.dat", "ftDataLink");
    failures += check_ft_part_anims(image, "PlFx.dat", "ftDataFox");
    failures += check_ft_part_anims(image, "PlPk.dat", "ftDataPikachu");
    failures += check_ft_data_tables(image, "PlMr.dat", "ftDataMario");
    failures += check_ft_data_tables(image, "PlNs.dat", "ftDataNess");
    failures += check_ft_data_tables(image, "PlGw.dat", "ftDataGamewatch");
    failures += check_ft_data_tables(image, "PlPe.dat", "ftDataPeach");
    failures += check_ft_data_tables(image, "PlFx.dat", "ftDataFox");
    failures += check_ft_data_tables(image, "PlDr.dat", "ftDataDrmario");
    failures += check_ft_data_tables(image, "PlFc.dat", "ftDataFalco");
    failures += check_ft_data_tables(image, "PlKb.dat", "ftDataKirby");
    failures += check_kirby_hats(image);
    failures += check_fighter_articles(image);
    failures += check_item_articles(image);
    failures += check_vis_lookups(image);
    failures += check_ft_data_tables(image, "PlLk.dat", "ftDataLink");
    failures += check_ft_data_tables(image, "PlCl.dat", "ftDataClink");
    failures += check_ft_data_tables(image, "PlYs.dat", "ftDataYoshi");
    failures += check_item_models(image);
    failures += check_ty_data_tables(image);
    failures += check_ty_datai_tables(image);
    failures += check_ty_sobj_backgrounds(image);
    failures += check_staffroll_modelset(image);
    failures += check_ifall_hud_modelsets(image);
    failures += check_kumite_tables(image);
    failures += check_yorster_param(image);
    failures += check_stage_params(image);
    failures += check_stage_display_lists(image);
    failures += check_castle_dynamics(image);
    failures += check_castle_param(image);
    failures += check_pstadium_param(image, "GrPs.dat");
    failures += check_pstadium_param(image, "GrPs3.dat");
    failures += check_unk_flag_bit_order();
    failures += check_kraid_param(image);
    failures += check_orphan_matanims(image);
    failures += check_hidden_parts_none_slot(image);
    failures += check_scene_root(image, "GmRgStnd.dat", "standScene");
    failures += check_scene_root(image, "GmRegEnd.dat", "cut1CanimScene");
    failures += check_intro_easy(image);
    failures += check_event_levels(image);
    failures += check_converter_sweep(image);
    failures += check_stage_matanims(image, "GrNBa.dat");
    failures += check_stage_matanims(image, "GrNLa.dat");
    /* P-762: the stage Articles `itemdata` names, and the model trees behind
     * them.  GrGb is where the crash was; the other three are stages with
     * their own articles, so the walk is checked on more than its one case. */
    failures += check_stage_item_articles(image, "GrGb.dat", 1);
    failures += check_stage_item_articles(image, "GrNBa.dat", 0);
    failures += check_stage_item_articles(image, "GrIz.dat", 0);
    failures += check_stage_item_articles(image, "GrSh.dat", 0);

    /* One stage and the common archives. */
    {
        static const struct {
            const char* path;
            int require_public;
        } common[] = {
            { "GrNBa.dat", 0 },
            { "MnSlChr.dat", 1 },
            { "IfAll.dat", 1 },
            { "NtMsgWin.dat", 1 },
        };
        size_t c;
        for (c = 0; c < sizeof(common) / sizeof(common[0]); c++) {
            ModelResult result;
            if (check_archive(image, common[c].path, &result,
                              common[c].require_public) != 0) {
                failures++;
            } else {
                printf("decomp_assets: %-14s public=%-3u reloc=%u/%u ok\n",
                       common[c].path, result.stats.public_symbols,
                       result.stats.reloc_valid, result.stats.reloc_total);
            }
        }
    }

    if (cache_failures != 0) {
        failures++;
    }
    printf("decomp_assets: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures != 0;
}

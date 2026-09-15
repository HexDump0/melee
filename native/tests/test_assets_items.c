/*
 * Asset checks for item archives (ItCo): article attributes, models, orphaned animation trees.
 *
 * Split out of test_decomp_assets.c; see asset_common.h.
 */
#include "asset_common.h"

int check_orphan_matanims(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "ItCo.usd", NULL, &size, error,
                                         sizeof(error));
    HsdConvertStats stats;
    int failed = 0;

    if (buffer == NULL) {
        printf("decomp_assets: ItCo.usd SKIP (%s)\n", error);
        return 0;
    }
    if (!hsd_asset_convert(buffer, size, &stats)) {
        fprintf(stderr, "decomp_assets: ItCo.usd conversion failed\n");
        free(buffer);
        return 1;
    }
    if (stats.orphan_matanims == 0) {
        fprintf(stderr,
                "decomp_assets: ItCo.usd reached no orphan matanim trees "
                "(expected at least one; P-753)\n");
        failed = 1;
    } else {
        printf("decomp_assets: ItCo.usd orphan matanim trees = %u\n",
               stats.orphan_matanims);
    }
    free(buffer);
    return failed;
}

/* HSD_JObjLoadJoint resolves every PObj joint ID against the descriptors in
 * the root it just loaded.  Exercise all common-item model roots so an item
 * whose PObj points outside that root is caught before a random match spawn. */
int check_item_models(const char* image)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, "ItCo.usd", NULL, &size,
                                         error, sizeof(error));
    HsdConvertStats stats;
    HSD_Archive archive;
    unsigned char* public_data;
    unsigned char** articles;
    unsigned checked = 0;
    unsigned loaded = 0;
    unsigned i;
    int attr_failed = 0;

    if (buffer == NULL) {
        buffer = load_archive(image, "ItCo.dat", NULL, &size, error,
                              sizeof(error));
    }
    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: item models: %s\n", error);
        return 1;
    }
    if (!hsd_asset_convert(buffer, size, &stats) ||
        HSD_ArchiveParse(&archive, buffer, size) != 0)
    {
        fprintf(stderr, "decomp_assets: item model conversion failed\n");
        free(buffer);
        return 1;
    }
    for (i = 0; i < archive.header.nb_extern; i++) {
        const char* symbol = HSD_ArchiveGetExtern(&archive, (int) i);
        if (symbol != NULL) {
            HSD_ArchiveLocateExtern(&archive, symbol, NULL);
        }
    }
    public_data = HSD_ArchiveGetPublicAddress(&archive, "itPublicData");
    articles = public_data != NULL ? read_host_ptr(public_data + 0x04) : NULL;
    if (articles == NULL || !ptr_in_buffer(articles, buffer, size)) {
        fprintf(stderr, "decomp_assets: common item article table missing\n");
        free(buffer);
        return 1;
    }
    attr_failed = check_item_attr_bits(articles, 43);
    for (i = 0; i < 43; i++) {
        unsigned char* article = articles[i];
        unsigned char* model;
        HSD_Joint* root;
        HSD_JObj* jobj;

        if (article == NULL) {
            continue;
        }
        if (!ptr_in_buffer(article, buffer, size)) {
            fprintf(stderr,
                    "decomp_assets: item kind %u article pointer outside "
                    "archive\n",
                    i);
            free(buffer);
            return 1;
        }
        model = read_host_ptr(article + 0x10);
        if (model != NULL && !ptr_in_buffer(model, buffer, size)) {
            fprintf(stderr,
                    "decomp_assets: item kind %u article=%td model=%p "
                    "outside archive\n",
                    i, article - buffer, (void*) model);
            free(buffer);
            return 1;
        }
        root = model != NULL ? read_host_ptr(model) : NULL;
        if (root == NULL) {
            continue;
        }
        checked++;
        jobj = HSD_JObjLoadJoint(root);
        if (jobj == NULL) {
            fprintf(stderr,
                    "decomp_assets: item kind %u model failed to load\n", i);
            free(buffer);
            return 1;
        }
        HSD_JObjUnrefThis(jobj);
        loaded++;
    }
    printf("decomp_assets: item models=%u loaded=%u\n", checked, loaded);
    free(buffer);
    return (checked == loaded && attr_failed == 0) ? 0 : 1;
}

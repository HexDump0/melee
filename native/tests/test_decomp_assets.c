/*
 * S3 asset pipeline sweep.
 *
 * Loads every Pl*Nr character archive (plus one stage and the common
 * MnSlChr/IfAll assets) through the compiled HSD path:
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
#include "decomp/assets/hsd_convert.h"
#include "platform/disc.h"

#include <dolphin/os.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/robj.h>

void JObjInfoInit(void);

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"
#define MAX_JOINT_NODES 8192

/* ------------------------------------------------------------------ shims */

void OSReport(char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void __assert(char* file, u32 line, char* msg)
{
    fprintf(stderr, "assert %s:%u: %s\n", file, (unsigned) line, msg);
    abort();
}

OSHeapHandle HSD_GetHeap(void) { return 1; }
void HSD_SetHeap(OSHeapHandle handle) { (void) handle; }
void* OSAllocFromHeap(int heap, unsigned long size)
{
    (void) heap;
    return malloc(size);
}
void OSFreeToHeap(int heap, void* ptr)
{
    (void) heap;
    free(ptr);
}

typedef struct {
    char name[64];
    char root[128];
    unsigned joints;
    unsigned hidden;
    HsdConvertStats stats;
    int ok;
} ModelResult;

static unsigned char* load_archive(const char* image, const char* path,
                                   const char* root_hint, size_t* size,
                                   char* error, size_t error_size)
{
    DiscFile file;
    unsigned char* buffer;

    if (disc_load(image, path, &file, error, error_size) != DISC_OK) {
        return NULL;
    }
    buffer = malloc(file.size);
    if (buffer == NULL) {
        disc_free(&file);
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    memcpy(buffer, file.data, file.size);
    *size = file.size;
    disc_free(&file);
    (void) root_hint;
    return buffer;
}

static void* read_host_ptr(const unsigned char* p)
{
    void* value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint32_t read_host_u32(const unsigned char* p)
{
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static float read_host_f32(const unsigned char* p)
{
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

/* P-631: ftDataLink's cap chain copies packed 0x3C-byte solver parameters
 * through lb_80011710.  The relocation table covers the pointer, but these
 * numeric pointees also have to be converted. */
static int check_link_dynamics(const char* image)
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

/* Poses the joint tree and returns the number of HSD_JObj nodes. */
static unsigned pose_tree(HSD_JObj* root, float* out_min, float* out_max)
{
    HSD_JObj* stack[MAX_JOINT_NODES];
    unsigned count = 0;
    int sp = 0;
    int i;

    for (i = 0; i < 3; i++) {
        out_min[i] = 1e30f;
        out_max[i] = -1e30f;
    }
    if (root == NULL) {
        return 0;
    }
    stack[sp++] = root;
    while (sp > 0 && count < MAX_JOINT_NODES) {
        HSD_JObj* j = stack[--sp];
        if (j == NULL) {
            continue;
        }
        count++;
        for (i = 0; i < 3; i++) {
            float v = j->mtx[i][3];
            if (v < out_min[i]) out_min[i] = v;
            if (v > out_max[i]) out_max[i] = v;
        }
        if (j->child != NULL) {
            stack[sp++] = j->child;
        }
        if (j->next != NULL) {
            stack[sp++] = j->next;
        }
    }
    return count;
}

static int check_archive(const char* image, const char* path,
                         ModelResult* result, int require_public)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, path, NULL, &size, error,
                                         sizeof(error));
    HSD_Archive archive;
    char* root_name = NULL;
    HSD_Joint* joint;
    HSD_JObj* root;
    size_t i;
    float mn[3];
    float mx[3];
    int failures = 0;

    memset(result, 0, sizeof(*result));
    snprintf(result->name, sizeof(result->name), "%s", path);
    if (buffer == NULL) {
        fprintf(stderr, "decomp_assets: %s: %s\n", path, error);
        return -1;
    }
    if (size < 0x20 ||
        hsd_asset_convert(buffer, size, &result->stats) == 0) {
        fprintf(stderr, "decomp_assets: %s: not an HSD archive\n", path);
        free(buffer);
        return -1;
    }
    if (!result->stats.ok) {
        fprintf(stderr, "decomp_assets: %s: reloc %u/%u (desync)\n", path,
                result->stats.reloc_valid, result->stats.reloc_total);
        failures++;
    }
    if (HSD_ArchiveParse(&archive, buffer, size) != 0) {
        fprintf(stderr, "decomp_assets: %s: parse failed\n", path);
        free(buffer);
        return -1;
    }

    /* Root joint (if any): load, pose and count. */
    for (i = 0; i < archive.header.nb_public; ++i) {
        const char* sym = archive.symbols + archive.public_info[i].symbol;
        if (strstr(sym, "_joint") != NULL &&
            strstr(sym, "matanim") == NULL &&
            strstr(sym, "animjoint") == NULL) {
            root_name = (char*) sym;
            break;
        }
    }
    if (root_name == NULL && require_public) {
        /* Scene/common archives have no joint root; require at least one
         * resolvable public symbol and a clean conversion instead. */
        if (result->stats.public_symbols == 0) {
            fprintf(stderr, "decomp_assets: %s: no public symbols\n", path);
            failures++;
        }
    }
    if (root_name != NULL) {
        joint = HSD_ArchiveGetPublicAddress(&archive, root_name);
        if (joint == NULL) {
            failures++;
        } else {
            snprintf(result->root, sizeof(result->root), "%s", root_name);
            root = HSD_JObjLoadJoint(joint);
            if (root == NULL) {
                failures++;
            } else {
                HSD_JObjSetupMatrix(root);
                result->joints = pose_tree(root, mn, mx);
                if (!(mx[0] >= mn[0]) || !(mx[1] >= mn[1]) ||
                    !(mx[2] >= mn[2])) {
                    failures++;
                }
                HSD_JObjUnrefThis(root);
            }
        }
    }

    result->ok = failures == 0;
    free(buffer);
    return failures;
}

static int cache_check(const char* image, const char* path)
{
    char error[256];
    char canonical[512];
    const char* cache_dir = getenv("MELEE_ASSET_CACHE");
    const char* disable = getenv("MELEE_NO_ASSET_CACHE");
    size_t size = 0;
    unsigned char* first;
    unsigned char* second;
    HsdConvertStats first_stats;
    HsdConvertStats second_stats;
    int same = 1;

    if (cache_dir == NULL || disable != NULL) {
        return 0; /* cache disabled by the environment: nothing to verify */
    }
    first = load_archive(image, path, NULL, &size, error, sizeof(error));
    if (first == NULL) {
        return 0;
    }
    second = malloc(size);
    if (second == NULL) {
        free(first);
        return 0;
    }
    memcpy(second, first, size);

    /* Build the canonical image once, then convert it again (cache hit). */
    memset(canonical, 0, sizeof(canonical));
    hsd_asset_convert(first, size, &first_stats);
    hsd_asset_convert(second, size, &second_stats);
    same = memcmp(first, second, size) == 0 &&
           first_stats.reloc_valid == second_stats.reloc_valid &&
           first_stats.joints == second_stats.joints;
    free(first);
    free(second);
    if (!same) {
        fprintf(stderr, "decomp_assets: %s: cache mismatch\n", path);
        return 1;
    }
    return 0;
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

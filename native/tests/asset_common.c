/*
 * Shared helpers and link-time shims for the asset checks.
 *
 * The shims (OSReport, __assert, the heap hooks) satisfy symbols the compiled
 * decomp needs and must exist exactly once in the test binary, so they live
 * here rather than in any one domain file.
 */
#include "asset_common.h"

#define MAX_JOINT_NODES 8192

#define MAX_JOINT_NODES 8192

/* ------------------------------------------------------------------ shims */

void OSReport(char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void __assert(const char* file, u32 line, const char* msg)
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

unsigned char* load_archive(const char* image, const char* path,
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

void* read_host_ptr(const unsigned char* p)
{
    void* value;
    memcpy(&value, p, sizeof(value));
    return value;
}

uint32_t read_host_u32(const unsigned char* p)
{
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

uint16_t read_host_u16(const unsigned char* p)
{
    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

uint16_t read_be_u16(const unsigned char* p)
{
    return (uint16_t) (((uint16_t) p[0] << 8) | p[1]);
}

uint32_t read_be_u32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

float read_host_f32(const unsigned char* p)
{
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

int ptr_in_buffer(const void* p, const unsigned char* base, size_t size);

int archive_has_reloc(const HSD_Archive* archive,
                             const unsigned char* field)
{
    uint32_t offset;
    uint32_t i;

    if (field < archive->data ||
        field + sizeof(void*) > archive->data + archive->header.data_size)
    {
        return 0;
    }
    offset = (uint32_t) (field - archive->data);
    for (i = 0; i < archive->header.nb_reloc; i++) {
        if (archive->reloc_info[i].offset == offset) {
            return 1;
        }
    }
    return 0;
}

/* Relocation-field integrity: every listed pointer must be exactly the raw
 * big-endian offset plus the host data base.  A converter walk that writes
 * outside its own structure is caught here (P-652 walked ftData->xC/x14 into
 * the part-animation x8 arrays). */
int check_reloc_integrity(const char* path, const unsigned char* raw,
                                 const unsigned char* buffer,
                                 const HSD_Archive* archive)
{
    uint32_t r;
    int corrupted = 0;

    if (raw == NULL) {
        return 0;
    }
    for (r = 0; r < archive->header.nb_reloc; r++) {
        uint32_t off = archive->reloc_info[r].offset;
        uint32_t raw_v;
        uint32_t conv_v;
        uint32_t expect;
        if ((size_t) off + 4 > archive->header.data_size) {
            continue;
        }
        raw_v = read_be_u32(raw + 0x20 + off);
        conv_v = read_host_u32(buffer + 0x20 + off);
        expect = raw_v + (uint32_t) (uintptr_t) (buffer + 0x20);
        if (conv_v != expect) {
            if (corrupted == 0) {
                fprintf(stderr,
                        "decomp_assets: %s reloc field %u corrupted: "
                        "raw=%08x conv=%08x\n",
                        path, off, raw_v, conv_v);
            }
            corrupted++;
        }
    }
    if (corrupted != 0) {
        fprintf(stderr, "decomp_assets: %s %d corrupted reloc fields\n", path,
                corrupted);
        return 1;
    }
    return 0;
}

int ptr_in_buffer(const void* p, const unsigned char* base, size_t size)
{
    const unsigned char* q = (const unsigned char*) p;
    return q >= base && q < base + size;
}

float read_be_f32(const unsigned char* p)
{
    uint32_t v = read_be_u32(p);
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
}

/* Poses the joint tree and returns the number of HSD_JObj nodes. */
unsigned pose_tree(HSD_JObj* root, float* out_min, float* out_max)
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

/* P-689: a joint the converter walked must have host-order flags and nine
 * finite rot/scale/translate floats and a host-base-relative pointer in its
 * `field_off` slot.  A missed walk leaves the big-endian words in place; the
 * 1.0f scales then read back as denormals and the platform collapses. */
int check_converted_joint(const char* tag, const unsigned char* rdata,
                                 const unsigned char* cdata,
                                 uint32_t host_base, uint32_t field_off,
                                 uint32_t raw_joint)
{
    uint32_t host_joint = read_host_u32(cdata + field_off);
    uint32_t raw_flags;
    uint32_t host_flags;
    int i;
    int failed = 0;

    if (host_joint != raw_joint + host_base) {
        fprintf(stderr, "decomp_assets: %s joint pointer %08x want %08x\n",
                tag, host_joint, raw_joint + host_base);
        return 1;
    }
    raw_flags = read_be_u32(rdata + raw_joint + 0x04);
    host_flags = read_host_u32(cdata + raw_joint + 0x04);
    if (host_flags != raw_flags) {
        fprintf(stderr, "decomp_assets: %s joint flags %08x want %08x\n", tag,
                host_flags, raw_flags);
        failed = 1;
    }
    for (i = 0; i < 9; i++) {
        uint32_t off = raw_joint + 0x14 + (uint32_t) i * 4;
        float want = read_be_f32(rdata + off);
        float got = read_host_f32(cdata + off);
        if (!isfinite(want) || got != want) {
            fprintf(stderr, "decomp_assets: %s joint +%02x %g want %g\n", tag,
                    0x14 + i * 4, (double) got, (double) want);
            failed = 1;
        }
    }
    for (i = 0; i < 3; i++) {
        float scale = read_host_f32(cdata + raw_joint + 0x20 + i * 4);
        if (!isfinite(scale) || !(fabsf(scale) > 1e-6f)) {
            fprintf(stderr,
                    "decomp_assets: %s scale[%d]=%g is denormal/zero "
                    "(unconverted?)\n",
                    tag, i, (double) scale);
            failed = 1;
        }
    }
    return failed;
}

int check_archive(const char* image, const char* path,
                         ModelResult* result, int require_public)
{
    char error[256];
    size_t size = 0;
    unsigned char* buffer = load_archive(image, path, NULL, &size, error,
                                         sizeof(error));
    unsigned char* raw = NULL;
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
    if (size >= 0x20) {
        raw = malloc(size);
        if (raw != NULL) {
            memcpy(raw, buffer, size);
        }
    }
    if (size < 0x20 ||
        hsd_asset_convert(buffer, size, &result->stats) == 0) {
        fprintf(stderr, "decomp_assets: %s: not an HSD archive\n", path);
        free(raw);
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
        free(raw);
        free(buffer);
        return -1;
    }
    /* Relocation-field integrity: every listed pointer must be exactly the
     * raw big-endian offset plus the host data base. */
    failures += check_reloc_integrity(path, raw, buffer, &archive);
    free(raw);

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

int cache_check(const char* image, const char* path)
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

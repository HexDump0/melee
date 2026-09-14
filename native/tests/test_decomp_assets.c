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
#include "decomp/assets/hsd_convert.h"
#include "platform/disc.h"

#include <dolphin/os.h>
#include <math.h>
#include <melee/it/types.h>
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

static uint16_t read_host_u16(const unsigned char* p)
{
    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static uint32_t read_be_u32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

static float read_host_f32(const unsigned char* p)
{
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int ptr_in_buffer(const void* p, const unsigned char* base, size_t size);

static int archive_has_reloc(const HSD_Archive* archive,
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
static int check_reloc_integrity(const char* path, const unsigned char* raw,
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

/* Fighter.x8B0 has five part-animation slots.  Each ftData->x1C pointer
 * names a { u16 first_part, u16 part_count, u8*, AnimJoint** } descriptor;
 * the two u16 fields are runtime indices/counts, not byte data. */
static int check_ft_part_anims(const char* image, const char* path,
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

static int ptr_in_buffer(const void* p, const unsigned char* base, size_t size)
{
    const unsigned char* q = (const unsigned char*) p;
    return q >= base && q < base + size;
}

static float read_be_f32(const unsigned char* p)
{
    uint32_t v = read_be_u32(p);
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
}

/* P-655: ftData->x40 (itPickup: twelve grab-offset floats) and x4C_sfx
 * (FtSFX: twelve s32 sound ids plus two FtSFXArr {num, s32* ids}) are
 * numeric pointees that the converter used to leave big-endian.  A
 * byte-swapped pickup offset is a denormal near the origin (the grab volume
 * sits at the world origin) and a byte-swapped SFX id is either silent or
 * the wrong sound, so compare every field against the raw archive. */
static int check_ft_data_tables(const char* image, const char* path,
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

/* P-654: ItemAttr's two flag bytes are MSB-first on the console.  Retail
 * `itIsHeavy` is `lbz` + `extrwi r0,r0,1,24` (bit 0x80), `it_8026B30C` is
 * `extrwi r3,r3,4,25` (bits 0x78) and `itGetHoldKind` is `clrlwi r3,r3,29`
 * (bits 0x07); byte 1 is x1_1=0xB0, x1_3=0x20, x1_4=0x10, x1_5=0x08,
 * x1_67_cam_kind=0x06, x1_8=0x01.  GCC allocates bitfields LSB-first, so
 * without the PORT_PC ordering in `melee/it/types.h` every compiled read
 * below sees the wrong bits and item behavior (heavy/hold/camera flags) is
 * wrong.  Compare the compiled struct against the raw article bytes. */
static int check_item_attr_bits(unsigned char** articles, unsigned count)
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
/* P-696: three IfAll HUD model sets have names that match no converter rule
 * (`lupe`, `tdsce`, `Stc_rarwmdls`), so their whole sub-graph -- joints, TObjs
 * and the HSD_ImageDesc the magnifier copies the EFB into -- stayed
 * big-endian.  ifMagnify_802FBBDC then asked for a 0x4000 x 0x4000 EFB copy
 * (64 x 64 byte-swapped) every frame a player was off-camera, which cost
 * ~400 ms a frame in the copy encoder (G-146).  Reading the converted word as
 * host-endian must give the same value as reading the raw word as big-endian.
 */
static int check_ifall_hud_modelsets(const char* image)
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

static int check_staffroll_modelset(const char* image)
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
static int check_kumite_tables(const char* image)
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
static int check_converter_sweep(const char* image)
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
    if (failed == 0) {
        printf("decomp_assets: converter sweep archives=%u (loaded=%u converted=%u parsed=%u) ok\n",
               archives, loaded, converted, parsed);
    }
    return failed != 0 ? 1 : 0;
}

/* GrYt.dat (Yoshi's Story) `yakumono_param` is YorsterParams: four f32 then
 * four s32 read directly by grYorster_802024F0/grYorster_8020266C.  Left
 * big-endian, the block bump threshold x00 is a huge negative float (always
 * passes) and the bump velocity x10 is a denormal ~0, so hitting a Lucky
 * Block from below stops the fighter mid-air. */
static int check_yorster_param(const char* image)
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
};

static int check_stage_params(const char* image)
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
static int check_castle_dynamics(const char* image)
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
static int check_castle_param(const char* image)
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

/* P-658: the remaining unwalked public roots.  Each check loads the archive,
 * converts it, parses it and compares a numeric field inside the walked data
 * against the raw archive, so a missing branch fails on the first value. */
static int check_scene_root(const char* image, const char* path,
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

static int check_intro_easy(const char* image)
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

static int check_event_levels(const char* image)
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

/* P-645: TyDataf's trophy tables are 0x54-byte entries { s32 id; char
 * name[0x20]; char model[0x2c] }.  `Toy_8030813C` matches the id against the
 * table and `Toy_80308250` then hands out `entry + 4` (name) and `entry +
 * 0x24` (model symbol), so an unconverted big-endian id makes every lookup
 * miss and the results screen reads whatever the first entry says.  The
 * converter's symbol dispatch tested name lengths 15/17 for the 14/16-char
 * "tyModelFileTbl"/"tyModelFileUsTbl", so neither table was ever walked. */
static int check_ty_data_tables(const char* image)
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

/* HSD_JObjLoadJoint resolves every PObj joint ID against the descriptors in
 * the root it just loaded.  Exercise all common-item model roots so an item
 * whose PObj points outside that root is caught before a random match spawn. */
static int check_item_models(const char* image)
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

/* The compiled stage code (grAnime_801C7C1C -> grAnime_801C6C0C) reads the
 * map's MatAnimJoint/ShapeAnimJoint pointer arrays and loads their AObjDescs
 * at stage load.  Unconverted (big-endian) AObjDesc fields give the runtime
 * bogus end_frame/flags, so material/TEV animation never fades.  Check that the
 * converter walked the arrays and that the first reachable aobjdesc has a sane
 * host-order end_frame. */
static int check_stage_matanims(const char* image, const char* name)
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

/* P-689: a joint the converter walked must have host-order flags and nine
 * finite rot/scale/translate floats and a host-base-relative pointer in its
 * `field_off` slot.  A missed walk leaves the big-endian words in place; the
 * 1.0f scales then read back as denormals and the platform collapses. */
static int check_converted_joint(const char* tag, const unsigned char* rdata,
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

/* PlCo.dat pData[8] (`Fighter_804D6534`) is the respawn-platform
 * {joint, anim} pair read by ft_0D4D.c:139/148; pData[16] is the entry/trophy
 * platform (ft_0C31.c:101) the converter already walks, so it is the control
 * that the mechanics work. */
static int check_respawn_platform(const char* image)
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
static int check_cpu_attack_tables(const char* image)
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

static int check_archive(const char* image, const char* path,
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
    failures += check_ft_data_tables(image, "PlLk.dat", "ftDataLink");
    failures += check_ft_data_tables(image, "PlCl.dat", "ftDataClink");
    failures += check_ft_data_tables(image, "PlYs.dat", "ftDataYoshi");
    failures += check_item_models(image);
    failures += check_ty_data_tables(image);
    failures += check_staffroll_modelset(image);
    failures += check_ifall_hud_modelsets(image);
    failures += check_kumite_tables(image);
    failures += check_yorster_param(image);
    failures += check_stage_params(image);
    failures += check_castle_dynamics(image);
    failures += check_castle_param(image);
    failures += check_scene_root(image, "GmRgStnd.dat", "standScene");
    failures += check_scene_root(image, "GmRegEnd.dat", "cut1CanimScene");
    failures += check_intro_easy(image);
    failures += check_event_levels(image);
    failures += check_converter_sweep(image);
    failures += check_stage_matanims(image, "GrNBa.dat");
    failures += check_stage_matanims(image, "GrNLa.dat");

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

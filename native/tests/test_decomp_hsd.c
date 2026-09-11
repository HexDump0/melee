/*
 * S0 probe for the compiled decompilation port.
 *
 * S0a: run the decomp's HSD_ArchiveParse on a real PlMrNr.dat and diff its
 *      public-symbol table against the hand parser.
 * S0b: load the joint tree with the decomp's HSD_JObjLoadJoint and compare the
 *      resulting world matrices against a literal transcription of the port's
 *      pose math (hsd/model.c make_local_mtx + mtx_concat).
 *
 * Why 32-bit (-m32): archive.c:Locate relocates pointers into u32 slots and
 * the HSD structs assume 4-byte pointers. The host build for compiled decomp
 * code is therefore i686 (ADR-0012), matching ACGC-PC-Port's toolchain.
 *
 * Endianness: GameCube archives are big-endian. Before parsing we swap the u32
 * words of the structural prefix (header + data section + tables) to host
 * order and leave the symbols section as bytes. The data-section swap makes
 * descriptor u32/f32 fields correct; descriptor strings are corrupted, so the
 * probe nulls class/dobj/robj/mtx descriptor pointers before loading. Full
 * semantic conversion (strings, u16 fields, display lists) is S3.
 *
 * Asset-aware: exits 0 with a SKIP message when the disc image is absent
 * (ADR-0005).
 */
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
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>

void JObjInfoInit(void);

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"
#define MODEL_PATH "PlMrNr.dat"
#define MAX_JOINT_NODES 4096

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

/* Minimal OS heap shim: HSD_MemAlloc -> OSAllocFromHeap. */
OSHeapHandle HSD_GetHeap(void)
{
    return 1;
}

void HSD_SetHeap(OSHeapHandle handle)
{
    (void) handle;
}

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

/* ------------------------------------------------------------- byte order */

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

static void swap_u32_words(unsigned char* p, size_t bytes)
{
    size_t i;
    for (i = 0; i + 4 <= bytes; i += 4) {
        unsigned char a = p[i];
        unsigned char b = p[i + 1];
        p[i] = p[i + 3];
        p[i + 1] = p[i + 2];
        p[i + 2] = b;
        p[i + 3] = a;
    }
}

/* ------------------------------------------------------- symbol collection */

typedef struct {
    char** names;
    uint32_t* offsets;
    size_t count;
    size_t capacity;
} SymbolList;

static int collect_symbol(const char* name, unsigned int data_offset,
                          void* user)
{
    SymbolList* list = user;
    if (list->count == list->capacity) {
        size_t cap = list->capacity ? list->capacity * 2 : 64;
        char** names = realloc(list->names, cap * sizeof(*names));
        uint32_t* offsets = realloc(list->offsets, cap * sizeof(*offsets));
        if (names == NULL || offsets == NULL) {
            return 1;
        }
        list->names = names;
        list->offsets = offsets;
        list->capacity = cap;
    }
    list->names[list->count] = strdup(name);
    list->offsets[list->count] = data_offset;
    list->count++;
    return 0;
}

/* ------------------------------------------------ descriptor tree scrubbing */

static void scrub_joint_descs(HSD_Joint* joint, int* count, int depth)
{
    if (joint == NULL || depth > 64 || *count >= MAX_JOINT_NODES) {
        return;
    }
    (*count)++;
    joint->class_name = NULL;
    joint->u.dobjdesc = NULL;
    joint->robjdesc = NULL;
    joint->mtx = NULL;
    if (!(joint->flags & JOBJ_INSTANCE)) {
        scrub_joint_descs(joint->child, count, depth + 1);
    }
    scrub_joint_descs(joint->next, count, depth + 1);
}

/* ------------------------------------------------------------- pose oracle */

typedef struct {
    float scl[3];
    float world[3][4];
} RefPose;

/* Literal transcription of native/hsd/model.c:mtx_concat. */
static void ref_concat(const float* a, const float* b, float* out)
{
    float r[12];
    int i;
    int j;
    int k;
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            float sum = 0.0f;
            for (k = 0; k < 3; ++k) {
                sum += a[i * 4 + k] * b[k * 4 + j];
            }
            r[i * 4 + j] = sum;
        }
        r[i * 4 + 3] = a[i * 4 + 3];
        for (k = 0; k < 3; ++k) {
            r[i * 4 + 3] += a[i * 4 + k] * b[k * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static void check_pose(HSD_JObj* jobj, const RefPose* parent, int* count,
                       float* worst, int* failures)
{
    RefPose ref;
    float local[3][4];
    int r;
    int c;

    if (jobj == NULL) {
        return;
    }

    HSD_JObjSetupMatrixSub(jobj);

    /* Mirror HSD_JObjMakeMatrix's accumulated scale (JOBJ_CLASSICAL_SCALE). */
    if (jobj->flags & JOBJ_CLASSICAL_SCALE) {
        if (parent != NULL) {
            memcpy(ref.scl, parent->scl, sizeof(ref.scl));
        } else {
            ref.scl[0] = jobj->scale.x;
            ref.scl[1] = jobj->scale.y;
            ref.scl[2] = jobj->scale.z;
        }
    } else if (parent != NULL) {
        ref.scl[0] = jobj->scale.x * parent->scl[0];
        ref.scl[1] = jobj->scale.y * parent->scl[1];
        ref.scl[2] = jobj->scale.z * parent->scl[2];
    } else {
        ref.scl[0] = jobj->scale.x;
        ref.scl[1] = jobj->scale.y;
        ref.scl[2] = jobj->scale.z;
    }

    HSD_MtxSRT(local, &jobj->scale, (Vec3*) &jobj->rotate, &jobj->translate,
               parent != NULL ? (Vec3*) parent->scl : NULL);
    if (parent != NULL) {
        ref_concat(&parent->world[0][0], &local[0][0], &ref.world[0][0]);
    } else {
        memcpy(ref.world, local, sizeof(local));
    }

    for (r = 0; r < 3; ++r) {
        for (c = 0; c < 4; ++c) {
            float d = fabsf(jobj->mtx[r][c] - ref.world[r][c]);
            if (d > *worst) {
                *worst = d;
            }
            if (d > 1e-5f) {
                (*failures)++;
            }
        }
    }
    (*count)++;

    check_pose(jobj->child, &ref, count, worst, failures);
    check_pose(jobj->next, parent, count, worst, failures);
}

/* --------------------------------------------------------------------- main */

int main(int argc, char** argv)
{
    const char* image = argc > 1 ? argv[1] : DEFAULT_DISC;
    char error[256];
    DiscFile file;
    SymbolList hand = { 0 };
    unsigned char* buffer;
    HSD_Archive archive;
    size_t strings_offset;
    uint32_t file_size;
    uint32_t data_size;
    uint32_t nb_reloc;
    uint32_t nb_public;
    uint32_t nb_extern;
    size_t i;
    size_t matched = 0;
    size_t offset_ok = 0;
    int failures = 0;

    if (disc_load(image, MODEL_PATH, &file, error, sizeof(error)) != DISC_OK) {
        printf("decomp_hsd: SKIP (%s: %s)\n", image, error);
        return 0;
    }

    if (disc_enumerate_public_symbols(&file, collect_symbol, &hand, error,
                                      sizeof(error)) != DISC_OK) {
        fprintf(stderr, "decomp_hsd: hand parser failed: %s\n", error);
        return 1;
    }

    file_size = be32(file.data);
    data_size = be32((unsigned char*) file.data + 4);
    nb_reloc = be32((unsigned char*) file.data + 8);
    nb_public = be32((unsigned char*) file.data + 12);
    nb_extern = be32((unsigned char*) file.data + 16);
    strings_offset = 0x20u + data_size + (size_t) nb_reloc * 4 +
                     (size_t) nb_public * 8 + (size_t) nb_extern * 8;

    printf("decomp_hsd: %s size=%u data=%u reloc=%u public=%u extern=%u "
           "hand_symbols=%u\n",
           MODEL_PATH, file_size, data_size, nb_reloc, nb_public, nb_extern,
           (unsigned) hand.count);

    buffer = malloc(file.size);
    if (buffer == NULL) {
        fprintf(stderr, "decomp_hsd: out of memory\n");
        return 1;
    }
    memcpy(buffer, file.data, file.size);
    swap_u32_words(buffer, strings_offset);

    if (HSD_ArchiveParse(&archive, buffer, file.size) != 0) {
        fprintf(stderr, "decomp_hsd: HSD_ArchiveParse failed\n");
        return 1;
    }

    if (archive.header.nb_public != nb_public ||
        archive.header.nb_reloc != nb_reloc ||
        archive.header.nb_extern != nb_extern) {
        fprintf(stderr, "decomp_hsd: header mismatch after parse\n");
        failures++;
    }

    for (i = 0; i < hand.count; ++i) {
        void* addr = HSD_ArchiveGetPublicAddress(&archive, hand.names[i]);
        if (addr == NULL) {
            fprintf(stderr, "decomp_hsd: symbol not found: %s\n",
                    hand.names[i]);
            failures++;
            continue;
        }
        matched++;
        if ((unsigned char*) addr - archive.data == hand.offsets[i]) {
            offset_ok++;
        } else {
            fprintf(stderr,
                    "decomp_hsd: offset mismatch for %s: compiled %ld hand "
                    "%u\n",
                    hand.names[i],
                    (long) ((unsigned char*) addr - archive.data),
                    hand.offsets[i]);
            failures++;
        }
    }
    printf("decomp_hsd: S0a matched %u/%u symbols, %u offsets correct\n",
           (unsigned) matched, (unsigned) hand.count, (unsigned) offset_ok);

    /* ------------------------------------------------------------- S0b */
    {
        const char* root_name = NULL;
        HSD_Joint* joint;
        HSD_JObj* root;
        int desc_count = 0;
        int obj_count = 0;
        int pose_count = 0;
        float worst = 0.0f;
        int pose_failures = 0;
        void* arena = malloc(16 * 1024 * 1024);

        for (i = 0; i < hand.count; ++i) {
            if (strstr(hand.names[i], "_joint") != NULL &&
                strstr(hand.names[i], "matanim") == NULL) {
                root_name = hand.names[i];
                break;
            }
        }
        if (root_name == NULL) {
            fprintf(stderr, "decomp_hsd: no root joint symbol found\n");
            return 1;
        }
        joint = HSD_ArchiveGetPublicAddress(&archive, root_name);
        if (joint == NULL) {
            fprintf(stderr, "decomp_hsd: root joint not found: %s\n",
                    root_name);
            return 1;
        }

        HSD_ObjSetHeap(16 * 1024 * 1024, arena);
        HSD_VecInitAllocData();
        HSD_MtxInitAllocData();
        HSD_IDInitAllocData();
        JObjInfoInit();

        scrub_joint_descs(joint, &desc_count, 0);

        root = HSD_JObjLoadJoint(joint);
        if (root == NULL) {
            fprintf(stderr, "decomp_hsd: HSD_JObjLoadJoint returned NULL\n");
            return 1;
        }

        {
            HSD_JObj* stack[MAX_JOINT_NODES];
            int sp = 0;
            stack[sp++] = root;
            while (sp > 0) {
                HSD_JObj* j = stack[--sp];
                if (j == NULL) {
                    continue;
                }
                obj_count++;
                if (j->child != NULL && obj_count < MAX_JOINT_NODES) {
                    stack[sp++] = j->child;
                }
                if (j->next != NULL && obj_count < MAX_JOINT_NODES) {
                    stack[sp++] = j->next;
                }
            }
        }

        HSD_JObjSetupMatrixSub(root);
        check_pose(root, NULL, &pose_count, &worst, &pose_failures);

        printf("decomp_hsd: S0b root=%s descriptors=%d objects=%d posed=%d "
               "world_worst=%.9g pose_failures=%d\n",
               root_name, desc_count, obj_count, pose_count, worst,
               pose_failures);
        if (desc_count != obj_count) {
            fprintf(stderr, "decomp_hsd: joint count mismatch\n");
            failures++;
        }
        if (pose_failures != 0) {
            fprintf(stderr, "decomp_hsd: pose mismatch\n");
            failures++;
        }
    }

    printf("decomp_hsd: %s\n", failures == 0 ? "PASS" : "FAIL");

    for (i = 0; i < hand.count; ++i) {
        free(hand.names[i]);
    }
    free(hand.names);
    free(hand.offsets);
    free(buffer);
    disc_free(&file);
    return failures != 0;
}

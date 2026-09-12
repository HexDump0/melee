/*
 * S3 DVD backend.
 *
 * The decompilation's own DVDFS (`extern/dolphin/src/dolphin/dvd/dvdfs.c`) is
 * compiled verbatim and provides path resolution, FST walking, file open and
 * the async read entry points.  This file implements the drive below it:
 *
 *   - DVDInit mounts the user's disc image through platform/disc.c, loads the
 *     FST into the GameCube boot-info page (0x80000000+0x38) and calls
 *     __DVDFSInit, so DVDFS's static FstStart/FstStringStart point at it.
 *   - DVDReadAbsAsyncPrio/DVDSeekAbsAsyncPrio perform the host read/CISO
 *     mapping immediately but post their callbacks to the deferred completion
 *     queue (platform/complete.h): the console completes DVD commands from
 *     interrupt context and the game's DevCom state machine depends on that
 *     ordering.
 *   - Reads of `.ssm` sound banks are converted in place (platform/ssm.c)
 *     because the compiled synth consumes their fields as big-endian numbers.
 *
 * With no disc image the backend reports the same "no disc" states as the S1
 * stubs, so asset-free builds and tests keep running.
 */
#include <dolphin/dvd.h>
#include <dolphin/os.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/boot/boot_triage.h"
#include "platform/complete.h"
#include "platform/disc.h"
#include "platform/ssm.h"

#define DEFAULT_DISC "iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"

/* The GameCube boot info lives at the base of cached main RAM. */
#define GC_BOOT_INFO 0x80000000u
#define BOOT_INFO_MAGIC_OFF 0x20u
#define BOOT_INFO_FST_LOCATION_OFF 0x38u
#define BOOT_INFO_FST_LENGTH_OFF 0x3Cu

/* Disk header at +0x420: DOL/FST positions and lengths. */
#define DISC_BB2_OFF 0x420u
#define DISC_BB2_FST_POSITION 0x04u
#define DISC_BB2_FST_LENGTH 0x08u

#define MAX_FST_SIZE (8u * 1024u * 1024u)

extern void __DVDFSInit(void);

typedef struct DvdFile {
    uint32_t position;   /* absolute disc byte offset */
    uint32_t length;
    const char* name;    /* basename inside the FST string table */
    int is_ssm;
    SsmStreamTable ssm;
} DvdFile;

static DiscImage* disc_image;
static unsigned char* fst;
static uint32_t fst_size;
static uint32_t fst_entries;
static uint32_t fst_strings;
static DvdFile* files;
static uint32_t file_count;
static int disc_ready;

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

static int name_ends_with(const char* name, const char* suffix)
{
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    size_t i;

    if (name_len < suffix_len) {
        return 0;
    }
    for (i = 0; i < suffix_len; i++) {
        char a = name[name_len - suffix_len + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char) (a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char) (b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static void swap_fst_entries(unsigned char* entries_data, uint32_t count)
{
    uint32_t i;
    unsigned int j;

    for (i = 0; i < count; i++) {
        unsigned char* entry = entries_data + (size_t) i * 12;
        for (j = 0; j < 12; j += 4) {
            uint32_t v = be32(entry + j);
            memcpy(entry + j, &v, 4);
        }
    }
}

static void build_file_table(void)
{
    uint32_t i;

    files = calloc(fst_entries ? fst_entries : 1, sizeof(*files));
    if (files == NULL) {
        return;
    }
    for (i = 1; i < fst_entries; i++) {
        unsigned char* entry = fst + (size_t) i * 12;
        uint32_t type_and_name;
        uint32_t name_off;
        DvdFile* file;

        memcpy(&type_and_name, entry, 4);
        if (type_and_name & 0xFF000000u) {
            continue; /* directory */
        }
        name_off = type_and_name & 0x00FFFFFFu;
        if (fst_strings + name_off >= fst_size) {
            continue;
        }
        file = &files[file_count++];
        memcpy(&file->position, entry + 4, 4);
        memcpy(&file->length, entry + 8, 4);
        file->name = (const char*) (fst + fst_strings + name_off);
        file->is_ssm = name_ends_with(file->name, ".ssm");
        ssm_stream_init(&file->ssm);
    }
}

static DvdFile* find_file(uint32_t offset)
{
    uint32_t i;

    for (i = 0; i < file_count; i++) {
        if (files[i].length != 0 && offset >= files[i].position &&
            offset < files[i].position + files[i].length) {
            return &files[i];
        }
    }
    for (i = 0; i < file_count; i++) {
        if (offset == files[i].position) {
            return &files[i];
        }
    }
    return NULL;
}

static int mount_disc(void)
{
    const char* path = getenv("MELEE_DISC");
    unsigned char header[0x20];
    unsigned char bb2[0x14];
    uint32_t fst_position;
    uint32_t fst_length;
    char error[256];

    if (path == NULL || path[0] == '\0') {
        path = DEFAULT_DISC;
    }
    if (disc_mount(path, &disc_image, error, sizeof(error)) != DISC_OK) {
        boot_triage_note(
            "[boot] DVDInit: cannot mount '%s' (%s); running without a disc\n",
            path, error);
        return 0;
    }
    if (disc_image_read(disc_image, 0, header, sizeof(header)) != DISC_OK ||
        disc_image_read(disc_image, DISC_BB2_OFF, bb2, sizeof(bb2)) !=
            DISC_OK) {
        boot_triage_note("[boot] DVDInit: cannot read the disc header\n");
        disc_unmount(disc_image);
        disc_image = NULL;
        return 0;
    }
    fst_position = be32(bb2 + DISC_BB2_FST_POSITION);
    fst_length = be32(bb2 + DISC_BB2_FST_LENGTH);
    if (fst_length < 12 || fst_length > MAX_FST_SIZE) {
        boot_triage_note("[boot] DVDInit: implausible FST length %u\n",
                         fst_length);
        disc_unmount(disc_image);
        disc_image = NULL;
        return 0;
    }
    fst = malloc(fst_length);
    if (fst == NULL ||
        disc_image_read(disc_image, fst_position, fst, fst_length) !=
            DISC_OK) {
        boot_triage_note("[boot] DVDInit: cannot read the FST\n");
        free(fst);
        fst = NULL;
        disc_unmount(disc_image);
        disc_image = NULL;
        return 0;
    }
    fst_size = fst_length;
    fst_entries = be32(fst + 8);
    if (fst_entries < 2 || fst_entries > fst_length / 12) {
        boot_triage_note("[boot] DVDInit: implausible FST entry count %u\n",
                         fst_entries);
        free(fst);
        fst = NULL;
        disc_unmount(disc_image);
        disc_image = NULL;
        return 0;
    }
    swap_fst_entries(fst, fst_entries);
    fst_strings = fst_entries * 12;

    /* Publish the boot info the SDK reads through OSPhysicalToCached(0). */
    memcpy((void*) GC_BOOT_INFO, header, 0x20);
    *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_MAGIC_OFF) = 0x0D15EA5Eu;
    *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_FST_LOCATION_OFF) =
        (uint32_t) fst;
    *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_FST_LENGTH_OFF) =
        fst_length;

    build_file_table();
    __DVDFSInit();
    disc_ready = 1;
    boot_triage_note("[boot] DVD: mounted '%s' (FST %u entries, %u files)\n",
                     path, fst_entries, file_count);
    return 1;
}

/* Reads a DVD command range and fixes up any per-format data in place. */
static int dvd_read_range(uint32_t offset, void* addr, uint32_t length)
{
    DvdFile* file;

    if (!disc_ready || addr == NULL) {
        return 0;
    }
    if (disc_image_read(disc_image, offset, addr, length) != DISC_OK) {
        return 0;
    }
    file = find_file(offset);
    if (file != NULL && file->is_ssm) {
        uint32_t rel = offset - file->position;
        ssm_fix_read((unsigned char*) addr, rel, length, &file->ssm);
    }
    return 1;
}

/* ------------------------------------------------------------- completions */

typedef struct DvdCompletion {
    DVDCommandBlock* block;
    DVDCBCallback callback;
    s32 result;
    u32 transferred;
} DvdCompletion;

static void dvd_run_completion(void* arg)
{
    DvdCompletion* done = arg;
    DVDCommandBlock* block = done->block;

    if (done->result == DVD_RESULT_CANCELED) {
        block->state = DVD_STATE_CANCELED;
    } else if (done->result < 0) {
        block->state = DVD_STATE_FATAL_ERROR;
    } else {
        block->state = DVD_STATE_END;
    }
    block->transferredSize = done->transferred;
    if (done->callback != NULL) {
        done->callback(done->result, block);
    }
    free(done);
}

static BOOL dvd_post(DVDCommandBlock* block, DVDCBCallback callback, s32 result,
                     u32 transferred)
{
    DvdCompletion* done = malloc(sizeof(*done));

    if (done == NULL) {
        return FALSE;
    }
    done->block = block;
    done->callback = callback;
    done->result = result;
    done->transferred = transferred;
    block->state = DVD_STATE_BUSY;
    platform_post_completion(dvd_run_completion, done);
    return TRUE;
}

/* ----------------------------------------------------------- drive commands */

void DVDInit(void)
{
    boot_triage_real("DVDInit", BOOT_CAT_DVD);
    if (!mount_disc()) {
        /* Keep DVDFS usable without a disc: an empty FST makes path lookups
         * return -1 instead of dereferencing a null FstStart. */
        static unsigned char empty_fst[12];
        *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_FST_LOCATION_OFF) =
            (uint32_t) empty_fst;
        *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_FST_LENGTH_OFF) =
            sizeof(empty_fst);
        __DVDFSInit();
    }
}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, long length,
                        long offset, DVDCBCallback callback, long prio)
{
    int ok;

    (void) prio;
    if (block == NULL) {
        return FALSE;
    }
    block->command = DVD_COMMAND_READ;
    block->addr = addr;
    block->length = (u32) length;
    block->offset = (u32) offset;
    block->transferredSize = 0;
    block->callback = callback;
    ok = length > 0 && dvd_read_range((uint32_t) offset, addr, (uint32_t) length);
    return dvd_post(block, callback, ok ? (s32) length : DVD_RESULT_FATAL_ERROR,
                    ok ? (u32) length : 0);
}

int DVDSeekAbsAsyncPrio(DVDCommandBlock* block, long offset,
                        DVDCBCallback callback, long prio)
{
    (void) offset;
    (void) prio;
    if (block == NULL) {
        return FALSE;
    }
    block->command = DVD_COMMAND_SEEK;
    block->offset = (u32) offset;
    block->transferredSize = 0;
    block->callback = callback;
    return dvd_post(block, callback, 0, 0);
}

int DVDPrepareStreamAbsAsync(DVDCommandBlock* block, u32 length, u32 offset,
                             DVDCBCallback callback)
{
    (void) length;
    (void) offset;
    if (block == NULL) {
        return FALSE;
    }
    block->command = DVD_COMMAND_INITSTREAM;
    block->callback = callback;
    return dvd_post(block, callback, 0, 0);
}

int DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback)
{
    if (block == NULL) {
        return FALSE;
    }
    block->callback = callback;
    return dvd_post(block, callback, DVD_RESULT_CANCELED, 0);
}

long DVDCancel(volatile DVDCommandBlock* block)
{
    if (block == NULL) {
        return FALSE;
    }
    return dvd_post((DVDCommandBlock*) block, NULL, DVD_RESULT_CANCELED, 0);
}

long DVDGetCommandBlockStatus(DVDCommandBlock* block)
{
    if (block == NULL) {
        return DVD_STATE_FATAL_ERROR;
    }
    return block->state;
}

long DVDGetDriveStatus(void)
{
    return disc_ready ? DVD_STATE_END : DVD_STATE_NO_DISK;
}

BOOL DVDCheckDisk(void)
{
    return disc_ready ? TRUE : FALSE;
}

struct DVDDiskID* DVDGetCurrentDiskID(void)
{
    return (struct DVDDiskID*) GC_BOOT_INFO;
}

void DVDReset(void)
{
}

int DVDResetRequired(void)
{
    return 0;
}

void DVDPause(void)
{
}

void DVDResume(void)
{
}

int DVDSetAutoInvalidation(int autoInval)
{
    static int previous = 1;
    int old = previous;
    previous = autoInval;
    return old;
}

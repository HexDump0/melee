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
#include "platform/platform.h"
#include "platform/hps.h"
#include "platform/sem.h"
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
    int is_sem;
    int is_hps;
    const char* host_path; /* non-NULL: served from the host, not the disc */
    SsmStreamTable ssm;
} DvdFile;

/*
 * Host-backed disc files (P-847, the first slice of P-832).
 *
 * A mod can hand the game a file the disc never had.  The entry is appended
 * to the in-memory FST, so `DVDConvertPathToEntrynum` resolves it exactly
 * like a real one and every layer above -- DVDFS, DevCom, the MTH player's
 * entrynum streaming -- needs to know nothing.  Only this file knows the
 * difference, and only in `dvd_read_range`: the synthetic disc offsets sit
 * above any real one, so a read that lands in that range is served from a
 * host file instead of the image.
 *
 * Registration happens before the disc is mounted (mods initialise ahead of
 * the engine's main()), so requests queue until mount_disc applies them; a
 * later registration applies immediately and re-inits DVDFS.
 */
/* Above the real disc (a GameCube image tops out at 0x57058000) but still
 * positive as the signed 32-bit offset the DVD entry points take. */
#define HOST_FILE_BASE 0x70000000u
#define MAX_HOST_FILES 8

typedef struct HostFile {
    char name[64];
    char path[512];
    uint32_t position;
    uint32_t length;
    int applied;
} HostFile;

static HostFile host_files[MAX_HOST_FILES];
static unsigned host_file_count;

static DiscImage* disc_image;
static char disc_image_path[512];
static uint32_t disc_dol_offset;

/* The DOL is a raw region addressed by the disc header at 0x420, not an
 * FST entry (G-112). */
#define DOL_HEADER_SIZE 0x100u
#define DOL_MAX_SIZE (32u * 1024u * 1024u)

int melee_port_fonts_load(void);
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

static void build_file_table(void);

static const char* host_file_path(uint32_t position)
{
    unsigned i;

    if (position < HOST_FILE_BASE) {
        return NULL;
    }
    for (i = 0; i < host_file_count; i++) {
        if (host_files[i].position == position) {
            return host_files[i].path;
        }
    }
    return NULL;
}

/*
 * Append every registered-but-unapplied host file to the FST.
 *
 * Name offsets in an FST entry are relative to the string table, and the
 * string table starts where the entries end -- so appending entries and then
 * the new names leaves every existing offset correct.  Entry 0's length field
 * is the entry count, which is also the root directory's extent, so bumping
 * it is what makes the new files root-level and visible to path resolution.
 *
 * Runs before build_file_table() and __DVDFSInit(), with the FST already
 * byte-swapped into host order.
 */
static void host_files_apply(void)
{
    unsigned char* grown;
    uint32_t old_strings_len;
    uint32_t added_entries = 0;
    uint32_t added_names = 0;
    uint32_t name_cursor;
    unsigned char* entry;
    unsigned i;

    if (fst == NULL) {
        return;
    }
    for (i = 0; i < host_file_count; i++) {
        if (!host_files[i].applied) {
            added_entries++;
            added_names += (uint32_t) strlen(host_files[i].name) + 1;
        }
    }
    if (added_entries == 0) {
        return;
    }

    old_strings_len = fst_size - fst_strings;
    grown = malloc(fst_size + added_entries * 12 + added_names);
    if (grown == NULL) {
        boot_triage_note("[boot] DVD: out of memory adding %u host file(s)\n",
                         added_entries);
        return;
    }
    memcpy(grown, fst, fst_strings);
    memcpy(grown + fst_strings + added_entries * 12, fst + fst_strings,
           old_strings_len);

    entry = grown + fst_strings;
    name_cursor = old_strings_len;
    for (i = 0; i < host_file_count; i++) {
        uint32_t word;
        size_t len;

        if (host_files[i].applied) {
            continue;
        }
        len = strlen(host_files[i].name) + 1;
        memcpy(grown + fst_strings + added_entries * 12 + name_cursor,
               host_files[i].name, len);
        word = name_cursor; /* type 0 (file) in the high byte */
        memcpy(entry + 0, &word, 4);
        memcpy(entry + 4, &host_files[i].position, 4);
        memcpy(entry + 8, &host_files[i].length, 4);
        entry += 12;
        name_cursor += (uint32_t) len;
        host_files[i].applied = 1;
        boot_triage_note("[boot] DVD: %s -> %s (%u bytes, host file)\n",
                         host_files[i].name, host_files[i].path,
                         host_files[i].length);
    }

    free(fst);
    fst = grown;
    fst_entries += added_entries;
    fst_size += added_entries * 12 + added_names;
    fst_strings = fst_entries * 12;
    /* Entry 0's length is the entry count and the root's extent. */
    memcpy(fst + 8, &fst_entries, 4);
}

/*
 * Register a host file under `name` in the disc's root directory.
 *
 * Idempotent per name.  Safe before the disc is mounted, which is the normal
 * case: mods initialise ahead of the engine's main().
 */
int platform_disc_add_host_file(const char* name, const char* host_path)
{
    FILE* fp;
    long length;
    unsigned i;
    HostFile* slot;

    if (name == NULL || host_path == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < host_file_count; i++) {
        if (strcmp(host_files[i].name, name) == 0) {
            return 1;
        }
    }
    if (host_file_count >= MAX_HOST_FILES) {
        return 0;
    }
    fp = fopen(host_path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) <= 0) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    slot = &host_files[host_file_count];
    if (strlen(name) + 1 > sizeof(slot->name) ||
        strlen(host_path) + 1 > sizeof(slot->path)) {
        return 0;
    }
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    snprintf(slot->path, sizeof(slot->path), "%s", host_path);
    slot->length = (uint32_t) length;
    /* 32-byte aligned and clear of every real disc offset, with a gap so a
     * read that runs past one file cannot land inside the next. */
    slot->position = HOST_FILE_BASE + host_file_count * 0x00800000u;
    slot->applied = 0;
    host_file_count++;

    if (disc_ready) {
        host_files_apply();
        build_file_table();
        __DVDFSInit();
    }
    return 1;
}

static int host_file_read(const DvdFile* file, uint32_t offset, void* addr,
                          uint32_t length)
{
    FILE* fp = fopen(file->host_path, "rb");
    uint32_t rel = offset - file->position;
    size_t got;

    if (fp == NULL) {
        return 0;
    }
    if (rel >= file->length || fseek(fp, (long) rel, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    if (length > file->length - rel) {
        /* The last read of a file is padded up to the DVD's 32-byte grain. */
        memset((unsigned char*) addr + (file->length - rel), 0,
               length - (file->length - rel));
        length = file->length - rel;
    }
    got = fread(addr, 1, length, fp);
    fclose(fp);
    return got == length;
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
        file->is_sem = name_ends_with(file->name, ".sem");
        file->is_hps = name_ends_with(file->name, ".hps");
        file->host_path = host_file_path(file->position);
        if (file->is_ssm) {
            ssm_stream_init(&file->ssm);
        }
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

static int is_dol_path(const char* disc_path)
{
    return disc_path != NULL &&
        (strcmp(disc_path, "sys/main.dol") == 0 ||
         strcmp(disc_path, "main.dol") == 0 ||
         strcmp(disc_path, "/sys/main.dol") == 0);
}

/* Reads the raw DOL at disc_dol_offset with disc_image_read(). The size
 * comes from the 18 section headers: text offsets at 0x00 with sizes at
 * 0x90, data offsets at 0x1C with sizes at 0xAC; the file is at least
 * the 0x100-byte header. */
static int load_raw_dol(void** data, size_t* size)
{
    unsigned char header[DOL_HEADER_SIZE];
    uint64_t total = DOL_HEADER_SIZE;
    unsigned char* dol;
    int i;

    if (disc_image == NULL || disc_dol_offset == 0 || data == NULL ||
        size == NULL)
    {
        return -1;
    }
    if (disc_image_read(disc_image, disc_dol_offset, header, sizeof(header)) !=
        DISC_OK)
    {
        return -1;
    }
    for (i = 0; i < 7; i++) {
        uint32_t off = be32(header + 4 * (uint32_t) i);
        uint32_t section_size = be32(header + 0x90 + 4 * (uint32_t) i);
        if (off != 0 && section_size != 0 &&
            (uint64_t) off + section_size > total)
        {
            total = (uint64_t) off + section_size;
        }
    }
    for (i = 0; i < 11; i++) {
        uint32_t off = be32(header + 0x1C + 4 * (uint32_t) i);
        uint32_t section_size = be32(header + 0xAC + 4 * (uint32_t) i);
        if (off != 0 && section_size != 0 &&
            (uint64_t) off + section_size > total)
        {
            total = (uint64_t) off + section_size;
        }
    }
    if (total < DOL_HEADER_SIZE || total > DOL_MAX_SIZE) {
        return -1;
    }
    dol = malloc((size_t) total);
    if (dol == NULL) {
        return -1;
    }
    if (disc_image_read(disc_image, disc_dol_offset, dol, (size_t) total) !=
        DISC_OK)
    {
        free(dol);
        return -1;
    }
    *data = dol;
    *size = (size_t) total;
    return 0;
}

int platform_disc_load_file(const char* disc_path, void** data, size_t* size)
{
    DiscFile file;
    char error[256];
    if (disc_image == NULL || disc_path == NULL || data == NULL || size == NULL) {
        return -1;
    }
    if (is_dol_path(disc_path)) {
        if (load_raw_dol(data, size) != 0) {
            boot_triage_note("[boot] cannot read '%s' from disc: raw DOL read failed\n",
                             disc_path);
            return -1;
        }
        return 0;
    }
    if (disc_load(disc_image_path, disc_path, &file, error, sizeof(error)) != DISC_OK) {
        boot_triage_note("[boot] cannot read '%s' from disc: %s\n", disc_path, error);
        return -1;
    }
    *data = file.data;
    *size = file.size;
    return 0;
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
    snprintf(disc_image_path, sizeof(disc_image_path), "%s", path);
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
        disc_dol_offset = 0;
        return 0;
    }
    disc_dol_offset = be32(bb2);
    fst_position = be32(bb2 + DISC_BB2_FST_POSITION);
    fst_length = be32(bb2 + DISC_BB2_FST_LENGTH);
    if (fst_length < 12 || fst_length > MAX_FST_SIZE) {
        boot_triage_note("[boot] DVDInit: implausible FST length %u\n",
                         fst_length);
        disc_unmount(disc_image);
        disc_image = NULL;
        disc_dol_offset = 0;
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
        disc_dol_offset = 0;
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
        disc_dol_offset = 0;
        return 0;
    }
    swap_fst_entries(fst, fst_entries);
    fst_strings = fst_entries * 12;
    /* Before the boot info is published and before DVDFS reads it, so the
     * added entries are indistinguishable from the disc's own. */
    host_files_apply();
    melee_port_fonts_load();

    /* Publish the boot info the SDK reads through OSPhysicalToCached(0). */
    memcpy((void*) GC_BOOT_INFO, header, 0x20);
    *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_MAGIC_OFF) = 0x0D15EA5Eu;
    *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_FST_LOCATION_OFF) =
        (uint32_t) fst;
    *(volatile uint32_t*) (GC_BOOT_INFO + BOOT_INFO_FST_LENGTH_OFF) =
        fst_size;

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
    file = find_file(offset);
    if (file != NULL && file->host_path != NULL) {
        return host_file_read(file, offset, addr, length);
    }
    if (disc_image_read(disc_image, offset, addr, length) != DISC_OK) {
        return 0;
    }
    if (file != NULL && file->is_ssm) {
        uint32_t rel = offset - file->position;
        ssm_fix_read((unsigned char*) addr, rel, length, &file->ssm);
    } else if (file != NULL && file->is_hps) {
        hps_fix_read((unsigned char*) addr, offset - file->position, length);
    } else if (file != NULL && file->is_sem && offset == file->position &&
               length >= file->length) {
        /* AXDriver_8038DA70 reads smash2.sem in one shot and consumes it as
         * host-order words. */
        if (!sem_fix_read((unsigned char*) addr, file->length)) {
            boot_triage_note("[boot] sem: smash2.sem conversion failed\n");
        }
    }
    return 1;
}

/* ------------------------------------------------------------- completions */

typedef struct DvdCompletion {
    DVDCommandBlock* block;
    DVDCBCallback callback;
    s32 result;
    u32 transferred;
    int canceled;
} DvdCompletion;

static void dvd_run_completion(void* arg)
{
    DvdCompletion* done = arg;
    DVDCommandBlock* block = done->block;

    if (done->canceled) {
        /* DVDClose cancels a command whose DVDFileInfo (and command block)
         * very often lives on the caller's stack; the command must not be
         * touched after DVDCancel returns. */
        free(done);
        return;
    }
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
    done->canceled = 0;
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

/* Which disc file a loaded buffer came from.
 *
 * When a texture descriptor decodes to nonsense the only useful question is
 * "which archive is this?", and neither the GX asset registry nor
 * HSD_ArchiveParse knows -- both see an address and a length.  The DVD layer
 * is the one place the name and the destination address are both in hand.
 *
 * Buffers are reused as the game loads and frees archives, so this is a hint,
 * not a fact: the newest matching entry wins and an entry can name a file
 * whose data has since been replaced.  Good enough to point at a file. */
#define DVD_ORIGIN_SLOTS 64

static struct {
    const unsigned char* base;
    size_t size;
    const char* name;
} dvd_origins[DVD_ORIGIN_SLOTS];
static unsigned dvd_origin_next;

static void dvd_note_origin(const void* addr, size_t len, const char* name)
{
    unsigned slot;

    if (addr == NULL || len == 0 || name == NULL) {
        return;
    }
    slot = dvd_origin_next % DVD_ORIGIN_SLOTS;
    dvd_origins[slot].base = (const unsigned char*) addr;
    dvd_origins[slot].size = len;
    dvd_origins[slot].name = name;
    dvd_origin_next++;
}

const char* melee_dvd_origin(const void* ptr)
{
    const unsigned char* p = (const unsigned char*) ptr;
    unsigned i;

    if (p == NULL) {
        return NULL;
    }
    for (i = 0; i < DVD_ORIGIN_SLOTS; i++) {
        unsigned slot = (dvd_origin_next - 1 - i) % DVD_ORIGIN_SLOTS;
        if (dvd_origins[slot].base != NULL &&
            p >= dvd_origins[slot].base &&
            p < dvd_origins[slot].base + dvd_origins[slot].size) {
            return dvd_origins[slot].name;
        }
    }
    return NULL;
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
    if (ok) {
        DvdFile* origin = find_file((uint32_t) offset);
        if (origin != NULL) {
            dvd_note_origin(addr, (size_t) length, origin->name);
        }
    }
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

/* Marks any queued completion for `block` as canceled; the pump then frees it
 * without touching the (possibly dead) command block. */
static void dvd_mark_canceled(PlatformCompletionFn fn, void* arg, void* key)
{
    DvdCompletion* done;
    if (fn != dvd_run_completion) {
        return;
    }
    done = arg;
    if ((DVDCommandBlock*) key == done->block) {
        done->canceled = 1;
    }
}

static void dvd_mark_canceled_for(DVDCommandBlock* block)
{
    platform_visit_completions(dvd_mark_canceled, block);
}

int DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback)
{
    if (block == NULL) {
        return FALSE;
    }
    dvd_mark_canceled_for(block);
    block->state = DVD_STATE_CANCELED;
    block->transferredSize = 0;
    if (callback != NULL) {
        callback(DVD_RESULT_CANCELED, block);
    }
    return TRUE;
}

long DVDCancel(volatile DVDCommandBlock* block)
{
    if (block == NULL) {
        return FALSE;
    }
    return DVDCancelAsync((DVDCommandBlock*) block, NULL);
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
    boot_platform_idle_tick();
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

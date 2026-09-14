/*
 * Host memory card backend (S6).
 *
 * The compiled game drives the SDK CARD API (through hsd/baselib/hsd_3A94.c
 * and lb/lbcardnew.c) for save data, snapshots, records and the boot-time card
 * check.  The GameCube card itself is a filesystem on flash behind EXI; this
 * backend implements the same API over one directory per channel on the host.
 *
 * Storage model:
 *   $MELEE_CARD_DIR (or $XDG_DATA_HOME/melee/card, or ~/.local/share/melee/card)
 *     card_a/file_000.gcm ...      one file per card file number
 *
 * Each `.gcm` is a 0x80-byte CardFileHeader (CARDStat fields + name) followed
 * by the file data.  File numbers are the lowest free index, like the card
 * directory.  Cards are "inserted" by default; MELEE_NO_CARD=1 simulates an
 * empty slot (used by the S6 boot test).
 *
 * Async calls complete from the platform completion queue, which the game's
 * card pump reaches through OSRestoreInterrupts (see the comment above
 * card_callback); an inline completion would run before the game has set its
 * pending-operation flag and deadlock the pump.
 */
#include <dolphin/card.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "platform/complete.h"

/* ------------------------------------------------------------------ files */

#define CARD_CHANNELS 4
#define CARD_SECTOR_SIZE 0x2000u
#define CARD_MEM_SIZE 0x200000u /* 2 MiB / 256 sectors, like a 251-block card */
#define CARD_FILE_HEADER_SIZE 0x80u
#define CARD_FILE_MAGIC 0x47434D46u /* "GCMF" */
#define CARD_ICON_MAX 8

typedef struct CardFileHeader {
    unsigned int magic;
    unsigned int length;
    unsigned int time;
    unsigned int game_name;
    unsigned short company;
    unsigned char banner_format;
    unsigned char icon_format;
    unsigned short icon_speed;
    unsigned short pad0;
    unsigned int icon_addr;
    unsigned int comment_addr;
    unsigned int offset_banner;
    unsigned int offset_banner_tlut;
    unsigned int offset_icon[CARD_ICON_MAX];
    unsigned int offset_icon_tlut;
    unsigned int offset_data;
    char name[CARD_FILENAME_MAX];
    unsigned char pad[0x80 - 0x70];
} CardFileHeader;

_Static_assert(sizeof(CardFileHeader) == CARD_FILE_HEADER_SIZE,
               "card file header must be 0x80 bytes");

typedef struct CardChannel {
    int mounted;
    long xferred;
    s32 last_result;
} CardChannel;

static CardChannel card_channel[CARD_CHANNELS];

/* ------------------------------------------------------------------ paths */

static int card_base_dir(char* out, size_t out_size)
{
    const char* env = getenv("MELEE_CARD_DIR");
    const char* xdg;
    const char* home;

    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s", env);
        return 1;
    }
    xdg = getenv("XDG_DATA_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(out, out_size, "%s/melee/card", xdg);
        return 1;
    }
    home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        snprintf(out, out_size, "%s/.local/share/melee/card", home);
        return 1;
    }
    return 0;
}

static int card_channel_dir(char* out, size_t out_size, s32 chan)
{
    char base[400];
    int n;

    if (chan < 0 || chan >= CARD_CHANNELS) {
        return 0;
    }
    if (!card_base_dir(base, sizeof(base))) {
        return 0;
    }
    n = snprintf(out, out_size, "%s/card_%c", base, (char) ('a' + chan));
    return n > 0 && (size_t) n < out_size;
}

static int card_file_path(char* out, size_t out_size, s32 chan, s32 file_no)
{
    char dir[448];
    int n;

    if (!card_channel_dir(dir, sizeof(dir), chan) || file_no < 0 ||
        file_no >= CARD_MAX_FILE)
    {
        return 0;
    }
    n = snprintf(out, out_size, "%s/file_%03d.gcm", dir, (int) file_no);
    return n > 0 && (size_t) n < out_size;
}

static int card_mkdir_p(const char* path)
{
    char tmp[512];
    char* p;

    if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(tmp)) {
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return 0;
        }
        *p = '/';
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

static int card_present(s32 chan)
{
    const char* none;

    if (chan < 0 || chan >= CARD_CHANNELS) {
        return 0;
    }
    /* A card is inserted by default, like a console with a card in slot A.
     * MELEE_NO_CARD=1 simulates an empty slot, which the headless frontend
     * test uses to skip the save-data flow. */
    none = getenv("MELEE_NO_CARD");
    return none == NULL || none[0] == '\0' || none[0] == '0';
}

static int card_read_header(const char* path, CardFileHeader* header)
{
    FILE* f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    if (fread(header, 1, sizeof(*header), f) != sizeof(*header) ||
        header->magic != CARD_FILE_MAGIC)
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int card_write_header(const char* path, const CardFileHeader* header)
{
    FILE* f = fopen(path, "r+b");

    if (f == NULL) {
        return 0;
    }
    if (fwrite(header, 1, sizeof(*header), f) != sizeof(*header)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static s32 card_find_by_name(s32 chan, const char* name, int* file_no_out)
{
    char path[512];
    CardFileHeader header;
    int i;

    if (name == NULL || name[0] == '\0') {
        return CARD_RESULT_NOFILE;
    }
    for (i = 0; i < CARD_MAX_FILE; i++) {
        if (!card_file_path(path, sizeof(path), chan, i)) {
            break;
        }
        if (!card_read_header(path, &header)) {
            continue;
        }
        if (strncmp(header.name, name, CARD_FILENAME_MAX) == 0) {
            *file_no_out = i;
            return CARD_RESULT_READY;
        }
    }
    return CARD_RESULT_NOFILE;
}

static s32 card_find_free(s32 chan, int* file_no_out)
{
    char path[512];
    CardFileHeader header;
    int i;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        if (!card_file_path(path, sizeof(path), chan, i)) {
            break;
        }
        if (!card_read_header(path, &header)) {
            *file_no_out = i;
            return CARD_RESULT_READY;
        }
    }
    return CARD_RESULT_LIMIT;
}

static s32 card_used_blocks(s32 chan)
{
    char path[512];
    CardFileHeader header;
    int i;
    s32 blocks = 0;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        if (!card_file_path(path, sizeof(path), chan, i)) {
            break;
        }
        if (!card_read_header(path, &header)) {
            continue;
        }
        blocks += (s32) ((header.length + CARD_SECTOR_SIZE - 1) /
                         CARD_SECTOR_SIZE);
    }
    return blocks;
}

static s32 card_count_files(s32 chan)
{
    char path[512];
    CardFileHeader header;
    int i;
    s32 count = 0;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        if (!card_file_path(path, sizeof(path), chan, i)) {
            break;
        }
        if (card_read_header(path, &header)) {
            count++;
        }
    }
    return count;
}

static void card_result(s32 chan, s32 result)
{
    if (chan >= 0 && chan < CARD_CHANNELS) {
        card_channel[chan].last_result = result;
    }
}

/*
 * Async completions are deferred to the platform completion queue: the hsd
 * card layer sets its "command pending" flag after issuing the async call, so
 * an inline callback would run before that flag exists and the command pump
 * would then spin forever (the same ordering problem platform/complete.h was
 * built for).  The card pumps reach it through OSRestoreInterrupts.
 */
typedef struct CardCompletion {
    CARDCallback callback;
    s32 chan;
    s32 result;
} CardCompletion;

static void card_completion_run(void* arg)
{
    CardCompletion* done = arg;

    if (done->callback != NULL) {
        done->callback(done->chan, done->result);
    }
    free(done);
}

static void card_callback(CARDCallback callback, s32 chan, s32 result)
{
    CardCompletion* done;

    if (callback == NULL) {
        return;
    }
    done = malloc(sizeof(*done));
    if (done == NULL) {
        callback(chan, result);
        return;
    }
    done->callback = callback;
    done->chan = chan;
    done->result = result;
    platform_post_completion(card_completion_run, done);
}

/* --------------------------------------------------------------- public API */

void CARDInit(void)
{
    int i;
    for (i = 0; i < CARD_CHANNELS; i++) {
        card_channel[i].mounted = 0;
        card_channel[i].xferred = 0;
        card_channel[i].last_result = CARD_RESULT_READY;
    }
}

int CARDProbe(long chan)
{
    return card_present((s32) chan) ? 1 : 0;
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)
{
    if (!card_present(chan)) {
        return CARD_RESULT_NOCARD;
    }
    if (memSize != NULL) {
        *memSize = (s32) CARD_MEM_SIZE;
    }
    if (sectorSize != NULL) {
        *sectorSize = (s32) CARD_SECTOR_SIZE;
    }
    return CARD_RESULT_READY;
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback,
                   CARDCallback attachCallback)
{
    char dir[512];
    s32 result;

    (void) workArea;
    (void) detachCallback;
    if (!card_present(chan)) {
        result = CARD_RESULT_NOCARD;
    } else if (!card_channel_dir(dir, sizeof(dir), chan) || !card_mkdir_p(dir)) {
        result = CARD_RESULT_IOERROR;
    } else {
        card_channel[chan].mounted = 1;
        result = CARD_RESULT_READY;
    }
    card_result(chan, result);
    card_callback(attachCallback, chan, result);
    return result;
}

s32 CARDUnmount(s32 chan)
{
    if (chan < 0 || chan >= CARD_CHANNELS) {
        return CARD_RESULT_FATAL_ERROR;
    }
    if (!card_present(chan)) {
        return CARD_RESULT_NOCARD;
    }
    card_channel[chan].mounted = 0;
    return CARD_RESULT_READY;
}

s32 CARDCheckAsync(s32 chan, CARDCallback callback)
{
    s32 result;

    if (!card_present(chan)) {
        result = CARD_RESULT_NOCARD;
    } else {
        result = CARD_RESULT_READY;
    }
    card_result(chan, result);
    card_callback(callback, chan, result);
    return result;
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback)
{
    char dir[512];
    char path[512];
    s32 result = CARD_RESULT_READY;
    int i;

    if (!card_present(chan)) {
        result = CARD_RESULT_NOCARD;
    } else if (!card_channel_dir(dir, sizeof(dir), chan) || !card_mkdir_p(dir)) {
        result = CARD_RESULT_IOERROR;
    } else {
        for (i = 0; i < CARD_MAX_FILE; i++) {
            if (!card_file_path(path, sizeof(path), chan, i)) {
                break;
            }
            remove(path);
        }
    }
    card_result(chan, result);
    card_callback(callback, chan, result);
    return result;
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed)
{
    if (!card_present(chan)) {
        return CARD_RESULT_NOCARD;
    }
    if (byteNotUsed != NULL) {
        /* Reserve the card's directory/FAT blocks like a real card. */
        s32 free_blocks = (s32) (CARD_MEM_SIZE / CARD_SECTOR_SIZE) - 5 -
                          card_used_blocks(chan);
        if (free_blocks < 0) {
            free_blocks = 0;
        }
        *byteNotUsed = free_blocks * (s32) CARD_SECTOR_SIZE;
    }
    if (filesNotUsed != NULL) {
        *filesNotUsed = CARD_MAX_FILE - card_count_files(chan);
    }
    return CARD_RESULT_READY;
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat)
{
    char path[512];
    CardFileHeader header;

    if (!card_present(chan)) {
        return CARD_RESULT_NOCARD;
    }
    if (stat == NULL || !card_file_path(path, sizeof(path), chan, fileNo) ||
        !card_read_header(path, &header))
    {
        return CARD_RESULT_NOFILE;
    }
    memset(stat, 0, sizeof(*stat));
    memcpy(stat->fileName, header.name, CARD_FILENAME_MAX);
    stat->length = header.length;
    stat->time = header.time;
    memcpy(&stat->gameName, &header.game_name, 4);
    memcpy(stat->company, &header.company, 2);
    stat->bannerFormat = header.banner_format;
    stat->iconAddr = header.icon_addr;
    stat->iconFormat = header.icon_format;
    stat->iconSpeed = header.icon_speed;
    stat->commentAddr = header.comment_addr;
    stat->offsetBanner = header.offset_banner;
    stat->offsetBannerTlut = header.offset_banner_tlut;
    memcpy(stat->offsetIcon, header.offset_icon, sizeof(stat->offsetIcon));
    stat->offsetIconTlut = header.offset_icon_tlut;
    stat->offsetData = header.offset_data;
    return CARD_RESULT_READY;
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat,
                       CARDCallback callback)
{
    char path[512];
    CardFileHeader header;
    s32 result;

    if (!card_present(chan)) {
        result = CARD_RESULT_NOCARD;
    } else if (stat == NULL ||
               !card_file_path(path, sizeof(path), chan, fileNo) ||
               !card_read_header(path, &header))
    {
        result = CARD_RESULT_NOFILE;
    } else {
        memcpy(header.name, stat->fileName, CARD_FILENAME_MAX);
        header.length = stat->length;
        header.time = stat->time;
        memcpy(&header.game_name, &stat->gameName, 4);
        memcpy(&header.company, stat->company, 2);
        header.banner_format = stat->bannerFormat;
        header.icon_addr = stat->iconAddr;
        header.icon_format = stat->iconFormat;
        header.icon_speed = stat->iconSpeed;
        header.comment_addr = stat->commentAddr;
        header.offset_banner = stat->offsetBanner;
        header.offset_banner_tlut = stat->offsetBannerTlut;
        memcpy(header.offset_icon, stat->offsetIcon,
               sizeof(header.offset_icon));
        header.offset_icon_tlut = stat->offsetIconTlut;
        header.offset_data = stat->offsetData;
        result = card_write_header(path, &header) ? CARD_RESULT_READY
                                                  : CARD_RESULT_IOERROR;
    }
    card_result(chan, result);
    card_callback(callback, chan, result);
    return result;
}

static void card_fill_file_info(CARDFileInfo* fileInfo, s32 chan, s32 fileNo,
                                const CardFileHeader* header)
{
    fileInfo->chan = chan;
    fileInfo->fileNo = fileNo;
    fileInfo->offset = 0;
    fileInfo->length = (s32) header->length;
    fileInfo->iBlock = 0;
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo)
{
    char path[512];
    CardFileHeader header;

    if (!card_present(chan)) {
        return CARD_RESULT_NOCARD;
    }
    if (fileInfo == NULL ||
        !card_file_path(path, sizeof(path), chan, fileNo) ||
        !card_read_header(path, &header))
    {
        return CARD_RESULT_NOFILE;
    }
    card_fill_file_info(fileInfo, chan, fileNo, &header);
    return CARD_RESULT_READY;
}

s32 CARDOpen(s32 chan, char* fileName, CARDFileInfo* fileInfo)
{
    int file_no = 0;
    s32 result;

    if (!card_present(chan)) {
        return CARD_RESULT_NOCARD;
    }
    if (fileInfo == NULL) {
        return CARD_RESULT_NOFILE;
    }
    result = card_find_by_name(chan, fileName, &file_no);
    if (result != CARD_RESULT_READY) {
        return result;
    }
    return CARDFastOpen(chan, file_no, fileInfo);
}

s32 CARDClose(CARDFileInfo* fileInfo)
{
    return fileInfo != NULL ? CARD_RESULT_READY : CARD_RESULT_NOFILE;
}

long CARDGetXferredBytes(long chan)
{
    if (chan < 0 || chan >= CARD_CHANNELS) {
        return 0;
    }
    return card_channel[chan].xferred;
}

static s32 card_io(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                   int write)
{
    char path[512];
    CardFileHeader header;
    FILE* f;
    s32 result;

    if (fileInfo == NULL) {
        return CARD_RESULT_NOFILE;
    }
    if (!card_present(fileInfo->chan)) {
        return CARD_RESULT_NOCARD;
    }
    if (!card_file_path(path, sizeof(path), fileInfo->chan, fileInfo->fileNo) ||
        !card_read_header(path, &header))
    {
        return CARD_RESULT_NOFILE;
    }
    if (length < 0 || offset < 0 ||
        (u32) (offset + length) > header.length)
    {
        return CARD_RESULT_INSSPACE;
    }
    f = fopen(path, write ? "r+b" : "rb");
    if (f == NULL) {
        return CARD_RESULT_IOERROR;
    }
    if (fseek(f, (long) (CARD_FILE_HEADER_SIZE + (u32) offset), SEEK_SET) !=
        0)
    {
        fclose(f);
        return CARD_RESULT_IOERROR;
    }
    if (length > 0) {
        size_t moved = write ? fwrite(buf, 1, (size_t) length, f)
                             : fread(buf, 1, (size_t) length, f);
        if (moved != (size_t) length) {
            fclose(f);
            return CARD_RESULT_IOERROR;
        }
    }
    fclose(f);
    if (write && (u32) (offset + length) > header.length) {
        header.length = (u32) (offset + length);
        card_write_header(path, &header);
    }
    card_channel[fileInfo->chan].xferred = length;
    result = CARD_RESULT_READY;
    card_result(fileInfo->chan, result);
    return result;
}

long CARDRead(struct CARDFileInfo* fileInfo, void* buf, long length,
              long offset)
{
    s32 r = card_io(fileInfo, buf, (s32) length, (s32) offset, 0);
    return r;
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                  CARDCallback callback)
{
    s32 result = card_io(fileInfo, buf, length, offset, 0);

    if (result == CARD_RESULT_READY) {
        card_callback(callback, fileInfo != NULL ? fileInfo->chan : 0, result);
    }
    return result;
}

long CARDWrite(struct CARDFileInfo* fileInfo, void* buf, long length,
               long offset)
{
    s32 r = card_io(fileInfo, buf, (s32) length, (s32) offset, 1);
    return r;
}

s32 CARDWriteAsync(CARDFileInfo* fileInfo, void* buf, long length, long offset,
                   void (*callback)(long, long))
{
    s32 result = card_io(fileInfo, buf, (s32) length, (s32) offset, 1);

    if (result == CARD_RESULT_READY) {
        card_callback((CARDCallback) callback,
                      fileInfo != NULL ? fileInfo->chan : 0, result);
    }
    return result;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size,
                    CARDFileInfo* fileInfo, CARDCallback callback)
{
    char path[512];
    CardFileHeader header;
    int file_no = 0;
    FILE* f;
    s32 result;

    if (!card_present(chan)) {
        result = CARD_RESULT_NOCARD;
    } else if (fileName == NULL || fileInfo == NULL) {
        result = CARD_RESULT_NOENT;
    } else if (strlen(fileName) >= CARD_FILENAME_MAX) {
        result = CARD_RESULT_NAMETOOLONG;
    } else if (card_find_by_name(chan, fileName, &file_no) ==
               CARD_RESULT_READY)
    {
        result = CARD_RESULT_EXIST;
    } else if (card_find_free(chan, &file_no) != CARD_RESULT_READY) {
        result = CARD_RESULT_LIMIT;
    } else if (!card_file_path(path, sizeof(path), chan, file_no)) {
        result = CARD_RESULT_IOERROR;
    } else {
        memset(&header, 0, sizeof(header));
        header.magic = CARD_FILE_MAGIC;
        header.length = size;
        memcpy(header.name, fileName, strlen(fileName));
        f = fopen(path, "wb");
        if (f == NULL) {
            result = CARD_RESULT_IOERROR;
        } else {
            fwrite(&header, 1, sizeof(header), f);
            if (size > 0) {
                fseek(f, (long) (CARD_FILE_HEADER_SIZE + size - 1), SEEK_SET);
                fputc(0, f);
            }
            fclose(f);
            card_fill_file_info(fileInfo, chan, file_no, &header);
            result = CARD_RESULT_READY;
        }
    }
    card_result(chan, result);
    card_callback(callback, chan, result);
    return result;
}

static s32 card_delete(s32 chan, char* fileName)
{
    char path[512];
    int file_no = 0;

    if (!card_present(chan)) {
        return CARD_RESULT_NOCARD;
    }
    if (fileName == NULL ||
        card_find_by_name(chan, fileName, &file_no) != CARD_RESULT_READY ||
        !card_file_path(path, sizeof(path), chan, file_no))
    {
        return CARD_RESULT_NOFILE;
    }
    return remove(path) == 0 ? CARD_RESULT_READY : CARD_RESULT_IOERROR;
}

s32 CARDDeleteAsync(s32 chan, char* fileName, CARDCallback callback)
{
    s32 result = card_delete(chan, fileName);

    card_result(chan, result);
    card_callback(callback, chan, result);
    return result;
}

s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName,
                    CARDCallback callback)
{
    char path[512];
    CardFileHeader header;
    int file_no = 0;
    s32 result;

    if (!card_present(chan)) {
        result = CARD_RESULT_NOCARD;
    } else if (oldName == NULL || newName == NULL ||
               strlen(newName) >= CARD_FILENAME_MAX)
    {
        result = CARD_RESULT_NAMETOOLONG;
    } else if (card_find_by_name(chan, oldName, &file_no) != CARD_RESULT_READY)
    {
        result = CARD_RESULT_NOFILE;
    } else if (card_find_by_name(chan, newName, &file_no) ==
               CARD_RESULT_READY)
    {
        result = CARD_RESULT_EXIST;
    } else if (card_find_by_name(chan, oldName, &file_no) != CARD_RESULT_READY ||
               !card_file_path(path, sizeof(path), chan, file_no) ||
               !card_read_header(path, &header))
    {
        result = CARD_RESULT_NOFILE;
    } else {
        memset(header.name, 0, sizeof(header.name));
        memcpy(header.name, newName, strlen(newName));
        result = card_write_header(path, &header) ? CARD_RESULT_READY
                                                  : CARD_RESULT_IOERROR;
    }
    card_result(chan, result);
    card_callback(callback, chan, result);
    return result;
}

/*
 * S1 DVD backend: log-only stubs.
 *
 * Every call is recorded by the triage logger and fails the way a GameCube
 * with no disc does (ConvertPathToEntrynum -> -1, DVDFastOpen/CheckDisk ->
 * FALSE, drive status DVD_STATE_NO_DISK), so the compiled game's no-disc
 * paths run.  S3 replaces this with the host DVD/asset pipeline built on
 * native/platform/disc.c.
 */
#include <dolphin/dvd.h>

#include "decomp/boot/boot_triage.h"

static struct DVDDiskID disc_id;

void DVDInit(void)
{
    boot_triage_real("DVDInit", BOOT_CAT_DVD);
}

s32 DVDConvertPathToEntrynum(const char* pathPtr)
{
    (void) pathPtr;
    boot_triage_stub("DVDConvertPathToEntrynum", BOOT_CAT_DVD);
    return -1;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo)
{
    (void) entrynum;
    (void) fileInfo;
    boot_triage_stub("DVDFastOpen", BOOT_CAT_DVD);
    return FALSE;
}

BOOL DVDClose(DVDFileInfo* fileInfo)
{
    (void) fileInfo;
    boot_triage_stub("DVDClose", BOOT_CAT_DVD);
    return FALSE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length,
                      s32 offset, DVDCallback callback, s32 prio)
{
    (void) fileInfo;
    (void) addr;
    (void) length;
    (void) offset;
    (void) callback;
    (void) prio;
    boot_triage_stub("DVDReadAsyncPrio", BOOT_CAT_DVD);
    return FALSE;
}

long DVDGetDriveStatus(void)
{
    boot_triage_stub("DVDGetDriveStatus", BOOT_CAT_DVD);
    return DVD_STATE_NO_DISK;
}

struct DVDDiskID* DVDGetCurrentDiskID(void)
{
    boot_triage_stub("DVDGetCurrentDiskID", BOOT_CAT_DVD);
    return &disc_id;
}

BOOL DVDCheckDisk(void)
{
    boot_triage_stub("DVDCheckDisk", BOOT_CAT_DVD);
    return FALSE;
}

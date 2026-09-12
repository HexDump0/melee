/*
 * S1 PAD/CARD/FIO backend: log-only stubs with just enough state for the
 * compiled boot to proceed without input or memory cards.
 *
 * PADRead reports all four channels disconnected (err = PAD_ERR_NO_CONTROLLER)
 * so db_GetGameLaunchButtonState() finishes its do/while immediately.
 * CARDProbeEx reports success so its wait loop does not spin; every other
 * CARD/FIO/MCC call fails with CARD_RESULT_NOCARD or -1.
 *
 * Input, saves and the USB gecko are out of scope until S6/S7; the triage log
 * records what the boot actually touches.
 */
#include <dolphin/card.h>
#include <dolphin/mcc.h>
#include <dolphin/pad.h>

#include <string.h>

#include "decomp/boot/boot_triage.h"
#include "platform/platform.h"

/* --------------------------------------------------------------------- PAD */

static const PadInputFrame* pad_script;
static unsigned pad_script_channels;
static unsigned pad_script_frames;
static unsigned pad_script_frame;
static int pad_script_loop;

void pad_set_input_script(const PadInputFrame* frames, unsigned channels,
                          unsigned frame_count)
{
    pad_script = frames;
    pad_script_channels = channels;
    pad_script_frames = frame_count;
    pad_script_frame = 0;
}

void pad_set_input_loop(int enable)
{
    pad_script_loop = enable != 0;
}

unsigned pad_input_frame(void)
{
    return pad_script_frame;
}

BOOL PADInit(void)
{
    boot_triage_real("PADInit", BOOT_CAT_PAD);
    return TRUE;
}

static void pad_apply_script(PADStatus* status, int chan)
{
    const PadInputFrame* f;
    unsigned frame;

    if (pad_script == NULL || chan >= (int) pad_script_channels ||
        pad_script_frames == 0)
    {
        status->err = PAD_ERR_NO_CONTROLLER;
        return;
    }
    frame = pad_script_frame;
    if (pad_script_loop) {
        frame %= pad_script_frames;
    } else if (frame >= pad_script_frames) {
        frame = pad_script_frames - 1;
    }
    f = &pad_script[frame * pad_script_channels + (unsigned) chan];
    status->err = PAD_ERR_NONE;
    status->button = f->buttons;
    status->stickX = f->stick_x;
    status->stickY = f->stick_y;
    status->substickX = f->cstick_x;
    status->substickY = f->cstick_y;
    status->triggerLeft = f->trigger_l;
    status->triggerRight = f->trigger_r;
    status->analogA = 0;
    status->analogB = 0;
}

u32 PADRead(struct PADStatus* status)
{
    int i;

    boot_triage_stub("PADRead", BOOT_CAT_PAD);
    if (status != NULL) {
        memset(status, 0, sizeof(PADStatus) * 4);
        for (i = 0; i < 4; i++) {
            pad_apply_script(&status[i], i);
        }
    }
    pad_script_frame++;
    return 0;
}

void PADClamp(PADStatus* status)
{
    (void) status;
    boot_triage_stub("PADClamp", BOOT_CAT_PAD);
}

void PADControlMotor(s32 chan, u32 command)
{
    (void) chan;
    (void) command;
    boot_triage_stub("PADControlMotor", BOOT_CAT_PAD);
}

int PADReset(unsigned long mask)
{
    (void) mask;
    boot_triage_stub("PADReset", BOOT_CAT_PAD);
    return 1;
}

BOOL PADRecalibrate(u32 mask)
{
    (void) mask;
    boot_triage_stub("PADRecalibrate", BOOT_CAT_PAD);
    return TRUE;
}

void PADSetSamplingRate(unsigned long msec)
{
    (void) msec;
    boot_triage_stub("PADSetSamplingRate", BOOT_CAT_PAD);
}

void PADSetSpec(u32 spec)
{
    (void) spec;
    boot_triage_stub("PADSetSpec", BOOT_CAT_PAD);
}

/* -------------------------------------------------------------------- CARD */

void CARDInit(void)
{
    boot_triage_real("CARDInit", BOOT_CAT_CARD);
}

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)
{
    (void) chan;
    boot_triage_stub("CARDProbeEx", BOOT_CAT_CARD);
    if (memSize != NULL) {
        *memSize = 0x80000;
    }
    if (sectorSize != NULL) {
        *sectorSize = 0x2000;
    }
    return CARD_RESULT_READY;
}

int CARDProbe(long chan)
{
    (void) chan;
    boot_triage_stub("CARDProbe", BOOT_CAT_CARD);
    return 0;
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo)
{
    (void) chan;
    (void) fileNo;
    (void) fileInfo;
    boot_triage_stub("CARDFastOpen", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDOpen(s32 chan, char* fileName, CARDFileInfo* fileInfo)
{
    (void) chan;
    (void) fileName;
    (void) fileInfo;
    boot_triage_stub("CARDOpen", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDClose(CARDFileInfo* fileInfo)
{
    (void) fileInfo;
    boot_triage_stub("CARDClose", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback,
                   CARDCallback attachCallback)
{
    (void) chan;
    (void) workArea;
    (void) detachCallback;
    (void) attachCallback;
    boot_triage_stub("CARDMountAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDUnmount(s32 chan)
{
    (void) chan;
    boot_triage_stub("CARDUnmount", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDCheckAsync(s32 chan, CARDCallback callback)
{
    (void) chan;
    (void) callback;
    boot_triage_stub("CARDCheckAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback)
{
    (void) chan;
    (void) callback;
    boot_triage_stub("CARDFormatAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed)
{
    (void) chan;
    (void) byteNotUsed;
    (void) filesNotUsed;
    boot_triage_stub("CARDFreeBlocks", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat)
{
    (void) chan;
    (void) fileNo;
    (void) stat;
    boot_triage_stub("CARDGetStatus", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

long CARDGetXferredBytes(long chan)
{
    (void) chan;
    boot_triage_stub("CARDGetXferredBytes", BOOT_CAT_CARD);
    return 0;
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset,
                  CARDCallback callback)
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    (void) callback;
    boot_triage_stub("CARDReadAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

long CARDRead(struct CARDFileInfo* fileInfo, void* buf, long length,
              long offset)
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    boot_triage_stub("CARDRead", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDWriteAsync(struct CARDFileInfo* fileInfo, void* buf, long length,
                   long offset, void (*callback)(long, long))
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    (void) callback;
    boot_triage_stub("CARDWriteAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

long CARDWrite(struct CARDFileInfo* fileInfo, void* buf, long length,
               long offset)
{
    (void) fileInfo;
    (void) buf;
    (void) length;
    (void) offset;
    boot_triage_stub("CARDWrite", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size,
                    CARDFileInfo* fileInfo, CARDCallback callback)
{
    (void) chan;
    (void) fileName;
    (void) size;
    (void) fileInfo;
    (void) callback;
    boot_triage_stub("CARDCreateAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDDeleteAsync(s32 chan, char* fileName, CARDCallback callback)
{
    (void) chan;
    (void) fileName;
    (void) callback;
    boot_triage_stub("CARDDeleteAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName,
                    CARDCallback callback)
{
    (void) chan;
    (void) oldName;
    (void) newName;
    (void) callback;
    boot_triage_stub("CARDRenameAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat,
                       CARDCallback callback)
{
    (void) chan;
    (void) fileNo;
    (void) stat;
    (void) callback;
    boot_triage_stub("CARDSetStatusAsync", BOOT_CAT_CARD);
    return CARD_RESULT_NOCARD;
}

/* ---------------------------------------------------------------- MCC / FIO */

int MCCInit(enum MCC_EXI exiChannel, u8 timeout, MCC_CBSysEvent callbackSysEvent)
{
    (void) exiChannel;
    (void) timeout;
    (void) callbackSysEvent;
    boot_triage_stub("MCCInit", BOOT_CAT_CARD);
    return -1;
}

void MCCExit(void)
{
    boot_triage_stub("MCCExit", BOOT_CAT_CARD);
}

int MCCEnumDevices(MCC_CBEnumDevices callbackEnumDevices)
{
    (void) callbackEnumDevices;
    boot_triage_stub("MCCEnumDevices", BOOT_CAT_CARD);
    return -1;
}

int MCCGetConnectionStatus(enum MCC_CHANNEL chID, enum MCC_CONNECT* connect)
{
    (void) chID;
    (void) connect;
    boot_triage_stub("MCCGetConnectionStatus", BOOT_CAT_CARD);
    return -1;
}

u8 MCCGetFreeBlocks(enum MCC_MODE mode)
{
    (void) mode;
    boot_triage_stub("MCCGetFreeBlocks", BOOT_CAT_CARD);
    return 0;
}

u8 MCCGetLastError(void)
{
    boot_triage_stub("MCCGetLastError", BOOT_CAT_CARD);
    return 0;
}

int MCCNotify(enum MCC_CHANNEL chID, u32 notify)
{
    (void) chID;
    (void) notify;
    boot_triage_stub("MCCNotify", BOOT_CAT_CARD);
    return -1;
}

int MCCClose(enum MCC_CHANNEL chID)
{
    (void) chID;
    boot_triage_stub("MCCClose", BOOT_CAT_CARD);
    return -1;
}

int MCCOpen(enum MCC_CHANNEL chID, u8 blockSize, MCC_CBEvent callbackEvent)
{
    (void) chID;
    (void) blockSize;
    (void) callbackEvent;
    boot_triage_stub("MCCOpen", BOOT_CAT_CARD);
    return -1;
}

int MCCRead(enum MCC_CHANNEL chID, u32 offset, void* data, long size,
            enum MCC_SYNC_STATE async)
{
    (void) chID;
    (void) offset;
    (void) data;
    (void) size;
    (void) async;
    boot_triage_stub("MCCRead", BOOT_CAT_CARD);
    return -1;
}

int MCCWrite(enum MCC_CHANNEL chID, u32 offset, void* data, long size,
             enum MCC_SYNC_STATE async)
{
    (void) chID;
    (void) offset;
    (void) data;
    (void) size;
    (void) async;
    boot_triage_stub("MCCWrite", BOOT_CAT_CARD);
    return -1;
}

int MCCStreamOpen(enum MCC_CHANNEL chID, u8 blockSize)
{
    (void) chID;
    (void) blockSize;
    boot_triage_stub("MCCStreamOpen", BOOT_CAT_CARD);
    return -1;
}

int FIOInit(enum MCC_EXI exiChannel, enum MCC_CHANNEL chID, u8 blockSize)
{
    (void) exiChannel;
    (void) chID;
    (void) blockSize;
    boot_triage_stub("FIOInit", BOOT_CAT_CARD);
    return -1;
}

void FIOExit(void)
{
    boot_triage_stub("FIOExit", BOOT_CAT_CARD);
}

int FIOQuery(void)
{
    boot_triage_stub("FIOQuery", BOOT_CAT_CARD);
    return 0;
}

int FIOFopen(const char* filename, u32 mode)
{
    (void) filename;
    (void) mode;
    boot_triage_stub("FIOFopen", BOOT_CAT_CARD);
    return -1;
}

int FIOFclose(int handle)
{
    (void) handle;
    boot_triage_stub("FIOFclose", BOOT_CAT_CARD);
    return -1;
}

u32 FIOFwrite(int handle, void* data, u32 size)
{
    (void) handle;
    (void) data;
    (void) size;
    boot_triage_stub("FIOFwrite", BOOT_CAT_CARD);
    return 0;
}

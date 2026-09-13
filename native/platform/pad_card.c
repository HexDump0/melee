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

/* Live states pushed by the interactive frontend each frame. */
static PadInputFrame pad_live[4];
static unsigned pad_live_channels;

void pad_set_live_input(const PadInputFrame* frames, unsigned channels)
{
    unsigned i;

    if (channels > 4) {
        channels = 4;
    }
    pad_live_channels = channels;
    for (i = 0; i < channels; i++) {
        pad_live[i] = frames[i];
    }
}

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

static void pad_apply_live(PADStatus* status, int chan)
{
    const PadInputFrame* f;

    if (pad_script != NULL) {
        pad_apply_script(status, chan);
        return;
    }
    if (chan >= (int) pad_live_channels) {
        status->err = PAD_ERR_NO_CONTROLLER;
        return;
    }
    f = &pad_live[chan];
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
            pad_apply_live(&status[i], i);
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

/*
 * The memory card API lives in platform/card.c (S6): a host directory per
 * channel implementing the SDK CARD surface the compiled game uses.
 */

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

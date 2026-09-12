/*
 * S5 host-side AX backend (ADR-0013): the `AXOut.c` replacement.
 *
 * On hardware `__AXOutNewFrame` syncs the user PBs into the DSP shadow,
 * writes the command list and hands a 160-frame buffer to the AI DMA; the
 * DSP mixes asynchronously.  Here the same call sequence runs and then
 * `ax_mixer_frame` renders the frame on the CPU.
 *
 * `AX.c`, `AXAlloc.c`, `AXVPB.c`, `AXSPB.c`, `AXAux.c` and `AXCL.c` are
 * compiled verbatim from `extern/dolphin`; this file only replaces the
 * hardware boundary (DSP task + AI DMA), so the game's own driver code drives
 * the host mixer.
 */
#include "audio/ax_hle.h"

#include <dolphin/ai.h>
#include <dolphin/ax.h>
#include <dolphin/os.h>

#include "audio/ax_mixer.h"
#include "decomp/boot/boot_triage.h"

/* extern/dolphin/src/dolphin/ax/__ax.h (compiled into the same target). */
void __AXSyncPBs(u32 lessDspCycles);
void __AXPrintStudio(void);
void __AXProcessAux(void);
void __AXServiceCallbackStack(void);
void __AXNextFrame(void* sbuffer, void* buffer);
u32 __AXGetCommandListAddress(void);

#define AX_FRAME_SAMPLES 160 /* 5 ms at 32 kHz */

static void (*ax_user_frame_callback)(void);
static AxHleSink ax_sink;
static void* ax_sink_user;
static int ax_initialized;
static s16 ax_frame_output[AX_FRAME_SAMPLES * 2];
static s16 ax_frame_surround[AX_FRAME_SAMPLES];
static unsigned ax_frame_count;
static unsigned ax_video_accum; /* audio-frame thirds owed to the VI pump */

static u64 ax_hash = 0xCBF29CE484222325ull;

/* ------------------------------------------------------------------- AI API */

void AIInit(u8* stack)
{
    (void) stack;
    boot_triage_real("AIInit", BOOT_CAT_AX);
}

void AISetDSPSampleRate(u32 rate)
{
    (void) rate; /* 32 kHz is the only rate HSD_SynthInit asks for. */
}

void AISetStreamVolLeft(u8 vol)
{
    (void) vol; /* The game applies the volume per node (HSD_Synth_804D6030). */
}

void AISetStreamVolRight(u8 vol)
{
    (void) vol;
}

/* --------------------------------------------------------------- AXOut API */

void __AXOutInitDSP(void)
{
    /* No DSP: the mixer is the DSP. */
}

void __AXOutInit(void)
{
    ax_user_frame_callback = NULL;
    ax_frame_count = 0;
    ax_video_accum = 0;
    ax_hash = 0xCBF29CE484222325ull;
    ax_initialized = 1;
    ax_mixer_init();
}

void __AXOutQuit(void)
{
    ax_user_frame_callback = NULL;
    ax_initialized = 0;
}

void AXRegisterCallback(void (*callback)(void))
{
    ax_user_frame_callback = callback;
}

void __AXOutAiCallback(void)
{
}

void __AXOutNewFrame(u32 lessDspCycles)
{
    unsigned i;

    __AXSyncPBs(lessDspCycles);
    __AXPrintStudio();
    (void) __AXGetCommandListAddress();
    __AXServiceCallbackStack();
    __AXProcessAux();
    if (ax_user_frame_callback != NULL) {
        ax_user_frame_callback();
    }
    __AXNextFrame(ax_frame_surround, ax_frame_output);
    ax_mixer_frame(ax_frame_output, AX_FRAME_SAMPLES);

    for (i = 0; i < AX_FRAME_SAMPLES * 2; i++) {
        ax_hash ^= (u16) ax_frame_output[i];
        ax_hash *= 0x100000001B3ull;
    }
    ax_frame_count++;

    if (ax_sink != NULL) {
        ax_sink(ax_frame_output, AX_FRAME_SAMPLES, ax_sink_user);
    }
}

/* ------------------------------------------------------------------- pump */

void ax_hle_set_sink(AxHleSink sink, void* user)
{
    ax_sink = sink;
    ax_sink_user = user;
}

void ax_hle_pump_frames(unsigned frames)
{
    unsigned i;

    if (!ax_initialized) {
        return;
    }
    for (i = 0; i < frames; i++) {
        __AXOutNewFrame(0);
    }
}

void ax_hle_pump_video_frame(void)
{
    /* 10 audio frames per 3 VI frames, held in thirds to avoid drift. */
    if (!ax_initialized) {
        return;
    }
    ax_video_accum += 10;
    while (ax_video_accum >= 3) {
        ax_video_accum -= 3;
        __AXOutNewFrame(0);
    }
}

unsigned ax_hle_frame_count(void)
{
    return ax_frame_count;
}

void ax_hle_hash_reset(void)
{
    ax_hash = 0xCBF29CE484222325ull;
}

uint64_t ax_hle_pcm_hash(void)
{
    return ax_hash;
}

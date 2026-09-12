/*
 * S1 audio/video backend: log-only stubs for the HSD AX driver and the THP
 * movie decoder.
 *
 * The AX/AI backends are real as of S5 (`native/audio/`, ADR-0013) and the
 * AR/ARQ backend is real as of S3 (`native/platform/ar.c`).  What remains
 * here is the HSD AX driver surface (replaced by compiling the decompilation's
 * `src/sysdolphin/baselib/axdriver.c` in S5.3) and THP.
 */
#include <dolphin/ai.h>
#include <dolphin/ar.h>
#include <dolphin/ax.h>

#include <sysdolphin/baselib/axdriver.h>

#include "decomp/boot/boot_triage.h"

/*
 * Prototypes from extern/dolphin/include/dolphin/thp/thp.h.  The header is
 * not included because its static-function declarations trigger
 * -Wunused-function in this host TU; the data type is copied verbatim so the
 * stub signatures stay exact.
 */
typedef struct {
    s32 val0;
    u16 val1;
    u16 _pad;
    u8 val2;
} THPDec_8032FD40_Data;

void THPInit(void);
s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV,
                   void* work);
s32 THPDec_8032FD40(THPDec_8032FD40_Data* arg0, u16 arg1);
s32 THPDec_8032F8D4(u8* data, THPDec_8032FD40_Data* out);
void THPDec_80331340(s32 arg0, void* arg1, void* arg2, void* arg3);
void THPDec_803313D0(s32 arg0, void* arg1, void* arg2, void* arg3, u32 arg4);

/* -------------------------------------------------------------- AR (S3)
 * ARInit/ARAlloc/ARFree live in platform/ar.c (host ARAM); ARQInit and
 * ARQPostRequest come from the compiled extern/dolphin ARQ queue.
 * AI/AX live in native/audio/ax_hle.c (S5). */

/* -------------------------------------------------------------- HSD AX driver
 * src/sysdolphin/baselib/axdriver.c is excluded from the PC build (it drives
 * the DSP hardware); S5 replaces these with the real mixer. */

bool AXDriverKeyOff(int vid)
{
    (void) vid;
    boot_triage_stub("AXDriverKeyOff", BOOT_CAT_AX);
    return false;
}

void HSD_AudioSFXKeyOffAll(void)
{
    boot_triage_stub("HSD_AudioSFXKeyOffAll", BOOT_CAT_AX);
}

void HSD_AudioSFXKeyOffTrack(int track)
{
    (void) track;
    boot_triage_stub("HSD_AudioSFXKeyOffTrack", BOOT_CAT_AX);
}

int AXDriver_8038CFF4(int sound_id, u8 volume, u8 pan, int track, int channel)
{
    (void) sound_id;
    (void) volume;
    (void) pan;
    (void) track;
    (void) channel;
    boot_triage_stub("AXDriver_8038CFF4", BOOT_CAT_AX);
    return -1;
}

bool AXDriver_8038D2B4(int vid, u8 pan)
{
    (void) vid;
    (void) pan;
    boot_triage_stub("AXDriver_8038D2B4", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038D3B8(s32 vid, u8 volume)
{
    (void) vid;
    (void) volume;
    boot_triage_stub("AXDriver_8038D3B8", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038D4E4(s32 vid, s16 pitch)
{
    (void) vid;
    (void) pitch;
    boot_triage_stub("AXDriver_8038D4E4", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038D914(s32 channel, s32 aux_bus, s8 send_level)
{
    (void) channel;
    (void) aux_bus;
    (void) send_level;
    boot_triage_stub("AXDriver_8038D914", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038D9D8(int vid)
{
    (void) vid;
    boot_triage_stub("AXDriver_8038D9D8", BOOT_CAT_AX);
    return false;
}

void AXDriver_8038DA70(const char* path, void (*callback)(void))
{
    (void) path;
    (void) callback;
    boot_triage_stub("AXDriver_8038DA70", BOOT_CAT_AX);
}

void AXDriver_8038DCFC(void)
{
    boot_triage_stub("AXDriver_8038DCFC", BOOT_CAT_AX);
}

s32 HSD_AudioGetAuxHeapSize(AXDriverAuxType type, void* param)
{
    (void) type;
    (void) param;
    boot_triage_stub("HSD_AudioGetAuxHeapSize", BOOT_CAT_AX);
    return 0;
}

bool AXDriver_8038E30C(s32 channel, s32 type, void* param, u8* heap,
                       size_t heap_size)
{
    (void) channel;
    (void) type;
    (void) param;
    (void) heap;
    (void) heap_size;
    boot_triage_stub("AXDriver_8038E30C", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038E37C(AXDriverAuxType type, void* param)
{
    (void) type;
    (void) param;
    boot_triage_stub("AXDriver_8038E37C", BOOT_CAT_AX);
    return false;
}

/*
 * Host ARAM model is defined below; the synth init prototype is copied from
 * src/sysdolphin/baselib/synth.c:1469 (synth.h has placeholder names).
 */
void HSD_SynthInit(int dsp_size, int voices, int stream_size, int bank_size);

void AXDriver_8038E498(int voices, int priority, int sample_rate,
                       int aram_size)
{
    boot_triage_stub("AXDriver_8038E498", BOOT_CAT_AX);
    /* The real axdriver initializes the HSD synth and then wires the AX
     * callbacks; only the synth half is meaningful without a DSP. */
    HSD_SynthInit(voices, priority, sample_rate, aram_size);
}

int AXDriver_8038E5D4(void)
{
    boot_triage_stub("AXDriver_8038E5D4", BOOT_CAT_AX);
    return 0;
}

int AXDriver_8038E5DC(void)
{
    boot_triage_stub("AXDriver_8038E5DC", BOOT_CAT_AX);
    return 0;
}

bool AXDriver_8038E6C0(int channel)
{
    (void) channel;
    boot_triage_stub("AXDriver_8038E6C0", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038E844(int channel)
{
    (void) channel;
    boot_triage_stub("AXDriver_8038E844", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038E8EC(const char* path, u8 volume, int track)
{
    (void) path;
    (void) volume;
    (void) track;
    boot_triage_stub("AXDriver_8038E8EC", BOOT_CAT_AX);
    return false;
}

bool AXDriverStop(void)
{
    boot_triage_stub("AXDriverStop", BOOT_CAT_AX);
    return false;
}

bool AXDriverPause(void)
{
    boot_triage_stub("AXDriverPause", BOOT_CAT_AX);
    return false;
}

bool AXDriverResume(void)
{
    boot_triage_stub("AXDriverResume", BOOT_CAT_AX);
    return false;
}

bool AXDriver_8038EA18(void)
{
    boot_triage_stub("AXDriver_8038EA18", BOOT_CAT_AX);
    return false;
}

/* --------------------------------------------------------------------- THP */

void THPInit(void)
{
    boot_triage_stub("THPInit", BOOT_CAT_THP);
}

s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV,
                   void* work)
{
    (void) file;
    (void) tileY;
    (void) tileU;
    (void) tileV;
    (void) work;
    boot_triage_stub("THPVideoDecode", BOOT_CAT_THP);
    return -1;
}

s32 THPDec_8032FD40(THPDec_8032FD40_Data* arg0, u16 arg1)
{
    (void) arg0;
    (void) arg1;
    boot_triage_stub("THPDec_8032FD40", BOOT_CAT_THP);
    return 0;
}

s32 THPDec_8032F8D4(u8* data, THPDec_8032FD40_Data* out)
{
    (void) data;
    (void) out;
    boot_triage_stub("THPDec_8032F8D4", BOOT_CAT_THP);
    return -1;
}

void THPDec_80331340(s32 arg0, void* arg1, void* arg2, void* arg3)
{
    (void) arg0;
    (void) arg1;
    (void) arg2;
    (void) arg3;
    boot_triage_stub("THPDec_80331340", BOOT_CAT_THP);
}

void THPDec_803313D0(s32 arg0, void* arg1, void* arg2, void* arg3, u32 arg4)
{
    (void) arg0;
    (void) arg1;
    (void) arg2;
    (void) arg3;
    (void) arg4;
    boot_triage_stub("THPDec_803313D0", BOOT_CAT_THP);
}

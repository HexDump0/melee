/*
 * S1 audio/video backend: log-only stubs for AI, AR, AX, the HSD AX driver
 * and the THP movie decoder.
 *
 * The boot only initializes these subsystems; nothing here produces sound or
 * video yet.  Each entry is recorded by the triage logger.  S5 replaces the
 * AX/DSP surface (P-501/ADR) and the AR/ARQ streaming backend.
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

/* ---------------------------------------------------------------------- AI */

void AIInit(u8* stack)
{
    (void) stack;
    boot_triage_stub("AIInit", BOOT_CAT_AX);
}

void AISetStreamVolLeft(u8 vol)
{
    (void) vol;
    boot_triage_stub("AISetStreamVolLeft", BOOT_CAT_AX);
}

void AISetStreamVolRight(u8 vol)
{
    (void) vol;
    boot_triage_stub("AISetStreamVolRight", BOOT_CAT_AX);
}

void AISetDSPSampleRate(u32 rate)
{
    (void) rate;
    boot_triage_stub("AISetDSPSampleRate", BOOT_CAT_AX);
}

/* ---------------------------------------------------------------------- AR */

/*
 * Host ARAM model: a bump allocator over a 16 MB window starting at
 * 0x10000000.  The synth's bank allocator only checks addresses and sizes
 * (HSD_SynthSFXAllocateBank), never dereferences them in S1, so a monotonic
 * host window is enough for the boot to lay out its banks exactly once.
 */
#define ARAM_BASE 0x10000000u
#define ARAM_SIZE (16u * 1024u * 1024u)

static u32 aram_next;

u32 ARInit(u32* stack_index_addr, u32 num_entries)
{
    (void) stack_index_addr;
    (void) num_entries;
    boot_triage_real("ARInit", BOOT_CAT_AR);
    aram_next = ARAM_BASE;
    return ARAM_BASE;
}

u32 ARGetSize(void)
{
    return ARAM_SIZE;
}

u32 ARAlloc(u32 length)
{
    u32 ptr = (aram_next + 31) & ~31u;

    boot_triage_real("ARAlloc", BOOT_CAT_AR);
    if (ptr + length > ARAM_BASE + ARAM_SIZE) {
        boot_triage_note("[boot] ARAlloc: host ARAM exhausted (%u bytes)\n",
                         length);
        return 0;
    }
    aram_next = ptr + length;
    return ptr;
}

u32 ARFree(u32* length)
{
    (void) length;
    boot_triage_stub("ARFree", BOOT_CAT_AR);
    return 0;
}

void ARQInit(void)
{
    boot_triage_stub("ARQInit", BOOT_CAT_AR);
}

void ARQPostRequest(struct ARQRequest* request, u32 owner, u32 type,
                    u32 priority, u32 source, u32 dest, u32 length,
                    ARQCallback callback)
{
    (void) request;
    (void) owner;
    (void) type;
    (void) priority;
    (void) source;
    (void) dest;
    (void) length;
    (void) callback;
    boot_triage_stub("ARQPostRequest", BOOT_CAT_AR);
}

/* ---------------------------------------------------------------------- AX */

void AXInit(void)
{
    boot_triage_stub("AXInit", BOOT_CAT_AX);
}

void AXRegisterCallback(void (*callback)(void))
{
    (void) callback;
    boot_triage_stub("AXRegisterCallback", BOOT_CAT_AX);
}

AXVPB* AXAcquireVoice(u32 priority, void (*callback)(void*), u32 userContext)
{
    (void) priority;
    (void) callback;
    (void) userContext;
    boot_triage_stub("AXAcquireVoice", BOOT_CAT_AX);
    return NULL;
}

void AXSetVoiceAddr(AXVPB* p, AXPBADDR* addr)
{
    (void) p;
    (void) addr;
    boot_triage_stub("AXSetVoiceAddr", BOOT_CAT_AX);
}

void AXSetVoiceAdpcm(AXVPB* p, AXPBADPCM* adpcm)
{
    (void) p;
    (void) adpcm;
    boot_triage_stub("AXSetVoiceAdpcm", BOOT_CAT_AX);
}

void AXSetVoiceAdpcmLoop(AXVPB* p, AXPBADPCMLOOP* adpcmloop)
{
    (void) p;
    (void) adpcmloop;
    boot_triage_stub("AXSetVoiceAdpcmLoop", BOOT_CAT_AX);
}

void AXSetVoiceCurrentAddr(AXVPB* p, u32 addr)
{
    (void) p;
    (void) addr;
    boot_triage_stub("AXSetVoiceCurrentAddr", BOOT_CAT_AX);
}

void AXSetVoiceEndAddr(AXVPB* p, u32 addr)
{
    (void) p;
    (void) addr;
    boot_triage_stub("AXSetVoiceEndAddr", BOOT_CAT_AX);
}

void AXSetVoiceLoop(AXVPB* p, u16 loop)
{
    (void) p;
    (void) loop;
    boot_triage_stub("AXSetVoiceLoop", BOOT_CAT_AX);
}

void AXSetVoiceLoopAddr(AXVPB* p, u32 addr)
{
    (void) p;
    (void) addr;
    boot_triage_stub("AXSetVoiceLoopAddr", BOOT_CAT_AX);
}

void AXSetVoicePriority(AXVPB* p, u32 priority)
{
    (void) p;
    (void) priority;
    boot_triage_stub("AXSetVoicePriority", BOOT_CAT_AX);
}

void AXSetVoiceSrc(AXVPB* p, AXPBSRC* src_)
{
    (void) p;
    (void) src_;
    boot_triage_stub("AXSetVoiceSrc", BOOT_CAT_AX);
}

void AXSetVoiceSrcRatio(AXVPB* p, float ratio)
{
    (void) p;
    (void) ratio;
    boot_triage_stub("AXSetVoiceSrcRatio", BOOT_CAT_AX);
}

void AXSetVoiceState(AXVPB* p, u16 state)
{
    (void) p;
    (void) state;
    boot_triage_stub("AXSetVoiceState", BOOT_CAT_AX);
}

void AXSetVoiceVe(AXVPB* p, AXPBVE* ve)
{
    (void) p;
    (void) ve;
    boot_triage_stub("AXSetVoiceVe", BOOT_CAT_AX);
}

void AXSetVoiceVeDelta(AXVPB* p, s16 delta)
{
    (void) p;
    (void) delta;
    boot_triage_stub("AXSetVoiceVeDelta", BOOT_CAT_AX);
}

void AXFreeVoice(AXVPB* p)
{
    (void) p;
    boot_triage_stub("AXFreeVoice", BOOT_CAT_AX);
}

void AXSetVoiceItdOn(AXVPB* p)
{
    (void) p;
    boot_triage_stub("AXSetVoiceItdOn", BOOT_CAT_AX);
}

void AXSetVoiceItdTarget(AXVPB* p, u16 lShift, u16 rShift)
{
    (void) p;
    (void) lShift;
    (void) rShift;
    boot_triage_stub("AXSetVoiceItdTarget", BOOT_CAT_AX);
}

void AXSetVoiceMix(AXVPB* p, AXPBMIX* mix)
{
    (void) p;
    (void) mix;
    boot_triage_stub("AXSetVoiceMix", BOOT_CAT_AX);
}

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

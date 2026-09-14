/*
 * S5 AX mixer unit test (ADR-0013).
 *
 * Runs the compiled SDK AX bookkeeping + `native/audio/` mixer without a
 * disc, a GPU or a sound device.  The fixture is a synthetic ADPCM frame
 * (predictor 0 / scale 0 / zero coefficients), so this contains no game data
 * (ADR-0005).
 *
 * Checks:
 *   - AXInit/AXRegisterCallback install the frame callback; the pump invokes
 *     it exactly once per 5 ms frame,
 *   - ADPCM decode reproduces the known nibble ramp,
 *   - the 16.16 SRC ratio resamples (0.5x holds each sample for two outputs),
 *   - a looped voice wraps at endAddress and stays in state 1,
 *   - a one-shot voice reaches endAddress, goes silent and the state
 *     write-back reaches the user PB (the `__AXServiceVPB` sync == 0 path),
 *   - the PCM hash is stable for identical pump runs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dolphin/ax.h>
#include <dolphin/axfx.h>
#include <dolphin/os.h>

#include "audio/ax_hle.h"
#include "decomp/boot/boot_triage.h"
#include "platform/ssm.h"

/* ------------------------------------------------------- host ARAM backing */

#define TEST_ARAM_SIZE (16u * 1024u * 1024u)

static unsigned char test_aram[TEST_ARAM_SIZE];

unsigned char* platform_aram_base(void)
{
    return test_aram;
}

unsigned platform_aram_size(void)
{
    return TEST_ARAM_SIZE;
}

/* axfx.c links its default heap hooks even when the test installs its own;
 * satisfy their OS-heap references without pulling in the allocator. */
volatile OSHeapHandle __OSCurrHeap = -1;
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

/* ----------------------------------------------------------------- fixture */

/* One DSP-ADPCM frame: coefficient index 0, scale 0, zero coefficients =>
 * each nibble is the sample value.  8 bytes = header + 7 data bytes = 14
 * samples (-7..6). */
static const unsigned char ramp_frame[8] = { 0x00, 0x9A, 0xBC, 0xDE,
                                             0xF0, 0x12, 0x34, 0x56 };
static const int ramp_expected[14] = { -7, -6, -5, -4, -3, -2, -1, 0,
                                       1,  2,  3,  4,  5,  6 };

#define AX_FRAME_SAMPLES 160

static int callback_count;

static void frame_callback(void)
{
    callback_count++;
}

static s16 captured[AX_FRAME_SAMPLES * 2];

static void capture_sink(const s16* pcm, unsigned frames, void* user)
{
    (void) user;
    if (frames > AX_FRAME_SAMPLES) {
        frames = AX_FRAME_SAMPLES;
    }
    memcpy(captured, pcm, frames * 2 * sizeof(s16));
}

static int failures;

static void check(int ok, const char* what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* A voice playing `test_aram` from byte 0 with the given end/loop addresses
 * (game domain: byte * 2 + 2). */
static AXVPB* setup_voice(u16 end_addr, u16 loop_flag, u16 loop_addr,
                          u32 ratio)
{
    AXVPB* voice = AXAcquireVoice(1, NULL, 0);
    AXPBADDR addr;
    AXPBADPCM adpcm;
    AXPBADPCMLOOP adpcmloop;
    AXPBMIX mix;
    AXPBSRC src;
    AXPBVE ve;

    check(voice != NULL, "AXAcquireVoice");
    if (voice == NULL) {
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.loopFlag = loop_flag;
    addr.format = 0;
    addr.loopAddressLo = loop_addr;
    addr.endAddressLo = end_addr;
    addr.currentAddressLo = 2;
    AXSetVoiceAddr(voice, &addr);

    memset(&adpcm, 0, sizeof(adpcm));
    AXSetVoiceAdpcm(voice, &adpcm);

    memset(&adpcmloop, 0, sizeof(adpcmloop));
    AXSetVoiceAdpcmLoop(voice, &adpcmloop);

    memset(&mix, 0, sizeof(mix));
    mix.vL = 0x8000; /* 1.0 in Q15: keep the fixture arithmetic exact */
    mix.vR = 0x8000;
    AXSetVoiceMix(voice, &mix);

    memset(&src, 0, sizeof(src));
    src.ratioHi = (u16) (ratio >> 16);
    src.ratioLo = (u16) ratio;
    AXSetVoiceSrc(voice, &src);

    memset(&ve, 0, sizeof(ve));
    ve.currentVolume = 0x8000;
    AXSetVoiceVe(voice, &ve);

    AXSetVoiceState(voice, 1);
    return voice;
}

static int near(int a, int b)
{
    int d = a - b;
    return d >= -1 && d <= 1;
}

/* S5.5: the `.ssm` record converter must leave every field in host order,
 * including Hi/Lo address pairs (a u32 swap would reverse them), and must
 * resume across DVD read boundaries.  Each read goes to its own buffer, like
 * the DVD backend delivers them. */
static void test_ssm_conversion(void)
{
    static const unsigned char file_image[0x58] = {
        /* header */
        0x00, 0x00, 0x00, 0x48, /* table size */
        0x00, 0x00, 0x10, 0x00, /* sample bytes */
        0x00, 0x00, 0x00, 0x01, /* groups */
        0x00, 0x00, 0x00, 0x00, /* base */
        /* group: n=1, rate=16000 */
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x3E, 0x80,
        /* entry: loop flag 1, format 0, loop 2, end 0x1234, cur 2 */
        0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x12, 0x34,
        0x00, 0x00, 0x00, 0x02,
        /* AXPBADPCM: first pair 0x0102, rest zero */
        0x01, 0x02,
        /* 19 more zero u16 coefficients + gain/ps/yn1/yn2 */
    };
    unsigned char chunk1[0x20];
    unsigned char chunk2[0x38];
    SsmStreamTable table;

    memcpy(chunk1, file_image, sizeof(chunk1));
    memcpy(chunk2, file_image + 0x20, sizeof(chunk2));
    memcpy(chunk2 + 0x30, (u16[]) { 0x1111, 0x2222, 0x3333 }, 6);

    ssm_stream_init(&table);
    ssm_fix_read(chunk1, 0, sizeof(chunk1), &table);
    ssm_fix_read(chunk2, 0x20, sizeof(chunk2), &table);

    check(*(u32*) (chunk1 + 0x00) == 0x48, "ssm table size");
    check(*(u32*) (chunk1 + 0x08) == 1, "ssm group count");
    check(*(u32*) (chunk1 + 0x10) == 1, "ssm group n");
    check(*(u32*) (chunk1 + 0x14) == 16000, "ssm sample rate");
    check(*(u16*) (chunk1 + 0x18) == 1, "ssm loop flag");
    check(*(u16*) (chunk1 + 0x1C) == 0 && *(u16*) (chunk1 + 0x1E) == 2,
          "ssm loop address pair");
    check(*(u16*) (chunk2 + 0x00) == 0 && *(u16*) (chunk2 + 0x02) == 0x1234,
          "ssm end address pair");
    check(*(u16*) (chunk2 + 0x04) == 0 && *(u16*) (chunk2 + 0x06) == 2,
          "ssm current address pair");
    check(*(u16*) (chunk2 + 0x08) == 0x0102, "ssm ADPCM coefficient");
    check(*(u16*) (chunk2 + 0x30) == 0x1111 &&
              *(u16*) (chunk2 + 0x32) == 0x2222 &&
              *(u16*) (chunk2 + 0x34) == 0x3333,
          "ssm ADPCMLOOP fields");
}

/* P-638: the reverb_hi and chorus replacement TU (Melee registers neither)
 * must create their delay lines, process a buffer without touching memory it
 * does not own, re-create on Settings and free on Shutdown.  The processing
 * itself has no retail oracle (the effects are DSP microcode), so the checks
 * are structural: init/settings/shutdown succeed, the mix is finite and
 * deterministic, and ASan sees every allocation freed. */
static void* effect_alloc(unsigned long size)
{
    return malloc(size);
}

static void effect_free(void* ptr)
{
    free(ptr);
}

static void test_axfx_effects(void)
{
    struct AXFX_REVERBHI rev;
    struct AXFX_CHORUS chorus;
    long left[160];
    long right[160];
    long sur[160];
    long first_left[160];
    struct AXFX_BUFFERUPDATE update;
    unsigned i;

    AXFXSetHooks(effect_alloc, effect_free);

    update.left = left;
    update.right = right;
    update.surround = sur;
    for (i = 0; i < 160; i++) {
        left[i] = (long) (i * 977);
        right[i] = (long) (i * -613);
        sur[i] = (long) (i * 311);
    }

    memset(&rev, 0, sizeof(rev));
    rev.coloration = 0.5f;
    rev.time = 1.0f;
    rev.mix = 0.5f;
    rev.damping = 0.5f;
    rev.preDelay = 0.01f;
    rev.crosstalk = 0.25f;
    check(AXFXReverbHiInit(&rev) == 1, "reverb_hi init");
    AXFXReverbHiCallback(&update, &rev);
    memcpy(first_left, left, sizeof(first_left));
    check(left[100] != (long) (100 * 977), "reverb_hi processes the buffer");
    for (i = 0; i < 160; i++) {
        if (left[i] > (1L << 26) || left[i] < -(1L << 26) ||
            right[i] > (1L << 26) || right[i] < -(1L << 26) ||
            sur[i] > (1L << 26) || sur[i] < -(1L << 26))
        {
            break;
        }
    }
    check(i == 160, "reverb_hi output is bounded");
    check(AXFXReverbHiSettings(&rev) == 1, "reverb_hi settings");
    /* The modified delay lines start empty: the same input yields a
     * different tail than the first pass. */
    for (i = 0; i < 160; i++) {
        left[i] = (long) (i * 977);
    }
    AXFXReverbHiCallback(&update, &rev);
    check(memcmp(first_left, left, sizeof(first_left)) != 0,
          "reverb_hi settings rebuild the work buffers");
    check(AXFXReverbHiShutdown(&rev) == 1, "reverb_hi shutdown");

    memset(&chorus, 0, sizeof(chorus));
    chorus.baseDelay = 5;
    chorus.variation = 13;
    chorus.period = 100;
    check(AXFXChorusInit(&chorus) == 1, "chorus init");
    memcpy(first_left, left, sizeof(first_left));
    AXFXChorusCallback(&update, &chorus);
    check(memcmp(first_left, left, sizeof(first_left)) != 0,
          "chorus writes the buffer");
    for (i = 0; i < 160; i++) {
        left[i] = (long) (i * 977);
        right[i] = (long) (i * -613);
        sur[i] = (long) (i * 311);
    }
    memcpy(first_left, left, sizeof(first_left));
    AXFXChorusCallback(&update, &chorus);
    check(memcmp(first_left, left, sizeof(first_left)) != 0,
          "chorus output advances with the ring");
    check(AXFXChorusSettings(&chorus) == 1, "chorus settings");
    check(AXFXChorusShutdown(&chorus) == 1, "chorus shutdown");
}

int main(void)
{
    AXVPB* voice;
    unsigned i;

    test_ssm_conversion();

    boot_triage_init(stderr, 0, 0);
    OSInit();
    AXInit();
    AXRegisterCallback(frame_callback);
    ax_hle_set_sink(capture_sink, NULL);
    ax_hle_hash_reset();

    memset(test_aram, 0, sizeof(test_aram));
    memcpy(test_aram, ramp_frame, sizeof(ramp_frame));

    /* 1. One-shot ADPCM ramp at ratio 1.0. */
    callback_count = 0;
    voice = setup_voice(18, 0, 2, 0x10000);
    ax_hle_pump_frames(1);
    check(callback_count == 1, "callback fires once per frame");
    for (i = 0; i < 14; i++) {
        if (!near(captured[i * 2], ramp_expected[i])) {
            break;
        }
    }
    check(i == 14, "ADPCM ramp decodes");
    check(captured[14 * 2] == 0 && captured[14 * 2 + 1] == 0,
          "one-shot goes silent after end");
    ax_hle_pump_frames(1);
    check(voice != NULL && voice->pb.state == 0,
          "state write-back reaches the user PB");
    if (voice != NULL) {
        AXFreeVoice(voice);
    }

    /* 2. Looped voice wraps at endAddress and stays in state 1. */
    memcpy(test_aram + 8, ramp_frame, sizeof(ramp_frame));
    callback_count = 0;
    voice = setup_voice(34, 1, 2, 0x10000);
    ax_hle_pump_frames(1);
    for (i = 0; i < 14; i++) {
        if (!near(captured[(14 + i) * 2], ramp_expected[i])) {
            break;
        }
    }
    check(i == 14, "looped voice replays the loop");
    check(voice != NULL && voice->pb.state == 1, "looping voice stays active");
    if (voice != NULL) {
        AXSetVoiceState(voice, 0);
        ax_hle_pump_frames(1);
        AXFreeVoice(voice);
    }

    /* 3. SRC ratio 0.5 holds each input sample for two outputs. */
    voice = setup_voice(18, 0, 2, 0x8000);
    ax_hle_pump_frames(1);
    check(near(captured[0], -7) && near(captured[2], -7) &&
              near(captured[4], -6) && near(captured[6], -6),
          "0.5x SRC resamples");
    if (voice != NULL) {
        AXFreeVoice(voice);
    }

    /* 4. Same pump run -> same PCM hash. */
    {
        u64 hash_a;
        u64 hash_b;

        ax_hle_hash_reset();
        ax_hle_pump_frames(5);
        hash_a = ax_hle_pcm_hash();
        ax_hle_hash_reset();
        ax_hle_pump_frames(5);
        hash_b = ax_hle_pcm_hash();
        check(hash_a == hash_b, "hash is stable across identical runs");
    }

    /* 5. Unused AXFX effects (P-638). */
    test_axfx_effects();

    if (failures == 0) {
        printf("test_audio: PASS (%u frames, callback total %d)\n",
               ax_hle_frame_count(), callback_count);
        return 0;
    }
    printf("test_audio: FAIL (%d checks)\n", failures);
    return 1;
}

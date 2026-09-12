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
#include <string.h>

#include <dolphin/ax.h>
#include <dolphin/os.h>

#include "audio/ax_hle.h"
#include "decomp/boot/boot_triage.h"

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

int main(void)
{
    AXVPB* voice;
    unsigned i;

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

    if (failures == 0) {
        printf("test_audio: PASS (%u frames, callback total %d)\n",
               ax_hle_frame_count(), callback_count);
        return 0;
    }
    printf("test_audio: FAIL (%d checks)\n", failures);
    return 1;
}

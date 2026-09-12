/*
 * S5 AXFX replacement TU (ADR-0011 rule 3).
 *
 * `extern/dolphin/src/dolphin/axfx/{reverb_std,reverb_hi,chorus}.c` are
 * hybrid C/MWCC-asm files and cannot be compiled by GCC on the host.  This
 * file provides the same symbols:
 *
 *   - `reverb_std` is a full port.  The C scaffolding (create/modify/free)
 *     follows the upstream file; the asm `HandleReverb` is transcribed to C
 *     below (the only asm in the file).  The algorithm is a per-channel
 *     pre-delay -> two parallel damped comb filters -> two allpass filters,
 *     mixed dry/wet exactly as the asm computes
 *     (`out = 0.6*(1-level)*in + 0.6*level*allpass`).
 *   - `reverb_hi` and `chorus` are not registered by Melee (only
 *     `AXFXReverbStd` and `AXFXDelay` are; see `lbAudioAx_8002838C`), so
 *     their `Init` reports failure and no callback is installed.  The entry
 *     points exist because `AXDriverSetupAux` references them.
 *
 * The work structs and parameter semantics come from
 * `extern/dolphin/include/dolphin/axfx.h`.
 */
#include <dolphin.h>
#include <dolphin/ax.h>
#include <dolphin/axfx.h>

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------ reverb (std) */

static void DLsetdelay(struct AXFX_REVSTD_DELAYLINE* dl, long lag)
{
    dl->outPoint = dl->inPoint - (lag * 4);
    while (dl->outPoint < 0) {
        dl->outPoint += dl->length;
    }
}

static void DLcreate(struct AXFX_REVSTD_DELAYLINE* dl, long max_length)
{
    dl->length = (max_length * 4);
    dl->inputs = __AXFXAlloc(max_length * 4);
    memset(dl->inputs, 0, max_length * 4);
    dl->lastOutput = 0.0f;
    DLsetdelay(dl, max_length >> 1);
    dl->inPoint = 0;
    dl->outPoint = 0;
}

static void DLdelete(struct AXFX_REVSTD_DELAYLINE* dl)
{
    __AXFXFree(dl->inputs);
}

/* extern/dolphin/src/dolphin/axfx/reverb_std.c: HandleReverb.
 *
 * The asm treats sptr as three contiguous 160-sample channels (left, right,
 * surround) and each 32-bit word as an integer sample: it converts to float
 * on load (`(x ^ 0x80000000) - i2fMagic`), runs the filter chain in float and
 * converts back with `fctiwz` (truncating) on store.  Delay-line cursors are
 * byte offsets into `inputs` and wrap at `length` (bytes).
 */
static void handle_reverb(long* sptr, struct AXFX_REVSTD_WORK* rv)
{
    const float k06 = 0.6f;
    const float k03 = 0.3f;
    float ap = rv->allPassCoeff;
    float damping = rv->damping;
    float level = rv->level;
    float wet = level * k06;
    float dry = k06 - wet;
    int k;

    for (k = 0; k < 3; k++) {
        struct AXFX_REVSTD_DELAYLINE* c0 = &rv->C[k * 2];
        struct AXFX_REVSTD_DELAYLINE* c1 = &rv->C[k * 2 + 1];
        struct AXFX_REVSTD_DELAYLINE* ap0 = &rv->AP[k * 2];
        struct AXFX_REVSTD_DELAYLINE* ap1 = &rv->AP[k * 2 + 1];
        float coef0 = rv->combCoef[k * 2];
        float coef1 = rv->combCoef[k * 2 + 1];
        s32* io = (s32*) (sptr + k * 160);
        float lp = rv->lpLastout[k];
        float c0_last = c0->lastOutput;
        float c1_last = c1->lastOutput;
        float ap0_last = ap0->lastOutput;
        float ap1_last = ap1->lastOutput;
        long c0_in = c0->inPoint;
        long c0_out = c0->outPoint;
        long c1_in = c1->inPoint;
        long c1_out = c1->outPoint;
        long ap0_in = ap0->inPoint;
        long ap0_out = ap0->outPoint;
        long ap1_in = ap1->inPoint;
        long ap1_out = ap1->outPoint;
        float* pre_line = rv->preDelayLine[k];
        float* pre_ptr = rv->preDelayPtr[k];
        long pre_time = rv->preDelayTime;
        int t;

        for (t = 0; t < 160; t++) {
            float x = (float) io[t];
            float pre = x;
            float c0w;
            float c1w;
            float sum;
            float ap0_in_val;
            float ap0_out_val;
            float ap0_delayed;
            float y;
            float ap1_in_val;
            float ap1_out_val;
            float ap1_delayed;
            float out;

            if (pre_time != 0) {
                pre = *pre_ptr;
                *pre_ptr = x;
                pre_ptr++;
                if (pre_ptr >= pre_line + (pre_time - 1)) {
                    pre_ptr = pre_line;
                }
            }

            c0w = coef0 * c0_last + pre;
            ((float*) c0->inputs)[c0_in >> 2] = c0w;
            c0_in += 4;
            if (c0_in >= c0->length) {
                c0_in = 0;
            }
            c1w = coef1 * c1_last + pre;
            ((float*) c1->inputs)[c1_in >> 2] = c1w;
            c1_in += 4;
            if (c1_in >= c1->length) {
                c1_in = 0;
            }

            c0_last = ((float*) c0->inputs)[c0_out >> 2];
            c0_out += 4;
            if (c0_out >= c0->length) {
                c0_out = 0;
            }
            c1_last = ((float*) c1->inputs)[c1_out >> 2];
            c1_out += 4;
            if (c1_out >= c1->length) {
                c1_out = 0;
            }
            sum = c0_last + c1_last;

            ap0_in_val = ap * ap0_last + sum;
            ((float*) ap0->inputs)[ap0_in >> 2] = ap0_in_val;
            ap0_in += 4;
            if (ap0_in >= ap0->length) {
                ap0_in = 0;
            }
            ap0_out_val = ap0_last - ap * ap0_in_val;
            ap0_delayed = ((float*) ap0->inputs)[ap0_out >> 2];
            ap0_out += 4;
            if (ap0_out >= ap0->length) {
                ap0_out = 0;
            }

            y = ap0_out_val * k03 + damping * lp;
            lp = y;

            ap1_in_val = ap * ap1_last + y;
            ((float*) ap1->inputs)[ap1_in >> 2] = ap1_in_val;
            ap1_in += 4;
            if (ap1_in >= ap1->length) {
                ap1_in = 0;
            }
            ap1_out_val = ap1_last - ap * ap1_in_val;
            ap1_delayed = ((float*) ap1->inputs)[ap1_out >> 2];
            ap1_out += 4;
            if (ap1_out >= ap1->length) {
                ap1_out = 0;
            }

            out = wet * ap1_out_val + dry * x;
            io[t] = (s32) out;

            ap0_last = ap0_delayed;
            ap1_last = ap1_delayed;
        }

        c0->inPoint = c0_in;
        c0->outPoint = c0_out;
        c1->inPoint = c1_in;
        c1->outPoint = c1_out;
        c0->lastOutput = c0_last;
        c1->lastOutput = c1_last;
        ap0->inPoint = ap0_in;
        ap0->outPoint = ap0_out;
        ap1->inPoint = ap1_in;
        ap1->outPoint = ap1_out;
        ap0->lastOutput = ap0_last;
        ap1->lastOutput = ap1_last;
        rv->lpLastout[k] = lp;
        rv->preDelayPtr[k] = pre_ptr;
    }
}

static int ReverbSTDCreate(struct AXFX_REVSTD_WORK* rv, float coloration,
                           float time, float mix, float damping,
                           float predelay)
{
    u8 i;
    u8 k;
    static long lens[4] = {
        0x000006FD,
        0x000007CF,
        0x000001B1,
        0x00000095,
    };

    if ((coloration < 0.0f) || (coloration > 1.0f) || (time < 0.01f) ||
        (time > 10.0f) || (mix < 0.0f) || (mix > 1.0f) || (damping < 0.0f) ||
        (damping > 1.0f) || (predelay < 0.0f) || (predelay > 0.1f))
    {
        return 0;
    }

    memset(rv, 0, sizeof(struct AXFX_REVSTD_WORK));
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 2; i++) {
            DLcreate(&rv->C[i + (k * 2)], lens[i] + 2);
            DLsetdelay(&rv->C[i + (k * 2)], lens[i]);
            /* G-051: never call the game's own powf. */
            rv->combCoef[i + (k * 2)] =
                (float) pow(10.0, (lens[i] * -3) / (32000.0 * time));
        }
        for (i = 0; i < 2; i++) {
            DLcreate(&rv->AP[i + (k * 2)], lens[i + 2] + 2);
            DLsetdelay(&rv->AP[i + (k * 2)], lens[i + 2]);
        }
        rv->lpLastout[k] = 0.0f;
    }
    rv->allPassCoeff = coloration;
    rv->level = mix;
    rv->damping = damping;
    if (rv->damping < 0.05f) {
        rv->damping = 0.05f;
    }
    rv->damping = (1.0f - (0.05f + (0.8f * rv->damping)));
    if (0.0f != predelay) {
        rv->preDelayTime = (32000.0f * predelay);
        for (i = 0; i < 3; i++) {
            rv->preDelayLine[i] = __AXFXAlloc(rv->preDelayTime * 4);
            memset(rv->preDelayLine[i], 0, rv->preDelayTime * 4);
            rv->preDelayPtr[i] = rv->preDelayLine[i];
        }
    } else {
        rv->preDelayTime = 0;
        for (i = 0; i < 3; i++) {
            rv->preDelayPtr[i] = 0;
            rv->preDelayLine[i] = 0;
        }
    }
    return 1;
}

static int ReverbSTDModify(struct AXFX_REVSTD_WORK* rv, float coloration,
                           float time, float mix, float damping,
                           float predelay)
{
    u8 i;

    if ((coloration < 0.0f) || (coloration > 1.0f) || (time < 0.01f) ||
        (time > 10.0f) || (mix < 0.0f) || (mix > 1.0f) || (damping < 0.0f) ||
        (damping > 1.0f) || (predelay < 0.0f) || (predelay > 100.0f))
    {
        return 0;
    }
    rv->allPassCoeff = coloration;
    rv->level = mix;
    rv->damping = damping;
    if (rv->damping < 0.05f) {
        rv->damping = 0.05f;
    }
    rv->damping = (1.0f - (0.05f + (0.8f * rv->damping)));
    for (i = 0; i < 6; i++) {
        DLdelete(&rv->AP[i]);
    }
    for (i = 0; i < 6; i++) {
        DLdelete(&rv->C[i]);
    }
    if (rv->preDelayTime) {
        for (i = 0; i < 3; i++) {
            __AXFXFree(rv->preDelayLine[i]);
        }
    }
    return ReverbSTDCreate(rv, coloration, time, mix, damping, predelay);
}

static void ReverbSTDFree(struct AXFX_REVSTD_WORK* rv)
{
    u8 i;

    for (i = 0; i < 6; i++) {
        DLdelete(&rv->AP[i]);
    }
    for (i = 0; i < 6; i++) {
        DLdelete(&rv->C[i]);
    }
    if (rv->preDelayTime) {
        for (i = 0; i < 3; i++) {
            __AXFXFree(rv->preDelayLine[i]);
        }
    }
}

int AXFXReverbStdInit(struct AXFX_REVERBSTD* rev)
{
    int ret;
    int old;

    old = OSDisableInterrupts();
    rev->tempDisableFX = 0;
    ret = ReverbSTDCreate(&rev->rv, rev->coloration, rev->time, rev->mix,
                          rev->damping, rev->preDelay);
    OSRestoreInterrupts(old);
    return ret;
}

int AXFXReverbStdShutdown(struct AXFX_REVERBSTD* rev)
{
    int old;

    old = OSDisableInterrupts();
    ReverbSTDFree(&rev->rv);
    OSRestoreInterrupts(old);
    return 1;
}

int AXFXReverbStdSettings(struct AXFX_REVERBSTD* rev)
{
    int old;

    old = OSDisableInterrupts();
    rev->tempDisableFX = 1;
    ReverbSTDModify(&rev->rv, rev->coloration, rev->time, rev->mix,
                    rev->damping, rev->preDelay);
    rev->tempDisableFX = 0;
    OSRestoreInterrupts(old);
    return 1;
}

void AXFXReverbStdCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                           struct AXFX_REVERBSTD* reverb)
{
    if (reverb->tempDisableFX == 0) {
        handle_reverb(bufferUpdate->left, &reverb->rv);
    }
}

/* ------------------------------------------------- reverb (hi) / chorus
 * Melee never registers either effect; the symbols exist for
 * AXDriverSetupAux's switch.  Init reports failure so no callback is
 * installed and no uninitialised work buffer is ever processed. */

int AXFXReverbHiInit(struct AXFX_REVERBHI* rev)
{
    (void) rev;
    return 0;
}

int AXFXReverbHiShutdown(struct AXFX_REVERBHI* rev)
{
    (void) rev;
    return 1;
}

int AXFXReverbHiSettings(struct AXFX_REVERBHI* rev)
{
    (void) rev;
    return 0;
}

void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                          struct AXFX_REVERBHI* reverb)
{
    (void) bufferUpdate;
    (void) reverb;
}

int AXFXChorusInit(struct AXFX_CHORUS* c)
{
    (void) c;
    return 0;
}

int AXFXChorusShutdown(struct AXFX_CHORUS* c)
{
    (void) c;
    return 1;
}

int AXFXChorusSettings(struct AXFX_CHORUS* c)
{
    (void) c;
    return 0;
}

void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                        struct AXFX_CHORUS* chorus)
{
    (void) bufferUpdate;
    (void) chorus;
}

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
 * `reverb_hi` and `chorus` are not registered by Melee (only
 * `AXFXReverbStd` and `AXFXDelay` are; see `lbAudioAx_8002838C`), but the
 * symbols exist for `AXDriverSetupAux` and future modes, and the SDK sources
 * are hybrid C/MWCC-asm.  The C scaffolding below follows the SDK files; the
 * asm handle functions are transcribed to C exactly like reverb_std above
 * (integer samples through i2fMagic, `fctiwz` truncation on store). */

static void hi_DLsetdelay(struct AXFX_REVHI_DELAYLINE* dl, long lag)
{
    dl->outPoint = dl->inPoint - (lag * 4);
    while (dl->outPoint < 0) {
        dl->outPoint += dl->length;
    }
}

static void hi_DLcreate(struct AXFX_REVHI_DELAYLINE* dl, long max_length)
{
    dl->length = (max_length * 4);
    dl->inputs = __AXFXAlloc(max_length << 2);
    memset(dl->inputs, 0, max_length << 2);
    dl->lastOutput = 0.0f;
    hi_DLsetdelay(dl, max_length >> 1);
    dl->inPoint = 0;
    dl->outPoint = 0;
}

static void hi_DLdelete(struct AXFX_REVHI_DELAYLINE* dl)
{
    __AXFXFree(dl->inputs);
}


static int ReverbHICreate(struct AXFX_REVHI_WORK* rv, float coloration,
                          float time, float mix, float damping, float preDelay,
                          float crosstalk)
{
    static long lens[8] = { 0x000006FD, 0x000007CF, 0x0000091D, 0x000001B1,
                            0x00000095, 0x0000002F, 0x00000049, 0x00000043 };
    u8 i;
    u8 k;

    if ((coloration < 0.0f) || (coloration > 1.0f) || (time < 0.01f) ||
        (time > 10.0f) || (mix < 0.0f) || (mix > 1.0f) || (crosstalk < 0.0f) ||
        (crosstalk > 1.0f) || (damping < 0.0f) || (damping > 1.0f) ||
        (preDelay < 0.0f) || (preDelay > 0.1f))
    {
        return 0;
    }
    memset(rv, 0, sizeof(struct AXFX_REVHI_WORK));
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 3; i++) {
            hi_DLcreate(&rv->C[i + (k * 3)], lens[i] + 2);
            hi_DLsetdelay(&rv->C[i + (k * 3)], lens[i]);
            rv->combCoef[i + (k * 3)] =
                (float) pow(10.0, (lens[i] * -3) / (32000.0 * time));
        }
        for (i = 0; i < 2; i++) {
            hi_DLcreate(&rv->AP[i + (k * 3)], lens[i + 3] + 2);
            hi_DLsetdelay(&rv->AP[i + (k * 3)], lens[i + 3]);
        }
        hi_DLcreate(&rv->AP[2 + (k * 3)], lens[k + 5] + 2);
        hi_DLsetdelay(&rv->AP[2 + (k * 3)], lens[k + 5]);
        rv->lpLastout[k] = 0.0f;
    }
    rv->allPassCoeff = coloration;
    rv->level = mix;
    rv->crosstalk = crosstalk;
    rv->damping = damping;
    if (rv->damping < 0.05f) {
        rv->damping = 0.05f;
    }
    rv->damping = (1.0f - (0.05f + (0.8f * rv->damping)));
    if (0.0f != preDelay) {
        rv->preDelayTime = (32000.0f * preDelay);
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

static int ReverbHIModify(struct AXFX_REVHI_WORK* rv, float coloration,
                          float time, float mix, float damping, float preDelay,
                          float crosstalk)
{
    u8 i;

    if ((coloration < 0.0f) || (coloration > 1.0f) || (time < 0.01f) ||
        (time > 10.0f) || (mix < 0.0f) || (mix > 1.0f) || (crosstalk < 0.0f) ||
        (crosstalk > 1.0f) || (damping < 0.0f) || (damping > 1.0f) ||
        (preDelay < 0.0f) || (preDelay > 100.0f))
    {
        return 0;
    }
    rv->allPassCoeff = coloration;
    rv->level = mix;
    rv->crosstalk = crosstalk;
    rv->damping = damping;
    if (rv->damping < 0.05f) {
        rv->damping = 0.05f;
    }
    rv->damping = (1.0f - (0.05f + (0.8f * rv->damping)));
    for (i = 0; i < 9; i++) {
        hi_DLdelete(&rv->AP[i]);
    }
    for (i = 0; i < 9; i++) {
        hi_DLdelete(&rv->C[i]);
    }
    if (rv->preDelayTime) {
        for (i = 0; i < 3; i++) {
            __AXFXFree(rv->preDelayLine[i]);
        }
    }
    return ReverbHICreate(rv, coloration, time, mix, damping, preDelay,
                          crosstalk);
}

/* extern/dolphin/src/dolphin/axfx/reverb_hi.c: DoCrossTalk.  Left is
 * `l*invcross + r*cross`; right is `0.6*(l*cross + r*invcross)` (the asm
 * scales only the right pair with `ps_muls0`). */
static void hi_crosstalk(long* l, long* r, float cross, float invcross)
{
    int i;

    for (i = 0; i < 160; i++) {
        float lv = (float) l[i];
        float rv = (float) r[i];
        l[i] = (long) (lv * invcross + rv * cross);
        r[i] = (long) (0.6f * (lv * cross + rv * invcross));
    }
}

/* extern/dolphin/src/dolphin/axfx/reverb_hi.c: HandleReverb.  One channel's
 * 160 samples: optional pre-delay -> three parallel feedback combs -> three
 * series allpasses -> one-pole low-pass -> dry/wet mix
 * (`out = 0.6*level*allpass + 0.6*(1-level)*in`).  Delay cursors are byte
 * offsets and wrap at `length`. */
static void handle_reverb_hi(long* sptr, struct AXFX_REVHI_WORK* rv, long k)
{
    const float k06 = 0.6f;
    const float k03 = 0.3f;
    float ap = rv->allPassCoeff;
    float damping = rv->damping;
    float wet = rv->level * k06;
    float dry = k06 - wet;
    struct AXFX_REVHI_DELAYLINE* c0 = &rv->C[k * 3];
    struct AXFX_REVHI_DELAYLINE* c1 = &rv->C[k * 3 + 1];
    struct AXFX_REVHI_DELAYLINE* c2 = &rv->C[k * 3 + 2];
    struct AXFX_REVHI_DELAYLINE* ap0 = &rv->AP[k * 3];
    struct AXFX_REVHI_DELAYLINE* ap1 = &rv->AP[k * 3 + 1];
    struct AXFX_REVHI_DELAYLINE* ap2 = &rv->AP[k * 3 + 2];
    float coef0 = rv->combCoef[k * 3];
    float coef1 = rv->combCoef[k * 3 + 1];
    float coef2 = rv->combCoef[k * 3 + 2];
    float lp = rv->lpLastout[k];
    float c0_last = c0->lastOutput;
    float c1_last = c1->lastOutput;
    float c2_last = c2->lastOutput;
    float ap0_last = ap0->lastOutput;
    float ap1_last = ap1->lastOutput;
    float ap2_last = ap2->lastOutput;
    long c0_in = c0->inPoint;
    long c0_out = c0->outPoint;
    long c1_in = c1->inPoint;
    long c1_out = c1->outPoint;
    long c2_in = c2->inPoint;
    long c2_out = c2->outPoint;
    long ap0_in = ap0->inPoint;
    long ap0_out = ap0->outPoint;
    long ap1_in = ap1->inPoint;
    long ap1_out = ap1->outPoint;
    long ap2_in = ap2->inPoint;
    long ap2_out = ap2->outPoint;
    float* pre_line = rv->preDelayLine[k];
    float* pre_ptr = rv->preDelayPtr[k];
    long pre_time = rv->preDelayTime;
    int t;

    for (t = 0; t < 160; t++) {
        float x = (float) sptr[t];
        float pre = x;
        float c0w;
        float c1w;
        float c2w;
        float c0d;
        float c1d;
        float c2d;
        float sum;
        float ap0_in_val;
        float ap0_out_val;
        float ap0_delayed;
        float ap1_in_val;
        float ap1_out_val;
        float ap1_delayed;
        float ap2_in_val;
        float ap2_out_val;
        float ap2_delayed;
        float tmp;
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
        c2w = coef2 * c2_last + pre;
        ((float*) c2->inputs)[c2_in >> 2] = c2w;
        c2_in += 4;
        if (c2_in >= c2->length) {
            c2_in = 0;
        }

        c0d = ((float*) c0->inputs)[c0_out >> 2];
        c0_out += 4;
        if (c0_out >= c0->length) {
            c0_out = 0;
        }
        c1d = ((float*) c1->inputs)[c1_out >> 2];
        c1_out += 4;
        if (c1_out >= c1->length) {
            c1_out = 0;
        }
        c2d = ((float*) c2->inputs)[c2_out >> 2];
        c2_out += 4;
        if (c2_out >= c2->length) {
            c2_out = 0;
        }
        c0_last = c0d;
        c1_last = c1d;
        c2_last = c2d;
        sum = c0d + c1d + c2d;

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
        ap0_last = ap0_delayed;

        ap1_in_val = ap * ap1_last + ap0_out_val;
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
        ap1_last = ap1_delayed;

        tmp = ap1_out_val * k03 + damping * lp;
        lp = tmp;

        ap2_in_val = ap * ap2_last + tmp;
        ((float*) ap2->inputs)[ap2_in >> 2] = ap2_in_val;
        ap2_in += 4;
        if (ap2_in >= ap2->length) {
            ap2_in = 0;
        }
        ap2_out_val = ap2_last - ap * ap2_in_val;
        ap2_delayed = ((float*) ap2->inputs)[ap2_out >> 2];
        ap2_out += 4;
        if (ap2_out >= ap2->length) {
            ap2_out = 0;
        }
        ap2_last = ap2_delayed;

        out = wet * ap2_out_val + dry * x;
        sptr[t] = (long) out;
    }

    c0->inPoint = c0_in;
    c0->outPoint = c0_out;
    c1->inPoint = c1_in;
    c1->outPoint = c1_out;
    c2->inPoint = c2_in;
    c2->outPoint = c2_out;
    c0->lastOutput = c0_last;
    c1->lastOutput = c1_last;
    c2->lastOutput = c2_last;
    ap0->inPoint = ap0_in;
    ap0->outPoint = ap0_out;
    ap1->inPoint = ap1_in;
    ap1->outPoint = ap1_out;
    ap2->inPoint = ap2_in;
    ap2->outPoint = ap2_out;
    ap0->lastOutput = ap0_last;
    ap1->lastOutput = ap1_last;
    ap2->lastOutput = ap2_last;
    rv->lpLastout[k] = lp;
    rv->preDelayPtr[k] = pre_ptr;
}

static void ReverbHIFree(struct AXFX_REVHI_WORK* rv)
{
    u8 i;

    for (i = 0; i < 9; i++) {
        hi_DLdelete(&rv->AP[i]);
    }
    for (i = 0; i < 9; i++) {
        hi_DLdelete(&rv->C[i]);
    }
    if (rv->preDelayTime) {
        for (i = 0; i < 3; i++) {
            __AXFXFree(rv->preDelayLine[i]);
        }
    }
}

int AXFXReverbHiInit(struct AXFX_REVERBHI* rev)
{
    int ret;
    int old;

    old = OSDisableInterrupts();
    rev->tempDisableFX = 0;
    ret = ReverbHICreate(&rev->rv, rev->coloration, rev->time, rev->mix,
                         rev->damping, rev->preDelay, rev->crosstalk);
    OSRestoreInterrupts(old);
    return ret;
}

int AXFXReverbHiShutdown(struct AXFX_REVERBHI* rev)
{
    int old;

    old = OSDisableInterrupts();
    ReverbHIFree(&rev->rv);
    OSRestoreInterrupts(old);
    return 1;
}

int AXFXReverbHiSettings(struct AXFX_REVERBHI* rev)
{
    int old;

    old = OSDisableInterrupts();
    rev->tempDisableFX = 1;
    ReverbHIModify(&rev->rv, rev->coloration, rev->time, rev->mix,
                   rev->damping, rev->preDelay, rev->crosstalk);
    rev->tempDisableFX = 0;
    OSRestoreInterrupts(old);
    return 1;
}

void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                          struct AXFX_REVERBHI* reverb)
{
    if (reverb->tempDisableFX == 0) {
        if (0.0f != reverb->rv.crosstalk) {
            hi_crosstalk(bufferUpdate->left, bufferUpdate->right,
                         0.5f * reverb->rv.crosstalk,
                         1.0f - (0.5f * reverb->rv.crosstalk));
        }
        handle_reverb_hi(bufferUpdate->left, &reverb->rv, 0);
        handle_reverb_hi(bufferUpdate->right, &reverb->rv, 1);
        handle_reverb_hi(bufferUpdate->surround, &reverb->rv, 2);
    }
}

/* --------------------------------------------------------------- chorus */

static const float rsmpTab12khz[512] = {
    0.097503662109f,  0.802215576172f, 0.101593017578f, -0.000976562500f,
    0.093505859375f,  0.802032470703f, 0.105804443359f, -0.001037597656f,
    0.089599609375f,  0.801696777344f, 0.110107421875f, -0.001159667969f,
    0.085784912109f,  0.801177978516f, 0.114471435547f, -0.001281738281f,
    0.082031250000f,  0.800476074219f, 0.118927001953f, -0.001403808594f,
    0.078369140625f,  0.799621582031f, 0.123474121094f, -0.001525878906f,
    0.074798583984f,  0.798614501953f, 0.128143310547f, -0.001647949219f,
    0.071350097656f,  0.797424316406f, 0.132873535156f, -0.001770019531f,
    0.067962646484f,  0.796051025391f, 0.137695312500f, -0.001922607422f,
    0.064697265625f,  0.794525146484f, 0.142608642578f, -0.002044677734f,
    0.061492919922f,  0.792846679688f, 0.147613525391f, -0.002197265625f,
    0.058349609375f,  0.790985107422f, 0.152709960938f, -0.002319335938f,
    0.055328369141f,  0.788940429688f, 0.157897949219f, -0.002471923828f,
    0.052368164062f,  0.786743164062f, 0.163177490234f, -0.002655029297f,
    0.049499511719f,  0.784423828125f, 0.168518066406f, -0.002807617188f,
    0.046722412109f,  0.781890869141f, 0.173980712891f, -0.002990722656f,
    0.044006347656f,  0.779205322266f, 0.179504394531f, -0.003143310547f,
    0.041412353516f,  0.776367187500f, 0.185119628906f, -0.003326416016f,
    0.038879394531f,  0.773376464844f, 0.190826416016f, -0.003509521484f,
    0.036407470703f,  0.770233154297f, 0.196594238281f, -0.003692626953f,
    0.034027099609f,  0.766937255859f, 0.202484130859f, -0.003875732422f,
    0.031738281250f,  0.763488769531f, 0.208435058594f, -0.004058837891f,
    0.029510498047f,  0.759857177734f, 0.214447021484f, -0.004272460938f,
    0.027374267578f,  0.756103515625f, 0.220550537109f, -0.004455566406f,
    0.025299072266f,  0.752197265625f, 0.226745605469f, -0.004669189453f,
    0.023315429688f,  0.748168945312f, 0.233001708984f, -0.004852294922f,
    0.021392822266f,  0.743988037109f, 0.239318847656f, -0.005065917969f,
    0.019561767578f,  0.739654541016f, 0.245727539062f, -0.005310058594f,
    0.017791748047f,  0.735198974609f, 0.252197265625f, -0.005523681641f,
    0.016052246094f,  0.730590820312f, 0.258728027344f, -0.005706787109f,
    0.014404296875f,  0.725860595703f, 0.265350341797f, -0.005920410156f,
    0.012817382812f,  0.721008300781f, 0.272033691406f, -0.006164550781f,
    0.011322021484f,  0.716003417969f, 0.278778076172f, -0.006378173828f,
    0.009887695312f,  0.710906982422f, 0.285583496094f, -0.006561279297f,
    0.008514404297f,  0.705657958984f, 0.292449951172f, -0.006774902344f,
    0.007202148438f,  0.700317382812f, 0.299346923828f, -0.007019042969f,
    0.005920410156f,  0.694854736328f, 0.306335449219f, -0.007232666016f,
    0.004699707031f,  0.689270019531f, 0.313385009766f, -0.007415771484f,
    0.003570556641f,  0.683563232422f, 0.320465087891f, -0.007629394531f,
    0.002471923828f,  0.677734375000f, 0.327606201172f, -0.007873535156f,
    0.001434326172f,  0.671844482422f, 0.334777832031f, -0.008087158203f,
    0.000457763672f,  0.665832519531f, 0.341979980469f, -0.008270263672f,
    -0.000488281250f, 0.659729003906f, 0.349243164062f, -0.008453369141f,
    -0.001342773438f, 0.653533935547f, 0.356567382812f, -0.008636474609f,
    -0.002166748047f, 0.647216796875f, 0.363891601562f, -0.008850097656f,
    -0.002960205078f, 0.640838623047f, 0.371276855469f, -0.009033203125f,
    -0.003692626953f, 0.634338378906f, 0.378692626953f, -0.009216308594f,
    -0.004364013672f, 0.627777099609f, 0.386138916016f, -0.009338378906f,
    -0.004974365234f, 0.621154785156f, 0.393615722656f, -0.009490966797f,
    -0.005584716797f, 0.614440917969f, 0.401092529297f, -0.009643554688f,
    -0.006134033203f, 0.607635498047f, 0.408599853516f, -0.009796142578f,
    -0.006652832031f, 0.600769042969f, 0.416107177734f, -0.009918212891f,
    -0.007141113281f, 0.593841552734f, 0.423645019531f, -0.010009765625f,
    -0.007568359375f, 0.586853027344f, 0.431213378906f, -0.010131835938f,
    -0.007965087891f, 0.579772949219f, 0.438751220703f, -0.010223388672f,
    -0.008331298828f, 0.572662353516f, 0.446319580078f, -0.010284423828f,
    -0.008666992188f, 0.565521240234f, 0.453887939453f, -0.010345458984f,
    -0.008972167969f, 0.558319091797f, 0.461456298828f, -0.010406494141f,
    -0.009216308594f, 0.551055908203f, 0.469024658203f, -0.010406494141f,
    -0.009460449219f, 0.543731689453f, 0.476593017578f, -0.010406494141f,
    -0.009674072266f, 0.536407470703f, 0.484130859375f, -0.010375976562f,
    -0.009857177734f, 0.529022216797f, 0.491668701172f, -0.010375976562f,
    -0.010009765625f, 0.521606445312f, 0.499176025391f, -0.010314941406f,
    -0.010131835938f, 0.514160156250f, 0.506683349609f, -0.010253906250f,
    -0.010253906250f, 0.506683349609f, 0.514160156250f, -0.010131835938f,
    -0.010314941406f, 0.499176025391f, 0.521606445312f, -0.010009765625f,
    -0.010375976562f, 0.491668701172f, 0.529022216797f, -0.009857177734f,
    -0.010375976562f, 0.484130859375f, 0.536407470703f, -0.009674072266f,
    -0.010406494141f, 0.476593017578f, 0.543731689453f, -0.009460449219f,
    -0.010406494141f, 0.469024658203f, 0.551055908203f, -0.009216308594f,
    -0.010406494141f, 0.461456298828f, 0.558319091797f, -0.008972167969f,
    -0.010345458984f, 0.453887939453f, 0.565521240234f, -0.008666992188f,
    -0.010284423828f, 0.446319580078f, 0.572662353516f, -0.008331298828f,
    -0.010223388672f, 0.438751220703f, 0.579772949219f, -0.007965087891f,
    -0.010131835938f, 0.431213378906f, 0.586853027344f, -0.007568359375f,
    -0.010009765625f, 0.423645019531f, 0.593841552734f, -0.007141113281f,
    -0.009918212891f, 0.416107177734f, 0.600769042969f, -0.006652832031f,
    -0.009796142578f, 0.408599853516f, 0.607635498047f, -0.006134033203f,
    -0.009643554688f, 0.401092529297f, 0.614440917969f, -0.005584716797f,
    -0.009490966797f, 0.393615722656f, 0.621154785156f, -0.004974365234f,
    -0.009338378906f, 0.386138916016f, 0.627777099609f, -0.004364013672f,
    -0.009216308594f, 0.378692626953f, 0.634338378906f, -0.003692626953f,
    -0.009033203125f, 0.371276855469f, 0.640838623047f, -0.002960205078f,
    -0.008850097656f, 0.363891601562f, 0.647216796875f, -0.002166748047f,
    -0.008636474609f, 0.356567382812f, 0.653533935547f, -0.001342773438f,
    -0.008453369141f, 0.349243164062f, 0.659729003906f, -0.000488281250f,
    -0.008270263672f, 0.341979980469f, 0.665832519531f, 0.000457763672f,
    -0.008087158203f, 0.334777832031f, 0.671844482422f, 0.001434326172f,
    -0.007873535156f, 0.327606201172f, 0.677734375000f, 0.002471923828f,
    -0.007629394531f, 0.320465087891f, 0.683563232422f, 0.003570556641f,
    -0.007415771484f, 0.313385009766f, 0.689270019531f, 0.004699707031f,
    -0.007232666016f, 0.306335449219f, 0.694854736328f, 0.005920410156f,
    -0.007019042969f, 0.299346923828f, 0.700317382812f, 0.007202148438f,
    -0.006774902344f, 0.292449951172f, 0.705657958984f, 0.008514404297f,
    -0.006561279297f, 0.285583496094f, 0.710906982422f, 0.009887695312f,
    -0.006378173828f, 0.278778076172f, 0.716003417969f, 0.011322021484f,
    -0.006164550781f, 0.272033691406f, 0.721008300781f, 0.012817382812f,
    -0.005920410156f, 0.265350341797f, 0.725860595703f, 0.014404296875f,
    -0.005706787109f, 0.258728027344f, 0.730590820312f, 0.016052246094f,
    -0.005523681641f, 0.252197265625f, 0.735198974609f, 0.017791748047f,
    -0.005310058594f, 0.245727539062f, 0.739654541016f, 0.019561767578f,
    -0.005065917969f, 0.239318847656f, 0.743988037109f, 0.021392822266f,
    -0.004852294922f, 0.233001708984f, 0.748168945312f, 0.023315429688f,
    -0.004669189453f, 0.226745605469f, 0.752197265625f, 0.025299072266f,
    -0.004455566406f, 0.220550537109f, 0.756103515625f, 0.027374267578f,
    -0.004272460938f, 0.214447021484f, 0.759857177734f, 0.029510498047f,
    -0.004058837891f, 0.208435058594f, 0.763488769531f, 0.031738281250f,
    -0.003875732422f, 0.202484130859f, 0.766937255859f, 0.034027099609f,
    -0.003692626953f, 0.196594238281f, 0.770233154297f, 0.036407470703f,
    -0.003509521484f, 0.190826416016f, 0.773376464844f, 0.038879394531f,
    -0.003326416016f, 0.185119628906f, 0.776367187500f, 0.041412353516f,
    -0.003143310547f, 0.179504394531f, 0.779205322266f, 0.044006347656f,
    -0.002990722656f, 0.173980712891f, 0.781890869141f, 0.046722412109f,
    -0.002807617188f, 0.168518066406f, 0.784423828125f, 0.049499511719f,
    -0.002655029297f, 0.163177490234f, 0.786743164062f, 0.052368164062f,
    -0.002471923828f, 0.157897949219f, 0.788940429688f, 0.055328369141f,
    -0.002319335938f, 0.152709960938f, 0.790985107422f, 0.058349609375f,
    -0.002197265625f, 0.147613525391f, 0.792846679688f, 0.061492919922f,
    -0.002044677734f, 0.142608642578f, 0.794525146484f, 0.064697265625f,
    -0.001922607422f, 0.137695312500f, 0.796051025391f, 0.067962646484f,
    -0.001770019531f, 0.132873535156f, 0.797424316406f, 0.071350097656f,
    -0.001647949219f, 0.128143310547f, 0.798614501953f, 0.074798583984f,
    -0.001525878906f, 0.123474121094f, 0.799621582031f, 0.078369140625f,
    -0.001403808594f, 0.118927001953f, 0.800476074219f, 0.082031250000f,
    -0.001281738281f, 0.114471435547f, 0.801177978516f, 0.085784912109f,
    -0.001159667969f, 0.110107421875f, 0.801696777344f, 0.089599609375f,
    -0.001037597656f, 0.105804443359f, 0.802032470703f, 0.093505859375f,
    -0.000976562500f, 0.101593017578f, 0.802215576172f, 0.097503662109f,
};

/* extern/dolphin/src/dolphin/axfx/chorus.c: do_src1.  Resamples the 3 x 160
 * sample ring at 12 kHz back to the output rate.  `posLo` is a 32-bit phase
 * (bits 14..20 index the 128-phase x 4-tap table), `pitchLo` the per-sample
 * increment; a carry advances `posHi`, and the FIR history is the last four
 * ring samples. */
static void do_src1(struct AXFX_CHORUS_SRCINFO* src)
{
    u32 pos_lo = src->posLo;
    u32 pos_hi = src->posHi;
    u32 pitch = src->pitchLo;
    u32 trigger = src->trigger;
    u32 target = src->target;
    long* smp_base = src->smpBase;
    long* dest = src->dest;
    long* old = src->old;
    float f1 = (float) old[0];
    float f2 = (float) old[1];
    float f3 = (float) old[2];
    float f4 = (float) smp_base[pos_hi];
    int i;

    for (i = 0; i < 160; i++) {
        const float* tap =
            &rsmpTab12khz[(((pos_lo << 7) | (pos_lo >> 25)) & 0x7F0u) >> 2];
        float out;
        u32 sum = pos_lo + pitch;
        int carry = sum < pos_lo;

        pos_lo = sum;
        out = f1 * tap[0] + f2 * tap[1] + f3 * tap[2] + f4 * tap[3];
        dest[i] = (long) out;

        if (carry) {
            pos_hi++;
            if (pos_hi == trigger) {
                pos_hi = target;
            }
            f1 = f2;
            f2 = f3;
            f3 = f4;
            if (i != 159) {
                f4 = (float) smp_base[pos_hi];
            }
        }
    }
    old[0] = (long) f1;
    old[1] = (long) f2;
    old[2] = (long) f3;
    src->posLo = pos_lo;
    src->posHi = pos_hi;
}

/* extern/dolphin/src/dolphin/axfx/chorus.c: do_src2.  Same table, but
 * `pitchHi == 1`: `posHi` advances every output sample and a carry inserts
 * the skipped ring sample into the FIR history, so the pitch sits one whole
 * ring sample per output sample above do_src1. */
static void do_src2(struct AXFX_CHORUS_SRCINFO* src)
{
    u32 pos_lo = src->posLo;
    u32 pos_hi = src->posHi;
    u32 pitch = src->pitchLo;
    u32 trigger = src->trigger;
    u32 target = src->target;
    long* smp_base = src->smpBase;
    long* dest = src->dest;
    long* old = src->old;
    float f1 = (float) old[0];
    float f2 = (float) old[1];
    float f3 = (float) old[2];
    float f4 = (float) smp_base[pos_hi];
    int i;

    for (i = 0; i < 160; i++) {
        const float* tap =
            &rsmpTab12khz[(((pos_lo << 7) | (pos_lo >> 25)) & 0x7F0u) >> 2];
        float out;
        u32 sum = pos_lo + pitch;
        int carry = sum < pos_lo;

        pos_lo = sum;
        pos_hi++;
        out = f1 * tap[0] + f2 * tap[1] + f3 * tap[2] + f4 * tap[3];
        dest[i] = (long) out;

        if (carry) {
            u32 skipped = pos_hi;
            if (skipped == trigger) {
                skipped = target;
            }
            pos_hi++;
            if (pos_hi == trigger) {
                pos_hi = target;
            }
            f1 = f3;
            f2 = f4;
            f3 = (float) smp_base[skipped];
            if (i != 159) {
                f4 = (float) smp_base[pos_hi];
            }
        } else {
            if (pos_hi == trigger) {
                pos_hi = target;
            }
            f1 = f2;
            f2 = f3;
            f3 = f4;
            if (i != 159) {
                f4 = (float) smp_base[pos_hi];
            }
        }
    }
    old[0] = (long) f1;
    old[1] = (long) f2;
    old[2] = (long) f3;
    src->posLo = pos_lo;
    src->posHi = pos_hi;
}

int AXFXChorusInit(struct AXFX_CHORUS* c)
{
    long* left;
    long* right;
    long* sur;
    u32 i;
    int old;

    old = OSDisableInterrupts();
    c->work.lastLeft[0] = __AXFXAlloc(0x1680);
    if (c->work.lastLeft[0] != NULL) {
        c->work.lastRight[0] = (void*) (c->work.lastLeft[0] + 0x1E0);
        c->work.lastSur[0] = (void*) (c->work.lastRight[0] + 0x1E0);
        for (i = 1; i < 3; i++) {
            c->work.lastLeft[i] = (void*) &c->work.lastLeft[0][i * 0xA0];
            c->work.lastRight[i] = (void*) &c->work.lastRight[0][i * 0xA0];
            c->work.lastSur[i] = (void*) &c->work.lastSur[0][i * 0xA0];
        }
        left = c->work.lastLeft[0];
        right = c->work.lastRight[0];
        sur = c->work.lastSur[0];
        for (i = 0; i < 0x140; i++) {
            *left++ = 0;
            *right++ = 0;
            *sur++ = 0;
        }
        c->work.currentLast = 1;
        c->work.oldLeft[0] = c->work.oldLeft[1] = c->work.oldLeft[2] =
            c->work.oldLeft[3] = 0;
        c->work.oldRight[0] = c->work.oldRight[1] = c->work.oldRight[2] =
            c->work.oldRight[3] = 0;
        c->work.oldSur[0] = c->work.oldSur[1] = c->work.oldSur[2] =
            c->work.oldSur[3] = 0;
        c->work.src.trigger = 0x1E0;
        c->work.src.target = 0;
        OSRestoreInterrupts(old);
        return AXFXChorusSettings(c);
    }
    OSRestoreInterrupts(old);
    return 0;
}

int AXFXChorusShutdown(struct AXFX_CHORUS* c)
{
    int old;

    old = OSDisableInterrupts();
    __AXFXFree(c->work.lastLeft[0]);
    OSRestoreInterrupts(old);
    return 1;
}

int AXFXChorusSettings(struct AXFX_CHORUS* c)
{
    int old;

    old = OSDisableInterrupts();
    c->work.currentPosHi = 0x140 - ((c->baseDelay - 5) << 5);
    c->work.currentPosLo = 0;
    c->work.currentPosHi =
        (c->work.currentPosHi + ((c->work.currentLast - 1) * 0xA0 / 1)) % 480;
    c->work.pitchOffsetPeriod = ((c->period / 5) + 1) & ~(1);
    c->work.pitchOffsetPeriodCount = c->work.pitchOffsetPeriod >> 1;
    c->work.pitchOffset =
        (c->variation << 0x10) / (c->work.pitchOffsetPeriod * 5);
    OSRestoreInterrupts(old);
    return 1;
}

void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* bufferUpdate,
                        struct AXFX_CHORUS* chorus)
{
    long* leftD;
    long* rightD;
    long* surD;
    long* leftS;
    long* rightS;
    long* surS;
    u32 i;
    u8 nextCurrentLast;

    nextCurrentLast = (chorus->work.currentLast + 1) % 3;
    leftD = chorus->work.lastLeft[nextCurrentLast];
    rightD = chorus->work.lastRight[nextCurrentLast];
    surD = chorus->work.lastSur[nextCurrentLast];
    leftS = bufferUpdate->left;
    rightS = bufferUpdate->right;
    surS = bufferUpdate->surround;
    for (i = 0; i < 0xA0; i++) {
        *leftD++ = *leftS++;
        *rightD++ = *rightS++;
        *surD++ = *surS++;
    }
    chorus->work.src.pitchHi = (chorus->work.pitchOffset >> 0x10) + 1;
    chorus->work.src.pitchLo = (chorus->work.pitchOffset & 0xFFFF) << 0x10;
    if (--chorus->work.pitchOffsetPeriodCount == 0) {
        chorus->work.pitchOffsetPeriodCount = chorus->work.pitchOffsetPeriod;
        chorus->work.pitchOffset = -chorus->work.pitchOffset;
    }
    for (i = 0; i < 3; i++) {
        chorus->work.src.posHi = chorus->work.currentPosHi;
        chorus->work.src.posLo = chorus->work.currentPosLo;
        switch (i) {
        case 0:
            chorus->work.src.smpBase = chorus->work.lastLeft[0];
            chorus->work.src.dest = bufferUpdate->left;
            chorus->work.src.old = &chorus->work.oldLeft[0];
            break;
        case 1:
            chorus->work.src.smpBase = chorus->work.lastRight[0];
            chorus->work.src.dest = bufferUpdate->right;
            chorus->work.src.old = &chorus->work.oldRight[0];
            break;
        case 2:
            chorus->work.src.smpBase = chorus->work.lastSur[0];
            chorus->work.src.dest = bufferUpdate->surround;
            chorus->work.src.old = &chorus->work.oldSur[0];
            break;
        }
        switch (chorus->work.src.pitchHi) {
        case 0:
            do_src1(&chorus->work.src);
            break;
        case 1:
            do_src2(&chorus->work.src);
            break;
        }
    }
    chorus->work.currentPosHi = (chorus->work.src.posHi % 480);
    chorus->work.currentPosLo = chorus->work.src.posLo;
    chorus->work.currentLast = nextCurrentLast;
}


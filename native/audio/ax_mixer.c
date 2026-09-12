/*
 * S5 software AX mixer (ADR-0013).
 *
 * Replaces the stock AX microcode's voice/mix stage.  The compiled SDK
 * bookkeeping (`AXAlloc.c`/`AXVPB.c`/`AXSPB.c`/`AXAux.c`/`AXCL.c`) keeps the
 * user PB and the DSP shadow PB (`__AXGetPBs()`); this file consumes the
 * shadow exactly like the DSP would:
 *
 *   - DSP-ADPCM format 0 (8-byte / 14-sample frames, `AXPBADPCM.a[pred]`
 *     coefficients), PCM formats 10/25,
 *   - `AXPBSRC` ratio SRC with a 16.16 fractional position and linear
 *     interpolation,
 *   - `AXPBMIX` main/aux A/aux B routing with Q15 gains,
 *   - `AXPBVE` currentVolume/currentDelta stepping once per output sample,
 *   - loop/end/current address bookkeeping with `pb.state` write-back,
 *   - ITD delay shifts (`AXPBITD`),
 *   - aux A/B returns added to the main mix (buffer handoff mirrors the
 *     command list's aux input/output pointers).
 *
 * Address convention (probed from retail `main.ssm`, see
 * `learnings/decomp_audio.md`): the game builds every voice address as
 * `aram_byte_offset * 2 + 2`, so the byte offset is `(addr - 2) / 2`.  The
 * `+ 2` term is the game's own convention, not a host offset.
 */
#include "audio/ax_mixer.h"

#include <dolphin/ax.h>
#include <stddef.h>
#include <string.h>

#define AX_FRAME_SAMPLES 160
#define AX_SRC_ONE 0x10000u
#define AX_ITD_SAMPLES 32

/* extern/dolphin/src/dolphin/ax/__ax.h (compiled into the same target). */
AXPB* __AXGetPBs(void);
void __AXGetAuxAInput(u32* p);
void __AXGetAuxAOutput(u32* p);
void __AXGetAuxBInput(u32* p);
void __AXGetAuxBOutput(u32* p);

typedef struct AxVoiceMix {
    int active;      /* the PB was in state 1 last frame */
    int ended;       /* the sample reached its end this frame */
    s16 yn1, yn2;    /* ADPCM predictor history */
    u32 frame_addr;  /* AX address of the next frame to decode */
    u32 write_addr;  /* currentAddress we last wrote back */
    s16 pcm[16];     /* decoded frame */
    int index;       /* next sample to consume in pcm[] (16 = need frame) */
    u32 frac;        /* 16.16 fraction between pcm[index] and pcm[index+1] */
    s16 itd_l[AX_ITD_SAMPLES];
    s16 itd_r[AX_ITD_SAMPLES];
    u32 itd_pos;
} AxVoiceMix;

static AxVoiceMix voices[AX_MAX_VOICES];

static s32 mix_l[AX_FRAME_SAMPLES];
static s32 mix_r[AX_FRAME_SAMPLES];

/* ------------------------------------------------------------------ helpers */

/* AX address (game domain: aram_byte * 2 + 2) -> ARAM byte offset. */
static long ax_aram_byte(u32 addr)
{
    if (addr < 2) {
        return -1;
    }
    return (long) ((addr - 2) >> 1);
}

static u32 pb_end_addr(const AXPB* pb)
{
    return ((u32) pb->addr.endAddressHi << 16) | pb->addr.endAddressLo;
}

static u32 pb_loop_addr(const AXPB* pb)
{
    return ((u32) pb->addr.loopAddressHi << 16) | pb->addr.loopAddressLo;
}

static u32 pb_cur_addr(const AXPB* pb)
{
    return ((u32) pb->addr.currentAddressHi << 16) | pb->addr.currentAddressLo;
}

static void pb_set_cur_addr(AXPB* pb, u32 addr)
{
    pb->addr.currentAddressHi = (u16) (addr >> 16);
    pb->addr.currentAddressLo = (u16) addr;
}

static s16 clamp_s16(s32 v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (s16) v;
}

static void voice_reset(AxVoiceMix* v, AXPB* pb)
{
    u32 cur = pb_cur_addr(pb);
    s16 itd_l[AX_ITD_SAMPLES];
    s16 itd_r[AX_ITD_SAMPLES];
    unsigned itd_pos = v->itd_pos;

    if (v->active) {
        memcpy(itd_l, v->itd_l, sizeof(itd_l));
        memcpy(itd_r, v->itd_r, sizeof(itd_r));
    } else {
        memset(itd_l, 0, sizeof(itd_l));
        memset(itd_r, 0, sizeof(itd_r));
        itd_pos = 0;
    }
    memset(v, 0, sizeof(*v));
    v->active = 1;
    v->frame_addr = cur;
    v->write_addr = cur;
    v->yn1 = (s16) pb->adpcm.yn1;
    v->yn2 = (s16) pb->adpcm.yn2;
    v->index = 14; /* force a frame decode */
    memcpy(v->itd_l, itd_l, sizeof(itd_l));
    memcpy(v->itd_r, itd_r, sizeof(itd_r));
    v->itd_pos = itd_pos;
}

/* Decode one ADPCM/PCM frame into v->pcm.  Returns 0 at the end of a
 * non-looping sample (v->ended is set). */
static int voice_decode_frame(AxVoiceMix* v, AXPB* pb)
{
    unsigned char* aram = platform_aram_base();
    unsigned size = platform_aram_size();
    u32 end_addr;
    long off;
    int i;

    if (v->ended) {
        return 0;
    }

    end_addr = pb_end_addr(pb);
    if (end_addr != 0 && v->frame_addr >= end_addr) {
        u32 loop_addr = pb_loop_addr(pb);
        int can_loop = pb->addr.loopFlag != 0 && loop_addr != 0 &&
                       loop_addr < end_addr;

        if (can_loop) {
            v->frame_addr = loop_addr;
            v->yn1 = (s16) pb->adpcmLoop.loop_yn1;
            v->yn2 = (s16) pb->adpcmLoop.loop_yn2;
        } else {
            v->ended = 1;
            return 0;
        }
    }

    off = ax_aram_byte(v->frame_addr);
    if (aram == NULL || off < 0 || (unsigned) off + 9 > size) {
        v->ended = 1;
        return 0;
    }

    if (pb->addr.format == 10 || pb->addr.format == 25) {
        /* 16-bit linear PCM.  Format 10 is the HPS stream page layout:
         * 16 samples per 0x20-byte block with one sample of padding. */
        u32 addr = v->frame_addr;
        for (i = 0; i < 16; i++) {
            long p = ax_aram_byte(addr);
            s16 s = 0;

            if (pb->addr.format == 10) {
                if (p >= 0 && (unsigned) p + 2 <= size) {
                    s = (s16) ((aram[p] << 8) | aram[p + 1]);
                }
                addr += 2;
                if ((i & 0xF) == 0xF) {
                    addr += 2; /* block padding */
                }
            } else {
                if (p >= 0 && (unsigned) p + 2 <= size) {
                    s = *(s16*) &aram[p];
                }
                addr += 2;
            }
            v->pcm[i] = s;
        }
        v->frame_addr = addr;
        v->index = 0;
        return 1;
    }

    {
        /* DSP-ADPCM: 8-byte frame = 1 header + 7 data bytes, 14 samples.
         * The header packs the coefficient index in the high nibble and the
         * scale exponent in the low nibble (DSPADPCM spec; the nibbles are
         * decoded high-first and sign-extended). */
        unsigned char hdr = aram[off];
        s32 scale = 1 << (hdr & 0x0F);
        unsigned pred = (hdr >> 4) & 0x0F;
        s32 c0;
        s32 c1;

        if (pred > 7) {
            pred = 7;
        }
        c0 = (s16) pb->adpcm.a[pred][0];
        c1 = (s16) pb->adpcm.a[pred][1];

        for (i = 0; i < 14; i++) {
            unsigned byte = aram[off + 1 + (i >> 1)];
            s32 nib = (i & 1) ? (byte & 0x0F) : (byte >> 4);
            s32 sample;

            if (nib >= 8) {
                nib -= 16;
            }
            sample = (nib * scale * 2048 + 1024 + c0 * v->yn1 + c1 * v->yn2) >>
                     11;
            sample = clamp_s16(sample);
            v->pcm[i] = (s16) sample;
            v->yn2 = v->yn1;
            v->yn1 = (s16) sample;
        }
    }

    v->frame_addr += 8 * 2;
    v->index = 0;
    return 1;
}

/* One sample out of the voice's source stream. */
static s16 voice_source_sample(AxVoiceMix* v, AXPB* pb)
{
    s32 cur;
    s32 nxt;
    u32 ratio;
    s16 out;

    if (v->index >= 14) {
        if (!voice_decode_frame(v, pb)) {
            return 0;
        }
    }

    cur = v->pcm[v->index];
    nxt = (v->index < 13) ? v->pcm[v->index + 1] : v->pcm[13];
    out = (s16) (cur + (((nxt - cur) * (s32) (v->frac >> 16)) >> 16));

    ratio = ((u32) pb->src.ratioHi << 16) | pb->src.ratioLo;
    v->frac += ratio;
    while (v->frac >= AX_SRC_ONE) {
        v->frac -= AX_SRC_ONE;
        v->index++;
        if (v->index >= 14) {
            if (!voice_decode_frame(v, pb)) {
                break;
            }
        }
    }
    pb->src.currentAddressFrac = (u16) (v->frac >> 8);
    return out;
}

/* ------------------------------------------------------------------- mixer */

void ax_mixer_init(void)
{
    memset(voices, 0, sizeof(voices));
}

void ax_mixer_frame(s16* out, unsigned frames)
{
    AXPB* pbs = __AXGetPBs();
    u32 aux_in_addr[2] = { 0, 0 };
    u32 aux_out_addr[2] = { 0, 0 };
    s32* aux_in[2] = { NULL, NULL };
    s32* aux_out[2] = { NULL, NULL };
    unsigned i;
    unsigned f;

    if (frames > AX_FRAME_SAMPLES) {
        frames = AX_FRAME_SAMPLES;
    }

    memset(mix_l, 0, sizeof(mix_l));
    memset(mix_r, 0, sizeof(mix_r));

    __AXGetAuxAInput(&aux_in_addr[0]);
    __AXGetAuxBInput(&aux_in_addr[1]);
    __AXGetAuxAOutput(&aux_out_addr[0]);
    __AXGetAuxBOutput(&aux_out_addr[1]);
    aux_in[0] = (s32*) (uintptr_t) aux_in_addr[0];
    aux_in[1] = (s32*) (uintptr_t) aux_in_addr[1];
    aux_out[0] = (s32*) (uintptr_t) aux_out_addr[0];
    aux_out[1] = (s32*) (uintptr_t) aux_out_addr[1];
    if (aux_in[0] != NULL) {
        memset(aux_in[0], 0, 3 * AX_FRAME_SAMPLES * sizeof(s32));
    }
    if (aux_in[1] != NULL) {
        memset(aux_in[1], 0, 3 * AX_FRAME_SAMPLES * sizeof(s32));
    }

    for (i = 0; i < AX_MAX_VOICES; i++) {
        AXPB* pb = &pbs[i];
        AxVoiceMix* v = &voices[i];
        u32 ratio;

        if (pb->state != 1) {
            v->active = 0;
            v->ended = 0;
            continue;
        }
        if (!v->active || pb_cur_addr(pb) != v->write_addr) {
            /* New voice, or the game repositioned a live one (HPS stream
             * start / page handoff, SFX voice reuse): restart the decoder
             * from the address the game just wrote. */
            voice_reset(v, pb);
        }

        for (f = 0; f < frames; f++) {
            s16 x;
            s32 gain;
            s32 y;
            s32 dl;
            s32 dr;

            if (v->ended) {
                break;
            }
            /* The end check is before the call: the last sample of a
             * one-shot must still be emitted when the decoder hits
             * endAddress while advancing past it. */
            x = voice_source_sample(v, pb);

            /* VE ramp: currentVolume is Q15, currentDelta steps per sample. */
            gain = (s32) pb->ve.currentVolume;
            y = (x * gain) >> 15;
            pb->ve.currentVolume =
                (u16) (pb->ve.currentVolume + (u16) (s16) pb->ve.currentDelta);

            /* ITD: delay each ear by its own shift (0 disables). */
            dl = y;
            dr = y;
            if (pb->itd.flag != 0) {
                s32 shift_l = (s16) pb->itd.shiftL;
                s32 shift_r = (s16) pb->itd.shiftR;

                if (pb->itd.shiftL < pb->itd.targetShiftL) {
                    pb->itd.shiftL++;
                } else if (pb->itd.shiftL > pb->itd.targetShiftL) {
                    pb->itd.shiftL--;
                }
                if (pb->itd.shiftR < pb->itd.targetShiftR) {
                    pb->itd.shiftR++;
                } else if (pb->itd.shiftR > pb->itd.targetShiftR) {
                    pb->itd.shiftR--;
                }
                if (shift_l > 0 && shift_l < AX_ITD_SAMPLES) {
                    dl = v->itd_l[(v->itd_pos + AX_ITD_SAMPLES -
                                   (u32) shift_l) %
                                  AX_ITD_SAMPLES];
                }
                if (shift_r > 0 && shift_r < AX_ITD_SAMPLES) {
                    dr = v->itd_r[(v->itd_pos + AX_ITD_SAMPLES -
                                   (u32) shift_r) %
                                  AX_ITD_SAMPLES];
                }
            }
            v->itd_l[v->itd_pos] = (s16) y;
            v->itd_r[v->itd_pos] = (s16) y;
            v->itd_pos = (v->itd_pos + 1) % AX_ITD_SAMPLES;

            mix_l[f] += (dl * (s32) pb->mix.vL) >> 15;
            mix_r[f] += (dr * (s32) pb->mix.vR) >> 15;

            if (aux_in[0] != NULL) {
                aux_in[0][0 * AX_FRAME_SAMPLES + f] +=
                    (y * (s32) pb->mix.vAuxAL) >> 15;
                aux_in[0][1 * AX_FRAME_SAMPLES + f] +=
                    (y * (s32) pb->mix.vAuxAR) >> 15;
                aux_in[0][2 * AX_FRAME_SAMPLES + f] +=
                    (y * (s32) pb->mix.vAuxAS) >> 15;
            }
            if (aux_in[1] != NULL) {
                aux_in[1][0 * AX_FRAME_SAMPLES + f] +=
                    (y * (s32) pb->mix.vAuxBL) >> 15;
                aux_in[1][1 * AX_FRAME_SAMPLES + f] +=
                    (y * (s32) pb->mix.vAuxBR) >> 15;
                aux_in[1][2 * AX_FRAME_SAMPLES + f] +=
                    (y * (s32) pb->mix.vAuxBS) >> 15;
            }
        }

        /* Write the consumed position back into the DSP shadow.  The game
         * polls currentAddress for stream page advance and voice teardown. */
        {
            u32 addr = v->frame_addr;
            if (v->index < 14) {
                addr -= 8 * 2;
                addr += (u32) ((v->index * 8 * 2) / 14);
            }
            pb_set_cur_addr(pb, addr);
            v->write_addr = addr;
        }

        if (v->ended) {
            pb->state = 0;
            v->active = 0;
        }

        /* A voice whose source rate is zero holds its position; keep the
         * frame address bookkeeping stable. */
        ratio = ((u32) pb->src.ratioHi << 16) | pb->src.ratioLo;
        (void) ratio;
    }

    /* Aux returns from the buffers the command list handed the DSP. */
    for (i = 0; i < 2; i++) {
        if (aux_out[i] == NULL) {
            continue;
        }
        for (f = 0; f < frames; f++) {
            mix_l[f] += aux_out[i][0 * AX_FRAME_SAMPLES + f];
            mix_r[f] += aux_out[i][1 * AX_FRAME_SAMPLES + f];
        }
    }

    for (f = 0; f < frames; f++) {
        out[f * 2 + 0] = clamp_s16(mix_l[f]);
        out[f * 2 + 1] = clamp_s16(mix_r[f]);
    }
}

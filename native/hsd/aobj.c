#include "hsd/aobj.h"

#include <math.h>
#include <string.h>

/*
 * Port of src/sysdolphin/baselib/fobj.c.  Function names and structure follow
 * the decompilation so differences are easy to spot.
 */

#define HSD_A_FRAC_FLOAT (0 << 5)
#define HSD_A_FRAC_S16 (1 << 5)
#define HSD_A_FRAC_U16 (2 << 5)
#define HSD_A_FRAC_S8 (3 << 5)
#define HSD_A_FRAC_U8 (4 << 5)

#define FOBJ_LOAD_DATA0 1
#define FOBJ_LOAD_DATA 2
#define FOBJ_LOAD_WAIT 3

static uint8_t fobj_get_state(const Fobj *f)
{
    return f->flags & 0xF;
}

static uint8_t fobj_set_state(Fobj *f, uint8_t state)
{
    f->flags = (uint8_t) ((state & 0xF) | (f->flags & 0xF0));
    return state;
}

/* fobj.c parseFloat; the stream is little-endian. */
static float parse_float(Fobj *f, uint8_t frac)
{
    const uint8_t *ad = f->ad_head + f->ad_pos;
    if (frac == HSD_A_FRAC_FLOAT) {
        union {
            float f;
            uint32_t d;
        } u;
        u.d = (uint32_t) ad[0];
        u.d |= (uint32_t) ad[1] << 8;
        u.d |= (uint32_t) ad[2] << 16;
        u.d |= (uint32_t) ad[3] << 24;
        f->ad_pos += 4;
        return u.f;
    }
    {
        float numer;
        switch (frac & 0xE0) {
        case HSD_A_FRAC_S8:
            numer = (float) (int8_t) ad[0];
            f->ad_pos += 1;
            break;
        case HSD_A_FRAC_U8:
            numer = (float) ad[0];
            f->ad_pos += 1;
            break;
        case HSD_A_FRAC_S16:
            /* fobj.c sign-extends the high byte before shifting; do it
             * unsigned here so the shift is well defined. */
            numer = (float) (int16_t) (((uint16_t) ad[1] << 8) | ad[0]);
            f->ad_pos += 2;
            break;
        case HSD_A_FRAC_U16:
            numer = (float) (uint16_t) (((uint16_t) ad[1] << 8) | ad[0]);
            f->ad_pos += 2;
            break;
        default:
            return 0.0f;
        }
        return numer / (float) (1 << (frac & 0x1F));
    }
}

/* fobj.c parseOpCode. */
static uint8_t parse_op_code(const Fobj *f)
{
    return f->ad_head[f->ad_pos] & 0xF;
}

/* fobj.c parsePackInfo. */
static uint32_t parse_pack_info(Fobj *f)
{
    const uint8_t *ad = f->ad_head;
    uint8_t d = ad[f->ad_pos++];
    uint32_t nb_pack = (uint32_t) ((d >> 4) & 7) + 1;
    int shift = 3;
    if (!(d & 0x80)) {
        return nb_pack;
    }
    do {
        d = ad[f->ad_pos++];
        nb_pack += (uint32_t) (d & 0x7F) << shift;
        shift += 7;
    } while (d & 0x80);
    return nb_pack;
}

/* fobj.c parseWait. */
static int32_t parse_wait(Fobj *f)
{
    const uint8_t *ad = f->ad_head;
    int32_t wait = 0;
    int shift = 0;
    uint8_t d;
    do {
        d = ad[f->ad_pos++];
        wait |= (int32_t) (d & 0x7F) << shift;
        shift += 7;
    } while (d & 0x80);
    return wait;
}

/* fobj.c FObjLaunchKeyData. */
static void fobj_launch_key_data(Fobj *f)
{
    if ((f->flags & 0x40) != 0) {
        f->op_intrp = f->op;
        f->flags &= (uint8_t) ~0x40;
        f->flags |= 0x80;
        f->p0 = f->p1;
    }
}

/* fobj.c FObjLoadWait. */
static uint8_t fobj_load_wait(Fobj *f)
{
    if (f->ad_pos >= f->length) {
        return 6;
    }
    f->fterm = (uint16_t) parse_wait(f);
    f->flags |= 0x20;
    return fobj_set_state(f, FOBJ_LOAD_DATA);
}

static uint8_t fobj_anim_con(Fobj *f)
{
    uint8_t st = fobj_get_state(f);
    f->p0 = f->p1;
    f->p1 = parse_float(f, f->frac_value);
    if (f->op_intrp != 5) {
        f->d0 = f->d1;
        f->d1 = 0.0f;
    }
    return fobj_set_state(f, st == FOBJ_LOAD_DATA0 ? 3 : 4);
}

static uint8_t fobj_anim_linear(Fobj *f)
{
    uint8_t st = fobj_get_state(f);
    f->p0 = f->p1;
    f->p1 = parse_float(f, f->frac_value);
    if (f->op_intrp != 5) {
        f->d0 = f->d1;
        f->d1 = 0.0f;
    }
    return fobj_set_state(f, st == FOBJ_LOAD_DATA0 ? 3 : 4);
}

static uint8_t fobj_anim_spl0(Fobj *f)
{
    uint8_t st = fobj_get_state(f);
    f->p0 = f->p1;
    f->d0 = f->d1;
    f->p1 = parse_float(f, f->frac_value);
    f->d1 = 0.0f;
    return fobj_set_state(f, st == FOBJ_LOAD_DATA0 ? 3 : 4);
}

static uint8_t fobj_anim_spl(Fobj *f)
{
    uint8_t st = fobj_get_state(f);
    f->p0 = f->p1;
    f->p1 = parse_float(f, f->frac_value);
    f->d0 = f->d1;
    f->d1 = parse_float(f, f->frac_slope);
    return fobj_set_state(f, st == FOBJ_LOAD_DATA0 ? 3 : 4);
}

static uint8_t fobj_anim_slp(Fobj *f)
{
    f->d0 = f->d1;
    f->d1 = parse_float(f, f->frac_slope);
    return fobj_get_state(f);
}

static uint8_t fobj_anim_key(Fobj *f)
{
    uint8_t st = fobj_get_state(f);
    fobj_launch_key_data(f);
    f->p1 = parse_float(f, f->frac_value);
    f->flags |= 0x40;
    return fobj_set_state(f, st == FOBJ_LOAD_DATA0 ? 3 : 4);
}

/* fobj.c FObjLoadData. */
static uint8_t fobj_load_data(Fobj *f)
{
    if (f->ad_pos >= f->length) {
        return 6;
    }
    f->op_intrp = f->op;
    if (f->nb_pack == 0) {
        f->op = parse_op_code(f);
        f->nb_pack = (uint16_t) parse_pack_info(f);
    }
    f->nb_pack -= 1;
    switch (f->op) {
    case HSD_A_OP_CON:
        return fobj_anim_con(f);
    case HSD_A_OP_LIN:
        return fobj_anim_linear(f);
    case HSD_A_OP_SPL0:
        return fobj_anim_spl0(f);
    case HSD_A_OP_SPL:
        return fobj_anim_spl(f);
    case HSD_A_OP_SLP:
        return fobj_anim_slp(f);
    case HSD_A_OP_KEY:
        return fobj_anim_key(f);
    default:
        return 0;
    }
}

/* spline.c splGetHelmite. */
static float spl_get_helmite(float fterm, float time, float p0, float p1,
                             float d0, float d1)
{
    float time2 = time * time;
    float fterm2 = fterm * fterm;
    float t2_T = time2 * fterm;
    float t3_T2 = fterm2 * (time2 * time);
    float two_t3_T3 = 2.0f * t3_T2 * fterm;
    float three_t2_T2 = 3.0f * time2 * fterm2;
    return (d1 * (t3_T2 - t2_T)) +
           (d0 * (time + ((t3_T2 - t2_T) - t2_T))) +
           (p0 * (1.0f + (two_t3_T3 - three_t2_T2))) +
           (p1 * (-two_t3_T3 + three_t2_T2));
}

/* fobj.c FObjUpdateAnim: writes one channel value when the curve emits it. */
static int fobj_update_anim(Fobj *f, float *out)
{
    switch (f->op_intrp) {
    case HSD_A_OP_KEY:
        if (f->flags & 0x80) {
            *out = f->p0;
            f->flags &= (uint8_t) ~0x80;
            return 1;
        }
        return 0;
    case HSD_A_OP_CON:
        *out = (f->time >= f->fterm) ? f->p1 : f->p0;
        return 1;
    case HSD_A_OP_LIN:
        if (f->flags & 0x20) {
            f->flags &= (uint8_t) ~0x20;
            if (f->fterm != 0) {
                f->d0 = (f->p1 - f->p0) / f->fterm;
            } else {
                f->d0 = 0.0f;
                f->p0 = f->p1;
            }
        }
        *out = f->d0 * f->time + f->p0;
        return 1;
    case HSD_A_OP_SPL0:
    case HSD_A_OP_SPL:
    case HSD_A_OP_SLP:
        if (f->fterm != 0) {
            *out = spl_get_helmite(1.0f / f->fterm, f->time, f->p0, f->p1,
                                   f->d0, f->d1);
        } else {
            *out = f->p1;
        }
        return 1;
    default:
        return 0;
    }
}

void fobj_init(Fobj *f, const uint8_t *ad, size_t length,
                    uint8_t obj_type, int16_t startframe, uint8_t frac_value,
                    uint8_t frac_slope)
{
    memset(f, 0, sizeof(*f));
    f->ad_head = ad;
    f->length = length;
    f->obj_type = obj_type;
    f->startframe = startframe;
    f->frac_value = frac_value;
    f->frac_slope = frac_slope;
}

void fobj_req_anim(Fobj *f, float frame)
{
    f->ad_pos = 0;
    f->time = (float) f->startframe + frame;
    f->op = 0;
    f->op_intrp = 0;
    f->flags &= (uint8_t) ~0x40;
    f->nb_pack = 0;
    f->fterm = 0;
    f->p0 = 0.0f;
    f->p1 = 0.0f;
    f->d0 = 0.0f;
    f->d1 = 0.0f;
    fobj_set_state(f, 1);
}

int fobj_interpret(Fobj *f, float rate, float *out)
{
    float fterm = 0.0f;
    uint8_t state = fobj_get_state(f);
    if (state == 0) {
        return 0;
    }
    f->time += rate;
    if (f->time < 0.0f) {
        return 0;
    }
    for (;;) {
        switch (state) {
        case 6:
            f->time += fterm;
            fobj_launch_key_data(f);
            return fobj_update_anim(f, out);
        case 1:
        case 2:
            state = fobj_load_data(f);
            break;
        case 3:
            if (f->flags & 0x80) {
                float discarded;
                fobj_update_anim(f, &discarded);
            }
            state = fobj_load_wait(f);
            break;
        case 4:
            if (f->fterm <= f->time) {
                fterm = (float) f->fterm;
                f->time -= (float) f->fterm;
                state = 3;
                fobj_set_state(f, state);
                break;
            }
            fobj_set_state(f, 5);
            return fobj_update_anim(f, out);
        case 5:
            state = 4;
            fobj_set_state(f, state);
            break;
        default:
            return 0;
        }
    }
}

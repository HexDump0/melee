#ifndef MELEE_NATIVE_DEMO_AOBJ_H
#define MELEE_NATIVE_DEMO_AOBJ_H

/*
 * Literal port of the HSD FObj animation curve player from
 * src/sysdolphin/baselib/fobj.c, used by Melee's FigaTree fighter animations
 * (src/melee/lb/lbanim.c).
 *
 * HSD_FObjInterpretAnim walks the compressed stream incrementally while the
 * animation plays; HSD_AObjReqAnim rewinds it to a frame, and the next
 * interpretation call fast-forwards to that frame.  This port keeps the same
 * state machine byte for byte, so evaluating at any frame is exactly
 * "demo_fobj_req_anim(f, frame); demo_fobj_interpret(f, 0, &value)".
 */
#include <stddef.h>
#include <stdint.h>

/* HSD_A_OP_*: the interpolation op of a run. */
enum {
    DEMO_A_OP_NONE = 0,
    DEMO_A_OP_CON = 1,
    DEMO_A_OP_LIN = 2,
    DEMO_A_OP_SPL0 = 3,
    DEMO_A_OP_SPL = 4,
    DEMO_A_OP_SLP = 5,
    DEMO_A_OP_KEY = 6
};

/* HSD_A_J_*: the animated channel (JObjUpdateFunc switch). */
enum {
    DEMO_A_J_ROTX = 1,
    DEMO_A_J_ROTY = 2,
    DEMO_A_J_ROTZ = 3,
    DEMO_A_J_PATH = 4,
    DEMO_A_J_TRAX = 5,
    DEMO_A_J_TRAY = 6,
    DEMO_A_J_TRAZ = 7,
    DEMO_A_J_SCAX = 8,
    DEMO_A_J_SCAY = 9,
    DEMO_A_J_SCAZ = 10,
    DEMO_A_J_NODE = 11,
    DEMO_A_J_BRANCH = 12,
    DEMO_A_J_SETBYTE0 = 20,
    DEMO_A_J_SETBYTE9 = 29,
    DEMO_A_J_SETFLOAT0 = 30,
    DEMO_A_J_SETFLOAT9 = 39
};

typedef struct DemoFobj {
    const uint8_t *ad_head; /* track byte stream inside the animation archive */
    size_t length;          /* track length in bytes */
    size_t ad_pos;          /* ad - ad_head */

    uint8_t flags; /* bit 0-3 state, 0x20 wait pending, 0x40 key, 0x80 launched */
    uint8_t op;
    uint8_t op_intrp;
    uint8_t obj_type; /* DEMO_A_J_* */
    uint8_t frac_value;
    uint8_t frac_slope;
    uint16_t nb_pack;
    int16_t startframe;
    uint16_t fterm;
    float time;
    float p0;
    float p1;
    float d0;
    float d1;
} DemoFobj;

/* Binds a track's constant fields; FObjLoadDesc + FObjAlloc equivalent. */
void demo_fobj_init(DemoFobj *f, const uint8_t *ad, size_t length,
                    uint8_t obj_type, int16_t startframe, uint8_t frac_value,
                    uint8_t frac_slope);

/* HSD_FObjReqAnim: rewinds the stream and offsets it to `frame`. */
void demo_fobj_req_anim(DemoFobj *f, float frame);

/*
 * HSD_FObjInterpretAnim.  Advances by `rate` frames and writes the channel
 * value to *out when the curve emits one.  Returns 1 when *out was written
 * (i.e. obj_update was invoked in the engine).
 */
int demo_fobj_interpret(DemoFobj *f, float rate, float *out);

#endif

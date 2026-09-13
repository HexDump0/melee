#include "platform/hps.h"

static void swap16(unsigned char* p)
{
    unsigned char t = p[0];

    p[0] = p[1];
    p[1] = t;
}

static void swap32(unsigned char* p)
{
    unsigned char t0 = p[0];
    unsigned char t1 = p[1];

    p[0] = p[3];
    p[1] = p[2];
    p[2] = t1;
    p[3] = t0;
}

void hps_fix_read(unsigned char* dst, uint32_t file_offset, uint32_t length)
{
    uint32_t i;

    if (file_offset == 0 && length >= 0x80) {
        swap32(dst + 0x08); /* sample rate */
        swap32(dst + 0x0C); /* voice count */
        for (i = 0x10; i + 2 <= 0x80; i += 2) {
            swap16(dst + i); /* AXPBADDR + AXPBADPCM for both voices */
        }
    } else if (file_offset != 0 && length == 0x20) {
        /* Page table: x0/x4/x8 (u32) plus the per-voice AXPBADPCMLOOP
         * loop_pred_scale/loop_yn1/loop_yn2 context pairs.  The u16 halves of
         * a u32 are not a byte swap, so keep swap32 for the three words and
         * swap16 the rest; leaving the loop context big-endian fed the mixer
         * byte-swapped predictor history at every page seam (P-648). */
        swap32(dst + 0x00); /* page stride */
        swap32(dst + 0x04); /* voice data size */
        swap32(dst + 0x08); /* next page-table offset */
        for (i = 0x0C; i + 2 <= length; i += 2) {
            swap16(dst + i); /* AXPBADPCMLOOP pairs */
        }
    }
}

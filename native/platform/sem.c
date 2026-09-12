#include "platform/sem.h"

#define SEM_SECTIONS 5u

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
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

int sem_fix_read(unsigned char* dst, uint32_t file_size)
{
    uint32_t counts[SEM_SECTIONS];
    uint32_t list_off[SEM_SECTIONS];
    uint32_t off = 0;
    uint32_t i;
    unsigned s;

    for (s = 0; s < SEM_SECTIONS; s++) {
        uint32_t count;

        if (off + 4 > file_size) {
            return 0;
        }
        count = be32(dst + off);
        list_off[s] = off + 4;
        counts[s] = count;
        swap32(dst + off);
        off += 4;
        if (count > (file_size - off) / 4) {
            return 0;
        }
        for (i = 0; i < count; i++) {
            swap32(dst + off + i * 4);
        }
        off += count * 4;
    }

    if (counts[4] != 0) {
        /* Unknown section-4 semantics: do not guess at its data. */
        return 0;
    }

    /* Command streams: swap each section-3 chunk word-wise.  The list was
     * just converted to host order, so the chunk offsets are usable. */
    for (i = 0; i < counts[3]; i++) {
        uint32_t start = *(uint32_t*) (dst + list_off[3] + i * 4);
        uint32_t end = (i + 1 < counts[3])
                           ? *(uint32_t*) (dst + list_off[3] + (i + 1) * 4)
                           : (file_size & ~3u);
        uint32_t p;

        if (start > end || end > file_size || (start & 3) != 0 ||
            (end & 3) != 0) {
            return 0;
        }
        for (p = start; p + 4 <= end; p += 4) {
            swap32(dst + p);
        }
    }

    return 1;
}

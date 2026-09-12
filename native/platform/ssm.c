#include "platform/ssm.h"

#include <string.h>

#define SSM_HEADER_SIZE 0x10u
#define SSM_RECORD_ENTRY 0x40u
#define SSM_MAX_GROUP 0x40000u
#define SSM_MAX_RECORDS (1u << 20)

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

static void wr32(unsigned char* p, uint32_t v)
{
    memcpy(p, &v, 4);
}

void ssm_stream_init(SsmStreamTable* table)
{
    table->next = SSM_HEADER_SIZE;
    table->initialized = 0;
}

void ssm_fix_read(unsigned char* dst, uint32_t file_offset, uint32_t length,
                  SsmStreamTable* table)
{
    uint32_t base = file_offset;
    uint32_t end = file_offset + length;
    uint32_t next;
    uint32_t guard = 0;
    unsigned int i;

    if (!table->initialized) {
        table->next = SSM_HEADER_SIZE;
        table->initialized = 1;
    }

    /* File header: four u32 words at +0x00. */
    if (file_offset == 0) {
        uint32_t words = length < SSM_HEADER_SIZE ? length : SSM_HEADER_SIZE;
        for (i = 0; i + 4 <= words; i += 4) {
            wr32(dst + i, be32(dst + i));
        }
    }

    next = table->next;
    while (next + 4 <= end && guard++ < SSM_MAX_RECORDS) {
        uint32_t n;
        if (next < base) {
            /* A previous chunk of the stream table was not converted; stop
             * instead of guessing record boundaries. */
            table->initialized = 0;
            return;
        }
        n = be32(dst + (next - base));
        wr32(dst + (next - base), n);
        if (n > SSM_MAX_GROUP) {
            table->initialized = 0;
            return;
        }
        next += 8 + n * SSM_RECORD_ENTRY;
    }
    table->next = next;
}

#include "platform/ssm.h"

#include <string.h>

#define SSM_HEADER_SIZE 0x10u
#define SSM_RECORD_ENTRY 0x40u
#define SSM_MAX_GROUP 0x40000u

static uint32_t be32(const unsigned char* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

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

/* Size of the entry field at `rel` bytes into a 0x40-byte entry. */
static uint32_t entry_field_size(uint32_t rel)
{
    if (rel == 4 || rel == 8 || rel == 0xC) {
        return 4;
    }
    return 2;
}

/* Next field offset after converting the field at `rel`. */
static uint32_t entry_field_next(uint32_t rel, uint32_t size)
{
    if (rel == 0x3C) {
        return 0x40; /* the last field is followed by two pad bytes */
    }
    return rel + size;
}

void ssm_stream_init(SsmStreamTable* table)
{
    memset(table, 0, sizeof(*table));
    table->next = SSM_HEADER_SIZE;
}

void ssm_fix_read(unsigned char* dst, uint32_t file_offset, uint32_t length,
                  SsmStreamTable* table)
{
    uint32_t base = file_offset;
    uint32_t end = file_offset + length;
    unsigned int i;

    if (length == 0) {
        return;
    }

    if (file_offset == 0) {
        /* Header: four u32 words, then the first bytes of the group table. */
        uint32_t words = length < SSM_HEADER_SIZE ? length : SSM_HEADER_SIZE;

        for (i = 0; i + 4 <= words; i += 4) {
            swap32(dst + i);
        }
        table->dir_end = be32(dst) + SSM_HEADER_SIZE;
        table->groups = be32(dst + 8);
        table->next = SSM_HEADER_SIZE;
        table->index = 0;
        table->group_start = 0;
        table->group_n = 0;
        table->field_off = 0;
        table->records_end = 0;
        table->stage = 0;
        table->initialized = 1;
    }

    if (!table->initialized) {
        return;
    }

    for (;;) {
        if (table->stage == 0) {
            uint32_t off = table->next;

            if (off + 8 > table->dir_end) {
                table->stage = 2;
                table->initialized = 0;
                return;
            }
            if (off + 8 > end) {
                return; /* wait for the next read */
            }
            if (off < base) {
                table->initialized = 0;
                return;
            }
            table->group_n = be32(dst + off - base);
            table->group_start = off;
            swap32(dst + off - base);
            swap32(dst + off + 4 - base);
            table->field_off = off + 8;
            table->records_end = off + 8 + table->group_n * SSM_RECORD_ENTRY;
            if (table->group_n > SSM_MAX_GROUP ||
                table->records_end > table->dir_end) {
                table->initialized = 0;
                return;
            }
            table->stage = 1;
        }

        while (table->field_off < table->records_end) {
            uint32_t rel = (table->field_off - table->group_start - 8) %
                           SSM_RECORD_ENTRY;
            uint32_t size = entry_field_size(rel);

            if (table->field_off + size > end) {
                return; /* wait for the next read */
            }
            if (table->field_off < base) {
                table->initialized = 0;
                return;
            }
            if (size == 4) {
                swap32(dst + table->field_off - base);
            } else {
                swap16(dst + table->field_off - base);
            }
            table->field_off += entry_field_next(rel, size) - rel;
        }

        table->index++;
        table->next = table->records_end;
        table->stage = 0;
        if (table->index >= table->groups || table->next >= table->dir_end) {
            table->stage = 2;
            table->initialized = 0;
            return;
        }
    }
}

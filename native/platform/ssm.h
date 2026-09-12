#ifndef MELEE_NATIVE_PLATFORM_SSM_H
#define MELEE_NATIVE_PLATFORM_SSM_H

#include <stdint.h>

/*
 * `.ssm` sound-bank conversion (S3 header/counts, S5 records).
 *
 * The compiled `HSD_Synth` loader (`src/sysdolphin/baselib/synth.c`) consumes
 * the file with big-endian field reads.  The host is little-endian, so every
 * numeric field the loader or the AX mixer touches is converted in the
 * destination buffer as the DVD backend delivers the reads.
 *
 * Layout (probed from `audio/us/main.ssm`, see
 * `native/AI/learnings/decomp_audio.md`):
 *
 *   +0x00 u32 header_size        size of the stream table in bytes
 *   +0x04 u32 sample_data_size   bytes of ADPCM sample data
 *   +0x08 u32 group_count
 *   +0x0C u32 base
 *   +0x10 .. +0x10+header_size   stream table: groups of
 *                                { u32 n; u32 rate; n * 0x40-byte entries }
 *   +0x10+header_size .. EOF     ADPCM sample bytes (never touched)
 *
 * A group at `g` has `n` 0x40-byte entries.  Entry `k` starts at `g + 8 +
 * k*0x40` and carries (relative to the entry):
 *
 *   +0x00 u16 loop flag          +0x02 u16 format (0 = ADPCM)
 *   +0x04 u32 loop address       +0x08 u32 end address
 *   +0x0C u32 current address
 *   +0x10 AXPBADPCM              (20 u16: coefficients, gain, pred, yn1/yn2)
 *   +0x38 AXPBADPCMLOOP          (3 u16: loop predictor history)
 *
 * Only the mixer-facing numeric fields are converted.  The sample region is
 * never touched.
 */

typedef struct SsmStreamTable {
    uint32_t next;     /* absolute offset of the next group header */
    uint32_t dir_end;  /* absolute offset where the sample region starts */
    uint32_t groups;   /* group count from the file header */
    uint32_t index;    /* groups completely converted */
    uint32_t group_start;
    uint32_t group_n;
    uint32_t field_off;   /* absolute offset of the next entry field */
    uint32_t records_end; /* absolute offset where the entries end */
    int stage;            /* 0 = group header, 1 = entry fields, 2 = done */
    int initialized;
} SsmStreamTable;

void ssm_stream_init(SsmStreamTable* table);

/* Converts the part of an `.ssm` read that covers [file_offset, +length).
 * Call it for every read of an `.ssm` file, in order.  Fields whose bytes are
 * not all present stay big-endian and are converted by a later read; if a
 * read skips bytes the table gives up (initialized = 0) instead of
 * mis-converting. */
void ssm_fix_read(unsigned char* dst, uint32_t file_offset, uint32_t length,
                  SsmStreamTable* table);

#endif

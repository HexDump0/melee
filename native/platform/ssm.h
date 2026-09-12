#ifndef MELEE_NATIVE_PLATFORM_SSM_H
#define MELEE_NATIVE_PLATFORM_SSM_H

#include <stdint.h>

/*
 * `.ssm` sound-bank conversion (S3).
 *
 * The compiled `HSD_Synth` loader (`src/sysdolphin/baselib/synth.c`) consumes
 * the file with big-endian field reads.  The host is little-endian, so the
 * header and the stream-table record counts have to be converted in the
 * destination buffer as the DVD backend delivers the reads.
 *
 * Layout (probed from `audio/us/main.ssm`, see
 * `native/AI/learnings/decomp_assets.md`):
 *
 *   +0x00 u32 header_size        size of the stream table in bytes
 *   +0x04 u32 sample_data_size   bytes of ADPCM sample data
 *   +0x08 u32 group_count
 *   +0x0C u32 base
 *   +0x10 .. +0x10+header_size   stream table: records of
 *                                { u32 n; n * 0x40-byte entries }
 *   +0x10+header_size .. EOF     ADPCM sample bytes (never touched)
 *
 * Only the header words and each record's count are converted here; the
 * 0x40-byte entry fields are the mixer's business (S5).
 */

typedef struct SsmStreamTable {
    uint32_t next;     /* file offset of the next unconverted record count */
    int initialized;
} SsmStreamTable;

void ssm_stream_init(SsmStreamTable* table);

/* Converts the part of an `.ssm` read that covers [file_offset, +length).
 * Call it for every read of an `.ssm` file, in order.  Records whose counts
 * are unknown because a previous read was missed are skipped (the data stays
 * big-endian) rather than mis-converted. */
void ssm_fix_read(unsigned char* dst, uint32_t file_offset, uint32_t length,
                  SsmStreamTable* table);

#endif

#ifndef MELEE_NATIVE_PLATFORM_HPS_H
#define MELEE_NATIVE_PLATFORM_HPS_H

#include <stdint.h>

/*
 * `.hps` streamed-music conversion (S5.4).
 *
 * `HSD_Synth_8038B5AC` and `HSD_Synth_8038ADD0` read the HPS directory as
 * host-order fields:
 *
 *   +0x00  u32 magic[2]
 *   +0x08  u32 sample rate          (e.g. 32000)
 *   +0x0C  u32 voice count          (1 or 2)
 *   +0x10  voice 0: AXPBADDR (8 u16) + AXPBADPCM (20 u16)
 *   +0x48  voice 1: same
 *   +0x80  page table: u32 x0 (page stride), u32 x4 (voice data size),
 *          u32 x8 (next page-table offset, -1 at the end), padding
 *   +0xA0  page data (x0 bytes)
 *   ...    next page at x8
 *
 * The first header arrives through DevCom's internal buffer (type 0x22) and
 * page tables through ordinary reads of 0x20 bytes (type 0x21); both pass
 * through the DVD backend, which converts them here.  Page data is ADPCM
 * bytes and is never touched.
 */
void hps_fix_read(unsigned char* dst, uint32_t file_offset, uint32_t length);

#endif

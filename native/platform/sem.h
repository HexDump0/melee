#ifndef MELEE_NATIVE_PLATFORM_SEM_H
#define MELEE_NATIVE_PLATFORM_SEM_H

#include <stdint.h>

/*
 * `smash2.sem` command-table conversion (S5).
 *
 * The compiled `AXDriver_8038DA70` (`src/sysdolphin/baselib/axdriver.c`)
 * loads the file into main memory and reads it as host-order words:
 *
 *   +0x00  five sections, each { u32 count; count * u32 values }
 *            section 0: unused pointer list
 *            section 1: unused pointer list
 *            section 2: per-sound-bank sample index boundaries
 *            section 3: per-sample command-stream pointers (rebased at load)
 *            section 4: unused pointer list
 *   data   command streams: 32-bit words [type:8][param:24], each stream
 *          ends with a type 14/15 word.  `AXDriver_8038C6C0` interprets the
 *          words in host order, so the whole stream (not just its header)
 *          must be byte-swapped.
 *
 * `sem_fix_read` converts a full-file read in place.  It returns 1 on
 * success; on failure the buffer is left as read (the caller's data stays
 * big-endian, which is visible in the boot triage).
 */
int sem_fix_read(unsigned char* dst, uint32_t file_size);

#endif

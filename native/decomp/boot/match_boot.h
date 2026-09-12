#ifndef MELEE_DECOMP_BOOT_MATCH_BOOT_H
#define MELEE_DECOMP_BOOT_MATCH_BOOT_H

/*
 * S4: headless match entry.  `match_boot_init(N)` arms a VI-frame hook that
 * switches to the game's debug-VS mode after N frames (0 disarms).
 */
void match_boot_init(unsigned start_frame);

/* Switch modes immediately from a frame hook/current scene context. */
void match_boot_force(unsigned char mode);

#endif

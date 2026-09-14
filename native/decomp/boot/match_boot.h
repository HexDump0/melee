#ifndef MELEE_DECOMP_BOOT_MATCH_BOOT_H
#define MELEE_DECOMP_BOOT_MATCH_BOOT_H

/*
 * S4: headless match entry.  `match_boot_init(N)` arms a VI-frame hook that
 * switches to the game's debug-VS mode after N frames (0 disarms).
 */
void match_boot_init(unsigned start_frame);

/* Non-zero once MELEE_GAMEOVER_TEST has driven the match into the 1P clear
 * overlay, so the viewer can probe the finished frame (P-698). */
int match_boot_gameover_active(void);

/* Non-zero once MELEE_INTRO_TEST has reached the Classic splash screen
 * (GS_INTRO_EASY), so the viewer can probe the finished frame (P-701). */
int match_boot_intro_active(void);

/* Non-zero once MELEE_CLASSIC_TEST has reached the 1P character-select
 * screen, so the viewer can probe the finished frame (P-700). */
int match_boot_classic_active(void);

/* Switch modes immediately from a frame hook/current scene context. */
void match_boot_force(unsigned char mode);

#endif

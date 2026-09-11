#ifndef MELEE_PLATFORM_PLATFORM_H
#define MELEE_PLATFORM_PLATFORM_H

/*
 * Shared declarations for the native platform layer (S1+).  Real backends and
 * clearly-labelled log-only stubs both live here; native/AI/learnings/
 * decomp_boot.md tracks which is which.
 */

/* Advance the virtual 40.5 MHz timebase by one 60 Hz frame. */
void boot_platform_advance_frame(void);

#endif /* MELEE_PLATFORM_PLATFORM_H */

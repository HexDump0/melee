#ifndef MELEE_PLATFORM_PLATFORM_H
#define MELEE_PLATFORM_PLATFORM_H

/*
 * Shared declarations for the native platform layer (S1+).  Real backends and
 * clearly-labelled log-only stubs both live here; native/AI/learnings/
 * decomp_boot.md tracks which is which.
 */

/* Advance the virtual 40.5 MHz timebase by one 60 Hz frame. */
void boot_platform_advance_frame(void);

/* Advance the virtual timebase by 1 ms and fire any due alarms.
 *
 * The compiled game idles in a pad-queue spin whose only platform call is
 * DVDGetDriveStatus, so the drive-status poll doubles as the host's "time
 * passes while the CPU spins" hook (real hardware gets this from the alarm
 * interrupt).  Used by the S4 boot to let OSSetPeriodicAlarm handlers run. */
void boot_platform_idle_tick(void);

/* Called once per emulated VI frame, after the game's own retrace callbacks.
 * The S4 boot uses it to script scene transitions (no window/OS thread yet). */
void boot_platform_set_frame_hook(void (*hook)(void));

#endif /* MELEE_PLATFORM_PLATFORM_H */

#ifndef MELEE_PLATFORM_PLATFORM_H
#define MELEE_PLATFORM_PLATFORM_H

#include <stddef.h>

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

/* Called once per emulated VI frame, after the frame hook.  The match viewer
 * uses it to render and present the GX HLE frame captured during the previous
 * game frame and to start the next capture. */
void boot_platform_set_present_hook(void (*hook)(void));

/*
 * Deterministic scripted input (S4).  `frames` is frame-major and
 * channel-minor: frames[frame * channels + channel].  Once the script is
 * exhausted every channel holds its last frame.  `channels` channels are
 * reported connected; the rest stay PAD_ERR_NO_CONTROLLER so the boot paths
 * that expect no controller are unaffected until a script is installed.
 */
typedef struct PadInputFrame {
    unsigned short buttons;
    signed char stick_x;
    signed char stick_y;
    signed char cstick_x;
    signed char cstick_y;
    unsigned char trigger_l;
    unsigned char trigger_r;
} PadInputFrame;

void pad_set_input_script(const PadInputFrame* frames, unsigned channels,
                          unsigned frame_count);

/* Live input from the host window (frontend mode).  The caller refreshes the
 * four channel states before each game frame; channels beyond `channels` stay
 * disconnected.  A script installed with pad_set_input_script takes
 * precedence, so the deterministic capture paths are unaffected. */
void pad_set_live_input(const PadInputFrame* frames, unsigned channels);

/* When enabled the script restarts from frame 0 after its last frame instead
 * of holding the last frame (the viewer's looping full-match demo input). */
void pad_set_input_loop(int enable);

/* Number of PADRead calls since the script was installed. */
unsigned pad_input_frame(void);

/* Reads a file from the mounted host disc image (port bootstrap helpers). */
int platform_disc_load_file(const char* disc_path, void** data, size_t* size);

/* Name of the disc file a loaded buffer came from, or NULL.  A hint for
 * diagnostics only -- buffers are reused, so an entry can outlive its data. */
const char* melee_dvd_origin(const void* ptr);

#endif /* MELEE_PLATFORM_PLATFORM_H */

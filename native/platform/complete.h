#ifndef MELEE_NATIVE_PLATFORM_COMPLETE_H
#define MELEE_NATIVE_PLATFORM_COMPLETE_H

/*
 * Deferred hardware-completion queue (S3).
 *
 * The GameCube completes DVD reads and ARAM DMAs from interrupt handlers, so
 * the game's DevCom/ARQ state machines set their "busy" flags *after* issuing
 * the request and expect the callback to run later.  A host backend that
 * invoked callbacks inline would re-enter those state machines before the
 * flags exist (DevCom would issue the same read twice and leave
 * HSD_DevCom_804D77F5 stuck at 1).
 *
 * Host backends therefore post the callback here and it runs at the next
 * "interrupts enabled" point (OSRestoreInterrupts/OSEnableInterrupts) or VI
 * retrace, which is where the console would take the hardware interrupt.
 */

typedef void (*PlatformCompletionFn)(void* arg);

void platform_complete_init(void);
void platform_post_completion(PlatformCompletionFn fn, void* arg);

/* Runs every completion queued so far; completions posted by a callback are
 * handled by the same drain.  Re-entrant calls return immediately. */
void platform_pump_completions(void);

#endif

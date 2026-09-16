#ifndef MELEE_DECOMP_BOOT_TRIAGE_H
#define MELEE_DECOMP_BOOT_TRIAGE_H

/*
 * S1 boot triage: a call-recording log for the compiled decompilation running
 * under the stubbed platform.  Every platform function that is still a
 * log-only stub reports here; real backends report their first hit too, so the
 * log reads as the boot's dependency order.  At a controlled stop the summary
 * lists the first-hit order with call counts, which is the backend work list.
 *
 * See native/AI/learnings/decomp_boot.md for the S1 results.
 */

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

typedef enum {
    BOOT_CAT_OS,
    BOOT_CAT_GX,
    BOOT_CAT_VI,
    BOOT_CAT_DVD,
    BOOT_CAT_PAD,
    BOOT_CAT_SI,
    BOOT_CAT_CARD,
    BOOT_CAT_AX,
    BOOT_CAT_AR,
    BOOT_CAT_THP,
    BOOT_CAT_HSD,
    BOOT_CAT_GAME,
    BOOT_CAT_OTHER,
    BOOT_CAT_COUNT
} BootCategory;

/* One-time setup.  out is the log stream (borrowed, not closed); when
 * trace != 0 every stub call is written, otherwise only the first hit of each
 * symbol is.  stub_limit stops the boot when exceeded (0 = no limit). */
void boot_triage_init(FILE* out, int trace, unsigned long stub_limit);

/* The stream OSReport and the triage share. */
FILE* boot_triage_out(void);

/* Log-only backend call.  Records first hit, counts repeats. */
void boot_triage_stub(const char* name, BootCategory category);

/* First-hit note for a real backend (kept out of the stub counters). */
void boot_triage_real(const char* name, BootCategory category);

/* Free-form note (milestones, flags, errors). */
void boot_triage_note(const char* fmt, ...);

/* Frame boundary (VI retrace).  Stops the boot at the frame budget. */
void boot_triage_frame(void);
void boot_triage_set_frame_budget(unsigned frames);
unsigned boot_triage_frames(void);

/* Stop the boot in a controlled way: print the reason and longjmp to the
 * harness.  Safe to call from a signal handler. */
void boot_triage_stop(const char* reason);
void boot_triage_install_stop_target(sigjmp_buf* env);
/* Whether a harness is waiting on that longjmp.  Without one boot_triage_stop
 * exits 0, which is the right answer for a finished boot run and the wrong one
 * for a panic -- OSPanic uses this to abort() instead. */
int boot_triage_has_stop_target(void);
extern volatile sig_atomic_t boot_triage_stopped;

/* Print the summary: totals per category and first-hit order. */
void boot_triage_summary(FILE* out);

unsigned long boot_triage_stub_calls(void);
unsigned boot_triage_stub_unique(void);
const char* boot_triage_stop_reason(void);
const char* boot_triage_crash_signal_name(void);

/* Captured in the signal handler, symbolized after the longjmp. */
void boot_triage_capture_crash(int signo);
void boot_triage_print_crash(FILE* out);

/* Installs a SIGSEGV/SIGBUS/SIGFPE/SIGILL handler that prints the signal and
 * a backtrace to stderr and then re-raises with the default action.  For the
 * windowed build, which has no stop target to longjmp to. */
void boot_triage_install_crash_reporter(void);

/* Symbolised backtrace of the caller, taken live rather than from the signal
 * handler's capture.  label says what prompted it, e.g. "panic". */
void boot_triage_print_backtrace(FILE* out, const char* label);

#endif /* MELEE_DECOMP_BOOT_TRIAGE_H */

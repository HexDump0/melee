/*
 * S1 boot triage implementation.  See boot_triage.h.
 *
 * The ordered first-hit table is intentionally small and fixed: the boot only
 * needs the first ~512 distinct backend symbols to produce a work list.
 */
#include "boot_triage.h"

#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define BOOT_MAX_SYMBOLS 512
#define BOOT_MAX_FRAMES 32

typedef struct {
    const char* name;
    BootCategory category;
    unsigned long calls;
} BootSymbol;

static FILE* out_stream;
static int trace_all;
static unsigned long stub_limit;
static unsigned long stub_calls;
static unsigned frame_budget = 60;
static unsigned frames;
static unsigned long unique_symbols;
static unsigned long real_symbols;
static unsigned long category_calls[BOOT_CAT_COUNT];
static BootSymbol symbols[BOOT_MAX_SYMBOLS];
static BootSymbol real[BOOT_MAX_SYMBOLS];
static sigjmp_buf* stop_target;
static void (*state_dumper)(FILE* out);

volatile sig_atomic_t boot_triage_stopped;

static void* crash_frames[BOOT_MAX_FRAMES];
static int crash_depth;
static int crash_signo;
/* P-800: the address the faulting access touched.  One number separates the
 * two things a SIGSEGV in this codebase is ever caused by -- a null-ish
 * offset means a NULL base with a struct member added to it and names the
 * field, while a wild value means a dangling or byte-swapped pointer.  The
 * handler has it for free and printing the stack without it threw it away. */
static void* crash_addr;
static int crash_have_addr;
static const char* stop_reason;

static const char* const category_names[BOOT_CAT_COUNT] = {
    "os",  "gx",   "vi",  "dvd",   "pad", "si",  "card",
    "ax",  "ar",   "thp", "hsd",   "game", "other",
};

void boot_triage_init(FILE* out, int trace, unsigned long limit)
{
    out_stream = out;
    trace_all = trace;
    stub_limit = limit;
}

FILE* boot_triage_out(void)
{
    return out_stream;
}

void boot_triage_set_state_dumper(void (*fn)(FILE* out))
{
    state_dumper = fn;
}

void boot_triage_dump_state(FILE* out)
{
    if (state_dumper != NULL && out != NULL) {
        state_dumper(out);
    }
}

static BootSymbol* record(BootSymbol* table, const char* name,
                          BootCategory category, unsigned long* count)
{
    BootSymbol* entry;

    for (entry = table; entry < table + BOOT_MAX_SYMBOLS; entry++) {
        if (entry->name == NULL) {
            break;
        }
        if (strcmp(entry->name, name) == 0) {
            entry->calls++;
            return entry;
        }
    }
    if (entry >= table + BOOT_MAX_SYMBOLS) {
        return NULL;
    }
    entry->name = name;
    entry->category = category;
    entry->calls = 1;
    (*count)++;
    return entry;
}

void boot_triage_stub(const char* name, BootCategory category)
{
    BootSymbol* entry;

    stub_calls++;
    category_calls[category]++;
    entry = record(symbols, name, category, &unique_symbols);
    if (trace_all) {
        fprintf(out_stream, "[boot] stub  %s\n", name);
    } else if (entry != NULL && entry->calls == 1) {
        fprintf(out_stream, "[boot] stub  %s (%s)\n", name,
                category_names[category]);
    }
    if (stub_limit != 0 && stub_calls >= stub_limit) {
        boot_triage_stop("stub call limit reached");
    }
}

void boot_triage_real(const char* name, BootCategory category)
{
    if (out_stream == NULL) {
        return;
    }
    if (record(real, name, category, &real_symbols) != NULL) {
        fprintf(out_stream, "[boot] real  %s (%s)\n", name,
                category_names[category]);
    }
}

void boot_triage_note(const char* fmt, ...)
{
    va_list ap;

    if (out_stream == NULL) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(out_stream, fmt, ap);
    va_end(ap);
}

void boot_triage_set_frame_budget(unsigned frames_in)
{
    frame_budget = frames_in;
}

unsigned boot_triage_frames(void)
{
    return frames;
}

void boot_triage_frame(void)
{
    frames++;
    if (out_stream != NULL) {
        fprintf(out_stream, "[boot] frame %u\n", frames);
    }
    if (frame_budget != 0 && frames >= frame_budget) {
        boot_triage_stop("frame budget reached");
    }
    if (stub_limit != 0 && stub_calls >= stub_limit) {
        boot_triage_stop("stub call limit reached");
    }
}

void boot_triage_install_stop_target(sigjmp_buf* env)
{
    stop_target = env;
}

int boot_triage_has_stop_target(void)
{
    return stop_target != NULL;
}

void boot_triage_stop(const char* reason)
{
    boot_triage_stopped = 1;
    stop_reason = reason;
    if (stop_target != NULL) {
        siglongjmp(*stop_target, 1);
    }
    if (out_stream != NULL) {
        fprintf(out_stream, "[boot] STOP: %s\n", reason);
        fflush(out_stream);
    }
    exit(0);
}

const char* boot_triage_stop_reason(void)
{
    return stop_reason;
}

void boot_triage_capture_crash_at(int signo, void* addr, int have_addr)
{
    crash_addr = addr;
    crash_have_addr = have_addr;
    boot_triage_capture_crash(signo);
}

void boot_triage_capture_crash(int signo)
{
    crash_signo = signo;
    crash_depth = backtrace(crash_frames, BOOT_MAX_FRAMES);
    boot_triage_stopped = 1;
    if (stop_target != NULL) {
        siglongjmp(*stop_target, 1);
    }
}

/* The owner plays the windowed build, and that build never installed a signal
 * handler -- only `boot_main.c` did -- so every segfault he hit reported
 * nothing at all while the headless harness printed a full stack for the same
 * fault.  Three separate crash reports this session arrived with no frames
 * for exactly that reason, and the first two were unrecoverable because the
 * process was gone by the time anyone noticed.
 *
 * This is deliberately *not* the harness's handler.  That one captures and
 * `siglongjmp`s to a stop target the viewer does not have, so without one it
 * returns straight back to the faulting instruction and spins.  Here the
 * report is printed on the spot and the signal is then re-raised with the
 * default action, so the process still dies the way it would have, a debugger
 * still sees the original signal, and the terminal has the stack either way. */
/* Set while the report is being produced, so a fault *inside* the report does
 * not start it again.  `backtrace()` is not async-signal-safe and unwinds the
 * very stack that has just gone wrong, so it is entirely capable of faulting;
 * without this the handler re-entered itself until the stack ran out and the
 * owner got thirty frames of `_Unwind_Backtrace` instead of his crash. */
static volatile sig_atomic_t in_crash_reporter;

static void crash_reporter(int signo, siginfo_t* info, void* ctx)
{
    (void) ctx;
    if (in_crash_reporter) {
        /* Second fault, inside the report.  Say so with a plain `write` --
         * `fprintf` is neither async-signal-safe nor trustworthy here -- and
         * let the default action end the process.  A truncated report with a
         * reason beats an infinite one. */
        static const char msg[] =
            "[boot] the crash reporter faulted; no backtrace for this one\n";
        ssize_t ignored = write(2, msg, sizeof(msg) - 1);
        (void) ignored;
        signal(signo, SIG_DFL);
        raise(signo);
        _exit(128 + signo);
    }
    in_crash_reporter = 1;
    boot_triage_capture_crash_at(signo, info != NULL ? info->si_addr : NULL,
                                 info != NULL);
    /* print_crash prints the `controlled stop:` line itself. */
    boot_triage_print_crash(stderr);
    boot_triage_dump_state(stderr);
    fflush(stderr);
    signal(signo, SIG_DFL);
    raise(signo);
}

void boot_triage_install_crash_reporter(void)
{
    /* SIGSEGV/SIGBUS/SIGFPE/SIGILL only.  SIGABRT is left alone because the
     * panic path already prints its own backtrace before aborting, and
     * SIGALRM because the viewer has no watchdog and SDL may use timers. */
    static const int signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL };
    static int alt_stack_ready;
    struct sigaction sa;
    size_t i;

    if (getenv("MELEE_NO_CRASH_HANDLER") != NULL) {
        return; /* let a sanitizer runtime report the failure itself */
    }
    /* **An alternate signal stack, because the interesting crashes are the
     * ones that exhaust the stack.** Without it a stack overflow cannot be
     * reported at all: the handler is entered on the stack that has just run
     * out, faults on its first push, and the process dies with nothing said.
     * That is what the owner's Data/VS-Records crash looked like -- a fault
     * inside `backtrace()` repeating until the frames scrolled away. */
    if (!alt_stack_ready) {
        /* A fixed 64 KB rather than `SIGSTKSZ`: on current glibc that macro
         * expands to a `sysconf()` call and is not a constant expression. */
        static char alt[65536];
        stack_t ss;
        memset(&ss, 0, sizeof(ss));
        ss.ss_sp = alt;
        ss.ss_size = sizeof(alt);
        ss.ss_flags = 0;
        if (sigaltstack(&ss, NULL) == 0) {
            alt_stack_ready = 1;
        }
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_reporter;
    sigemptyset(&sa.sa_mask);
    /* **`SA_NODEFER` is deliberately absent.** It used to be set, which left
     * the signal unblocked inside its own handler, so a fault while producing
     * the report re-entered the handler instead of ending the process.  With
     * it blocked, the `raise()` below is delivered when the handler returns
     * and the default action still kills the process, which is all it was
     * there for. */
    sa.sa_flags = SA_SIGINFO | (alt_stack_ready ? SA_ONSTACK : 0);
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        sigaction(signals[i], &sa, NULL);
    }
}

const char* boot_triage_crash_signal_name(void)
{
    switch (crash_signo) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    case SIGALRM:
        return "SIGALRM";
    default:
        return "";
    }
}

/* `dladdr` only resolves dynamic symbols, so every `static` function -- which
 * is most of the platform layer -- came out as a bare `<unknown>` with the
 * address thrown away.  Three of the five frames in the owner's Home-Run
 * crash report were unusable for exactly that reason.  Always print the
 * address, and next to it the containing module and the offset within it,
 * which is what `addr2line -e <module> <offset>` wants and which survives
 * ASLR. */
static void print_frame(FILE* out, int i, void* pc)
{
    Dl_info info;

    if (dladdr(pc, &info) == 0) {
        fprintf(out, "[boot]   #%d %p\n", i, pc);
        return;
    }
    if (info.dli_sname != NULL) {
        fprintf(out, "[boot]   #%d %s+0x%lx", i, info.dli_sname,
                (unsigned long) ((char*) pc - (char*) info.dli_saddr));
    } else {
        fprintf(out, "[boot]   #%d %p", i, pc);
    }
    if (info.dli_fname != NULL && info.dli_fbase != NULL) {
        fprintf(out, "  [%s+0x%lx]", info.dli_fname,
                (unsigned long) ((char*) pc - (char*) info.dli_fbase));
    }
    fputc('\n', out);
}

/* One address, symbolised the way `print_frame` does it, for callers that
 * want a callback's name inside a line of their own rather than a backtrace.
 * Same `dladdr` caveat: a `static` function resolves to the nearest exported
 * one, so the module offset is always printed too. */
void boot_triage_symbol(const void* pc, char* buf, size_t n)
{
    Dl_info info;

    if (buf == NULL || n == 0) {
        return;
    }
    if (pc == NULL) {
        snprintf(buf, n, "(null)");
        return;
    }
    if (dladdr((void*) (uintptr_t) pc, &info) != 0 && info.dli_sname != NULL) {
        snprintf(buf, n, "%s+0x%lx", info.dli_sname,
                 (unsigned long) ((char*) pc - (char*) info.dli_saddr));
        return;
    }
    snprintf(buf, n, "%p", pc);
}

void boot_triage_print_backtrace(FILE* out, const char* label)
{
    void* frames_here[BOOT_MAX_FRAMES];
    int depth;
    int i;

    if (out == NULL) {
        return;
    }
    depth = backtrace(frames_here, BOOT_MAX_FRAMES);
    fprintf(out, "[boot] backtrace (%s):\n", label);
    for (i = 0; i < depth; i++) {
        print_frame(out, i, frames_here[i]);
    }
    fflush(out);
}

void boot_triage_print_crash(FILE* out)
{
    int i;

    if (crash_signo == 0) {
        return;
    }
    if (crash_have_addr) {
        fprintf(out, "[boot] controlled stop: %s at %p\n",
                boot_triage_crash_signal_name(), crash_addr);
    } else {
        fprintf(out, "[boot] controlled stop: %s\n",
                boot_triage_crash_signal_name());
    }
    fprintf(out, "[boot] backtrace (captured in handler):\n");
    for (i = 0; i < crash_depth; i++) {
        print_frame(out, i, crash_frames[i]);
    }
}

void boot_triage_summary(FILE* out)
{
    BootCategory c;
    unsigned long i;

    if (out == NULL) {
        return;
    }
    fprintf(out, "[boot] ------------------------------------------------\n");
    fprintf(out, "[boot] summary: frames=%u stub_calls=%lu unique=%lu\n", frames,
            stub_calls, unique_symbols);
    for (c = 0; c < BOOT_CAT_COUNT; c++) {
        if (category_calls[c] != 0) {
            fprintf(out, "[boot]   %-5s %lu\n", category_names[c],
                    category_calls[c]);
        }
    }
    fprintf(out, "[boot] first-hit order (real backends):\n");
    for (i = 0; i < real_symbols && i < BOOT_MAX_SYMBOLS; i++) {
        if (real[i].name != NULL) {
            fprintf(out, "[boot]   real  %-28s (%s) x%lu\n", real[i].name,
                    category_names[real[i].category], real[i].calls);
        }
    }
    fprintf(out, "[boot] first-hit order (stubs):\n");
    for (i = 0; i < unique_symbols && i < BOOT_MAX_SYMBOLS; i++) {
        if (symbols[i].name != NULL) {
            fprintf(out, "[boot]   %3lu. %-28s (%s) x%lu\n", i + 1,
                    symbols[i].name, category_names[symbols[i].category],
                    symbols[i].calls);
        }
    }
    if (unique_symbols > BOOT_MAX_SYMBOLS) {
        fprintf(out, "[boot]   (%lu more symbols not shown)\n",
                unique_symbols - BOOT_MAX_SYMBOLS);
    }
    fflush(out);
}

unsigned long boot_triage_stub_calls(void)
{
    return stub_calls;
}

unsigned boot_triage_stub_unique(void)
{
    return (unsigned) unique_symbols;
}

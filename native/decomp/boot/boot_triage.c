/*
 * S1 boot triage implementation.  See boot_triage.h.
 *
 * The ordered first-hit table is intentionally small and fixed: the boot only
 * needs the first ~512 distinct backend symbols to produce a work list.
 */
#include "boot_triage.h"

#ifdef _WIN32
/*
 * Windows has no `backtrace`, no `dladdr` and no `sigaction`.  DbgHelp walks
 * the stack and resolves symbols, and a vectored exception handler is the
 * equivalent hook -- it runs before the process dies, which is all the crash
 * reporter needs.  `dbghelp.h` requires `windows.h` first.
 */
#include <windows.h>
#include <dbghelp.h>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#endif
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* One place that knows how this platform walks a stack. */
static int boot_capture(void** frames, int max)
{
    return (int) CaptureStackBackTrace(0, (ULONG) max, frames, NULL);
}

static void boot_sym_init(void)
{
    static int done;
    if (!done) {
        done = 1;
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);
    }
}

/* Writes "name+0xoff" when a symbol is known, else nothing.  Returns whether
 * it wrote.  The module and offset are printed by the caller either way, for
 * the same reason the POSIX path does it: a `static` function resolves to the
 * nearest exported one, so the raw offset is what stays usable. */
static int boot_sym_name(void* pc, char* out, size_t n)
{
    char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO* sym = (SYMBOL_INFO*) buf;
    DWORD64 disp = 0;

    boot_sym_init();
    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = MAX_SYM_NAME;
    if (!SymFromAddr(GetCurrentProcess(), (DWORD64) (uintptr_t) pc, &disp,
                     sym))
    {
        return 0;
    }
    snprintf(out, n, "%s+0x%lx", sym->Name, (unsigned long) disp);
    return 1;
}
#else
static int boot_capture(void** frames, int max)
{
    return backtrace(frames, max);
}

static int boot_sym_name(void* pc, char* out, size_t n)
{
    Dl_info info;
    if (dladdr(pc, &info) == 0 || info.dli_sname == NULL) {
        return 0;
    }
    snprintf(out, n, "%s+0x%lx", info.dli_sname,
             (unsigned long) ((char*) pc - (char*) info.dli_saddr));
    return 1;
}
#endif

/* The module a pc belongs to and the offset within it: what
 * `addr2line -e <module> <offset>` wants, and what survives ASLR. */
static int boot_module(void* pc, const char** name, unsigned long* off)
{
#ifdef _WIN32
    static char path[MAX_PATH];
    HMODULE mod = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR) pc, &mod) ||
        mod == NULL)
    {
        return 0;
    }
    if (GetModuleFileNameA(mod, path, sizeof(path)) == 0) {
        return 0;
    }
    *name = path;
    *off = (unsigned long) ((char*) pc - (char*) mod);
    return 1;
#else
    Dl_info info;
    if (dladdr(pc, &info) == 0 || info.dli_fname == NULL ||
        info.dli_fbase == NULL)
    {
        return 0;
    }
    *name = info.dli_fname;
    *off = (unsigned long) ((char*) pc - (char*) info.dli_fbase);
    return 1;
#endif
}

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
static BootJmpBuf* stop_target;
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

void boot_triage_install_stop_target(BootJmpBuf* env)
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
        BOOT_LONGJMP(*stop_target, 1);
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
    crash_depth = boot_capture(crash_frames, BOOT_MAX_FRAMES);
    boot_triage_stopped = 1;
    if (stop_target != NULL) {
        BOOT_LONGJMP(*stop_target, 1);
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

#ifdef _WIN32
/*
 * Windows has no signals worth the name for this: a vectored exception handler
 * is the hook, and it runs before the process unwinds, which is all the
 * reporter needs.
 *
 * There is no alternate-stack equivalent, and that costs something real -- a
 * stack overflow is reported on the stack that just overflowed.  Windows gives
 * one guard page of grace, which `CaptureStackBackTrace` fits inside where
 * glibc's `backtrace()` did not, so the common case still prints.  It is a
 * weaker guarantee than `sigaltstack` and worth knowing before trusting a
 * stack-overflow report from this build.
 */
static int exception_signo(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_STACK_OVERFLOW:
        return SIGSEGV;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_INVALID_OPERATION:
        return SIGFPE;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        return SIGILL;
    default:
        return 0;
    }
}

static LONG CALLBACK crash_reporter_seh(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
    int signo = exception_signo(rec->ExceptionCode);
    void* addr = NULL;
    int have_addr = 0;

    if (signo == 0) {
        return EXCEPTION_CONTINUE_SEARCH; /* not ours: a C++ throw, a probe */
    }
    if (in_crash_reporter) {
        static const char msg[] =
            "[boot] the crash reporter faulted; no backtrace for this one\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg,
                  (DWORD) (sizeof(msg) - 1), &written, NULL);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    in_crash_reporter = 1;

    /* An access violation records the faulting address as its second
     * parameter; nothing else does. */
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        rec->NumberParameters >= 2)
    {
        addr = (void*) rec->ExceptionInformation[1];
        have_addr = 1;
    }
    boot_triage_capture_crash_at(signo, addr, have_addr);
    boot_triage_print_crash(stderr);
    boot_triage_dump_state(stderr);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH; /* let the process die as it would */
}

void boot_triage_install_crash_reporter(void)
{
    static int installed;

    if (getenv("MELEE_NO_CRASH_HANDLER") != NULL) {
        return;
    }
    if (installed) {
        return;
    }
    installed = 1;
    /* First in the chain, so a debugger or a runtime that installs later
     * cannot swallow the fault before it is reported. */
    AddVectoredExceptionHandler(1, crash_reporter_seh);
}
#else
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
#endif /* _WIN32 */

const char* boot_triage_crash_signal_name(void)
{
    switch (crash_signo) {
    case SIGSEGV:
        return "SIGSEGV";
#ifdef SIGBUS
    /* Windows has no SIGBUS: an unaligned or bad-page access arrives as an
     * access violation, which maps to SIGSEGV above. */
    case SIGBUS:
        return "SIGBUS";
#endif
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
#ifdef SIGALRM
    case SIGALRM:
        return "SIGALRM";
#endif
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
    char sym[512];
    const char* mod = NULL;
    unsigned long off = 0;

    if (boot_sym_name(pc, sym, sizeof(sym))) {
        fprintf(out, "[boot]   #%d %s", i, sym);
    } else {
        fprintf(out, "[boot]   #%d %p", i, pc);
    }
    if (boot_module(pc, &mod, &off)) {
        fprintf(out, "  [%s+0x%lx]", mod, off);
    }
    fputc('\n', out);
}

/* One address, symbolised the way `print_frame` does it, for callers that
 * want a callback's name inside a line of their own rather than a backtrace.
 * Same `dladdr` caveat: a `static` function resolves to the nearest exported
 * one, so the module offset is always printed too. */
void boot_triage_symbol(const void* pc, char* buf, size_t n)
{
    if (buf == NULL || n == 0) {
        return;
    }
    if (pc == NULL) {
        snprintf(buf, n, "(null)");
        return;
    }
    if (boot_sym_name((void*) (uintptr_t) pc, buf, n)) {
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
    depth = boot_capture(frames_here, BOOT_MAX_FRAMES);
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

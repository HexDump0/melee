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

volatile sig_atomic_t boot_triage_stopped;

static void* crash_frames[BOOT_MAX_FRAMES];
static int crash_depth;
static int crash_signo;
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

void boot_triage_capture_crash(int signo)
{
    crash_signo = signo;
    crash_depth = backtrace(crash_frames, BOOT_MAX_FRAMES);
    boot_triage_stopped = 1;
    if (stop_target != NULL) {
        siglongjmp(*stop_target, 1);
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

void boot_triage_print_crash(FILE* out)
{
    int i;

    if (crash_signo == 0) {
        return;
    }
    fprintf(out, "[boot] controlled stop: %s\n",
            boot_triage_crash_signal_name());
    fprintf(out, "[boot] backtrace (captured in handler):\n");
    for (i = 0; i < crash_depth; i++) {
        Dl_info info;

        if (dladdr(crash_frames[i], &info) != 0 && info.dli_sname != NULL) {
            fprintf(out, "[boot]   #%d %s+0x%lx\n", i, info.dli_sname,
                    (unsigned long) ((char*) crash_frames[i] -
                                     (char*) info.dli_saddr));
        } else {
            fprintf(out, "[boot]   #%d <unknown>\n", i);
        }
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

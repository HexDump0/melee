/*
 * S1 platform OS backend.
 *
 * Real where the compiled engine depends on it for correctness:
 *   - arena setup (OSInit) and physical memory queries,
 *   - the 40.5 MHz timebase (OSGetTick/OSGetTime/OSTicksToCalendarTime),
 *   - interrupt enable/disable bookkeeping,
 *   - OSReport/OSPanic/OSSetErrorHandler and the FPU context,
 *   - cache maintenance (a no-op on x86, which is the correct host behavior).
 *
 * The arena and heap allocators themselves are the decompilation's own
 * OSAlloc.c/OSArena.c, compiled verbatim from extern/dolphin (see
 * native/CMakeLists.txt); this file only seeds the arena and implements what
 * those TUs do not define.
 *
 * Deterministic virtual time: the timebase starts at zero and advances by a
 * fixed step (1 ms at 40.5 MHz) on every read, and by one 60 Hz frame on
 * VIWaitForRetrace.  That keeps the boot log and the game's RNG seed
 * reproducible; the real 60 Hz pacing decision is S4.
 */
#include <dolphin/base/PPCArch.h>
#include <dolphin/db.h>
#include <dolphin/os.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include "decomp/boot/boot_triage.h"
#include "platform/complete.h"

#define OS_TICKS_PER_MSEC 40500ULL
#define OS_TICKS_PER_SEC 40500000ULL
#define OS_TICKS_PER_FRAME (OS_TICKS_PER_SEC / 60)
#define OS_ARENA_SIZE (24u * 1024u * 1024u)

/* Defined by the compiled extern/dolphin DVDFS; set by the SDK's OSInit.  The
 * symbol is weak so the audio unit test (which links this file without the
 * DVDFS TU) still links. */
extern unsigned long __DVDLongFileNameFlag __attribute__((weak));

/*
 * GameCube main RAM lives at the cached address 0x80000000 and the first
 * 0x3000 bytes are the OS boot info (disk ID, clock speeds, arena bounds).
 * The port maps the whole 24 MB there so compiled game code that classifies
 * pointers by address keeps working: e.g. `lbFile_800164A4` chooses the DVD
 * read type from `dst >= 0x80000000` and `ftData_80085E50` decides between an
 * ARAM DMA and a memcpy with `addr < 0x80000000`.
 */
#define GC_CACHED_BASE 0x80000000u
#define GC_BOOT_INFO_SIZE 0x3000u
#define GC_RAM_SIZE OS_ARENA_SIZE
#define GC_BUS_CLOCK 162000000u
#define GC_CORE_CLOCK 486000000u
#define GC_PHYS_MEM_SIZE_OFF 0x0028u
#define GC_SIM_MEM_SIZE_OFF 0x00F0u
#define GC_BUS_CLOCK_OFF 0x00F8u
#define GC_CORE_CLOCK_OFF 0x00FCu

#ifndef MAP_FIXED_NOREPLACE
/* Older libc headers: fall back to the plain fixed mapping. */
#define MAP_FIXED_NOREPLACE MAP_FIXED
#endif

static void map_gc_ram(void)
{
    void* page;

    page = mmap((void*) GC_CACHED_BASE, GC_RAM_SIZE,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (page == MAP_FAILED) {
        boot_triage_note("[boot] OSInit: cannot map GC main RAM at 0x%08x; "
                         "compiled game code cannot run\n",
                         GC_CACHED_BASE);
        boot_triage_stop("OSInit: GC main RAM mapping failed");
        return;
    }
    *(volatile u32*) (GC_CACHED_BASE + GC_PHYS_MEM_SIZE_OFF) = OS_ARENA_SIZE;
    *(volatile u32*) (GC_CACHED_BASE + GC_SIM_MEM_SIZE_OFF) = OS_ARENA_SIZE;
    *(volatile u32*) (GC_CACHED_BASE + GC_BUS_CLOCK_OFF) = GC_BUS_CLOCK;
    *(volatile u32*) (GC_CACHED_BASE + GC_CORE_CLOCK_OFF) = GC_CORE_CLOCK;
}

static u64 virtual_ticks;
static u32 sound_mode = OS_SOUND_MODE_STEREO;
static u32 progressive_mode;
static BOOL interrupts_enabled = TRUE;
static OSContext current_context;
static OSErrorHandler error_table[OS_ERROR_MAX];

/* --------------------------------------------------------------- alarms
 * Deterministic host scheduling: alarms fire from `advance_ticks` (every
 * OSGetTime/OSGetTick read and each VI frame), so a handler can never run
 * while the virtual clock stands still.  The game installs a periodic
 * 1/60 s alarm that renews the raw pad status; without it the scene loop
 * spins on an empty pad queue (S4 boot triage). */

#define BOOT_MAX_ALARMS 32

typedef struct {
    OSAlarm* alarm;
    OSAlarmHandler handler;
    u64 deadline;
    u64 period; /* 0 = one-shot */
} HostAlarm;

static HostAlarm host_alarms[BOOT_MAX_ALARMS];
static int alarm_pumping;

static HostAlarm* alarm_find(OSAlarm* alarm)
{
    int i;
    for (i = 0; i < BOOT_MAX_ALARMS; i++) {
        if (host_alarms[i].alarm == alarm) {
            return &host_alarms[i];
        }
    }
    return NULL;
}

static HostAlarm* alarm_slot(OSAlarm* alarm)
{
    HostAlarm* slot = alarm_find(alarm);
    int i;
    if (slot != NULL) {
        return slot;
    }
    for (i = 0; i < BOOT_MAX_ALARMS; i++) {
        if (host_alarms[i].alarm == NULL) {
            host_alarms[i].alarm = alarm;
            return &host_alarms[i];
        }
    }
    return NULL;
}

static void alarm_pump(void)
{
    int guard;

    if (alarm_pumping) {
        return;
    }
    alarm_pumping = 1;
    for (guard = 0; guard < BOOT_MAX_ALARMS * 4; guard++) {
        HostAlarm* due = NULL;
        OSAlarmHandler handler;
        OSAlarm* alarm;
        int i;
        for (i = 0; i < BOOT_MAX_ALARMS; i++) {
            HostAlarm* h = &host_alarms[i];
            if (h->alarm != NULL && h->deadline <= virtual_ticks) {
                due = h;
                break;
            }
        }
        if (due == NULL) {
            break;
        }
        alarm = due->alarm;
        handler = due->handler;
        if (due->period != 0) {
            due->deadline = virtual_ticks + due->period;
        } else {
            due->alarm = NULL;
            due->handler = NULL;
        }
        if (handler != NULL) {
            handler(alarm, &current_context);
        }
    }
    alarm_pumping = 0;
}

/* ------------------------------------------------------------- arena / init */

void OSInit(void)
{
    boot_triage_real("OSInit", BOOT_CAT_OS);

    map_gc_ram();
    /* Boot info keeps the first 0x3000 bytes, like the console; the arena is
     * the rest of main RAM. */
    OSSetArenaLo((void*) (GC_CACHED_BASE + GC_BOOT_INFO_SIZE));
    OSSetArenaHi((void*) (GC_CACHED_BASE + GC_RAM_SIZE));
    /* The SDK's OSInit enables long DVD file names (the retail disc uses
     * names like `nr_select.ssm`); dvdfs.c panics on non-8.3 paths without
     * it.  The real OSInit sets this unconditionally after BootInfo. */
    if (&__DVDLongFileNameFlag != NULL) {
        __DVDLongFileNameFlag = 1;
    }
    platform_complete_init();
}

u32 OSGetPhysicalMemSize(void)
{
    return OS_ARENA_SIZE;
}

u32 OSGetConsoleSimulatedMemSize(void)
{
    return OS_ARENA_SIZE;
}

/* -------------------------------------------------------------------- time */

static u64 advance_ticks(u64 step)
{
    virtual_ticks += step;
    alarm_pump();
    return virtual_ticks;
}

OSTick OSGetTick(void)
{
    return (OSTick) advance_ticks(OS_TICKS_PER_MSEC);
}

/* Seconds from the GameCube epoch (2000-01-01) to the host's wall clock,
 * sampled once.  The virtual timebase starts at zero, which is deliberate --
 * it keeps tick deltas and the boot log reproducible -- but OSGetTime is also
 * what the game stamps save files with and what the boot banner prints as the
 * calendar date, and zero there means every save claims 2000-01-01.  Adding a
 * fixed base leaves every difference between two OSGetTime reads untouched
 * while giving absolute reads the real date.  OSGetTick, which is what the
 * timing paths use, stays zero-based. */
#define GC_EPOCH_UNIX 946684800LL

static u64 wall_base_ticks;
static int wall_base_valid;

static u64 wall_base(void)
{
    if (!wall_base_valid) {
        wall_base_ticks = 0;
        /* Off by default.  A real clock makes the attract demo differ from run
         * to run, and the deterministic tests (decomp_opening's idle run in
         * particular) depend on replaying the same demo -- turning it on
         * surfaced a genuine, unrelated crash in Ness' yo-yo item allocation,
         * which is a bug to fix rather than a reason to randomise CI. */
        if (getenv("MELEE_WALL_CLOCK") != NULL) {
            time_t now = time(NULL);
            long long delta = (long long) now - GC_EPOCH_UNIX;

            /* A host clock before 2000 (or an unset RTC) would make the stamp
             * negative; fall back to the epoch rather than emit a
             * 20th-century date the game's date handling has never seen. */
            if (delta > 0) {
                wall_base_ticks = (u64) delta * OS_TICKS_PER_SEC;
            }
        }
        wall_base_valid = 1;
    }
    return wall_base_ticks;
}

OSTime OSGetTime(void)
{
    return (OSTime) (wall_base() + advance_ticks(OS_TICKS_PER_MSEC));
}

/* Advance the virtual clock by one 60 Hz frame (used by VI pacing). */
void boot_platform_advance_frame(void)
{
    advance_ticks(OS_TICKS_PER_FRAME);
}

/* 1 ms of virtual time; the compiled idle loops poll the drive status, so
 * that poll is where the alarm interrupt gets emulated (see platform.h).
 * DVDGetDriveStatus is also the wait point of the engine's synchronous
 * loaders (`AXDriver_8038DA70` spins on a DVD read callback with no VI in
 * sight), so deferred hardware completions are delivered here too when
 * interrupts are enabled; with interrupts disabled the completion must wait
 * for the OSRestoreInterrupts point, exactly as on hardware. */
void boot_platform_idle_tick(void)
{
    advance_ticks(OS_TICKS_PER_MSEC);
    if (interrupts_enabled) {
        platform_pump_completions();
    }
}

/* 2000-01-01 00:00:00 is the GameCube epoch. */
static void days_to_calendar(long long days, OSCalendarTime* td)
{
    long long era;
    long long unix_days;
    unsigned doe;
    unsigned yoe;
    unsigned doy;
    unsigned mp;
    int year;
    int month;
    int day;

    /* Howard Hinnant's civil_from_days.  `days` arrives counted from
     * 1970-01-01; the algorithm works in eras starting 0000-03-01, which is
     * 719468 days earlier, and it yields the real year (2002), not a
     * struct tm year-minus-1900.
     *
     * Both of those were previously missing -- no era shift, and a +1900 on
     * the result -- which is self-consistent enough to look plausible and is
     * wrong by seventy years: the GameCube epoch came out as 1930-03-01
     * instead of 2000-01-01, which is what the boot banner's "GC Calendar
     * Year 1930 Month 3 Day 1" was. */
    unix_days = days;
    days += 719468;

    era = (days >= 0 ? days : days - 146096) / 146097;
    doe = (unsigned) (days - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = (int) yoe + (int) (era * 400);
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    day = (int) (doy - (153 * mp + 2) / 5 + 1);
    month = (int) (mp < 10 ? mp + 3 : mp - 9);
    year += (month <= 2);

    td->year = year;
    td->mon = month - 1;
    td->mday = day;
    /* 1970-01-01 was a Thursday, so this counts from the unshifted value. */
    td->wday = (int) (((unix_days % 7) + 11) % 7);
    td->yday = 0;
}

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td)
{
    u64 seconds;
    u64 days;
    u64 rem;
    int i;
    int feb;
    static const int month_days[12] = { 31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31 };

    if (td == NULL) {
        return;
    }
    memset(td, 0, sizeof(*td));
    seconds = ticks / OS_TICKS_PER_SEC;
    days = seconds / 86400;
    rem = seconds % 86400;

    /* Days since 2000-01-01: 1970-01-01 + 10957 days. */
    days_to_calendar((long long) days + 10957, td);
    td->hour = (int) (rem / 3600);
    rem %= 3600;
    td->min = (int) (rem / 60);
    td->sec = (int) (rem % 60);
    td->msec = (int) ((ticks % OS_TICKS_PER_SEC) / OS_TICKS_PER_MSEC);
    td->usec = 0;

    feb = 28;
    if (td->year % 4 == 0 && (td->year % 100 != 0 || td->year % 400 == 0)) {
        feb = 29;
    }
    for (i = 0; i < td->mon; i++) {
        td->yday += (i == 1) ? feb : month_days[i];
    }
    td->yday += td->mday - 1;
}

/* --------------------------------------------------------------- interrupts
 * The GameCube delivers hardware completions (DVD, ARQ DMA) from interrupt
 * handlers, so the host runs its deferred completion queue at the point
 * interrupts become enabled again.  DevCom/ARQ set their busy flags before
 * that point, which is what keeps their re-entrancy-safe. */

BOOL OSDisableInterrupts(void)
{
    BOOL old = interrupts_enabled;
    interrupts_enabled = FALSE;
    return old;
}

BOOL OSRestoreInterrupts(BOOL level)
{
    BOOL old = interrupts_enabled;
    interrupts_enabled = level;
    if (level) {
        platform_pump_completions();
    }
    return old;
}

BOOL OSEnableInterrupts(void)
{
    return OSRestoreInterrupts(TRUE);
}

/* ----------------------------------------------------------------- contexts */

OSContext* OSGetCurrentContext(void)
{
    return &current_context;
}

void OSSetCurrentContext(OSContext* context)
{
    if (context != NULL) {
        current_context = *context;
    }
}

void OSClearContext(OSContext* context)
{
    if (context != NULL) {
        memset(context, 0, sizeof(*context));
    }
}

void OSSaveFPUContext(OSContext* context)
{
    (void) context;
}

void OSLoadFPUContext(OSContext* context)
{
    (void) context;
}

u32 OSSaveContext(OSContext* context)
{
    (void) context;
    return 0;
}

u32 OSGetStackPointer(void)
{
    return 0;
}

/* ------------------------------------------------------------ misc console */

u32 OSGetSoundMode(void)
{
    return sound_mode;
}

void OSSetSoundMode(u32 mode)
{
    sound_mode = mode;
}

u32 OSGetProgressiveMode(void)
{
    return progressive_mode;
}

void OSSetProgressiveMode(u32 mode)
{
    progressive_mode = mode;
}

unsigned long OSGetResetCode(void)
{
    /* 0x80000000 is the game's "reset to menu" code: gmMainLib_8015FCC0 maps
     * it to skip_intro, which routes GM_BOOT to the memory-card scene instead
     * of the opening movie.  The skip-intro path stays the default until the
     * opening is verified headlessly; MELEE_OPENING=1 cold-boots into
     * GM_OPENING_MV and plays MvOpen.mth (P-685). */
    if (getenv("MELEE_OPENING") != NULL) {
        return 0;
    }
    return 0x80000000;
}

BOOL OSGetResetSwitchState(void)
{
    return FALSE;
}

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu)
{
    (void) reset;
    (void) resetCode;
    (void) forceMenu;
    boot_triage_stub("OSResetSystem", BOOT_CAT_OS);
    boot_triage_stop("OSResetSystem requested");
}

BOOL DBIsDebuggerPresent(void)
{
    return FALSE;
}

u32 PPCMfmsr(void)
{
    return 0;
}

void PPCMtmsr(u32 msr)
{
    (void) msr;
}

/* --------------------------------------------------------------- cache ops */

void DCFlushRange(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
}

void DCStoreRange(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
}

void DCFlushRangeNoSync(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
}

void DCInvalidateRange(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
}

/* ---------------------------------------------------------- report / panic */

/* The game narrates its own failures through OSReport -- "**** Not Found Toy
 * Model!(%d)", "*** BG data aren't being loaded!" and so on -- and the line
 * before a panic is usually the one that names the bug.  The viewer sends the
 * triage stream to /dev/null by default, so those lines were being discarded
 * exactly when they mattered.
 *
 * Echoing all of them to stderr would bury the boot in traffic, so keep the
 * last few in a ring and dump it when something panics.  MELEE_LOG_REPORTS=1
 * echoes every line live instead, for when the interesting one scrolled past.
 *
 * Written from whichever thread is reporting, with no lock: a torn line in a
 * crash dump is an acceptable trade for not serialising OSReport. */
#define OS_REPORT_RING 64
#define OS_REPORT_LINE 256

static char report_ring[OS_REPORT_RING][OS_REPORT_LINE];
static unsigned report_next;
static int report_echo = -1;

void os_report_dump(FILE* out)
{
    unsigned n = report_next < OS_REPORT_RING ? report_next : OS_REPORT_RING;
    unsigned i;

    if (out == NULL || n == 0) {
        return;
    }
    fprintf(out, "[boot] last %u OSReport line%s before the panic:\n", n,
            n == 1 ? "" : "s");
    for (i = 0; i < n; i++) {
        const char* line = report_ring[(report_next - n + i) % OS_REPORT_RING];
        fprintf(out, "[boot]   %s", line);
        if (line[0] != '\0' && line[strlen(line) - 1] != '\n') {
            fputc('\n', out);
        }
    }
    fflush(out);
}

void OSReport(char* msg, ...)
{
    va_list ap;
    FILE* out = boot_triage_out();
    char* slot;

    if (report_echo < 0) {
        report_echo = getenv("MELEE_LOG_REPORTS") != NULL;
    }

    slot = report_ring[report_next % OS_REPORT_RING];
    report_next++;
    va_start(ap, msg);
    vsnprintf(slot, OS_REPORT_LINE, msg, ap);
    va_end(ap);

    if (report_echo) {
        fprintf(stderr, "[report] %s", slot);
        if (slot[0] != '\0' && slot[strlen(slot) - 1] != '\n') {
            fputc('\n', stderr);
        }
        fflush(stderr);
    }

    if (out == NULL) {
        out = stderr;
    }
    va_start(ap, msg);
    vfprintf(out, msg, ap);
    va_end(ap);
    fflush(out);
}

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler)
{
    OSErrorHandler old;

    if ((u32) error >= (u32) OS_ERROR_MAX) {
        return NULL;
    }
    old = error_table[error];
    error_table[error] = handler;
    return old;
}

void OSPanic(const char* file, int line, const char* msg, ...)
{
    va_list ap;
    FILE* out = boot_triage_out();

    if (out == NULL) {
        out = stderr;
    }
    va_start(ap, msg);
    vfprintf(out, msg, ap);
    va_end(ap);
    fprintf(out, " in \"%s\" on line %d.\n", file, line);
    fflush(out);

    /* The viewer points the triage stream at /dev/null unless
     * MELEE_VIEWER_TRIAGE is set, so the report above goes nowhere and the
     * process then leaves through boot_triage_stop's exit(0) -- a failed
     * assertion is indistinguishable from the user closing the window: no
     * message, status 0, and no core to open.  A panic is never something to
     * swallow, so repeat it on stderr unconditionally. */
    if (out != stderr) {
        fprintf(stderr, "\n*** PANIC: ");
        va_start(ap, msg);
        vfprintf(stderr, msg, ap);
        va_end(ap);
        fprintf(stderr, " in \"%s\" on line %d.\n", file, line);
        fflush(stderr);
    }

    /* Most panics are HSD_ASSERT, whose message names the file and line of the
     * assert but says nothing about how the game got there -- and the game got
     * there through a mode/scene callback that the assert site cannot name.
     * The backtrace is the part that identifies the bug. */
    os_report_dump(stderr);
    boot_triage_print_backtrace(stderr, "panic");

    /* Under the boot harness this longjmps, which is how the triage runs
     * report a panic as a result rather than a crash.  With no harness it
     * would exit(0); abort() instead, so the shell sees a failure and a
     * debugger stops with the stack still intact. */
    if (boot_triage_has_stop_target()) {
        boot_triage_stop("OSPanic");
    }
    abort();
}

/* ------------------------------------------------------- alarm / thread stubs
 * The S1 boot does not schedule; the triage log records every hit so S4 can
 * quantify what a real thread/alarm backend must provide. */

/* DVDFS's synchronous DVDReadPrio uses this queue.  Host DVD reads complete
 * before the status loop runs (the block state is set by the backend), so the
 * thread never actually has to block. */
void OSInitThreadQueue(OSThreadQueue* queue)
{
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
    }
}

void OSSleepThread(OSThreadQueue* queue)
{
    (void) queue;
    platform_pump_completions();
}

void OSWakeupThread(OSThreadQueue* queue)
{
    (void) queue;
}

void OSInitAlarm(void)
{
    boot_triage_real("OSInitAlarm", BOOT_CAT_OS);
}

void OSCreateAlarm(OSAlarm* alarm)
{
    HostAlarm* slot;

    if (alarm == NULL) {
        return;
    }
    slot = alarm_find(alarm);
    if (slot != NULL) {
        slot->alarm = NULL;
        slot->handler = NULL;
        slot->deadline = 0;
        slot->period = 0;
    }
    memset(alarm, 0, sizeof(*alarm));
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    HostAlarm* slot = alarm_slot(alarm);

    if (slot == NULL) {
        return;
    }
    slot->handler = handler;
    slot->period = 0;
    slot->deadline = virtual_ticks + (u64) tick;
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period,
                        OSAlarmHandler handler)
{
    HostAlarm* slot = alarm_slot(alarm);

    if (slot == NULL) {
        return;
    }
    slot->handler = handler;
    slot->period = (u64) period;
    slot->deadline = virtual_ticks + (u64) start;
}

void OSCancelAlarm(OSAlarm* alarm)
{
    HostAlarm* slot = alarm_find(alarm);

    if (slot != NULL) {
        slot->alarm = NULL;
        slot->handler = NULL;
        slot->deadline = 0;
        slot->period = 0;
    }
}

int OSCreateThread(OSThread* thread, void* (*func)(void*), void* param,
                   void* stack, unsigned long stackSize, long priority,
                   unsigned short attr)
{
    (void) thread;
    (void) func;
    (void) param;
    (void) stack;
    (void) stackSize;
    (void) priority;
    (void) attr;
    boot_triage_stub("OSCreateThread", BOOT_CAT_OS);
    return -1;
}

s32 OSResumeThread(OSThread* thread)
{
    (void) thread;
    boot_triage_stub("OSResumeThread", BOOT_CAT_OS);
    return -1;
}

long OSCheckActiveThreads(void)
{
    boot_triage_stub("OSCheckActiveThreads", BOOT_CAT_OS);
    return 0;
}

/* EABI stack bounds; dbinit.c only prints them. */
unsigned char _stack_addr[4096];
unsigned char _stack_end[16];

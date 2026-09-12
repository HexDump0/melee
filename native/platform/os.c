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

#include "decomp/boot/boot_triage.h"
#include "platform/complete.h"

#define OS_TICKS_PER_MSEC 40500ULL
#define OS_TICKS_PER_SEC 40500000ULL
#define OS_TICKS_PER_FRAME (OS_TICKS_PER_SEC / 60)
#define OS_ARENA_SIZE (24u * 1024u * 1024u)

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

/* ------------------------------------------------------------- arena / init */

void OSInit(void)
{
    boot_triage_real("OSInit", BOOT_CAT_OS);

    map_gc_ram();
    /* Boot info keeps the first 0x3000 bytes, like the console; the arena is
     * the rest of main RAM. */
    OSSetArenaLo((void*) (GC_CACHED_BASE + GC_BOOT_INFO_SIZE));
    OSSetArenaHi((void*) (GC_CACHED_BASE + GC_RAM_SIZE));
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
    return virtual_ticks;
}

OSTick OSGetTick(void)
{
    return (OSTick) advance_ticks(OS_TICKS_PER_MSEC);
}

OSTime OSGetTime(void)
{
    return advance_ticks(OS_TICKS_PER_MSEC);
}

/* Advance the virtual clock by one 60 Hz frame (used by VI pacing). */
void boot_platform_advance_frame(void)
{
    advance_ticks(OS_TICKS_PER_FRAME);
}

/* 2000-01-01 00:00:00 is the GameCube epoch. */
static void days_to_calendar(long long days, OSCalendarTime* td)
{
    long long era;
    unsigned doe;
    unsigned yoe;
    unsigned doy;
    unsigned mp;
    int year;
    int month;
    int day;

    /* Howard Hinnant's civil_from_days, shifted to the 1970 epoch, then
     * re-based to 2000 by starting from 10957 days. */
    era = (days >= 0 ? days : days - 146096) / 146097;
    doe = (unsigned) (days - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = (int) yoe + (int) (era * 400);
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    day = (int) (doy - (153 * mp + 2) / 5 + 1);
    month = (int) (mp < 10 ? mp + 3 : mp - 9);
    year += (month <= 2);

    td->year = year + 1900;
    td->mon = month - 1;
    td->mday = day;
    td->wday = (int) ((days + 4) % 7);
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
    return 0;
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

void DCInvalidateRange(void* addr, u32 nBytes)
{
    (void) addr;
    (void) nBytes;
}

/* ---------------------------------------------------------- report / panic */

void OSReport(char* msg, ...)
{
    va_list ap;
    FILE* out = boot_triage_out();

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

void OSPanic(char* file, int line, char* msg, ...)
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
    boot_triage_stop("OSPanic");
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
    boot_triage_stub("OSCreateAlarm", BOOT_CAT_OS);
    if (alarm != NULL) {
        memset(alarm, 0, sizeof(*alarm));
    }
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    (void) alarm;
    (void) tick;
    (void) handler;
    boot_triage_stub("OSSetAlarm", BOOT_CAT_OS);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period,
                        OSAlarmHandler handler)
{
    (void) alarm;
    (void) start;
    (void) period;
    (void) handler;
    boot_triage_stub("OSSetPeriodicAlarm", BOOT_CAT_OS);
}

void OSCancelAlarm(OSAlarm* alarm)
{
    (void) alarm;
    boot_triage_stub("OSCancelAlarm", BOOT_CAT_OS);
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

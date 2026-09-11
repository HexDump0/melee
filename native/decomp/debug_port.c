/*
 * Host replacement for src/sysdolphin/baselib/debug.c (S1, ADR-0011 rule 3).
 *
 * The upstream file is excluded from the PC build because it routes logging
 * through MSL's private FILE internals (stdout->write_proc, __io_proc).  The
 * observable behavior is kept: report/panic callbacks, the assert/panic
 * ordering, and the same messages.  The OSPanic tail is the platform's
 * (native/platform/os.c) controlled boot stop instead of PPCHalt.
 *
 * Keep this file in sync with src/sysdolphin/baselib/debug.c; when the MSL
 * FILE path can be shimmed, delete this replacement and compile upstream.
 */
#include <sysdolphin/baselib/debug.h>

#include <dolphin/os.h>

#include "decomp/boot/boot_triage.h"

struct DebugContext {
    OSContext context;
    u8 unk[0x10];
};

static struct DebugContext hsd_debug_context;
static ReportCallback report_callback;
static PanicCallback panic_callback;

void HSD_LogInit(void)
{
    boot_triage_real("HSD_LogInit", BOOT_CAT_HSD);
}

void HSD_SetReportCallback(ReportCallback cb)
{
    report_callback = cb;
}

void HSD_SetPanicCallback(PanicCallback cb)
{
    panic_callback = cb;
}

void HSD_Panic(char* arg0, u32 line, char* arg2)
{
    if (panic_callback != NULL) {
        OSSaveContext(&hsd_debug_context.context);
        OSReport("%s in %s on line %d.\n", arg2, arg0, line);
        panic_callback(&hsd_debug_context.context);
    }
    OSPanic(arg0, line, arg2);
}

void __assert(char* str, u32 arg1, char* arg2)
{
    OSReport("assertion \"%s\" failed", arg2);
    HSD_Panic(str, arg1, "");
}

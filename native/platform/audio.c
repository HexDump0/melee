/*
 * S1 audio/video backend: log-only stubs for the HSD AX driver and the THP
 * movie decoder.
 *
 * The AX/AI backends are real as of S5 (`native/audio/`, ADR-0013), the
 * HSD AX driver is the compiled `src/sysdolphin/baselib/axdriver.c`, and the
 * AR/ARQ backend is real as of S3 (`native/platform/ar.c`).  What remains
 * here is the THP movie decoder.
 */
#include <dolphin/types.h>

#include "decomp/boot/boot_triage.h"

/*
 * Prototypes from extern/dolphin/include/dolphin/thp/thp.h.  The header is
 * not included because its static-function declarations trigger
 * -Wunused-function in this host TU; the data type is copied verbatim so the
 * stub signatures stay exact.
 */
typedef struct {
    s32 val0;
    u16 val1;
    u16 _pad;
    u8 val2;
} THPDec_8032FD40_Data;

void THPInit(void);
s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV,
                   void* work);
s32 THPDec_8032FD40(THPDec_8032FD40_Data* arg0, u16 arg1);
s32 THPDec_8032F8D4(u8* data, THPDec_8032FD40_Data* out);
void THPDec_80331340(s32 arg0, void* arg1, void* arg2, void* arg3);
void THPDec_803313D0(s32 arg0, void* arg1, void* arg2, void* arg3, u32 arg4);

/* --------------------------------------------------------------------- THP */

void THPInit(void)
{
    boot_triage_stub("THPInit", BOOT_CAT_THP);
}

s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV,
                   void* work)
{
    (void) file;
    (void) tileY;
    (void) tileU;
    (void) tileV;
    (void) work;
    boot_triage_stub("THPVideoDecode", BOOT_CAT_THP);
    return -1;
}

s32 THPDec_8032FD40(THPDec_8032FD40_Data* arg0, u16 arg1)
{
    (void) arg0;
    (void) arg1;
    boot_triage_stub("THPDec_8032FD40", BOOT_CAT_THP);
    return 0;
}

s32 THPDec_8032F8D4(u8* data, THPDec_8032FD40_Data* out)
{
    (void) data;
    (void) out;
    boot_triage_stub("THPDec_8032F8D4", BOOT_CAT_THP);
    return -1;
}

void THPDec_80331340(s32 arg0, void* arg1, void* arg2, void* arg3)
{
    (void) arg0;
    (void) arg1;
    (void) arg2;
    (void) arg3;
    boot_triage_stub("THPDec_80331340", BOOT_CAT_THP);
}

void THPDec_803313D0(s32 arg0, void* arg1, void* arg2, void* arg3, u32 arg4)
{
    (void) arg0;
    (void) arg1;
    (void) arg2;
    (void) arg3;
    (void) arg4;
    boot_triage_stub("THPDec_803313D0", BOOT_CAT_THP);
}

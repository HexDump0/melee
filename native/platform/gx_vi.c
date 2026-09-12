/*
 * S1/S2 GX/VI backend: VI retrace pacing, draw-done completion and the GX
 * pieces that are not part of the command recorder (GXGetTexBufferSize).
 *
 * The GX command surface lives in native/decomp/gx/gx_hle.c.  Real here:
 *   - GXGetTexBufferSize (copied from extern/dolphin/src/dolphin/gx/
 *     GXTexture.c: GXGetTexBufferSize and __GXGetTexTileShift),
 *   - GXSetDrawDone/GXSetDrawDoneCallback (host draws complete immediately,
 *     which is what lets HSD's XFB machinery advance),
 *   - the VI retrace callbacks and count; VIWaitForRetrace drives HSD's XFB
 *     state machine and the S1 frame budget.
 *
 * VIConfigure/VI flush are log-only; they are the S2 presentation backend's
 * job once the compiled engine presents a frame (the character render
 * harness renders explicitly).
 */
#include <dolphin/gx.h>
#include <dolphin/vi.h>

#include "decomp/boot/boot_triage.h"
#include "platform/complete.h"
#include "platform/platform.h"

/* ------------------------------------------------------------ render modes
 * Verbatim from extern/dolphin/src/dolphin/gx/GXFrameBuf.c (GXNtsc480IntDf
 * 0x43, GXNtsc480Int 0x52, GXNtsc480Prog 0x70). */

GXRenderModeObj GXNtsc480IntDf = {
    0,    640,  480,  480,  40,   0,    640, 480, 1, 0, 0,
    { { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 },
      { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
    { 8, 8, 10, 12, 10, 8, 8 }
};

GXRenderModeObj GXNtsc480Int = {
    0,    640,  480,  480,  40,   0,    640, 480, 1, 0, 0,
    { { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 },
      { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
    { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc480Prog = {
    2,    640,  480,  480,  40,   0,    640, 480, 0, 0, 0,
    { { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 },
      { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
    { 0, 0, 21, 22, 21, 0, 0 }
};

/* --------------------------------------------------------------- GX specials */

static GXDrawDoneCallback gx_draw_done_callback;
static VIRetraceCallback vi_pre_callback;
static VIRetraceCallback vi_post_callback;
static void (*vi_frame_hook)(void);
static u32 vi_retrace_count;

/* extern/dolphin/src/dolphin/gx/GXTexture.c: __GXGetTexTileShift (0x2B) */
static void gx_tex_tile_shift(u32 format, u32* row_tile_s, u32* col_tile_s)
{
    switch (format) {
    case GX_TF_C4:
    case GX_TF_C8:
    case GX_TF_C14X2:
    case GX_TF_CMPR:
    case GX_CTF_R4:
    case GX_CTF_Z4:
        *row_tile_s = 3;
        *col_tile_s = 3;
        break;
    case GX_TF_I8:
    case GX_TF_IA4:
    case GX_TF_Z8:
    case GX_CTF_RA4:
    case GX_TF_A8:
    case GX_CTF_R8:
    case GX_CTF_G8:
    case GX_CTF_B8:
    case GX_CTF_Z8M:
    case GX_CTF_Z8L:
        *row_tile_s = 3;
        *col_tile_s = 2;
        break;
    case GX_TF_IA8:
    case GX_TF_RGB565:
    case GX_TF_RGB5A3:
    case GX_TF_RGBA8:
    case GX_TF_Z16:
    case GX_TF_Z24X8:
    case GX_CTF_RA8:
    case GX_CTF_RG8:
    case GX_CTF_GB8:
    case GX_CTF_Z16L:
        *row_tile_s = 2;
        *col_tile_s = 2;
        break;
    default:
        *row_tile_s = 0;
        *col_tile_s = 0;
        break;
    }
}

/* extern/dolphin/src/dolphin/gx/GXTexture.c: GXGetTexBufferSize (0x194) */
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap,
                       u8 max_lod)
{
    u32 tile_shift_x;
    u32 tile_shift_y;
    u32 tile_bytes;
    u32 buffer_size;
    u32 nx;
    u32 ny;
    u32 level;

    gx_tex_tile_shift(format, &tile_shift_x, &tile_shift_y);
    if (format == GX_TF_RGBA8 || format == GX_TF_Z24X8) {
        tile_bytes = 64;
    } else {
        tile_bytes = 32;
    }
    if (mipmap == 1) {
        buffer_size = 0;
        for (level = 0; level < max_lod; level++) {
            nx = (width + (1 << tile_shift_x) - 1) >> tile_shift_x;
            ny = (height + (1 << tile_shift_y) - 1) >> tile_shift_y;
            buffer_size += tile_bytes * (nx * ny);
            if (width == 1 && height == 1) {
                break;
            }
            width = (width > 1) ? width >> 1 : 1;
            height = (height > 1) ? height >> 1 : 1;
        }
    } else {
        nx = (width + (1 << tile_shift_x) - 1) >> tile_shift_x;
        ny = (height + (1 << tile_shift_y) - 1) >> tile_shift_y;
        buffer_size = nx * ny * tile_bytes;
    }
    return buffer_size;
}

GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb)
{
    GXDrawDoneCallback old = gx_draw_done_callback;

    gx_draw_done_callback = cb;
    return old;
}

/* Host draws complete immediately; HSD_VIGXSetDrawDone depends on the
 * callback firing so its XFB state machine can advance. */
void GXSetDrawDone(void)
{
    boot_triage_stub("GXSetDrawDone", BOOT_CAT_GX);
    if (gx_draw_done_callback != NULL) {
        gx_draw_done_callback();
    }
}

/* --------------------------------------------------------------- VI specials */

u32 VIGetDTVStatus(void)
{
    boot_triage_stub("VIGetDTVStatus", BOOT_CAT_VI);
    return 0;
}

u32 VIGetNextField(void)
{
    boot_triage_stub("VIGetNextField", BOOT_CAT_VI);
    return 0;
}

u32 VIGetRetraceCount(void)
{
    return vi_retrace_count;
}

u32 VIGetTvFormat(void)
{
    return VI_NTSC;
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = vi_pre_callback;

    vi_pre_callback = cb;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = vi_post_callback;

    vi_post_callback = cb;
    return old;
}

void VIInit(void)
{
    boot_triage_real("VIInit", BOOT_CAT_VI);
}

void boot_platform_set_frame_hook(void (*hook)(void))
{
    vi_frame_hook = hook;
}

/* One emulated display frame: run the HSD pre/post retrace callbacks, advance
 * the virtual clock, and charge the S1 frame budget. */
void VIWaitForRetrace(void)
{
    /* Loader wait loops spin on this; hardware completions that were posted
     * while interrupts stayed disabled also get delivered here. */
    platform_pump_completions();
    vi_retrace_count++;
    if (vi_pre_callback != NULL) {
        vi_pre_callback(vi_retrace_count);
    }
    if (vi_post_callback != NULL) {
        vi_post_callback(vi_retrace_count);
    }
    boot_platform_advance_frame();
    if (vi_frame_hook != NULL) {
        vi_frame_hook();
    }
    boot_triage_frame();
}

void VIConfigure(GXRenderModeObj* rm)
{
    (void) rm;
    boot_triage_stub("VIConfigure", BOOT_CAT_VI);
}

void VIFlush(void)
{
    boot_triage_stub("VIFlush", BOOT_CAT_VI);
}

void VISetBlack(BOOL black)
{
    (void) black;
    boot_triage_stub("VISetBlack", BOOT_CAT_VI);
}

void VISetNextFrameBuffer(void* fb)
{
    (void) fb;
    boot_triage_stub("VISetNextFrameBuffer", BOOT_CAT_VI);
}

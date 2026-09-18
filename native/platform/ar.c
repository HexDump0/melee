/*
 * S3 AR/ARAM backend.
 *
 * The decompilation's ARQ queue logic (`extern/dolphin/src/dolphin/ar/arq.c`)
 * is compiled verbatim and drives `ARStartDMA`; this file implements the ARAM
 * hardware beneath it:
 *
 *   - a 16 MB host ARAM buffer with the console's offset semantics (ARAlloc
 *     hands out 32-byte aligned offsets starting at 0x4000, ARFree pops),
 *   - ARStartDMA copies main memory <-> ARAM immediately and posts the DMA
 *     completion to the deferred queue, which invokes the registered
 *     interrupt handler (ARQ's `__ARQInterruptServiceRoutine`).
 *
 * Posting the completion instead of calling the handler inline matters: the
 * ARQ and DevCom state machines set their pending flags after ARQPostRequest
 * returns, exactly as they would after hardware DMA starts.
 */
#include <dolphin/ar.h>
#include <dolphin/os.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "decomp/boot/boot_triage.h"
#include "platform/complete.h"

/* Real ARAM is 16 MB and ARAlloc's stack starts above the reserved 0x4000. */
#define ARAM_SIZE (16u * 1024u * 1024u)
#define ARAM_ALLOC_START 0x4000u

static unsigned char* aram;

/* S5: the AX mixer reads sample data straight out of host ARAM. */
unsigned char* platform_aram_base(void)
{
    return aram;
}

unsigned platform_aram_size(void)
{
    return ARAM_SIZE;
}
static u32 stack_pointer;
static u32* block_length;
static u32 free_blocks;
static u32 block_capacity;
static int ar_init_flag;

static void (*dma_callback)(void);

/* ---------------------------------------------------------------- allocator */

u32 ARInit(u32* stack_index_addr, u32 num_entries)
{
    boot_triage_real("ARInit", BOOT_CAT_AR);
    if (ar_init_flag) {
        return ARAM_ALLOC_START;
    }
    if (aram == NULL) {
        aram = calloc(1, ARAM_SIZE);
        if (aram == NULL) {
            boot_triage_note("[boot] ARInit: cannot allocate host ARAM\n");
        }
    }
    stack_pointer = ARAM_ALLOC_START;
    block_length = stack_index_addr;
    free_blocks = num_entries;
    block_capacity = num_entries;
    ar_init_flag = 1;
    return stack_pointer;
}

int ARCheckInit(void)
{
    return ar_init_flag;
}

u32 ARGetBaseAddress(void)
{
    return ARAM_ALLOC_START;
}

u32 ARGetSize(void)
{
    return ARAM_SIZE;
}

void ARSetSize(void)
{
}

void ARReset(void)
{
    ar_init_flag = 0;
}

/*
 * The ARAM allocator's high-water mark, for the `devcom.c` alignment
 * diagnostic: a destination below it is an ARAM offset this file handed out,
 * and one above it is a host pointer from somewhere else entirely.  Telling
 * those apart is the whole question when that assert fires (P-857).
 */
unsigned long melee_ar_debug_sp(void)
{
    return stack_pointer;
}

u32 ARAlloc(u32 length)
{
    u32 pointer;

    boot_triage_real("ARAlloc", BOOT_CAT_AR);
    /*
     * **Round the block up to 32 bytes.**  ARAM DMA is 32-byte granular and
     * the SDK says so in its own words -- `ar.c:171` asserts
     * `!(length & 0x1F)`, "ARAlloc(): length is not multiple of 32bytes!" --
     * but it only asserts, and retail compiles that out.
     *
     * The game does pass a non-multiple.  `HSD_SynthInit`'s `bank_size` is
     * `lbl_804D643C + lbl_804D6440 + lbl_804D6444` (lbaudio_ax.c:2144), the
     * summed sizes of the sound banks that are loaded, so whether it lands on
     * 32 depends on which banks a given boot path loads.  When it does not,
     * every ARAM address after it is misaligned, and the **next** DMA
     * asserts instead -- `devcom.c:431`, `dest % 32 == 0`, five frames and
     * one subsystem away from the allocation that caused it.  Measured in the
     * browser build, where the opening movie's banks make the sum
     * `0xFFC28`: the following `ARAlloc(0x30000)` returned `0x104128`,
     * misaligned by exactly 8 (P-857).
     *
     * Retail survives it because both asserts are compiled out and the DSP
     * masks the low bits of a transfer address.  Here ARAM is a host buffer
     * and the transfer is a memcpy, so there is no hardware requirement to
     * satisfy -- only the invariant the rest of the code is entitled to
     * assume.  Rounding makes that invariant true rather than merely
     * unchecked, which keeps the assert useful for real bugs, and costs at
     * most 31 bytes of a 16 MB pool per allocation.  The stored block length
     * is the rounded one so `ARFree` pops exactly what was pushed.
     */
    length = (length + 31u) & ~31u;
    if (!ar_init_flag || block_length == NULL || free_blocks == 0 ||
        length > ARAM_SIZE - stack_pointer) {
        boot_triage_note("[boot] ARAlloc: out of ARAM (%u bytes)\n", length);
        return 0;
    }
    pointer = stack_pointer;
    stack_pointer += length;
    *block_length = length;
    block_length++;
    free_blocks--;
    return pointer;
}

u32 ARFree(u32* length)
{
    u32 block;

    if (!ar_init_flag || block_length == NULL || free_blocks >= block_capacity) {
        return stack_pointer;
    }
    block_length--;
    block = *block_length;
    if (length != NULL) {
        *length = block;
    }
    stack_pointer -= block;
    free_blocks++;
    return stack_pointer;
}

/* ------------------------------------------------------------------- DMA */

ARQCallback ARRegisterDMACallback(ARQCallback callback)
{
    ARQCallback old = (ARQCallback) dma_callback;

    dma_callback = (void (*)(void)) callback;
    return old;
}

u32 ARGetDMAStatus(void)
{
    return 0;
}

typedef struct ArDmaCompletion {
    void (*callback)(void);
} ArDmaCompletion;

static void ar_dma_completed(void* arg)
{
    ArDmaCompletion* done = arg;
    void (*callback)(void) = done->callback;

    free(done);
    if (callback != NULL) {
        callback();
    }
}

void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length)
{
    ArDmaCompletion* done;

    if (aram == NULL || length > ARAM_SIZE - aram_addr ||
        (length != 0 && mainmem_addr == 0)) {
        boot_triage_note(
            "[boot] ARStartDMA: bad transfer type=%u mm=%08x aram=%08x "
            "len=%u\n",
            type, mainmem_addr, aram_addr, length);
        return;
    }
    if (type == ARAM_DIR_MRAM_TO_ARAM) {
        memcpy(aram + aram_addr, (void*) (uintptr_t) mainmem_addr, length);
    } else {
        memcpy((void*) (uintptr_t) mainmem_addr, aram + aram_addr, length);
    }
    done = malloc(sizeof(*done));
    if (done == NULL) {
        return;
    }
    done->callback = dma_callback;
    platform_post_completion(ar_dma_completed, done);
}

#ifndef MELEE_AUDIO_AX_HLE_H
#define MELEE_AUDIO_AX_HLE_H

#include <stdint.h>

/*
 * S5 host-side AX backend (ADR-0013).
 *
 * `AXOut.c` owns the DSP task and the AI DMA on hardware; this module is its
 * host replacement.  It keeps the exact `__AXOutNewFrame` ordering
 * (`__AXSyncPBs -> __AXPrintStudio -> __AXGetCommandListAddress ->
 * __AXServiceCallbackStack -> __AXProcessAux -> user callback ->
 * __AXNextFrame`) and then renders the frame with the software mixer instead
 * of sending a command list to the DSP.
 *
 * The 200 Hz audio clock is derived from the VI pump: ten AX frames per three
 * VI retraces (60 * 10/3 = 200), exactly as ADR-0013 specifies.
 */

typedef void (*AxHleSink)(const int16_t* interleaved, unsigned frames,
                          void* user);

/* Output sink for the interactive build (SDL3 stream); NULL is a null sink.
 * The deterministic 32 kHz frame is produced either way. */
void ax_hle_set_sink(AxHleSink sink, void* user);

/* Called from VIWaitForRetrace: 10 audio frames per 3 VI frames. */
void ax_hle_pump_video_frame(void);

/* Deterministic direct pump (tests and frame-budget runs). */
void ax_hle_pump_frames(unsigned frames);

unsigned ax_hle_frame_count(void);

/* FNV-1a over every rendered PCM sample; the S5.5 determinism fingerprint. */
void ax_hle_hash_reset(void);
uint64_t ax_hle_pcm_hash(void);

#endif /* MELEE_AUDIO_AX_HLE_H */

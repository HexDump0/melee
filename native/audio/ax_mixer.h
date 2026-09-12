#ifndef MELEE_AUDIO_AX_MIXER_H
#define MELEE_AUDIO_AX_MIXER_H

#include <stdint.h>

/*
 * S5 software AX mixer (ADR-0013).
 *
 * Mixes the 64 `AXPB` voices that the compiled AX bookkeeping layer
 * (`extern/dolphin/src/dolphin/ax/`) leaves in its DSP shadow array into one
 * 5 ms / 160-frame stereo buffer at 32 kHz.  It is the host replacement for
 * the stock AX microcode's voice/mix stage: ADPCM/PCM decode, ratio SRC,
 * `AXPBMIX` routing (main + aux A/B), VE ramps, ITD delay, loop/end/current
 * address bookkeeping and the `pb.state` write-back the game polls.
 *
 * The mixer reads sample data from the platform ARAM buffer through
 * `platform_aram_base()`/`platform_aram_size()` (defined by
 * `native/platform/ar.c` in product targets and by the unit test).
 */

void ax_mixer_init(void);

/* Render one AX frame (5 ms) into `out` as `frames` interleaved s16 L/R
 * samples (melee uses 160).  Must be called at the end of the AXOut frame,
 * after `__AXSyncPBs`/`__AXProcessAux`/`__AXNextFrame`. */
void ax_mixer_frame(int16_t* out, unsigned frames);

/* platform ARAM accessors (platform/ar.c). */
unsigned char* platform_aram_base(void);
unsigned platform_aram_size(void);

#endif /* MELEE_AUDIO_AX_MIXER_H */

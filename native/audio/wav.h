#ifndef MELEE_AUDIO_WAV_H
#define MELEE_AUDIO_WAV_H

#include <stdint.h>

/*
 * Deterministic 32 kHz / 16-bit stereo WAV sink for the headless boot path
 * (`melee_decomp_boot --audio-dump out.wav`).  It is an `AxHleSink`
 * (`audio/ax_hle.h`) and writes the RIFF sizes on close.  No game data is
 * committed; the file lives where the caller put it (ADR-0005).
 */

typedef struct WavSink WavSink;

WavSink* wav_sink_open(const char* path);

/* AxHleSink-compatible callback; `user` is the WavSink. */
void wav_sink_write(const int16_t* interleaved, unsigned frames, void* user);

void wav_sink_close(WavSink* sink);

#endif

#include "decomp/render/sdl_audio.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>

#include "audio/ax_hle.h"

#define VIEWER_AUDIO_RATE 32000
#define VIEWER_AUDIO_CHANNELS 2

static SDL_AudioStream* viewer_stream;

static void viewer_audio_sink(const int16_t* interleaved, unsigned frames,
                              void* user)
{
    (void) user;
    if (viewer_stream != NULL) {
        SDL_PutAudioStreamData(
            viewer_stream, interleaved,
            (int) (frames * VIEWER_AUDIO_CHANNELS * sizeof(int16_t)));
    }
}

void viewer_audio_init(void)
{
    SDL_AudioSpec spec;
    SDL_AudioStream* stream;

    spec.format = SDL_AUDIO_S16;
    spec.channels = VIEWER_AUDIO_CHANNELS;
    spec.freq = VIEWER_AUDIO_RATE;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                       &spec, NULL, NULL);
    if (stream == NULL) {
        fprintf(stderr, "viewer: audio unavailable: %s\n", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream);
    viewer_stream = stream;
    ax_hle_set_sink(viewer_audio_sink, NULL);
    printf("viewer: audio %d Hz stereo\n", VIEWER_AUDIO_RATE);
}

void viewer_audio_shutdown(void)
{
    if (viewer_stream != NULL) {
        SDL_DestroyAudioStream(viewer_stream);
        viewer_stream = NULL;
    }
    ax_hle_set_sink(NULL, NULL);
}

#include "audio/wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAV_RATE 32000u
#define WAV_CHANNELS 2u

struct WavSink {
    FILE* file;
    uint32_t data_bytes;
};

static void put32(unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16);
    p[3] = (unsigned char) (v >> 24);
}

static void put16(unsigned char* p, uint16_t v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
}

WavSink* wav_sink_open(const char* path)
{
    WavSink* sink = calloc(1, sizeof(*sink));
    unsigned char header[44];

    if (sink == NULL) {
        return NULL;
    }
    sink->file = fopen(path, "wb");
    if (sink->file == NULL) {
        free(sink);
        return NULL;
    }

    memcpy(header + 0, "RIFF", 4);
    put32(header + 4, 36);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    put32(header + 16, 16);
    put16(header + 20, 1); /* PCM */
    put16(header + 22, WAV_CHANNELS);
    put32(header + 24, WAV_RATE);
    put32(header + 28, WAV_RATE * WAV_CHANNELS * 2);
    put16(header + 32, WAV_CHANNELS * 2);
    put16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    put32(header + 40, 0);
    fwrite(header, 1, sizeof(header), sink->file);
    return sink;
}

void wav_sink_write(const int16_t* interleaved, unsigned frames, void* user)
{
    WavSink* sink = user;
    size_t bytes = (size_t) frames * WAV_CHANNELS * sizeof(int16_t);

    if (sink == NULL || sink->file == NULL) {
        return;
    }
    fwrite(interleaved, 1, bytes, sink->file);
    sink->data_bytes += (uint32_t) bytes;
}

void wav_sink_close(WavSink* sink)
{
    unsigned char sizes[8];

    if (sink == NULL) {
        return;
    }
    if (sink->file != NULL) {
        put32(sizes + 0, sink->data_bytes + 36);
        put32(sizes + 4, sink->data_bytes);
        fseek(sink->file, 4, SEEK_SET);
        fwrite(sizes, 1, 4, sink->file);
        fseek(sink->file, 40, SEEK_SET);
        fwrite(sizes + 4, 1, 4, sink->file);
        fclose(sink->file);
    }
    free(sink);
}

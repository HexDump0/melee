#ifndef MELEE_AUDIO_SFX_DEBUG_H
#define MELEE_AUDIO_SFX_DEBUG_H

#include <stdint.h>

#define MELEE_SFX_DEBUG_FIGHTERS 6

typedef struct MeleeSfxDebugFighter {
    int active;
    int player;
    int kind;
    int motion;
    int damage;
    float x;
    float y;
} MeleeSfxDebugFighter;

typedef struct MeleeSfxDebugGameState {
    unsigned video_frame;
    unsigned mode;
    unsigned scene;
    MeleeSfxDebugFighter fighters[MELEE_SFX_DEBUG_FIGHTERS];
} MeleeSfxDebugGameState;

/* Opt-in diagnostic selected with MELEE_SFX_DEBUG=<path>.  The value "1"
 * writes melee-sfx-debug.log and "-" writes to stderr. */
int melee_sfx_debug_enabled(void);
void melee_sfx_debug_set_audio_frame(unsigned frame);
void melee_sfx_debug_set_game_state(const MeleeSfxDebugGameState* state);

/* The fighter hook brackets the nested AXDriver request, letting the central
 * request recorder attach an owner without changing any game behavior. */
void melee_sfx_debug_fighter_context(int sound_id, int player, int kind,
                                     int motion, float x, float y);
void melee_sfx_debug_clear_context(void);

/* Called by the compiled AX driver for accepted requests and synth starts. */
void melee_sfx_debug_request(int sound_id, int driver_id, int volume, int pan,
                             int track, int channel);
void melee_sfx_debug_voice_start(int driver_id, int synth_id, int node_id,
                                 int track, int channel);
void melee_sfx_debug_voice_pair(int node_id, unsigned primary,
                                int secondary);

/* Called by the host mixer at the two observable DSP boundaries. */
void melee_sfx_debug_voice_loop(unsigned voice, unsigned format,
                                uint32_t from, uint32_t loop, uint32_t end,
                                uint32_t ratio);
void melee_sfx_debug_voice_idle(unsigned voice);
void melee_sfx_debug_mix(const int16_t* interleaved, unsigned frames);

void melee_sfx_debug_shutdown(void);

#endif /* MELEE_AUDIO_SFX_DEBUG_H */

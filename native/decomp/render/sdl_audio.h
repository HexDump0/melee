#ifndef MELEE_DECOMP_RENDER_SDL_AUDIO_H
#define MELEE_DECOMP_RENDER_SDL_AUDIO_H

/* S5.4: SDL3 audio output for the interactive compiled viewer.  Opens the
 * default playback device as a 32 kHz s16 stereo stream and installs it as
 * the AX HLE sink; a missing device only disables sound. */
void viewer_audio_init(void);
void viewer_audio_shutdown(void);

#endif

/*
 * P-706 intermittent SFX-spam recorder.
 *
 * This is deliberately outside the game source: it observes accepted sound
 * requests, actual synth voice starts, DSP sample wraps and final PCM.  The
 * resulting log distinguishes repeated gameplay requests from a single bad
 * looping voice without changing playback or timing when disabled.
 */
#include "audio/sfx_debug.h"

#include <dolphin/ax.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SFX_DEBUG_RECENT 64
#define SFX_DEBUG_IDS 128
#define SFX_DEBUG_ID_TIMES 8
#define SFX_DEBUG_VOICES 64

/* extern/dolphin/src/dolphin/ax/AXVPB.c (same target). */
AXPB* __AXGetPBs(void);

typedef struct SfxRequest {
    unsigned audio_frame;
    unsigned video_frame;
    int sound_id;
    int driver_id;
    int volume;
    int pan;
    int track;
    int channel;
    int has_fighter;
    int player;
    int kind;
    int motion;
    float x;
    float y;
} SfxRequest;

typedef struct SfxIdStats {
    int used;
    int sound_id;
    unsigned total;
    unsigned window;
    unsigned last_seen;
    unsigned last_alert;
    unsigned alerts;
    unsigned times[SFX_DEBUG_ID_TIMES];
    unsigned time_count;
    unsigned time_next;
} SfxIdStats;

typedef struct SfxVoice {
    int mapped;
    int sound_id;
    int synth_id;
    int driver_id;
    int track;
    int channel;
    unsigned start_frame;
    unsigned loop_window_start;
    unsigned loop_window_count;
    unsigned loops;
} SfxVoice;

static FILE* sfx_out;
static int sfx_init;
static int sfx_owns_file;
static int sfx_shutdown;
static unsigned sfx_audio_frame;
static MeleeSfxDebugGameState sfx_game;
static SfxRequest sfx_recent[SFX_DEBUG_RECENT];
static unsigned sfx_recent_total;
static SfxIdStats sfx_ids[SFX_DEBUG_IDS];
static SfxVoice sfx_voices[SFX_DEBUG_VOICES];
static int sfx_voice_pair_valid[SFX_DEBUG_VOICES];
static int sfx_voice_pair_node[SFX_DEBUG_VOICES];
static int sfx_voice_pair_second[SFX_DEBUG_VOICES];
static SfxRequest sfx_pending;
static int sfx_pending_valid;
static unsigned sfx_requests;
static unsigned sfx_starts;
static unsigned sfx_start_failures;
static unsigned sfx_loops;
static unsigned sfx_alerts;
static unsigned sfx_last_global_alert;
static unsigned sfx_last_pcm_alert;
static unsigned sfx_clip_run;
static unsigned sfx_voice_run;
static unsigned sfx_max_active;
static unsigned sfx_max_clipped;
static unsigned sfx_max_peak;
static unsigned sfx_window_requests;
static unsigned sfx_window_starts;
static unsigned sfx_window_failures;
static unsigned sfx_window_loops;
static unsigned sfx_window_max_active;
static unsigned sfx_window_max_clipped;
static unsigned sfx_window_max_peak;

static uint32_t pair_u32(uint16_t hi, uint16_t lo)
{
    return ((uint32_t) hi << 16) | lo;
}

static void sfx_debug_init(void)
{
    const char* setting;
    const char* path;

    if (sfx_init != 0) {
        return;
    }
    setting = getenv("MELEE_SFX_DEBUG");
    if (setting == NULL || setting[0] == '\0' || strcmp(setting, "0") == 0) {
        sfx_init = -1;
        return;
    }
    path = strcmp(setting, "1") == 0 ? "melee-sfx-debug.log" : setting;
    if (strcmp(path, "-") == 0 || strcmp(path, "stderr") == 0) {
        sfx_out = stderr;
    } else {
        sfx_out = fopen(path, "w");
        sfx_owns_file = sfx_out != NULL;
    }
    if (sfx_out == NULL) {
        fprintf(stderr, "sfx-debug: cannot open %s\n", path);
        sfx_init = -1;
        return;
    }
    setvbuf(sfx_out, NULL, _IOLBF, 0);
    sfx_init = 1;
    fprintf(sfx_out,
            "[sfx-debug] version=1 audio_hz=200 video_hz=60 "
            "request_burst=4/2s loop_burst=3/3s\n");
    fprintf(stderr, "sfx-debug: recording to %s\n", path);
    atexit(melee_sfx_debug_shutdown);
}

int melee_sfx_debug_enabled(void)
{
    sfx_debug_init();
    return sfx_init > 0;
}

void melee_sfx_debug_set_audio_frame(unsigned frame)
{
    if (!melee_sfx_debug_enabled()) {
        return;
    }
    sfx_audio_frame = frame;
}

void melee_sfx_debug_set_game_state(const MeleeSfxDebugGameState* state)
{
    if (!melee_sfx_debug_enabled() || state == NULL) {
        return;
    }
    sfx_game = *state;
}

void melee_sfx_debug_fighter_context(int sound_id, int player, int kind,
                                     int motion, float x, float y)
{
    if (!melee_sfx_debug_enabled()) {
        return;
    }
    memset(&sfx_pending, 0, sizeof(sfx_pending));
    sfx_pending.sound_id = sound_id;
    sfx_pending.has_fighter = 1;
    sfx_pending.player = player;
    sfx_pending.kind = kind;
    sfx_pending.motion = motion;
    sfx_pending.x = x;
    sfx_pending.y = y;
    sfx_pending_valid = 1;
}

void melee_sfx_debug_clear_context(void)
{
    sfx_pending_valid = 0;
}

static SfxIdStats* id_stats(int sound_id)
{
    SfxIdStats* oldest = &sfx_ids[0];
    unsigned i;

    for (i = 0; i < SFX_DEBUG_IDS; i++) {
        if (sfx_ids[i].used && sfx_ids[i].sound_id == sound_id) {
            return &sfx_ids[i];
        }
        if (!sfx_ids[i].used) {
            oldest = &sfx_ids[i];
            break;
        }
        if (sfx_ids[i].last_seen < oldest->last_seen) {
            oldest = &sfx_ids[i];
        }
    }
    memset(oldest, 0, sizeof(*oldest));
    oldest->used = 1;
    oldest->sound_id = sound_id;
    return oldest;
}

static void dump_game_state(void)
{
    unsigned i;

    fprintf(sfx_out, "  game vf=%u mode=%u scene=%u\n", sfx_game.video_frame,
            sfx_game.mode, sfx_game.scene);
    for (i = 0; i < MELEE_SFX_DEBUG_FIGHTERS; i++) {
        const MeleeSfxDebugFighter* f = &sfx_game.fighters[i];
        if (f->active) {
            fprintf(sfx_out,
                    "    fighter slot=%u player=%d kind=%d motion=%d "
                    "damage=%d pos=(%.3f,%.3f)\n",
                    i, f->player, f->kind, f->motion, f->damage,
                    (double) f->x, (double) f->y);
        }
    }
}

static void dump_recent(void)
{
    unsigned count = sfx_recent_total < SFX_DEBUG_RECENT
                         ? sfx_recent_total
                         : SFX_DEBUG_RECENT;
    unsigned first = sfx_recent_total - count;
    unsigned i;

    fprintf(sfx_out, "  recent requests=%u\n", count);
    for (i = first; i < sfx_recent_total; i++) {
        const SfxRequest* r = &sfx_recent[i % SFX_DEBUG_RECENT];
        fprintf(sfx_out,
                "    af=%u vf=%u id=%d/0x%x driver=%d vol=%d pan=%d "
                "track=%d channel=%d",
                r->audio_frame, r->video_frame, r->sound_id, r->sound_id,
                r->driver_id, r->volume, r->pan, r->track, r->channel);
        if (r->has_fighter) {
            fprintf(sfx_out,
                    " fighter=p%d/k%d/m%d@(%.3f,%.3f)", r->player,
                    r->kind, r->motion, (double) r->x, (double) r->y);
        }
        fputc('\n', sfx_out);
    }
}

static void dump_voices(void)
{
    AXPB* pbs = __AXGetPBs();
    unsigned i;

    fprintf(sfx_out, "  active voices\n");
    for (i = 0; i < SFX_DEBUG_VOICES; i++) {
        const AXPB* pb = &pbs[i];
        const SfxVoice* v = &sfx_voices[i];
        if (pb->state != 1) {
            continue;
        }
        fprintf(sfx_out,
                "    voice=%u id=%d synth=%d driver=%d fmt=%u loopflag=%u "
                "cur=%08x loop=%08x end=%08x ratio=%08x ve=%u "
                "mix=%u/%u loops=%u age=%u\n",
                i, v->mapped ? v->sound_id : -1,
                v->mapped ? v->synth_id : -1,
                v->mapped ? v->driver_id : -1, pb->addr.format,
                pb->addr.loopFlag,
                pair_u32(pb->addr.currentAddressHi,
                         pb->addr.currentAddressLo),
                pair_u32(pb->addr.loopAddressHi, pb->addr.loopAddressLo),
                pair_u32(pb->addr.endAddressHi, pb->addr.endAddressLo),
                pair_u32(pb->src.ratioHi, pb->src.ratioLo),
                pb->ve.currentVolume, pb->mix.vL, pb->mix.vR, v->loops,
                v->mapped ? sfx_audio_frame - v->start_frame : 0);
    }
}

static void dump_stack(void)
{
    void* frames[16];
    char** symbols;
    int count;
    int i;

    count = backtrace(frames, (int) (sizeof(frames) / sizeof(frames[0])));
    symbols = backtrace_symbols(frames, count);
    fprintf(sfx_out, "  call stack\n");
    if (symbols == NULL) {
        fprintf(sfx_out, "    unavailable\n");
        return;
    }
    for (i = 2; i < count; i++) {
        fprintf(sfx_out, "    %s\n", symbols[i]);
    }
    free(symbols);
}

static void alert(const char* reason, int sound_id, unsigned count,
                  unsigned span, int with_stack)
{
    sfx_alerts++;
    fprintf(sfx_out,
            "[sfx-alert] seq=%u reason=%s af=%u vf=%u id=%d/0x%x "
            "count=%u span_frames=%u span_ms=%u\n",
            sfx_alerts, reason, sfx_audio_frame, sfx_game.video_frame,
            sound_id, sound_id, count, span, span * 5);
    dump_game_state();
    dump_recent();
    dump_voices();
    if (with_stack) {
        dump_stack();
    }
    fprintf(sfx_out, "[sfx-alert-end] seq=%u\n", sfx_alerts);
    fflush(sfx_out);
}

void melee_sfx_debug_request(int sound_id, int driver_id, int volume, int pan,
                             int track, int channel)
{
    SfxRequest* request;
    SfxIdStats* stats;
    unsigned in_window = 0;
    unsigned oldest = sfx_audio_frame;
    unsigned recent_global = 0;
    unsigned i;

    if (!melee_sfx_debug_enabled()) {
        return;
    }
    request = &sfx_recent[sfx_recent_total % SFX_DEBUG_RECENT];
    memset(request, 0, sizeof(*request));
    request->audio_frame = sfx_audio_frame;
    request->video_frame = sfx_game.video_frame;
    request->sound_id = sound_id;
    request->driver_id = driver_id;
    request->volume = volume;
    request->pan = pan;
    request->track = track;
    request->channel = channel;
    if (sfx_pending_valid && sfx_pending.sound_id == sound_id) {
        request->has_fighter = 1;
        request->player = sfx_pending.player;
        request->kind = sfx_pending.kind;
        request->motion = sfx_pending.motion;
        request->x = sfx_pending.x;
        request->y = sfx_pending.y;
    }
    sfx_pending_valid = 0;
    sfx_recent_total++;
    sfx_requests++;

    stats = id_stats(sound_id);
    stats->total++;
    stats->window++;
    stats->last_seen = sfx_audio_frame;
    stats->times[stats->time_next] = sfx_audio_frame;
    stats->time_next = (stats->time_next + 1) % SFX_DEBUG_ID_TIMES;
    if (stats->time_count < SFX_DEBUG_ID_TIMES) {
        stats->time_count++;
    }
    for (i = 0; i < stats->time_count; i++) {
        unsigned at = stats->times[i];
        if (sfx_audio_frame - at <= 400) {
            in_window++;
            if (at < oldest) {
                oldest = at;
            }
        }
    }
    if (in_window >= 4 &&
        (stats->alerts == 0 || sfx_audio_frame - stats->last_alert >= 600))
    {
        stats->alerts++;
        stats->last_alert = sfx_audio_frame;
        alert("same-id-requests", sound_id, in_window,
              sfx_audio_frame - oldest, 1);
    }

    for (i = 0; i < SFX_DEBUG_RECENT && i < sfx_recent_total; i++) {
        const SfxRequest* r =
            &sfx_recent[(sfx_recent_total - 1 - i) % SFX_DEBUG_RECENT];
        if (sfx_audio_frame - r->audio_frame <= 40) {
            recent_global++;
        } else {
            break;
        }
    }
    if (recent_global >= 12 &&
        (sfx_last_global_alert == 0 ||
         sfx_audio_frame - sfx_last_global_alert >= 600))
    {
        sfx_last_global_alert = sfx_audio_frame;
        alert("global-request-burst", sound_id, recent_global, 40, 1);
    }
}

static int request_sound_id(int driver_id)
{
    unsigned count = sfx_recent_total < SFX_DEBUG_RECENT
                         ? sfx_recent_total
                         : SFX_DEBUG_RECENT;
    unsigned i;

    for (i = 0; i < count; i++) {
        const SfxRequest* r =
            &sfx_recent[(sfx_recent_total - 1 - i) % SFX_DEBUG_RECENT];
        if (r->driver_id == driver_id) {
            return r->sound_id;
        }
    }
    return -1;
}

void melee_sfx_debug_voice_start(int driver_id, int synth_id, int node_id,
                                 int track, int channel)
{
    SfxVoice* voice;
    unsigned index;

    if (!melee_sfx_debug_enabled()) {
        return;
    }
    if (node_id < 0) {
        sfx_start_failures++;
        return;
    }
    index = (unsigned) node_id & 0x3F;
    if (index >= SFX_DEBUG_VOICES) {
        return;
    }
    voice = &sfx_voices[index];
    memset(voice, 0, sizeof(*voice));
    voice->mapped = 1;
    voice->sound_id = request_sound_id(driver_id);
    voice->synth_id = synth_id;
    voice->driver_id = driver_id;
    voice->track = track;
    voice->channel = channel;
    voice->start_frame = sfx_audio_frame;
    if (sfx_voice_pair_valid[index] &&
        sfx_voice_pair_node[index] == node_id &&
        sfx_voice_pair_second[index] >= 0 &&
        sfx_voice_pair_second[index] < SFX_DEBUG_VOICES)
    {
        unsigned second = (unsigned) sfx_voice_pair_second[index];
        sfx_voices[second] = *voice;
    }
    sfx_voice_pair_valid[index] = 0;
    sfx_starts++;
}

void melee_sfx_debug_voice_pair(int node_id, unsigned primary, int secondary)
{
    if (!melee_sfx_debug_enabled() || primary >= SFX_DEBUG_VOICES) {
        return;
    }
    sfx_voice_pair_valid[primary] = 1;
    sfx_voice_pair_node[primary] = node_id;
    sfx_voice_pair_second[primary] = secondary;
}

void melee_sfx_debug_voice_loop(unsigned voice_index, unsigned format,
                                uint32_t from, uint32_t loop, uint32_t end,
                                uint32_t ratio)
{
    SfxVoice* voice;

    if (!melee_sfx_debug_enabled() || voice_index >= SFX_DEBUG_VOICES) {
        return;
    }
    voice = &sfx_voices[voice_index];
    sfx_loops++;
    voice->loops++;
    if (!voice->mapped) {
        return;
    }
    if (voice->loop_window_count == 0 ||
        sfx_audio_frame - voice->loop_window_start > 600)
    {
        voice->loop_window_start = sfx_audio_frame;
        voice->loop_window_count = 0;
    }
    voice->loop_window_count++;
    if (voice->loop_window_count == 3) {
        fprintf(sfx_out,
                "[sfx-loop] voice=%u id=%d synth=%d fmt=%u from=%08x "
                "loop=%08x end=%08x ratio=%08x\n",
                voice_index, voice->sound_id, voice->synth_id, format, from,
                loop, end, ratio);
        alert("short-voice-loop", voice->sound_id,
              voice->loop_window_count,
              sfx_audio_frame - voice->loop_window_start, 0);
    }
}

static void checkpoint(unsigned active)
{
    unsigned picked[3] = { SFX_DEBUG_IDS, SFX_DEBUG_IDS, SFX_DEBUG_IDS };
    unsigned rank;

    fprintf(sfx_out,
            "[sfx-window] af=%u vf=%u mode=%u scene=%u requests=%u "
            "starts=%u failures=%u loops=%u active=%u max_active=%u "
            "peak=%u max_clipped=%u",
            sfx_audio_frame, sfx_game.video_frame, sfx_game.mode,
            sfx_game.scene, sfx_requests - sfx_window_requests,
            sfx_starts - sfx_window_starts,
            sfx_start_failures - sfx_window_failures,
            sfx_loops - sfx_window_loops, active, sfx_window_max_active,
            sfx_window_max_peak, sfx_window_max_clipped);
    for (rank = 0; rank < 3; rank++) {
        unsigned best = SFX_DEBUG_IDS;
        unsigned i;
        for (i = 0; i < SFX_DEBUG_IDS; i++) {
            int already = i == picked[0] || i == picked[1];
            if (sfx_ids[i].used && sfx_ids[i].window != 0 && !already &&
                (best == SFX_DEBUG_IDS ||
                 sfx_ids[i].window > sfx_ids[best].window))
            {
                best = i;
            }
        }
        if (best == SFX_DEBUG_IDS) {
            break;
        }
        picked[rank] = best;
        fprintf(sfx_out, " top%u=%d:%u", rank + 1,
                sfx_ids[best].sound_id, sfx_ids[best].window);
    }
    fputc('\n', sfx_out);
    for (rank = 0; rank < SFX_DEBUG_IDS; rank++) {
        sfx_ids[rank].window = 0;
    }
    sfx_window_requests = sfx_requests;
    sfx_window_starts = sfx_starts;
    sfx_window_failures = sfx_start_failures;
    sfx_window_loops = sfx_loops;
    sfx_window_max_active = 0;
    sfx_window_max_clipped = 0;
    sfx_window_max_peak = 0;
}

void melee_sfx_debug_voice_idle(unsigned voice)
{
    if (!melee_sfx_debug_enabled() || voice >= SFX_DEBUG_VOICES) {
        return;
    }
    /* AXDriver starts the user PB after this audio frame's shadow sync.  The
     * mixer still sees the old idle shadow once before the voice becomes
     * active on the following frame; retain that just-created mapping. */
    if (sfx_voices[voice].mapped &&
        sfx_voices[voice].start_frame == sfx_audio_frame)
    {
        return;
    }
    memset(&sfx_voices[voice], 0, sizeof(sfx_voices[voice]));
}

void melee_sfx_debug_mix(const int16_t* interleaved, unsigned frames)
{
    AXPB* pbs;
    unsigned samples;
    unsigned active = 0;
    unsigned clipped = 0;
    unsigned peak = 0;
    unsigned i;

    if (!melee_sfx_debug_enabled() || interleaved == NULL) {
        return;
    }
    pbs = __AXGetPBs();
    for (i = 0; i < SFX_DEBUG_VOICES; i++) {
        if (pbs[i].state == 1) {
            active++;
        }
    }
    samples = frames * 2;
    for (i = 0; i < samples; i++) {
        int sample = interleaved[i];
        unsigned magnitude =
            (unsigned) (sample < 0 ? -(sample + 1) + 1 : sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
        if (magnitude >= 32760) {
            clipped++;
        }
    }
    if (active > sfx_max_active) {
        sfx_max_active = active;
    }
    if (clipped > sfx_max_clipped) {
        sfx_max_clipped = clipped;
    }
    if (peak > sfx_max_peak) {
        sfx_max_peak = peak;
    }
    if (active > sfx_window_max_active) {
        sfx_window_max_active = active;
    }
    if (clipped > sfx_window_max_clipped) {
        sfx_window_max_clipped = clipped;
    }
    if (peak > sfx_window_max_peak) {
        sfx_window_max_peak = peak;
    }
    sfx_clip_run = clipped >= frames / 5 ? sfx_clip_run + 1 : 0;
    sfx_voice_run = active >= 48 ? sfx_voice_run + 1 : 0;
    if ((sfx_clip_run == 20 || sfx_voice_run == 20) &&
        (sfx_last_pcm_alert == 0 ||
         sfx_audio_frame - sfx_last_pcm_alert >= 1000))
    {
        const char* reason =
            sfx_voice_run >= 20 ? "voice-saturation" : "sustained-clipping";
        sfx_last_pcm_alert = sfx_audio_frame;
        alert(reason, -1, active, sfx_clip_run, 0);
    }
    if (sfx_audio_frame != 0 && sfx_audio_frame % 200 == 0) {
        checkpoint(active);
    }
}

void melee_sfx_debug_shutdown(void)
{
    unsigned picked[SFX_DEBUG_IDS];
    unsigned rank;

    if (sfx_init <= 0 || sfx_shutdown) {
        return;
    }
    sfx_shutdown = 1;
    memset(picked, 0, sizeof(picked));
    fprintf(sfx_out,
            "[sfx-summary] af=%u vf=%u requests=%u starts=%u "
            "start_failures=%u loops=%u alerts=%u max_active=%u "
            "max_peak=%u max_clipped=%u\n",
            sfx_audio_frame, sfx_game.video_frame, sfx_requests, sfx_starts,
            sfx_start_failures, sfx_loops, sfx_alerts, sfx_max_active,
            sfx_max_peak, sfx_max_clipped);
    for (rank = 0; rank < 12; rank++) {
        unsigned best = SFX_DEBUG_IDS;
        unsigned i;
        for (i = 0; i < SFX_DEBUG_IDS; i++) {
            if (sfx_ids[i].used && !picked[i] &&
                (best == SFX_DEBUG_IDS ||
                 sfx_ids[i].total > sfx_ids[best].total))
            {
                best = i;
            }
        }
        if (best == SFX_DEBUG_IDS) {
            break;
        }
        picked[best] = 1;
        fprintf(sfx_out, "  top rank=%u id=%d/0x%x requests=%u alerts=%u\n",
                rank + 1, sfx_ids[best].sound_id,
                sfx_ids[best].sound_id, sfx_ids[best].total,
                sfx_ids[best].alerts);
    }
    dump_game_state();
    dump_recent();
    dump_voices();
    fflush(sfx_out);
    if (sfx_owns_file) {
        fclose(sfx_out);
    }
    sfx_out = NULL;
}

/*
 * S1 boot harness: runs the decompilation's own main() (compiled from
 * src/melee/gm/gmmain.c, renamed gm_main) with the GameCube hardware stubbed,
 * and stops in a controlled way on a frame budget, a stub-call limit, a
 * signal, or a game panic.
 *
 * Usage:
 *   melee_decomp_boot [--boot-log FILE] [--boot-frames N]
 *                     [--boot-stub-limit N] [--boot-timeout SECONDS]
 *                     [--boot-trace] [--boot-no-watchdog]
 *
 * Exit code is 0 for every controlled stop; a non-zero code means the harness
 * itself failed before the game was entered.  The triage log is written to
 * stderr (or FILE) and never contains game assets.
 */
#include "boot_triage.h"
#include "match_boot.h"

#include "audio/ax_hle.h"
#include "audio/wav.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int gm_main(void);

static void handle_signal(int signo, siginfo_t* info, void* ctx)
{
    (void) ctx;
    /* P-800: si_addr separates a NULL base from a dangling pointer. */
    boot_triage_capture_crash_at(signo, info != NULL ? info->si_addr : NULL,
                                 info != NULL);
}

static void install_handlers(void)
{
    struct sigaction sa;
    if (getenv("MELEE_NO_CRASH_HANDLER") != NULL) {
        return; /* let a sanitizer runtime report the failure itself */
    }
    int signals[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGALRM };
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER | SA_SIGINFO;
    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        sigaction(signals[i], &sa, NULL);
    }
}

static void usage(const char* argv0)
{
    fprintf(stderr,
            "usage: %s [--boot-log FILE] [--boot-frames N]\n"
            "          [--boot-stub-limit N] [--boot-timeout SECONDS]\n"
            "          [--boot-match FRAME] [--boot-trace] "
            "[--boot-no-watchdog]\n"
            "          [--audio-dump FILE.wav]\n",
            argv0);
}

int main(int argc, char** argv)
{
    FILE* volatile out = stderr;
    const char* log_path = NULL;
    unsigned frames = 60;
    unsigned long stub_limit = 5000000;
    unsigned timeout = 30;
    unsigned match_frame = 0;
    const char* audio_dump = NULL;
    WavSink* wav = NULL;
    int trace = 0;
    int watchdog = 1;
    sigjmp_buf stop;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--boot-log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        } else if (strcmp(argv[i], "--boot-frames") == 0 && i + 1 < argc) {
            frames = (unsigned) strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--boot-stub-limit") == 0 && i + 1 < argc) {
            stub_limit = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--boot-timeout") == 0 && i + 1 < argc) {
            timeout = (unsigned) strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--boot-match") == 0 && i + 1 < argc) {
            match_frame = (unsigned) strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--audio-dump") == 0 && i + 1 < argc) {
            audio_dump = argv[++i];
        } else if (strcmp(argv[i], "--boot-trace") == 0) {
            trace = 1;
        } else if (strcmp(argv[i], "--boot-no-watchdog") == 0) {
            watchdog = 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: unknown option: %s\n", argv[0], argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (log_path != NULL) {
        out = fopen(log_path, "w");
        if (out == NULL) {
            fprintf(stderr, "%s: cannot open log file: %s\n", argv[0],
                    log_path);
            return 1;
        }
    }

    if (audio_dump != NULL) {
        wav = wav_sink_open(audio_dump);
        if (wav == NULL) {
            fprintf(stderr, "%s: cannot open audio dump: %s\n", argv[0],
                    audio_dump);
            return 1;
        }
        ax_hle_set_sink(wav_sink_write, wav);
    }
    boot_triage_init(out, trace, stub_limit);
    boot_triage_set_frame_budget(frames);
    boot_triage_install_stop_target(&stop);
    match_boot_init(match_frame);
    match_boot_install_crash_dump();
    install_handlers();
    if (watchdog && timeout != 0) {
        alarm(timeout);
    }

    boot_triage_note("[boot] S1 boot skeleton: compiled decomp main() with "
                     "stubbed OS/DVD/GX/VI\n");
    boot_triage_note("[boot] budget: frames=%u stub_limit=%lu timeout=%us\n",
                     frames, stub_limit, watchdog ? timeout : 0);

    if (sigsetjmp(stop, 1) == 0) {
        boot_triage_real("gm_main", BOOT_CAT_GAME);
        gm_main();
        boot_triage_stopped = 1;
        boot_triage_note("[boot] gm_main returned without a stop\n");
    } else {
        if (boot_triage_crash_signal_name()[0] != '\0') {
            boot_triage_print_crash(out);
            /* P-796: the harness longjmps out of the signal handler, so the
             * game state is still standing here; print it next to the stack.
             * Only for a signal -- an assertion already dumped from
             * `__assert`, before the panic unwound anything. */
            match_boot_dump_fighters(out);
        } else if (boot_triage_stop_reason() != NULL) {
            boot_triage_note("[boot] STOP: %s\n",
                             boot_triage_stop_reason());
        }
    }

    boot_triage_note("[boot] audio: frames=%u hash=%016llx\n",
                     ax_hle_frame_count(), (unsigned long long) ax_hle_pcm_hash());
    boot_triage_summary(out);
    wav_sink_close(wav);
    if (out != stderr) {
        fclose(out);
    }
    return 0;
}

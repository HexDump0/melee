/*
 * melee.toml: the key -> environment mapping, and the precedence rule
 * (P-867).  The launcher writes this file and the port reads it, so the
 * mapping is a contract between two programs and belongs in a test rather
 * than in both their heads.
 */
#include "platform/melee_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect_name(const char* key, const char* want)
{
    char got[256];
    int ok = melee_config_env_name(key, got, sizeof(got));
    if (want == NULL) {
        if (ok) {
            fprintf(stderr, "config: %s mapped to %s, wanted a refusal\n", key,
                    got);
            failures++;
        }
        return;
    }
    if (!ok || strcmp(got, want) != 0) {
        fprintf(stderr, "config: %s -> %s (want %s)\n", key,
                ok ? got : "<refused>", want);
        failures++;
    }
}

static void expect_env(const char* name, const char* want)
{
    const char* got = getenv(name);
    if (got == NULL || strcmp(got, want) != 0) {
        fprintf(stderr, "config: %s=%s (want %s)\n", name,
                got != NULL ? got : "<unset>", want);
        failures++;
    }
}

int main(void)
{
    char path[] = "/tmp/melee-config-test.toml";
    FILE* f;

    /* The names the port already reads, spelled as a person would group
     * them.  These are the whole point: the sections are the prefixes. */
    expect_name("match.cpu", "MELEE_MATCH_CPU");
    expect_name("mods.unbound.widescreen", "MELEE_MOD_UNBOUND_WIDESCREEN");
    expect_name("gx.tex_stats", "MELEE_GX_TEX_STATS");
    expect_name("rng_seed", "MELEE_RNG_SEED");
    expect_name("disc", "MELEE_DISC");

    /* `enabled` collapses into its section, so one section can carry both a
     * switch and the settings it governs. */
    expect_name("cinematic.enabled", "MELEE_CINEMATIC");
    expect_name("cinematic.bloom", "MELEE_CINEMATIC_BLOOM");
    expect_name("mods.unbound.enabled", "MELEE_MOD_UNBOUND");

    /* Refusals: nothing that would compose a name the environment cannot
     * hold, or that would collide with an unrelated variable. */
    expect_name("enabled", NULL);
    expect_name("bad key", NULL);
    expect_name("weird$name", NULL);
    expect_name("", NULL);

    f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "config: cannot write %s\n", path);
        return 1;
    }
    fputs("# a comment\n"
          "disc = \"/discs/melee.iso\"   # trailing comment\n"
          "\n"
          "[cinematic]\n"
          "enabled = true\n"
          "bloom = 0.20\n"
          "\n"
          "[match]\n"
          "cpu = 9\n"
          "\n"
          "[mods.unbound]\n"
          "widescreen = 1\n"
          "aspect_correct = false\n",
          f);
    fclose(f);

    /* Set before loading: the environment must win. */
    setenv("MELEE_MATCH_CPU", "3", 1);
    unsetenv("MELEE_DISC");
    unsetenv("MELEE_CINEMATIC");
    unsetenv("MELEE_CINEMATIC_BLOOM");
    unsetenv("MELEE_MOD_UNBOUND_WIDESCREEN");
    unsetenv("MELEE_MOD_UNBOUND_ASPECT_CORRECT");

    if (melee_config_load(path) == NULL) {
        fprintf(stderr, "config: load failed\n");
        return 1;
    }

    expect_env("MELEE_DISC", "/discs/melee.iso");
    expect_env("MELEE_CINEMATIC", "1");
    expect_env("MELEE_CINEMATIC_BLOOM", "0.20");
    expect_env("MELEE_MOD_UNBOUND_WIDESCREEN", "1");
    expect_env("MELEE_MOD_UNBOUND_ASPECT_CORRECT", "0");
    /* The contract: exported beats written down. */
    expect_env("MELEE_MATCH_CPU", "3");

    remove(path);
    if (failures != 0) {
        fprintf(stderr, "config: FAIL (%d)\n", failures);
        return 1;
    }
    printf("config: PASS\n");
    return 0;
}

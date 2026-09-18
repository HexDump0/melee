/*
 * melee.toml reader.  See melee_config.h for the contract.
 *
 * Deliberately a subset: section headers, `key = value`, `#` comments,
 * strings, integers, floats and booleans.  `mod_wasm.c` says the same thing
 * about `mod.toml` and for the same reason -- a settings file is not worth a
 * dependency we would then have to carry into the wasm build.  What this adds
 * over that reader is sections, because the whole point is to group forty flat
 * names into something a person can read.
 */
#include "melee_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_MAX_LINE 512
/* A key the environment could actually hold.  `melee_config_env_name` refuses
 * anything longer, so an over-long key is reported rather than silently
 * truncated into a different variable's name. */
#define CFG_MAX_KEY 192
/* Sized from the line, not from the key, so composing "section.key" cannot
 * truncate; over-length is then rejected by the mapping, with a message. */
#define CFG_MAX_PATH (CFG_MAX_LINE * 2)

static void cfg_trim(char* s)
{
    size_t n = strlen(s);
    size_t i = 0;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                     s[n - 1] == '\n'))
    {
        s[--n] = '\0';
    }
    while (s[i] == ' ' || s[i] == '\t') {
        ++i;
    }
    if (i > 0) {
        memmove(s, s + i, n - i + 1);
    }
}

/* Strips one layer of surrounding quotes, after trimming. */
static void cfg_unquote(char* s)
{
    size_t n;
    cfg_trim(s);
    n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
                   (s[0] == '\'' && s[n - 1] == '\'')))
    {
        s[n - 1] = '\0';
        memmove(s, s + 1, n - 1);
    }
}

/*
 * The mapping, which is a rule rather than a table of forty rows.
 *
 * `MELEE_` + the dotted path, uppercased, dots to underscores.  The existing
 * variable names already have this shape -- `MELEE_MATCH_CPU`,
 * `MELEE_MOD_UNBOUND_WIDESCREEN`, `MELEE_GX_TEX_STATS` -- so the sections a
 * person would write for tidiness are the prefixes the port already uses, and
 * nothing needs translating.
 *
 * One rule on top: a key named `enabled` collapses into its section, so
 * `[cinematic] enabled = true` is `MELEE_CINEMATIC` rather than
 * `MELEE_CINEMATIC_ENABLED`, which nothing reads.  That is what lets a
 * section hold both an on/off switch and the settings it governs.
 *
 * `[mods.x]` is spelled `MELEE_MOD_X_`, singular, because that is what
 * `unbound_config_int` builds.
 */
int melee_config_env_name(const char* key, char* out, unsigned out_size)
{
    char work[CFG_MAX_KEY];
    size_t n;
    size_t w = 0;
    size_t i;

    if (key == NULL || out == NULL || out_size < 8) {
        return 0;
    }
    n = strlen(key);
    if (n == 0 || n >= sizeof(work)) {
        return 0;
    }
    memcpy(work, key, n + 1);

    /* `enabled` collapses into its section; a bare `enabled` has none. */
    if (n > 8 && strcmp(work + n - 8, ".enabled") == 0) {
        work[n - 8] = '\0';
        n -= 8;
    } else if (strcmp(work, "enabled") == 0) {
        return 0;
    }

    if (strncmp(work, "mods.", 5) == 0) {
        /* `mods.` is the only rename: the port's prefix is singular.  Drop
         * the `s` at index 3 and keep the dot, which becomes the separator. */
        memmove(work + 3, work + 4, n - 4 + 1);
        n -= 1;
    }

    w = (size_t) snprintf(out, out_size, "MELEE_");
    if (w >= out_size) {
        return 0;
    }
    for (i = 0; i < n && w + 1 < out_size; i++) {
        char ch = work[i];
        if (ch == '.' || ch == '-') {
            ch = '_';
        } else if (ch >= 'a' && ch <= 'z') {
            ch = (char) (ch - 'a' + 'A');
        } else if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                     ch == '_'))
        {
            return 0; /* not a name the environment can hold */
        }
        out[w++] = ch;
    }
    if (i != n) {
        return 0;
    }
    out[w] = '\0';
    return w > 6;
}

static void cfg_set(const char* dotted, const char* value, int verbose)
{
    char name[CFG_MAX_PATH];
    if (!melee_config_env_name(dotted, name, sizeof(name))) {
        fprintf(stderr, "[config] ignoring key \"%s\"\n", dotted);
        return;
    }
    /*
     * Never overwrite.  This is the whole precedence rule, and it is one
     * argument: anything already in the environment was put there by the
     * person running the port, and it outranks a file they wrote earlier.
     */
    if (getenv(name) != NULL) {
        if (verbose) {
            fprintf(stderr, "[config] %s: environment wins\n", name);
        }
        return;
    }
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
    if (verbose) {
        fprintf(stderr, "[config] %s=%s\n", name, value);
    }
}

static int cfg_read(const char* path, int verbose)
{
    FILE* f = fopen(path, "r");
    char line[CFG_MAX_LINE];
    char section[CFG_MAX_LINE];

    if (f == NULL) {
        return 0;
    }
    section[0] = '\0';

    while (fgets(line, sizeof(line), f) != NULL) {
        char* hash;
        char* eq;
        char dotted[CFG_MAX_PATH];

        /*
         * A `#` inside a quoted value is data, not a comment.  A path is the
         * value most likely to contain one, and silently truncating someone's
         * disc path at a `#` would be a long afternoon.
         */
        {
            int in_quote = 0;
            hash = NULL;
            for (eq = line; *eq != '\0'; ++eq) {
                if (*eq == '"' || *eq == '\'') {
                    in_quote = !in_quote;
                } else if (*eq == '#' && !in_quote) {
                    hash = eq;
                    break;
                }
            }
        }
        if (hash != NULL) {
            *hash = '\0';
        }
        cfg_trim(line);
        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == '[') {
            char* close = strchr(line, ']');
            if (close == NULL) {
                continue;
            }
            *close = '\0';
            snprintf(section, sizeof(section), "%s", line + 1);
            cfg_trim(section);
            continue;
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        cfg_trim(line);
        cfg_unquote(eq + 1);
        if (line[0] == '\0') {
            continue;
        }
        if (section[0] != '\0') {
            snprintf(dotted, sizeof(dotted), "%s.%s", section, line);
        } else {
            snprintf(dotted, sizeof(dotted), "%s", line);
        }
        /* TOML booleans; the port's variables are all "is it set, and to
         * what number", so these are the numbers it already expects. */
        if (strcmp(eq + 1, "true") == 0) {
            cfg_set(dotted, "1", verbose);
        } else if (strcmp(eq + 1, "false") == 0) {
            cfg_set(dotted, "0", verbose);
        } else {
            cfg_set(dotted, eq + 1, verbose);
        }
    }
    fclose(f);
    return 1;
}

const char* melee_config_load(const char* path)
{
    static char found[CFG_MAX_LINE];
    const char* verbose_env = getenv("MELEE_CONFIG_TRACE");
    int verbose = verbose_env != NULL && verbose_env[0] != '0';
    const char* home;
    const char* xdg;

    if (path != NULL && path[0] != '\0') {
        if (cfg_read(path, verbose)) {
            snprintf(found, sizeof(found), "%s", path);
            return found;
        }
        /* Asked for by name and not there: say so.  Falling back silently
         * would run with settings the person did not choose. */
        fprintf(stderr, "[config] cannot read %s\n", path);
        return NULL;
    }
    path = getenv("MELEE_CONFIG");
    if (path != NULL && path[0] != '\0') {
        if (cfg_read(path, verbose)) {
            snprintf(found, sizeof(found), "%s", path);
            return found;
        }
        fprintf(stderr, "[config] cannot read %s (MELEE_CONFIG)\n", path);
        return NULL;
    }
    xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(found, sizeof(found), "%s/melee/melee.toml", xdg);
        if (cfg_read(found, verbose)) {
            return found;
        }
    }
    home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        snprintf(found, sizeof(found), "%s/.config/melee/melee.toml", home);
        if (cfg_read(found, verbose)) {
            return found;
        }
    }
    snprintf(found, sizeof(found), "melee.toml");
    if (cfg_read(found, verbose)) {
        return found;
    }
    return NULL;
}

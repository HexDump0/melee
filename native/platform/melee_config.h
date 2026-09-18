#ifndef MELEE_PLATFORM_MELEE_CONFIG_H
#define MELEE_PLATFORM_MELEE_CONFIG_H

/*
 * melee.toml -- the port's settings file (P-867).
 *
 * The port is configured by around forty `MELEE_*` environment variables and
 * had nowhere to persist any of them, so every setting lived in a shell
 * history.  This reads a small TOML file and *populates the environment* from
 * it rather than introducing a second way to ask for a setting: every existing
 * `getenv` call site keeps working untouched, and there is still exactly one
 * source of truth at the point of use.
 *
 * Precedence, highest first: command line, environment, file, built-in
 * default.  The environment winning is not an accident of the implementation
 * -- `setenv(name, value, 0)` refuses to overwrite -- it is the contract, so
 * `MELEE_CINEMATIC=1 ./melee` still does what it says with a file present.
 *
 * Loads the first of: `path` (from --config), $MELEE_CONFIG,
 * $XDG_CONFIG_HOME/melee/melee.toml, ~/.config/melee/melee.toml,
 * ./melee.toml.  Returns the path it used, or NULL when there was no file,
 * which is not an error -- the port runs on defaults.
 */
const char* melee_config_load(const char* path);

/*
 * The environment variable a dotted config key maps to, e.g.
 * "mods.unbound.widescreen" -> "MELEE_MOD_UNBOUND_WIDESCREEN".  Exposed for
 * the tests and for the launcher, which has to write what the port will read.
 * Returns 0 if the key cannot be mapped.
 */
int melee_config_env_name(const char* key, char* out, unsigned out_size);

#endif

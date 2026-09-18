# Melee Unbound — launcher

A Tauri shell around the native port: settings, mods, and the crash reports the
port has always printed and never kept.

```sh
cd launcher
npm install
npm run app        # dev, with hot reload
npm run app:build  # .deb in src-tauri/target/release/bundle
```

`.deb` is the default bundle because it needs nothing from the network.
AppImage works too — add `"appimage"` to `bundle.targets` — but its first run
downloads `AppRun` and `linuxdeploy` from GitHub, so it fails on a machine
without network access rather than telling you why.

## What it is, and what it deliberately is not

**It does not own any settings.** Everything it writes goes into `melee.toml`,
which the port reads on its own — the launcher is one way to edit that file and
the file works without it. Precedence is unchanged: command line, then
environment, then file. An exported `MELEE_*` variable still beats anything set
here, which is why every control shows the variable it becomes.

**It does not know what a mod's settings are.** The Mods page is generated from
each mod's `mod.toml`. A mod that ships `[[setting]]` blocks gets labelled,
ranged, explained controls without a line of launcher code; one that doesn't
still loads, and simply has nothing to configure. That is the difference
between a settings screen and a mod platform.

**Its own preferences live elsewhere** — `~/.config/melee/launcher.json`, for
the port binary's path and the last profile. Those are nobody's business but
the launcher's, and every key in `melee.toml` becomes an environment variable
the port reads, so they must not go there.

## Profiles

Applied as environment overrides on top of the file, never saved into it, so
recording a clip cannot quietly rewrite the settings you play with.

| Profile | What it changes |
|---|---|
| Play | Nothing. Your settings as saved. |
| Record | Cinematic on, a level 9 CPU match, no memory card. |
| Debug | Triage output and a full config trace on stderr. |

## Crashes

When the port panics or dies on a signal, the launcher keeps the tail of its
output, the settings it ran with, and the seed it printed at boot. **Replay
this crash** relaunches with `MELEE_RNG_SEED` set to that seed, which
reproduces the run exactly. **Copy report** puts the whole thing on the
clipboard in one block.

Reports live in `~/.local/share/melee-unbound/crashes/`.

## Design

The palette, type and motion come from `site/src/styles.css`, which the
launcher's stylesheet imports rather than copies — `branding.md` is the source
of truth and a second copy of `--color-mu-violet` is a second thing to forget
when the brand moves. The one rule worth repeating: **black text on violet**,
never white, because white on violet is 2.77:1 and fails.

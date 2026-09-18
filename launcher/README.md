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

## Getting the port

**Check for update** asks the GitHub API for the latest release and shows the
tag and size; **Install** then downloads it. Two steps on purpose — a button
that silently pulls a binary off the internet is not one anyone should trust.

Assets are matched **by name**, `melee-<os>-<arch>[.exe]`, with no fuzzy
matching and no "it's the only asset" fallback: a wrong binary is worse than a
clear failure. When nothing matches, the launcher lists what the release did
contain. The scheme is `native/AI/reference/releases.md`, and it is a contract
with whatever publishes releases.

Downloads land in `$XDG_DATA_HOME/melee-unbound/bin/`, never next to the
launcher, which may be installed somewhere unwritable.

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

## Permissions

Tauri 2 denies every plugin command that a capability does not grant, silently.
`src-tauri/capabilities/default.json` is what makes the Browse buttons work;
without it they do nothing at all and say nothing about why. A new plugin
command needs a line there.

## Design

The palette, type and motion come from `site/src/styles.css`, which the
launcher's stylesheet imports rather than copies — `branding.md` is the source
of truth and a second copy of `--color-mu-violet` is a second thing to forget
when the brand moves. The one rule worth repeating: **black text on violet**,
never white, because white on violet is 2.77:1 and fails.

## Looking at the UI without building the shell

`preview.html` runs the real `App` against fabricated data in an ordinary
browser, by stubbing the `window.__TAURI_INTERNALS__` object that `invoke`
goes through. That makes the layout reviewable — and screenshottable — without
compiling Rust or opening a window on anyone's desktop.

```sh
npm run dev
# http://localhost:5183/preview.html?page=play|mods|graphics|crashes
chromium --headless --window-size=1180,760 \
    --screenshot=play.png "http://localhost:5183/preview.html?page=play"
```

It is dev-only: `vite build` has `index.html` as its entry, so nothing in the
harness ships. It has already earned its keep — the first pass drew range
thumbs in Chromium's default **blue**, against a palette that allows exactly
one accent, and rendered the toggle's two states almost identically. Neither
is visible in a type-check or a build log.

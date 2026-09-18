# Melee Unbound — launcher

A Tauri shell around the native port: settings, mods, and the crash reports the
port has always printed and never kept.

```sh
cd launcher
npm install
npm run app        # dev, with hot reload
npm run app:build  # .deb in src-tauri/target/release/bundle
```

`npx tauri build` makes a `.deb` locally. **The release also ships an
`.AppImage`, but CI asks for it rather than `tauri.conf.json`** — the AppImage
bundler downloads `linuxdeploy` from GitHub on first use, and that download
times out on some machines (it does on the maintainer's, even with network).
Keeping it out of the config means a local build always succeeds; the workflow
passes `--bundles deb,appimage`.

If you want one locally and the download works for you:
`npx tauri build --bundles appimage`.

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

## Noise on stderr

Running the launcher from a terminal on GTK3 prints, repeatedly:

```
*** BUG ***
In pixman_region32_init_rect: Invalid rectangle passed
```

That is GTK3, not this application. It fires whenever a widget is realized at
a size below the client-side-decoration shadow (~50x50), and it shows up the
same way in Inkscape, Audacity, Scintilla and Eclipse. The window works.

It cannot reach a crash report: the port runs as a child with its own piped
stdout and stderr, so the launcher's own stderr is a separate stream and never
enters the captured log. Launching from the desktop entry the `.deb` installs
sends it to the journal, where you will not see it. From a terminal, `2>/dev/null`.

## Third-party assets

The controller diagram on the Controls page is **GameCube Button Icons and
Controls** by **Zacksly**, licensed **CC BY 3.0**
(<https://creativecommons.org/licenses/by/3.0/>), from
<https://zacksly.itch.io>.

**It has been modified.** The licence requires saying so:

- every `#000` stroke and fill became `currentColor`, so the page's palette
  drives the art instead of a fixed black that would be invisible on it;
- the "NINTENDO GAMECUBE" wordmark group was removed, because it is somebody
  else's trademark and this is a launcher for a fan port.

The modified file is `src/assets/gamecube-controller.svg`; the credit is also
shown under the diagram in the app, which is where a reader of the UI can
actually see it. The hotspots drawn over it are ours.

The repository-level record is `THIRD-PARTY.md`.

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

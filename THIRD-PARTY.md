# Licensing and third-party material

## What the MIT licence in `LICENSE` covers

**This project's own work**, which is everything written for it:

- `native/` — the port layer: OS, DVD, GX→GLES3, AX audio, the asset
  converter, the boot triage
- `launcher/` — the Tauri launcher
- `mods/` — Melee Unbound and the mod SDK headers
- `site/` — the website
- `patches/` — the portability patch series
- the tooling, tests and documentation under `native/AI/` and `native/tools/`

## What it does not cover, and cannot

**`decomp/` is not ours to license.** It is the community decompilation at
[doldecomp/melee](https://github.com/doldecomp/melee), included as a submodule.
It is a reconstruction of *Super Smash Bros. Melee*, which is copyright
Nintendo — neither this project nor the decompilation holds any rights in that
game, and the MIT grant above extends to neither. The submodule carries no
licence file of its own, which is deliberate on their part and not an oversight
on ours.

Practically, that means: **this software ships no game data and requires you to
supply your own legally-obtained disc image.** It is a port of code that
describes the game, not a copy of the game.

**`native/third_party/` is other people's code**, under their own terms:

| | |
|---|---|
| `wasm-micro-runtime` | Apache-2.0 (Bytecode Alliance) — submodule |
| `khronos` | MIT and Apache-2.0, per each file's `SPDX-License-Identifier` |

Code dependencies pulled by `Cargo.toml` and `package.json` carry their own
licences and are not restated here.

## Assets

Assets in this repository that somebody else made, and what their licences
require of us. Code dependencies are declared by their own manifests
(`Cargo.toml`, `package.json`); this file is for **assets**, which nothing
checks automatically.

## GameCube Button Icons and Controls — Zacksly

- **Used for:** the controller diagram on the launcher's Controls page.
- **File:** `launcher/src/assets/gamecube-controller.svg`
- **Source:** <https://zacksly.itch.io>
- **Licence:** CC BY 3.0 — <https://creativecommons.org/licenses/by/3.0/>

**Modified.** The licence requires that we say so and say how:

1. Every `#000` stroke and fill became `currentColor`, so the launcher's own
   palette drives the art. Unmodified it is black line work, which is
   invisible on this product's black background.
2. The "NINTENDO GAMECUBE" wordmark group (`<g id="layer1">`) was removed. It
   is somebody else's trademark and this is a fan port's launcher, not a
   Nintendo product.

The credit is also rendered under the diagram in the application, because a
licence term satisfied only in a file nobody opens is satisfied in form
rather than in substance.

**If you replace or re-export this asset**, keep both the attribution and the
statement of modification. Dropping the wordmark removal in particular would
put a trademark back into the UI.

## OpenGL ES headers — The Khronos Group

- **Used for:** the Windows build. Linux links `libGLESv2` and gets its
  prototypes from the system headers; Windows has neither, so the API is
  declared from Khronos' own headers and the entry points are fetched at run
  time (`native/gx/gl_api.h`).
- **Files:** `native/third_party/khronos/{GLES3/gl3.h, GLES3/gl3platform.h,
  GLES2/gl2platform.h, KHR/khrplatform.h}`
- **Source:** <https://registry.khronos.org/OpenGL/>
- **Licence:** MIT (`gl3.h`) and Apache-2.0 (the platform headers), as each
  file's own `SPDX-License-Identifier` states.

**Unmodified**, deliberately: `gl_api.h` sets `GL_GLES_PROTOTYPES 0` to get the
typedefs without the prototypes rather than editing the headers, so they can be
replaced with a newer registry copy without re-applying anything.

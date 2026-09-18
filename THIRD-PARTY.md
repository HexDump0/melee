# Third-party assets

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

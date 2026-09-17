# The mod system

ADR-0026 (what a mod is) and ADR-0027 (what a mod is made of) hold the
decisions. This is the part a future agent needs in order to work on it.

## The shim was already a mod loader

`native/decomp/shim/decomp_shim.h` is force-included (`-include`) into every
compiled decomp TU. It has carried `#define HSD_ArchiveParse
melee_port_HSD_ArchiveParse` since S3. That single line is a **complete,
general, link-time symbol interposition facility**, and it has three properties
that are hard to get any other way:

- it costs nothing when unused -- no rename, no call, no branch;
- it needs no dynamic loader, so it works identically in the browser;
- it cannot desynchronise from the decomp, because the compiler resolves it.

The mod system did not invent a hook mechanism. It named the one already in the
tree and added a registry in front of it. If you are about to add a hook, the
work is: one `#define` in the shim, one interposer in `native/mod/`, one enum
value in `mods/include/unbound_abi.h`, and one row in `mod.c`'s `hook_effect`
table. Nothing else.

**The defining TU needs a guard.** `cobj.c` implements the functions the shim
renames, and so does the interposer; both get `MELEE_COBJ_INTERNAL=1` through
`set_source_files_properties`, exactly as `archive.c` and `hsd_convert.c` get
`MELEE_ARCHIVE_INTERNAL`. Forget it and you get infinite recursion, not a link
error.

## Transient application is what makes a hook "presentation-only"

`mod_cobj.c` does not write a mod's values into the camera and leave them
there. It saves the projection, applies, calls the real
`HSD_CObjSetCurrent`, and restores **unconditionally, including on the failure
path**. The camera the engine reads on the next line is byte-for-byte the one
it wrote.

That is not tidiness, it is the whole classification. Leave the value in place
and `Camera_ToScreen`, the magnifier, `lbrefract.c` and every other consumer
start seeing it, and the hook is no longer something two machines could
disagree about safely.

**A thing worth knowing before you "improve" this:** `cm/camera.c` fits the
shot to the players using the aspect from its **static descriptor**
(`cm_803BCB64`, `camera.c:88`), *not* from the live `HSD_CObj` -- see
`camera.c:756`, `887-893`, `947`, `1132-1177`. So widening the live camera
does not move the camera. If you ever switch to writing the descriptor
instead, widescreen stops being presentation-only and becomes a gameplay
change, and the classification in `unbound_abi.h` becomes a lie.

## Identify engine objects by identity, not by their numbers

The background-flash camera must not be widened: its quad is authored to
exactly fill its frustum, so a wider frustum leaves unflashed bars down both
sides. It is tempting to recognise it from its values (perspective, aspect
1.3333, 640x480 viewport).

**Don't.** `lbl_803BB028` is a global in `lbbgflash.c`, so the interposer
`extern`s it and compares the *descriptor pointer* in the `HSD_CObjLoadDesc`
wrapper. A "looks like an overlay" heuristic is the exact shape of bug that
cost us P-830, where a scan that accepted anything resembling a
`MatAnimJoint` walked into real `TObjDesc`s and zeroed two texture-repeat
bytes.

The console widescreen codes do the same thing, but had to spend a spare
register (`r14`) on it because they could not name the symbol. We can.

## What crosses into a wasm mod, and what cannot

Host is x86-32, guest is wasm32: both 32-bit little-endian with the same
alignment for `int`, `unsigned` and `float`. So a payload struct built only
from those has **one layout on both sides** and is `memcpy`'d whole into the
guest's own exported buffer. That is why `unbound_abi.h` bans everything else.

**No host pointer ever crosses.** A guest cannot dereference one. Engine
objects are opaque `unsigned` handles that are only ever compared. Guest
pointers going the other way are fine -- a wasm pointer is an i32 offset into
the guest's own memory -- but the host must `wasm_runtime_validate_app_addr`
before `wasm_runtime_addr_app_to_native`, because the offset came from guest
code. `mod_wasm.c` does this once for the payload buffer; WAMR's `*~`
signature does it for every string.

Strings use `(pointer, length)`, never NUL-termination, so a guest never has to
be trusted to terminate anything.

## Building a mod needs nothing that was not already installed

`clang --target=wasm32 -nostdlib` plus `wasm-ld`, both already on the machine.
No wasi-sdk, no emsdk. (The emsdk at `~/projects/emsdk` is for the browser
build of the *game* and is unrelated.) `unbound.wasm` is 1.3 KB.

The module is built into the **source** tree at `mods/unbound/unbound.wasm` and
gitignored, because that is where a run from the repo root looks for it. The
manifest is committed; the module is not.

## The registry cannot depend on the renderer

`melee_decomp_boot` is headless and links no GL backend at all, so `mod.c`
reaching into `gx_gl` directly fails to link there -- which is how the first
build broke. The display is behind `ModDisplayBackend`, installed by whoever
owns a drawable. With nothing installed the host reports a bare 640x480 at
4:3 and a display mod becomes a no-op, which is the right answer for a
headless run rather than a special case inside every mod.

## Verifying a presentation change

Pixel-diffing two captures is mostly a trap, and two of the three things tried
here were worthless:

- **Worked:** counting black columns. Vanilla at 1920x1080 gives exactly 240
  each side (`(1920 - 1080*4/3)/2`); widescreen gives 0, and the revealed
  strips are 100% non-black, so they are real content and not bars.
- **Worked:** the no-op test. At a 4:3 window the mod must change nothing, and
  mod-on vs mod-off differ by 13,103 bytes against a **10,710-byte** vanilla
  run-to-run noise floor. Always measure that floor first -- the 240-frame
  match capture is *not* byte-reproducible, though it is stable to 0.07 mean
  channel difference in the mid-screen band.
- **Useless:** comparing the overlapping region, and cross-correlating edge
  profiles. The overlap genuinely differs (~27 mean channel difference)
  because a wider frustum moves every 2D ortho element and changes GX fog
  range adjustment, which is a function of horizontal EFB distance from
  centre. Neither metric separates "correctly widened" from "wrongly scaled",
  and no amount of further arithmetic was going to. **Ask the owner to look.**

## Hold a camera change for the whole draw pass, not just the call

Applying the mod's value, calling the real `HSD_CObjSetCurrent`, and restoring
before returning is the tidy-looking version and it is wrong. Engine code
re-derives geometry from the live camera *after* SetCurrent returns --
`HSD_CObjEraseScreen` sizes its backdrop quad from the camera's aspect, and
`gmTitle_801A18D4` calls it one line later. Restoring early meant every
backdrop in the game was built for the old aspect and drawn through the new
projection, covering exactly the old 4:3 rectangle. Apply in SetCurrent,
restore in `HSD_CObjEndCurrent`, and restore defensively on the next
SetCurrent too because pairing is not universal. Full write-up in G-215.

## Only widen cameras that cover the whole EFB

The off-screen-player magnifier renders its bubble through an ortho camera
whose viewport is the bubble's own width and height (`ifmagnify.c:533`).
Widening that squashed its contents: the rectangle it draws into never
changed shape when the window did, because the EFB is fitted to the window
uniformly. `UnboundCameraSetup` carries the viewport so a mod can tell.

This is a fact about the camera, not a guess about what it draws -- which is
the distinction that matters after P-830.

**A related thing this ruled out.** The magnifier's *trigger* is world-space:
`ifmagnify.c:340` tests the player's x against
`Stage_GetCamBoundsLeftOffset/RightOffset`. Widescreen cannot make a bubble
appear early or late. An earlier reading of this had it as a screen-space test
and concluded widescreen would have to stop being presentation-only to fix it.
It didn't. Check the mechanism before trading away a property.

## Widescreen belongs to gameplay; a menu is a picture, not a window

Menus, splashes, results and cutscenes are compositions authored for 4:3.
There is no world behind them, so widening can only reveal the edge of the
composition. Their backdrop plates carry about **18% of overscan margin** --
measured off an owner screenshot where they end at x=104/1812 against
x=110/1808 predicted for a 1.18x asset at 16:9 -- so they fall ~6% short a
side. No camera transform fixes that; the plates are the wrong *size*.

So the mod asks `unbound_scene_kind()` and leaves non-gameplay screens at
their authored aspect. It asks the host rather than reading the engine's scene
index itself, so no mod carries a table of scene numbers the pin could move.

**How it is really fixed, from reading the community's `ssbmws.xdelta`:** that
patch is a VCDIFF over a v1.02 ISO changing **~13 KB of literal bytes** across
`main.dol` and roughly 200 archives -- trophies, stages, `SdVsCam`, `IfAll`,
`GmTtAll`, the menu archives, `GmRst*`, `GmPause`. It **rewrites the data**:
camera-descriptor `aspect` floats and plate sizes, edited in place. 13 KB is
far too little for new artwork, which settles the question -- the plates are
flat, so widening one costs nothing artistically. We have the same lever at
load time through `melee_port_HSD_ArchiveParse`. P-837.

**Parsing an xdelta without applying it is cheap and worth knowing.** The
VCDIFF window headers give target offset and how many literal bytes each
window carries, even when the sections themselves are secondary-compressed and
undecodable. Map those offsets against the disc's FST and you learn exactly
which files a patch touches and how much of each, from the patch alone.

## The harness is not the game

`--match` jumps straight into a match without running the scene machinery, so
`gm_GetCurrentSceneIndex()` still reports the boot menu (scene 1, mode 24)
forever. Anything that keys off engine scene state will silently do nothing
under that harness while working perfectly in the product. The port states the
truth with `mod_set_scene_override`; a mod should not have to know our harness
exists.

Verify a scene-dependent feature on the **frontend** path
(`--frontend --input native/tests/frontend_vs.txt`), not on `--match`.

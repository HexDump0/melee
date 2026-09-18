# Handoff: P-847 — an Unbound opening movie, played by the game's own player

**Date:** 2026-09-18
**Agent:** claude (opus-5 1M)
**Commit:** this commit
**Tree state:** builds warning-free; `ctest --test-dir build/native` 34/34.

## What I did

- The owner asked for a second opening screen that is native rather than an
  overlay. It is: `mods/unbound/files/MvUnbound.mth` is a **real MTH file**,
  streamed by `lbmthp.c` through the entrynum path, decoded by the port's own
  `thp_dec.c` and drawn on the same SObj as `MvOpen.mth`. No video decoder was
  added to the port and none is needed.
- `scripts/make_boot_mth.py` packs `assets/melee-unbound-boot.mp4` into that
  file. The format was read off the disc (`MvOpen.mth`) and cross-checked
  against `fn_8001EB14` / `fn_8001ECF4`: a 0x40 header, then blocks of
  `u32 be next-block-size` + JPEG, zero-padded to 32. 16:9 is letterboxed into
  the 640x480 plane, not squashed — the mark has to stay a circle.
- **Four link-time interposers, no new engine callback** (`decomp_shim.h`):
  `lbMthp_8001F410` parks the scene's `MvOpen.mth` request and starts the clip;
  `lbMthp_8001F578` — the opening scene's own per-frame pump, called from
  nowhere else — is the frame hook that hands over; `gm_GetButtonsTriggered`
  spends the skip press; `lbAudioAx_80023F28` holds the movie's music.
- **New capability:** `platform_disc_add_host_file()` publishes a host file
  into the disc's root by appending an FST entry, so
  `DVDConvertPathToEntrynum` resolves it like any other file and DVDFS, DevCom
  and the player's streaming know nothing about it. This is P-832's core.
- `decomp_unbound_opening` covers the clip, the hand-off, the skip, the music
  and `MELEE_NO_MODS=1`. `decomp_opening` now pins `MELEE_NO_MODS=1`, because
  it is the *retail* movie's regression and was about to start measuring a
  different movie without saying so.

## The finding worth keeping: THP scan data is not byte-stuffed

A standard JPEG escapes every 0xFF in the entropy segment as `FF 00` so a
decoder can still find markers. **THP does not**, and `thp_dec.c`'s bit reader
indexes scan bytes directly (`reader->data[bit_position >> 3]`) with no
unescaping anywhere. Measured on the disc: MvOpen.mth's first frames carry
**241, 507 and 648 raw 0xFF bytes and zero `FF 00` pairs**.

Feed that decoder a correctly stuffed JPEG and it desynchronises one bit-run
after the first 0xFF. The symptom is not a bad picture: `THPVideoDecode`
returns **-1**, `fn_8001EF5C` stores it in `unk_98`, and `THPDec_80331340`
dereferences it as a state pointer — a SIGSEGV four frames into the boot, in
a function whose NULL/magic guard looks like it would have caught this.

It presents as "only the first frame works": frame 0 of the Unbound clip is
flat black and contains no 0xFF at all, so **1 of 120 frames decoded**. That
asymmetry is the tell, and it is the fastest way back to this cause.

## Exact next action

Owner pass: `./build/native/melee` with a save on the card. Expect the Unbound
clip, then the Melee movie with its music starting on its own first frame, and
any button during the clip cutting to that movie (not to the title).

## What I tried that did not work

- ffmpeg's default `-huffman optimal`. Its per-frame tables can hold a single
  symbol, which the decoder also refuses. `-huffman default` emits the four
  standard tables retail uses. This was a real second defect, fixed before the
  stuffing one was found — neither alone was sufficient.
- Clearing the triggered bits to consume the skip press. `controller_map` is a
  **file-static** in `gm_1A36.c`, so another TU including the header gets its
  own copy, not the engine's. Interposing the read is the reachable version of
  the same idea.
- A synthetic disc offset of `0xF0000000` for host files. The DVD entry points
  take a signed 32-bit offset, so it lands negative. `0x70000000` is above any
  real disc offset (an image tops out at `0x57058000`) and stays positive.

## Open questions

- The clip is silent, which is what the artwork ships as. A fanfare would need
  an HPS track and its own start/stop around the hand-off — not attempted;
  say so if it is wanted.
- **No ADR was written**, though "mods may add files to the disc namespace"
  probably deserves one. `DECISIONS.md` has another agent's uncommitted
  ADR-0029 in it, and committing my paragraph would have dragged their work in
  with it. Worth adding as ADR-0030 once that lands.
- `native/AI/reference/branding.md` (untracked, another agent's in-flight
  work) has a section headed "It does not play in the port yet" listing three
  ways the animation could become a real boot. Option 3, "replace
  MvOpen.mth", is now half-built: the encoder exists, and the clip plays
  *before* the movie rather than instead of it. That section needs rewriting
  by whoever owns the file.
- `MvUnbound.mth` is 1.13 MiB of generated data and is committed, because the
  build must not require ffmpeg. It regenerates byte-identically from the mp4.

## Files touched / claimed

- `scripts/make_boot_mth.py` (new), `mods/unbound/files/MvUnbound.mth` (new)
- `native/mod/mod_opening.c` (new), `native/tests/unbound_opening.sh` (new)
- `native/platform/dvd.c`, `native/platform/platform.h`,
  `native/decomp/shim/decomp_shim.h`, `native/CMakeLists.txt`,
  `native/mod/mod.h`, `native/decomp/boot/boot_main.c`,
  `native/decomp/render/viewer_main.c`, `native/tests/frontend_opening.sh`
- `native/AI/{STATE.md,TASKS.md,TESTING.md,HANDOFFS.md}`

## Verification run

```
ctest --test-dir build/native -j2                 # 34/34 passed
ctest -R decomp_unbound_opening -V
  unbound_opening: clip bright=0.0294 skip bright=0.9219 movie bright=0.9219
  unbound_opening: PASS

# the decoder, against both movies, using the port's own thp_dec.c:
MvOpen.mth       3036 frames, 0 failed
MvUnbound.mth     120 frames, 0 failed      (stuffed: 119 of 120 failed)

# in the game, MELEE_VIEWER_TRIAGE=1, save on the card:
[unbound] opening: playing MvUnbound.mth before MvOpen.mth
[unbound] opening: holding track 0x3e until MvOpen.mth starts
[unbound] opening: clip finished at frame 239      (or: skipped at frame 54)
[unbound] opening: starting track 0x3e
  frame 110 screenshot: the mark, ring and MELEE wordmark, disc circular
  frame 480 screenshot: MvOpen.mth's sky montage
  MELEE_NO_MODS=1: no [unbound] lines, boots straight into MvOpen.mth
```

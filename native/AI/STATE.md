# State of the port

> **Race to the Finish panicked on entry, and it was the last stage on the raw
> fallback that anyone had hit (2026-09-18, P-845).** `GrNPo.dat` matched no
> `stage_param_markers` entry, so its `yakumono_param` stayed big-endian --
> and `grPushOn_80219230` scans that block's `{ key, value }` table for the
> player's character to get the stage's time limit, from
> `rules.on_match_start`, on **every** entry to the stage. Nothing matched a
> byte-reversed key, the scan ran past the `-1` terminator, and
> `HSD_ASSERT(861, 0)` fired at `grpushon.c:681`. It is Classic round `0x08`,
> the bonus stage immediately after the team battle, which is where the owner
> hit it. Converter **v136** adds the layout, keyed on `GrdPushon`; the raw
> table reads `0,39  1,43 ... 25,54`, per-character seconds, exactly as the
> decompilation says. Same family as P-707, P-770 and P-791. ctest 33/33.

> **The game boots into its own opening movie now (2026-09-18, P-844).**
> `OSGetResetCode` returned the console's "reset to menu" code `0x80000000` on
> every launch, which is what `gmMainLib_8015FCC0` turns into `skip_intro`, so
> `bootOnLoad` went straight to `GM_TITLE` and `MvOpen.mth` only played under
> `MELEE_OPENING=1`. A cold boot returns **0**, and that is what the port
> returns by default: `./build/native/melee` plays the movie and then falls
> into the title, as the console does. With no save on the card the game's own
> `lbCardGame_DecideGameMode` override still sends the boot to `GM_MEMCARD`
> first -- that is the decompilation's behaviour, not the port's.
> **`MELEE_NO_OPENING=1` asks for the old boot**, and the whole test suite
> does: the frontend input scripts, the title probe's frame-400 window and the
> match tests' frame-600 position all count frames from the boot, so the movie
> would move every one of them. `CMakeLists.txt` pins it for every test next to
> `MELEE_RNG_SEED`, and the four shell harnesses set it themselves so they
> still work run by hand. Empty and `0` read as unset, which is how
> `frontend_opening.sh` clears the pin for the run that is about the movie --
> that run boots exactly as the shipped game does and still measures
> `bright=0.925`. ctest 33/33.

> **The Classic VS splash was shredding the arena, and that is the crash
> P-816/P-836/P-843 have all been reporting (2026-09-18, G-220).** The owner
> placed it exactly: the versus screen between Classic rounds, on the middle
> round only. That round is the **team battle** (`entry->x1 & 8` ->
> `model_scale_kind == 4`), the only Classic intro that calls `fn_80185A0C`
> and so the only one that does a `GX_TF_Z24X8` EFB copy. Both 64-byte-tile
> encoders in `gx_gl.c` built the first half of a tile as a half-word index
> and doubled it -- a 128-byte stride over a 64-byte tile -- so the copy
> reached `tiles * 128` where `GXGetTexBufferSize` promised `tiles * 64`. The
> splash's 380x400 depth copy **overran its 608,000-byte buffer by 607,904
> bytes**, up to three times, laying far-plane `0xFF, 0xFF` 32 bytes in every
> 128 over ~600 KB of arena -- live `Fighter`s and gobjs in two pools at once,
> with `0xFFFFFFFF` in fields nothing assigns, which is what every one of the
> six reports has shown. ASan was always going to be silent: `HSD_MemAlloc` is
> `OSAllocFromHeap` on one host allocation, so there is no redzone between the
> image buffer and the fighter pools. Fixed, with a guard-band + round-trip
> regression test in `test_decomp_render --efb`; ctest 33/33, ASan clean.
> RGBA8 and Z24X8 EFB copies also *decode correctly* now for the first time --
> every tile but the first was landing on its neighbour.

> **The fighter crash dumps have been naming the wrong objects (2026-09-17,
> P-843, G-219).** `HSD_GOBJ_CLASS_FIGHTER` is **4** and nothing reserves the
> number -- the menus create their widgets with a bare
> `GObj_Create(4, 5, 0x80)`, while real fighters use p_link 8 -- so every
> `kind=?(-1) player=255 scale=(0.6,0.6,1)` line in P-816, P-836 and P-843 is
> a menu object printed through `Fighter*`. The "non-Fighter on the fighter
> GX link" three investigations chased was the dump's own doing. The
> discriminator is free and exact: `Fighter::gobj` is the struct's first word,
> which is the word `HSD_ObjFree` overwrites with the free-list link, so
> `fp->gobj == gobj` tells live fighter, freed fighter and menu widget apart
> with no false positives. **`MELEE_GOBJ_WATCH=<frames>`** is built on it and
> is armed in the windowed build: it reports a proc queued on
> `HSD_GObj_GObjProcHead[]` that its gobj no longer owns, and a fighter proc
> on a dead `Fighter`, on the frame it happens rather than minutes later.
> **Also fixed, on the screen all five reports crash on:** `ftdemo.c`'s
> `initFighter` left `plAllocInfo.x5` uninitialised, so every demo fighter's
> `x61C` was stack garbage -- measured as **0** where the match path always
> sets **-1** -- and `ftData_800859A8` uses it as an unbounded index into a
> six-element array *and writes through it*. The SIGSEGV itself is not yet
> proven; the watch is how the next report gets past guesswork.

> **Kirby's copy hats are converted for the first time (2026-09-17, P-842).**
> `KirbyHatStruct` has **two on-disc layouts** and the converter knew one, so
> the five hats the `LOAD_HAT` macro loads -- Donkey Kong, Jigglypuff, Mewtwo,
> Falco and Mr. Game & Watch -- were skipped whole by a guard that was right
> about the layout it knew and silent about the one it was not. Swallowing any
> of the five panicked in `dobj.c:312` on a big-endian `rendermode`, the G-199
> signature. `hat_dynamics[]` was also never walked *for any hat*: every
> copied special's `Article`, every hat's `ftDynamics` and Marth's, Roy's, the
> Ice Climbers' and Yoshi's extra models were unconverted too. The slot table
> is now named per archive from the decompilation, and **no slot in any of the
> 25 archives is unaccounted for**. Converter **v135**; coverage 82.85% ->
> 83.13%; new `check_kirby_hats` asserts the panic's own condition -- it
> reports `rendermode 0x3c000000 would panic DObjLoad` on the old converter.
> Two words are deliberately left big-endian: G&W's `hat_dynamics[4] + 4/+8`
> are `GXColor`s copied verbatim, whatever the source's type name says (G-218).

> **Mods are a real layer now, and widescreen is the first thing built on it
> (2026-09-17, ADR-0026/0027, P-831).** `mods/unbound/unbound.wasm` is a
> 1.3 KB WebAssembly module that the desktop build loads out of `mods/` under
> WAMR, and it holds no API a stranger's mod could not reach. `MELEE_NO_MODS=1`
> loads nothing and returns the port to vanilla, which makes "the core is still
> faithful" a configuration the suite can run rather than a claim.
>
> **Two things landed, and the first matters on its own.** `apply_viewport`
> scaled the EFB onto the window by `gl_width/640` and `gl_height/480`
> *independently*, so **every window that was not 4:3 stretched the picture** --
> the port has been anamorphic on a 16:9 monitor for its whole life and nobody
> had a name for it. `gx_gl.c` now fits a rect of `disp_aspect` (4:3 by
> default) inside the window, centres it, and routes every EFB->window mapping
> through it: viewport, scissor, EFB-copy source, raster sizes, fog width, the
> P-698 probe rect and the display filter's scanline step. Measured on a
> 1920x1080 capture: **240 px of black bar each side, exactly
> `(1920 - 1080*4/3)/2`**, and zero with widescreen on.
>
> **Widescreen is presentation-only, and the mechanism is what makes that
> true.** `native/mod/mod_cobj.c` applies the mod's aspect, runs the real
> `HSD_CObjSetCurrent`, and restores the camera unconditionally, so the object
> the engine reads on the next line is byte-for-byte the one it wrote.
> `cm/camera.c` fits the shot from its **static descriptor**, not the live
> camera, so the camera moves exactly where it would have on a 4:3 screen and
> the extra width is extra view. At a 4:3 window the mod is a no-op: mod-on vs
> mod-off differ by 13,103 bytes against a **10,710-byte run-to-run noise
> floor** for two identical vanilla runs.
>
> **The hook mechanism was already in the tree.** `decomp_shim.h` has renamed
> `HSD_ArchiveParse` since S3; that is a complete link-time interposition
> facility -- free when unused, no dynamic loader, works in the browser. The
> mod system named and curated it rather than inventing one.
>
> **Not built, and deliberately:** asset/data overrides (P-832), a second
> non-Unbound mod to find what the ABI is missing (P-833, and it will find
> plenty -- there is no way to read game state at all yet), HUD anchoring
> (P-834), WAMR AOT (P-835). ctest 33/33.
>
> **The main menu has a sixth entry (2026-09-17, P-838).** "Melee Unbound"
> sits under "Data" with the game's own pill, cursor, highlight and lighting,
> and opens an Unbound credits page. Owner-checked in-game.
>
> **The menu was already built for it.** `mn_803EAE68` has **ten** anchor
> joints, every loop over options is bounded by
> `mn_803EB6B0[kind].selection_count`, and that table is a writable global --
> so the entry is one assignment, not a patch, and joint 9 positioned itself
> with no transform override. The label is generated at 176x30 IA4 by
> `mods/unbound/tools/make_label.py` and substituted after the anim runs,
> because the retail swap table is packed and frame 10 still resolves to
> "Data".
>
> **One patch was unavoidable and is worth knowing about.** `mn_8022BFBC`'s
> switch covers selections 0..4 with no default and `fn_8022C128`
> dereferences the result every frame, so selection 5 segfaulted. **The shim
> cannot reach it** -- mnmain.c both defines and calls it, and a compiler
> resolves an intra-TU reference before any rename or linker wrap sees it.
> `mn_8022DB10` had the identical problem and needed no patch, because the
> table's `think` pointer is writable. That is the dividing line between what
> the shim can hook and what it cannot.
>
> **Three bugs found on the way were ours, not the game's:** the HUD applies
> one colour per batch, so every `hud_set_color` after the first was silently
> discarded and the viewer's own overlay has been single-coloured all along;
> drawing a backdrop out of text costs a quad per glyph pixel and exhausted
> the 65,536-vertex budget, dropping everything drawn after it; and **neither**
> `gm_GetButtonsTriggered(4)` **nor** `HSD_PadCopyStatus[].trigger` reports
> Start, which two reasoned fixes failed to find and one trace settled in a
> minute.
>
> ABI is at 7 -- the credits pulled a per-frame hook, overlay text, filled
> rects and pad input into existence, which is the ABI growing from a real
> feature rather than from guesswork. **Still in the port's font rather than
> Melee's (P-839)**, and that task now records the exact route.

> **Widescreen is gated to gameplay (2026-09-17, P-831, owner decision).**
> Menus, splashes, results and cutscenes keep their authored 4:3 aspect and
> get honest pillarbox bars; a match gets the wider view. Verified across the
> real frontend flow: scene 0 (title) and 1 (menu) stay at 1.3333, and the
> aspect flips to 1.7778 the moment scene 2 (`GS_VS`) is entered.
>
> **This is not a workaround, it is the correct behaviour**, and an owner
> screenshot is what established that. A menu is a composition authored for
> 4:3 -- there is no world behind it, so widening can only reveal the edge of
> the picture. Its backdrop plates end at x=104/1812 on a 1920-wide capture,
> against x=110/1808 predicted for an asset carrying the usual ~18% overscan
> margin, so at 16:9 they fall ~6% short a side.
>
> **The community's `ssbmws.xdelta` says how that is really fixed, and it is
> not a cleverer transform.** Parsed rather than applied: ~13 KB of literal
> bytes changed across `main.dol` and roughly 200 archives -- trophies,
> stages, `SdVsCam`, `IfAll`, `GmTtAll`, the menu archives, `GmRst*`,
> `GmPause`. It **rewrites the data**. 13 KB is far too little for new
> artwork, which corrects something said earlier in this work: the plates are
> flat, so widening one costs nothing artistically. We have the same lever at
> load time through `melee_port_HSD_ArchiveParse`, with nothing shipped --
> P-837.
>
> Three further defects fixed on the way, all found from owner screenshots
> rather than from tests: the resize was told to mods before the frame was
> submitted (so any large aspect jump stretched a frame), ortho cameras were
> widened about zero rather than their own centre (sliding asymmetric ones
> sideways), and the widened camera was restored before `HSD_CObjEraseScreen`
> read it (G-215), which drew every backdrop at exactly the old 4:3
> rectangle. Cameras that do not cover the whole EFB -- the off-screen-player
> magnifier renders into its bubble's own viewport -- are now left alone.
> ABI is at 3. ctest 33/33.

> **Owner-reported (Hyprland): fullscreening from a small window stretched the
> frame; from an already-fullscreen-sized one it did not.** `match_present` is
> the *present* hook -- the game has already built the frame's draw list, and
> its cameras were widened with the factor the display mod held at the time.
> Telling the mod about a resize before the submit updated the widening factor
> and the presentation aspect together, but only the aspect could still affect
> that frame, so geometry built for the old aspect was fitted to the new one
> and came out stretched by the ratio between them -- which is why the size of
> the jump decided how bad it looked. The dispatch now happens after
> `gx_gl_render_frame`, so cameras and rect always come from the same
> generation; `gx_gl_set_size` still runs immediately, because the GL target
> has to match the real surface. `MELEE_WIDESCREEN_TRACE=1` prints the
> drawable, the authored aspect, the fitted rect and the camera factor on
> every change, plus SDL's logical-vs-pixel size, which is what separates a
> compositor that has not reported the new size from a fractional scale we are
> mishandling. **Unconfirmed against the owner's report**, which described the
> stretch as persisting rather than lasting one frame.

> **Two things the owner has to look at.** Whether 4:3 or **73:60** is the right
> `disp_aspect` default -- `1.2173333` in `camera.c:88` is literally
> `584.32/480` -- and whether the widened frame actually looks right, which no
> amount of pixel arithmetic here settled.

Last updated: 2026-09-17 (mod system landed -- ADR-0026/0027, drop-in wasm mods under WAMR, Unbound is the first one and widescreen its first feature, P-831 in review, P-832..P-835 and P-837 open; earlier: S6 complete and owner-checked; S8/Aurora dropped by owner; parity program landed; P-695..P-707, P-709, P-710, P-712, P-713, P-714, P-716, P-718, P-719, P-720, P-737, P-738, P-739, P-742 and P-743 fixed, P-699/P-702/P-711/P-715/P-717/P-740/P-741 open)

> **The browser port is playable: it boots from a local disc, reaches the
> memory-card screen, title and character select, and plays a match
> (2026-09-16, P-808/P-801, branch `wasm`).** Owner-confirmed on Firefox with
> his own `.ciso`. Three defects stood between the title screen and a match,
> and each was latent on every target rather than browser-specific:
>
> - **A DMA destination that x86 was aligning by luck.** `hsd_SynthSFXLoadBuf`
>   is a `static u32[8]` handed to `HSD_DevComRequest`, which asserts
>   `dest % 32 == 0`; the console got that from MEM1 section alignment and the
>   declaration never said it. `ATTRIBUTE_ALIGN(32)`, here and on
>   `lbl_804C4540` (G-196).
> - **130 function-pointer casts** (`(GObj_RenderFunc) (Event) fn`) that PPC
>   and x86 ignore and wasm traps on. `EMULATE_FUNCTION_POINTER_CASTS` is now a
>   recorded decision rather than an uncommitted local hack; three casts in
>   `synth.c` that would have passed *garbage* got real adapters (G-195,
>   ADR-0022 second amendment).
> - **The browser had never been optimised.** `wasm_census.sh` began as a
>   compile census and passed no `-O` flag, so every browser build was `-O0`
>   (G-197). With `-O2` plus a uniform-upload cache that **skips 94.8% of
>   ~24,000 GL calls a frame** (G-198, P-801), the binary went 21.2 -> 9.2 MB
>   and desktop render 8.56 -> 6.15 ms.
>
> **It is playable, not finished.** One stage, two characters, one session, one
> browser. The desktop matrix still fails 59 of 754 fighter x stage runs and
> the browser runs the same game code, so those are all present there too;
> nobody has soaked the browser build (P-805). CP932 is untouched, audio is
> choppy (P-804), and mobile/Safari remain the realistic failures for the
> 2.25 GiB reservation.

> **Renumbered at the merge into master (2026-09-16).** This branch's
> P-796..P-800 and G-190..G-195 became **P-806..P-810** and **G-193..G-198**
> because mainline already had unrelated rows with those numbers. Rows,
> comments and commit messages written before the merge use the originals.

> **The browser build stopped decoding subaction commands wrongly
> (2026-09-16, P-806, branch `wasm`).** `CMD_BE` was GCC's
> `scalar_storage_order("big-endian")`, which reproduces MWCC's MSB-first
> bit-field allocation **and** the big-endian storage order -- on GCC. Clang
> ignores the attribute in silence and the port's `-w` hid the warning, so the
> WebAssembly build read a real "play sound" word (`0x44000000`) as **opcode 4
> instead of 17** while booting, rendering, playing audio and holding 60 fps.
> Everything downstream of the command interpreter was running other people's
> instructions.
>
> Fixed by stating the layout instead of asking for it: the 76 command structs
> (plus `gmScriptEventDefault` and three `ColorOverlay_x8_t` groups) are
> generated host-order declarations in
> `native/decomp/shim/decomp_cmd_bits.h`, read through `CMD_U()` / `CO_X8()`,
> which byte-swap one word at the point of the read. The **data stays raw** --
> it must, because a script carries relocated host pointers inline
> (`Command_05`, `Command_07`), so the "swap the stream at load" route in the
> W0 note corrupts every jump. `PORT_BF_BE`'s two groups sit in one byte and
> are simply padded and reversed. No `scalar_storage_order` is left in the
> tree.
>
> **Desktop behaviour is bit-identical**, proved by 924,000 differential field
> reads against the attribute version and re-proved after the first attempt
> regressed `decomp_match` -- the read-site sweep had grepped for member names
> rather than for the pointer and missed six sites plus one compiled C table
> (`itsamusgrapple.c`) that was written *positionally*. `ctest bit_order` now
> walks all 231 fields on both compilers. G-193, ADR-0022 amendment.
> Three cross-TU signature mismatches `wasm-ld` found are fixed too (P-807);
> the browser link is clean. The GameCube build was run for the first time in this tree (`orig/GALE01/sys/main.dol` had never been populated): **`main.dol` is byte-identical to retail**, and it caught a regression the token-stream argument could not.

> **Programme status, 2026-09-16.** Matrix **59 of 754 (7.8%)**, ten distinct,
> from 240/780 when the fighter x stage sweep first ran. Descriptor coverage
> **79.50%** (166,368 of 209,261), from 73.60% at the P-756 baseline -- P-758's
> head item, the `ftData->x1C` part-animation joint arrays, is in. ctest 32/32,
> GameCube 100.00% matched. **32,430 descriptors short of the 95% gate**, and
> the console diff (P-760) has not started.
>
> **Worth noting the two axes moved independently again:** P-758's head item
> raised coverage 2.6 points and changed the matrix count by nothing. Every
> crash fixed this session was found by the soak, not by coverage.
>
> **Matrix status, 2026-09-16 (earlier): 58 of 754 runs fail (7.7%), eleven
> distinct.**
> Fixed since the 81 reading: **P-769** (`conv_joint` skipped the JObj union
> for SPLINE joints, so the `HSD_Spline` stayed big-endian and its NaN went
> into the collision mesh) and **P-777** (`grMc_CarState` overlaid
> `grMc_8049F440` and reached `grMc_8049F4B8` by assumed adjacency, clobbering
> `Ground_804D6950` with the float 0.1f). Mute City's 26 failures are gone
> entirely. Largest remaining: Dream Land's `HSD_ObjAlloc` assert (26),
> `HSD_JObjAddAnim` on Venom/Corneria (11), position sanity (8).
>
> **Earlier reading, for the trend: 81 of 754 on 2026-09-15, nine
> distinct.** It was 240/780 when the fighter x stage sweep first ran. Fixed
> since: P-767 and P-774 (cross-symbol overlays, G-176 class), P-764 (the
> `vis_table` walk running off the end) and P-765 (G&W's fifth
> part-visibility group). Withdrawn as harness error: P-766, P-768 and the
> `St_Kind_Dummy` row -- the sweep was taking its stage axis from the `StKind`
> enum instead of the game's own stage-select table. Remaining: `mplib.c:4804`
> on Mute City (26), `memory.c:55` on Dream Land (26), `HSD_JObjAddAnim` on
> Venom/Corneria (10), `lbvector.c:383` position sanity (8), and five smaller
> ones including two new Icicle Mountain hangs.
>
> **Note what this did *not* move: descriptor coverage.** P-764 cost 0.01
> points and P-765 cost none. The soak and the coverage ratchet find different
> bugs -- **neither P-764 nor P-765 would have been caught by reaching 95%
> coverage**, because in both cases the data *was* being walked, just wrongly.
> They are complementary instruments, not sequential ones.

> **The soak matrix, and ten bugs that were hiding behind Link vs Mario
> (2026-09-15, P-759, P-764..P-773).** `onEnterDebugVs` hardcodes Link vs
> Mario, so seeds varied the *stage* and never the fighters -- which is why
> 200 clean seeds coexisted with P-725 (Ness) and P-755 (Kirby) sitting open.
> `melee_decomp_boot` now takes `MELEE_MATCH_P0`/`P1`/`STAGE`, and
> `MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=all` sweeps 26x30 = 780 runs in
> ten minutes on eight cores. **240 of 780 fail, in ten distinct bugs.** They
> separate cleanly: `ftparts.c:793` on Roy/Pichu/Ganondorf (all three clones,
> no non-clone affected); `HSD_DObjSetFlags` segfaults on Fox/G&W/Kirby;
> Icetop hangs, Venom and Akaneia segfault, Mute City asserts in map collision
> and Dream Land in `HSD_ObjAlloc`, each for all 26 fighters; and three
> interactions. The selection had to wrap `gm_Mode_DebugVs_States[0].on_enter`
> -- writing `gmVsMelee_StartData` from the frame hook *looks* like it works
> and does not, because `onEnterDebugVs` and `gm_Scene_Vs_OnEnter` run in the
> same game frame.
>
> **Venom's static-data overlays are named now (2026-09-15, P-767, P-774,
> G-176).** Both batches were the same class: `grvenom.c` built pointers and
> read tables by offsetting from `grVe_803E5348` (0x38 bytes), which is
> console-correct only because the linker emitted `grVe_803E5380`,
> `grVe_StageCallbacks`, `grVe_StageData` and `grVe_803E5530` back to back.
> The port builds with `-fdata-sections`, so every such read was garbage.
> `PORT_PC` now names each symbol (full console layout and offset arithmetic
> in the P-774 TASKS row and
> `handoffs/2026-09-15-P-774-venom-tables-and-G176-sweep.md`). Venom's
> stage-22 sweep: 26/26 failing -> 7 (P-767) -> 6; the six are 4 arwing-laser
> article crashes (P-775, converter family, **not** symbol adjacency) and 2
> P-765. The class sweep (P-776) is incomplete.

> **The `Pl*` gap is one struct (2026-09-15, diagnosis for P-758).** The 34
> `PlXx.dat` character-data files are at **20.2%** and hold 22,271 of the
> 35,157 `Pl*` gap; the 207 costume files are at 75.9%. Inside them, **20,623
> of 22,271 unwalked descriptors are the same 0x14-byte struct, carrying 82%
> of the still-big-endian words**: `HSD_FObjDesc`, confirmed field by field
> against `fobj.h:53`. `conv_aobjdesc` already exists, so nothing new needs
> writing -- the reference chain is what is broken. `conv_ft_data`'s `x1C`
> part-animation walk converts only the leading two `u16` of each descriptor
> and never follows `entry+0x08`, which reaches the per-part animation arrays
> and the AObj/FObj chains behind them. New probes `MELEE_UNWALKED` and
> `MELEE_DUMP` are how this was found and are how the burn-down proceeds;
> `MELEE_UNWALKED` separates *cold* words (still big-endian) from ones a
> walker already converted, which showed the coverage metric is honest -- only
> 5% of unwalked descriptors are already correct.

> **The DWARF cross-check (2026-09-15, P-757, ADR-0024).** ctest
> `decomp_layout` reads `type -> size, field offsets` out of the debug info of
> an object built with the port's own layout flags, then asserts each opted-in
> walker in `hsd_convert.c` agrees: its `in_data` bound equals `sizeof`, every
> `conv_u32/conv_u16/rd32/rd16(c, off + N)` lands on a real field of matching
> width, and no **swapping** access lands on a pointer field -- on-disc
> pointers are relocation targets and are already host order, which is the
> P-746 shape of bug. 49 of 106 walkers and 232 offsets are cross-checked, on
> a ratchet that may only be raised; `--list-unannotated` prints the rest.
> Proven by injection: a wrong offset, a swapped pointer field, a wrong access
> width, a wrong size macro and a removed annotation each fail it.
> **Bit-fields are deliberately out of scope** -- the native build's DWARF is
> GCC's LSB-first allocation and the disc is MWCC's MSB-first, so the byte
> offset is right and the bit numbering is not; `check_unk_flag_bit_order` and
> the `PORT_BF_BE` sites own that. ADR-0024 has why the native DWARF is the
> right source rather than a `-g` GameCube build.

> **The soak harness (2026-09-15, P-759).** `native/tests/soak.sh` runs
> `melee_decomp_boot` over N seeds in parallel and prints
> `failure -> count -> example seeds`. Two traps it exists to avoid, both hit
> by hand first: **the process exits 0 even when an assertion fires** (`__assert`
> goes through `OSPanic` -> `boot_triage_stop`, and `boot_main` returns 0 from
> every path), so classification greps the log and the exit status is only a
> backstop; and **one bug looks like many** -- P-762 failed 7 of 40 seeds with
> one assertion. A 900-frame headless match is ~1.25 s on one core, so 40 seeds
> cost 10 s wall on eight. Seeds are derived from a base with a 32-bit
> xorshift, so `ctest decomp_soak` is reproducible like every other test --
> a regression test, not a lottery -- while `MELEE_SOAK_SEEDS=200
> MELEE_SOAK_SEED_BASE=random` is the discovery sweep. `MELEE_SOAK_SEED_LIST`
> pins the seven P-762 seeds into CI. It is the one test that deliberately
> overrides the blanket `MELEE_RNG_SEED=tick`; that is the point of it.
> **After P-762, 200 random seeds pass in 68 s** -- the first reading on
> ADR-0023's "zero crashes" gate. It samples stages and RNG streams (that is
> how Great Bay came up) but runs one scenario, the built-in 900-frame script
> with no renderer, so it says nothing about drawing or about long matches.
> The crash rate on that scenario went from 1 in 6 to under 1 in 200.

> **Great Bay crashed one match in six, and the stage's Articles were never
> converted (2026-09-15, P-762).** `Gr*.dat`'s `itemdata` names the stage's own
> `Article*`s; `ground.c:488` files each one in `it_804A0F60`, and the item
> spawn path then loads `article->x10_modelDesc->x0_joint` (`item.c:578`).
> `conv_itemdata` swapped the item kind beside each pointer but never followed
> the pointer, and nothing else in the archive points at those Articles -- so
> an entire model tree stayed big-endian. On Great Bay that is the Tingle
> balloon: an `HSD_PObjDesc` kept its `flags`/`n_display` `u16` pair swapped,
> `flags` read `0x01a0` instead of `0xa001` and lost `POBJ_ENVELOPE` (0x2000),
> and because `POBJ_SKIN` is `0 << 12` a PObj with **no** type bits *is* a
> skin -- so `HSD_PObjResolveRefs` took the skin branch, handed the
> envelope-array pointer to `HSD_IDGetData` as a joint ID, got NULL and
> asserted at `pobj.c:411`. Converter **v100**. Descriptor coverage
> **73.60% -> 76.88%** (6,866 descriptors) from that one walker, and the floor
> is ratcheted to match. The regression test is structural rather than a crash
> replay: a `POBJ_SKIN` stores an *ID* in its union, never a pointer, so a
> union field the relocation table names while the type bits say skin is the
> corruption itself -- it would catch this in any archive at any offset.
> Found by the P-759 soak, not by playing.

> **Projectiles work end to end (2026-09-15, P-739/P-742/P-743, G-178/G-179).**
> Four bugs stacked on one another, each invisible until the one above it was
> fixed, and each new symptom pointing at the wrong subsystem: the fighter
> attribute walk overran into the special-move command scripts (no script ran,
> no article spawned); the particle bank's `HSD_PSCmdList` headers were
> big-endian (one effect asked for ~4.3e8 particles and killed the heap);
> `psdisp.c`'s hand-inlined `GXWGFifo` stores wrote the hardware FIFO address
> (SIGSEGV the moment particles rendered); and `Article::x4_specialAttributes`
> was converted for food items only (the arrow launched at 2.67e23);
> and `UnkFlagStruct`'s bitfields were LSB-first, so `Item::xDAA_byte = 1`
> set b0 instead of b7 and `it_8026EECC` skipped every item model while its
> hitbox still worked (G-180 -- already found once and fixed in the wrong
> place, as a private `FtStatusFlags` copy for one Fighter field).
> `ctest decomp_projectile` drives the whole chain and asserts **damage**,
> with a `FAIL_REGULAR_EXPRESSION` for the panic.  GameCube build still
> 100.00% matched with the three new `PORT_PC` patches applied.

> **No character could fire a projectile (2026-09-15, P-739/G-178).**
> `conv_ft_data` byte-swapped 0x424 bytes from `ftData->x0`.  0x424 is the
> size of `fighter_dat_attrs_alloc_data`, the runtime *backup* block;
> `ftCo_DatAttrs` is 0x184.  The extra 0x2A0 bytes ran through `ftData->x4`
> and into the special-move **command scripts** that follow it, which are
> `CMD_BE` -- so every special's script decoded as opcode 0 and terminated on
> its first word.  No subaction event in any special move ran for any
> character; the animations still played because animation is separate data.
> Converter v91 clamps x0 to the struct size and converts x4 explicitly,
> bounded by the next offset anything points at -- which reproduces the
> decompilation's own struct sizes across all 32 fighter archives.
> `ctest decomp_projectile` is the regression.  **Owner-confirmed as far as
> it goes:** the scripts run and the specials make their sounds.  It then
> exposed two pre-existing bugs nothing had ever been able to reach, both
> confirmed against the pre-P-739 converter: the particle bank's
> `HSD_PSCmdList` descriptors are big-endian, so a generator asks for ~4e8
> particles and exhausts the heap in under a second (**P-742**, a hard
> crash), and articles spawn but draw nothing and deal no damage
> (**P-743**).

> **Pokemon Stadium ran on big-endian parameters (2026-09-15, P-738/G-177).**
> `GrPs.dat`/`GrPs3.dat`'s `yakumono_param` had no converter descriptor, so
> the jumbotron's display countdown started at a garbage negative, the state
> machine changed state every frame, and its state-7 branch returned **before**
> clearing the capture flag -- the 640x406 live feed was never captured once
> and the monitor sampled uninitialised `HSD_MemAlloc` heap.  That is the
> noise the owner reported.  The same block also carries the 3600/3800-frame
> transformation interval, the rise/fall timings and the transformation
> weights, so the whole stage was running on garbage.  Converter v90.  The
> monitor was **not** a renderer bug, which is what P-736 spent a pass
> assuming.  Owner-confirmed 2026-09-15, together with P-737 (VS against a
> CPU starts again).

> **VS matches start again (2026-09-15, P-737/G-176).**  Every VS match used
> to panic on the character-name splash with `file isn't exist S<garbage>.usd`
> (`lbfile.c:114`): `tyDisplay_8031C454` reads its matanim and archive-filename
> tables by offsetting 0xAC and 0x158 past the joint-name table, and with
> `-fdata-sections` that offset lands on a function pointer, so
> `lbArchive_LoadSymbols` was handed machine code as a filename.  Sixth
> instance of the P-721 cross-symbol-overlay class and the first fatal one;
> note it emits **no** `-Warray-bounds` warning, so the P-721 inventory built
> from warnings will never list it.  Still open from the same owner session:
> the Stadium monitor is noise (P-738 -- P-736 fixed the 250x160 *text*
> capture, not the 640x406 live feed) and special-move projectiles never
> spawn (P-739).

> **Direction (2026-09-13/14): ADR-0017 — keep the GLES renderer, reach Aurora parity.**
> The 32-bit product, in-place converter and GLES3/WebGL2 renderer stay; the
> S8 Aurora-dependency plan (ADR-0015/0016) is parked and the owner confirmed
> (2026-09-14) that it is **not needed**: the Aurora algorithm ports
> (P-671..P-682, gap matrix in `learnings/gx_coverage_matrix.md`) are the
> accepted renderer path.  No C++/Aurora dependency; MIT attribution in
> `native/licenses/aurora-MIT.txt`.  P-617/P-642 were superseded by P-672/P-676.

> Update this file whenever behavior changes. Keep it factual: what a fresh
> `git pull` + build does today.

## Direction (2026-09-11): pivot to a decompilation-based port

**ADR-0010 supersedes ADR-0001.** The project is moving from the hand-written
prototype described below to a full-game port that compiles `src/` itself and
replaces only the GameCube hardware under `native/` (OS, DVD/asset loading,
GX→graphics HLE, AX→audio, input). The decomp is essentially complete
(19,820/19,828 functions, 99.96%, match the retail DOL) and 834/1034 `src`
files already compile behind the P-301 shim — see
[`learnings/decomp_port.md`](learnings/decomp_port.md).

Until S2/S3 land, **this file describes the prototype**, which stays runnable
as the dev tool and the per-layer parity oracle. Hand-port engine work
(P-204 leftovers, P-205..P-210, P-302, P-411, P-412) is `parked` in
`TASKS.md`. **S0 passed (2026-09-11):** the compiled decomp's own
`HSD_ArchiveParse` and `HSD_JObjLoadJoint` run on a retail `PlMrNr.dat` and
reproduce the prototype's bind-pose world matrices bitwise (61/61 joints); see
`learnings/decomp_port.md` §6. The port builds 32-bit for compiled code
(ADR-0012); **S1 passed (2026-09-11):** `melee_decomp_boot` compiles the
decompilation's own `main()` (`src/melee/gm/gmmain.c:130`) and runs it behind
the OS/DVD/GX/VI platform layer to a controlled stop, with the triage log and
backend work list in `learnings/decomp_boot.md` and
`logs/2026-09-11-S1-boot-triage.md`. The next milestone is **S2**
(HSD runtime + GX HLE). No `src/` or `extern/` file was changed in S1.

**S2 passed (2026-09-12):** `test_decomp_render` loads a retail `PlMrNr.dat`
through the compiled `HSD_ArchiveParse`/`HSD_JObjLoadJoint`/`HSD_JObjDispAll`
path and renders it through the new GX HLE + GLES3 backend
(`native/decomp/gx/`, `native/decomp/hsd/`) with the compiled `ftData` part
visibility (16/59 DObjs hidden) and the prototype's camera/lights.  Screenshot
parity against the prototype viewer is RMSE 10.53/255 over the model region;
the residual is specular shading (explained in
`learnings/decomp_s2_gx_hle.md`).  The S1 boot target now runs the GX command
surface for real (`94 stub_calls / 33 unique`).  Still no `src/`/`extern/`
edits. **P-611 (2026-09-12):** `melee_decomp_viewer` presents the same
compiled HSD + GX HLE frame in an SDL3 window (orbit/zoom/model cycle, part
visibility, texture/light toggles, F12 screenshot); SDL3 is a new compiled-
target dependency (ADR-0014, install list in `TESTING.md`). **P-610
(2026-09-12):** Falcon's silver body was the P-607 `out_reg` fix; Giga Koopa's
limb noise was a `GX_TG_TEXCOORDn` coord chain folded onto the 0/1 UV varyings
(G-058); the draw state now captures 8 TEV stages (Master Hand uses 6, G-059)
and each frame restores the depth/color write masks before `glClear` so
camera orbits stop losing geometry (G-057).  **P-608 (2026-09-12):**
direct-mode draws (`GXBegin` + the inline `GXPosition*`/`GXColor*`/
`GXTexCoord*` writers used by `displayfunc.c`, `pobj.c`, `psdisp.c`, the
shadow/afterimage/effect code) are captured and decoded through the same
vertex path as display lists via the shadowing
`native/decomp/shim/dolphin/gx/GXVert.h`; `ctest decomp_gx_direct` is the
regression.  **P-612 (partial):** `GXSetScissor` (scaled from 640x480 EFB
pixels), `GXSetDstAlpha`, texture LOD bias/min-max LOD/anisotropy, and the
missing `HSD_VIData` render-mode init the scissor exposed (G-061) are in;
indirect/bump/toon and NBT binormal/tangent remain.  Next milestone:
**S3** (host-endian asset pipeline + DVD/ARQ).

**S3 passed (2026-09-12):** the platform now hosts the real disc/audio data
path and a generic host-endian converter.  `native/platform/dvd.c` mounts the
user's image, loads the FST into the GC boot-info page and runs the
decompilation's own `extern/dolphin` DVDFS; `native/platform/ar.c` plus the
compiled `arq.c` provide 16 MB of host ARAM with console offset semantics.
DVD/ARQ completions are delivered through `native/platform/complete.[ch]` when
interrupts are restored (the console's interrupt ordering, which DevCom/ARQ
depend on).  `native/decomp/assets/hsd_convert.c` converts archives in place:
the header/tables, every relocation target (authoritative pointer list), and
the numeric fields of joints, DObj/MObj/PObj/TObj, textures, animation trees,
RObs, FigaTrees and scene data/CObj/light/fog, with a content-hash + version
disk cache and a reloc-coverage desync check.  The boot passes the sound-bank
wait (real `.ssm` load over DVD/DevCom/ARQ), loads `NtMsgWin.dat`/`SdMsgBox.usd`
through the compiled loaders and reaches the memory-card/pad wait.
`ctest` is 7/7 including the new `decomp_assets` sweep: all 33 `Pl*Nr.dat`
plus `GrNBa`/`MnSlChr`/`IfAll`/`NtMsgWin` load through the compiled HSD path
with full relocation coverage.  ASan/UBSan is clean (the boot's DVD-cancel
stack-write bug and the `.ssm` overlapping copy were fixed; see G-063/G-064).

**S4 passed (2026-09-12):** the compiled game runs a deterministic headless
match.  `melee_decomp_boot --boot-match N` enters `GM_DEBUG_VS` (the game's own
Link/Mario match on Final Destination), and the harness sets stocks, installs
a frame-indexed scripted PAD source (`native/platform/pad_card.c`) and logs
player positions/flags; two runs at 600 frames are byte-identical and a
release vs ASan run reports the same position.  The compiled `ft`/`it`/`gr`/`gm`
code does the rest (engine physics, stage collision and lights, camera, HUD
stocks/nametags, respawns).  Converter v56 walks the remaining tables
(`grGroundParam`, `itemdata`, `map_plit`, `Stc_scemdls`/`_scene_models`,
`MapCollData.dynamic_*`, `FtPartsVis` model entries, `ftData` x24/x34/x38/x3C/
dynamics, `Ef*.dat` effect models, PlCo pData[16]/[20]); five `PORT_PC` patches
are listed in `learnings/decomp_port.md` (including two real host-only bugs:
the 0x10-byte card work area used as a 0x1510-byte `CardContext`, and a
use-after-free in the completion queue).  `ctest` is 12/12 including the new
600-frame `decomp_match` regression; ASan/UBSan 600-frame run is clean.  The
PAD backend and scripted input are the S4 input deliverable; the remaining
open items are P-616 (Falcon eyes), P-617 (indirect/toon) and P-621 (stage
colors), plus the boot/title scene re-entry noted in the handoff.

**P-623 (2026-09-12):** `melee_decomp_viewer --match` runs that match live:
the compiled `main()` runs inside the viewer process, a new VI present hook
(`boot_platform_set_present_hook`) renders the captured GX frame each game
frame, and the scripted PAD inputs loop (`pad_set_input_loop`) so the match
stays in action.  Without `--frames` it paces at 60 Hz and runs until
ESC/window close; `--frames N --shot F` captures a still; `--record FILE|-`
streams concatenated PPM (P6) frames for `ffmpeg -f image2pipe` (the 40 s
`/tmp/melee_match.mp4` was produced this way).  The S4 deterministic boot path
is unchanged (ctest 12/12, ASan 600-frame run byte-identical positions).
Follow-up fix: the "GO!" logo's black quad was the GX HLE asset table capping
at 8 archives, so later archives' CI textures hit a 64 KB fallback bound and
failed to decode; the table now grows on demand (G-088) and the logo renders.
A second fix bounds every texture decode by the GX-declared dimensions instead
of the containing archive (G-089), which removed the title screen's decode
spam and black logo rectangles.  Audio is S5.  See `gotchas/GOTCHAS.md` G-087
(viewer triage frame budget).

**S5 passed (2026-09-12):** the compiled game's whole AX stack runs on the
host.  `src/sysdolphin/baselib/axdriver.c` and the SDK's pure-C AX voice layer
(`extern/dolphin/src/dolphin/ax/{AX,AXAlloc,AXAux,AXSPB,AXVPB,AXCL}.c`) are
compiled against `native/audio/` (`ax_hle.c` is the `AXOut`/DSP/AI boundary,
`ax_mixer.c` is the software DSP: DSP-ADPCM, ratio SRC, `AXPBMIX` routing, VE
ramps, ITD, loop/end/current-address and state write-back, aux returns).  The
200 Hz AX clock is derived from VI (10 frames per 3 retraces).  All three
AXFX effects are ported to C (`native/decomp/axfx/axfx_port.c`): `reverb_std`
(used by the game) and `reverb_hi`/`chorus` (never registered by Melee, but
their asm callbacks are transcribed and unit-tested).  The mixer's ITD ramps
`shiftL/shiftR` toward the target once per 5 ms frame (P-638/G-135).
`platform/{ssm,sem,hps}.c` convert the three audio asset formats on the DVD
read path.  `melee_decomp_boot --audio-dump out.wav` writes a deterministic
32 kHz mix and logs an FNV-1a hash; the viewer opens an SDL3 32 kHz stream.
The 10 s scripted match renders character/match SFX and the boot/title HPS
music (peak -1.3 dBFS, no clipping), `ctest` is 14/14 including `decomp_audio`
(two runs byte-identical) and ASan/UBSan is clean.  Getting here fixed four
real bugs: DSP-ADPCM is 8-byte/14-sample frames with the scale in the low
nibble (the 9-byte/16-sample assumption was loud noise, G-097), the engine's
u16-pair address/ratio aliasing needs host helpers (G-098), `.sem`/`.hps`
needed endian conversion (G-099), and synchronous `.sem` loading needed the
idle tick to pump completions (G-100).  Owner listening check passed
2026-09-13 ("audio sound pretty fine").

The owner later reported a separate intermittent, very loud repeating SFX.
P-706 adds an opt-in `MELEE_SFX_DEBUG=<path>` recorder at the game-request,
synth-start, sample-wrap and final-PCM boundaries. It keeps normal playback
unchanged while logging one-second checkpoints and detailed burst snapshots
with fighter state, voice addresses and request call stacks. The owner's
capture showed a single Peach's Castle ambient voice (`0x53025`) looping for
the whole title-demo match and clipping the mix; `GrCs.dat`'s `yakumono_param`
public was never converted, so the intro countdown that stops the loop read
big-endian (P-707, converter v87, G-157). The owner confirmed the sound no
longer glitches; `TESTING.md` keeps the capture recipe.

**S6 in progress (2026-09-13): the retail frontend flow runs end to end.**
The product target is now `melee` (the compiled game's own frontend; the old
hand-port sandbox is `melee_prototype`).  A bare `./build/native/melee` runs the
retail flow with live keyboard input (Enter=START, Z=A, X=B, C=X, V=Y, A=L,
S=R, Q=Z, arrows=stick); `--frontend --input FILE` replays a deterministic
script and `--no-items` uses the game's own debug item switch.  Items are
enabled by default.  Verified with
screenshots through: memory-card prompt (no card) -> title (logo starts at
frame 400, no reveal card) -> main menu -> VS. Mode -> character select (Mario
+ CPU DK) -> stage select -> live match (Yoshi's Story, HUD/stocks/timer) ->
results.  `ctest decomp_frontend` drives the same flow headlessly to the match
start and asserts the scene transition.  Eight `PORT_PC` patches now cover the
decomp's static-data adjacency assumptions (camera tables, `ftMapping_list`,
the results `CameraKindData` block) and a main-menu stack overflow.  Converter
v65 converts the CSS/stage-select/results scene tables, `GmRst`
`pnlsce`/`flmsce`, and the food item special attributes.  The real SisLib text engine now
compiles (`hsd_3A76.c`/`hsd_3915.c`) with big-endian SIS buffer accessors;
the font atlases load from the raw DOL region at disc header `0x420`
(G-112, P-647) and dialog text renders.  Image-sequence textures
(`HSD_TexAnim` on nonzero texture maps) bind correctly since the
`TexAnim.id` conversion (converter v67, G-114, P-650): the title logo fire
cycles and the main-menu 1-P preview shows its submenu lines (P-649).
Menu BGM now sustains through the HPS page ring (P-648, G-115..G-117): the
header `loopFlag` survives the single-frame DevCom burst, the mixer's end test
is crossing-based so a page handoff does not re-wrap into a buzz, and the page
table's `AXPBADPCMLOOP` predictor contexts byte-swap so the seams are
sample-continuous.  Converter v68 derives each article's variable-length item
state array from the DAT layout instead of assuming eight entries; this stops
the walk from corrupting adjacent model descriptors, and all 40 common-item
models now load through `HSD_JObjLoadJoint` (P-643).  Converter v69 also
byte-swaps the `ftData->x1C` part-animation descriptors' `u16` first-part and
part-count fields (P-630/G-119).  Without it, landing animation commands turn
part `0x29` into `0x2900` and index beyond `Fighter.parts`; Mario and Link's
four serialized descriptors are covered by the asset regression.  Pokémon
Stadium's stage-animation bootstrap now dispatches its AObj callbacks through
their declared argument shapes (P-651/G-120), so Classic can initialize its
first stage.  Converter v70 bounds the `Fighter_WaitAnimData` walk to the last
record that carries a name pointer; the overrun used to rewrite Fox's
part-animation pointers and crashed or froze the first Classic landing
(P-652/G-121).  Attacks now apply damage: the `spawn_hitbox_skip` flag is bit 3
of command byte 0xF on the console, and the `PORT_PC` layout reads that bit
instead of bit 4, so hitboxes activate (P-653/G-122; `ctest decomp_hit` checks
the scripted opponent's percent).  CPU fighters now attack too: converter v86
walks `PlCo.dat` pData[22], the `Fighter_804D64FC` CPU database.  Its 1,159
`ftCo_AttackEntry` records and reach tables previously stayed big-endian, so
attack IDs became values like `0x02000000` and weights became denormals; CPU
fighters approached but selected no usable attack and repeatedly retried the
action path.  The title-demo regression now observes sane tables, attack-state
entries and real damage with no PAD input (P-705/G-156).  Item attributes now
read the console bits:
`ItemAttr`'s two flag bytes are MSB-first (`itIsHeavy`/`it_8026B30C`/
`itGetHoldKind` asm pins 0x80/0x78/0x07), so a `PORT_PC` ordering lands
GCC's LSB-first fields on those bits (P-654/G-123); `test_decomp_assets`
diffs all nine per-article fields against the raw bytes.  Converter v71 walks
`ftData->x40` (`itPickup`: twelve grab-offset floats that used to put the
pickup volume at the world origin) and `ftData->x4C_sfx` (`FtSFX` sound ids
plus three `FtSFXArr` count/id tables), so held-item offsets and per-character
SFX are host order (P-655/G-124).  Converter v72 also walks the per-fighter
`ftData->x48_items` special-`Article` arrays, so Ness/Peach/Game & Watch/Link
specials read host-order attributes (P-656/G-125).  Converter v73 fixes the
`tyModelFileTbl`/`tyModelFileUsTbl` dispatch (name lengths 15/17 for 14/16-char
symbols), so the results screen resolves the right trophy name/model per
character (P-645/G-126).  Converter v74 adds the credits `_modelset` and
Stadium `gmKumiteSystemTable*` walks found by an unknown-root scan over every
disc archive (P-657/G-127).  Converter v75 hardens the walker bounds (32-bit
`in_data` overflow, unaligned writes, effect-descriptor overrun) and
`test_decomp_assets` now converts all 861 disc archives and checks every
relocation field against a raw copy (P-659/G-128).  The HUD stock icons are
correct again: `gm_80168B34`/`gm_80168BF8` relied on MWCC register leftovers
(uninitialized `base`, missing `return`), so GCC made every player request
atlas frame 0 = Captain Falcon; the `PORT_PC` fix plus `ctest decomp_icons`
is P-644/G-129.  Converter v76 converts Yoshi's Story's (`GrYt.dat`)
`yakumono_param` `YorsterParams`, so Lucky-Block head bumps no longer stop
the fighter in mid-air (P-661/G-130).  Converter v78 selects the
`yakumono_param` layout from the archive's own `Grd<Stage>*` publics and
converts GrCn/GrIz/GrKg/GrSt/GrVe/GrOt/GrI1 exactly (P-662/G-133; the packed
target-test layouts stay raw), and walks the `dynamicsdata_*` source
`DynamicsDesc` blocks in GrCs/GrRc, so Princess Peach's Castle enters the
match without draining the dynamics pool.  Converter v80 also covers the last
unwalked public roots: trophy/cutscene `SceneDesc`s (`standScene`,
`cut*CanimScene`), the Classic intro layout (`gmIntroEasyTable`), the 51 event
levels (`sqEventInitDataLevelTbl`, including the MWCC MSB-first evinit flags
repack), and documents `dbLoadCommonData` as pointer-only (P-658/G-134).
Memory-card save data works: the
console's card work-area symbols overlap inside `hsd_804D1138` (command ring
at +0x10, dispatch queue at +0x1210) and the host allocated them separately,
so the first file command was never dispatched; the `hsd_4D11.c` `PORT_PC`
patch now aliases them onto the base, cards are inserted by default
(`MELEE_NO_CARD=1` = empty slot), and `ctest decomp_frontend_card` creates
and reloads a save over one card directory (P-646/G-132).  The opening movie
is ported: a host THP decoder (`native/decomp/thp_dec.c`, from the Aurora
reference) replaces the MWCC-only SDK one, `lbmthp.c` swaps the big-endian
header/frame-size words, `GXInitTexObj` invalidates the decoded-texture cache
so the CPU-updated movie planes re-upload, and the title attract demo's
colanim opcode and `ftData->x54` endianness bugs are fixed.  `MELEE_OPENING=1`
cold-boots into `MvOpen.mth` (the skip-intro path stays the default);
`ctest decomp_opening` covers both the movie frame pixels and the attract
demo (P-685/G-136).

**P-686 (2026-09-14):** matches no longer stop immediately after the Ready
sequence.  Adding the THP decoder changed host link layout and exposed
`ftmaterial.c`'s DOL-only assumption that `ftMaterial_803C69D0` and
`ftMaterial_803C6A44` follow the declared `ftMObj` object in memory.  The
out-of-bounds cast produced a TExp type of 0 instead of `HSD_TE_CNST` (4), so
the first fighter material panicked in `HSD_TExpSetReg`; the viewer hid the
triage panic and appeared to exit normally.  The `PORT_PC` path now uses the
named templates.  Both `--match` and frontend VS mode share this fix, and the
deterministic match retains its exact frame-600 position (G-137).  The decomp
pin is updated from `7db32491c` to `40012f51f`; the affected `ft/types.h` and
`lb/types.h` patches were rebased, and native panic/assert signatures follow
upstream's const-correct declarations.  The existing `player.c` and
`gmclassic.c` adjacency patches now leave their original non-`PORT_PC` source
expressions intact, restoring the reference DOL checksum.

**P-689 (2026-09-14):** the respawn/rebirth platform renders.  `PlCo.dat`'s
`pData[8]` (`Fighter_804D6534`) is a `{joint, animation}` pair
(`ft_0D4D.c:139,148`) but the converter only walked pData 0/4/5/16/20, so the
joint's `1.0f` scales stayed big-endian and read back as ~4.6e-41 denormals —
the platform collapsed to a point.  Converter v82 walks both halves
(`conv_joint` + `conv_anim_joint`); `ctest decomp_assets` diffs the slot-8
joint flags/floats against the raw archive and keeps the entry platform
(slot 16, already converted) as the control.  Headless before/after frames of
the scripted match differ only in the rebirth windows (470–520, 580–600;
every other frame byte-identical) and frame 480 shows Link standing on the
restored platform (G-142).

**P-687 (2026-09-14):** three more silent non-Metrowerks divergences from
upstream #3456 are fixed behind `PORT_PC` fallbacks (ADR-0011 addendum).
Big Blue's `grBigBlue_801ECB50` compiled its five `rlwimi` state writes out,
so the cars' 6-bit state could never become 10 or 4; `fn_80166A8C` was an
empty non-void results-screen function, so its caller read uninitialised
`sp48_x` into `player_standings[i].xE`; `Runtime/runtime.c`'s
`__cvt_dbl_usll` compiled to a bare `ret` (training-mode speed).  `objdump`
shows all five Big Blue bit-inserts and the real conversion body; the
GameCube build still reports `build/GALE01/main.dol: OK` with the patches
applied (100.00% matched, 1130/1130 linked).  The Big Blue and results paths
are not exercised by a test today (G-144, needs a human/owner run for the
stage).

**P-709 (2026-09-14, cross-port corrections from `999sian/melee-pc`):** that
port pins the same upstream commit as we do (`40012f51f`), and a review of it
found two of our decisions wrong.  (a) The P-687 `fn_80166A8C` fallback stored
a 4-byte float, following upstream #3456's description of the `psq_st` as a
float store.  It is a **GQR3 quantized u16 store** — `init_spr_unk`
(`gmmain.c:107-120`) loads GQR2..GQR5 with `4/5/6/7` (U8/U16/S8/S16), and the
caller reads the halfword back with `*(u16*)&sp48_x` into
`MatchPlayerData.xE`, the joystick-activity score feeding the high-score
accumulator — so the results screen was reading float mantissa bits.  The
fallback now clamps to 0..65535 and stores two bytes; `objdump` of the
inlined copy in `gm_80166378` shows the `comiss` 0-clamp, the `$0xffff`
saturate and `mov %ax,0x66(%edi)`.  (b) `ftAnim_8006F3DC` fell off the end on
its not-found path (P-695 left it as "indeterminate in retail too"); it now
returns a defined `0.0f`, because both callers store the result into
`fp->cur_anim_frame`.  GC build still `main.dol: OK` (100.00% matched);
`ctest` 28/28.  The results path remains harness-unreachable (G-144), so `xE`
has no end-to-end assertion.  See G-158/G-159.

**P-710 (2026-09-14, the P-695 census's "unread" bucket was decided by the
wrong grep):** re-auditing our own triage against `999sian/melee-pc` — which
patched all 45 fall-off-the-end sites blindly — found the census had decided
"no caller reads the result" by grepping for `name(`, i.e. **direct calls
only**.  Nine of those 14 sites are never called directly at all: they live in
callback tables and the engine reads the result through a pointer (G-160).
Four are now fixed, all census outcome 1 with the retail `r3` read out of
`main.elf`.  The gameplay one is `itKyasarinegg_UnkMotion4_Anim`, the
`animated` predicate of the Chansey egg's motion-state-4 `ItemStateTable` row:
`Item_80269528` destroys the item when it returns true, so the port was
destroying eggs on a coin flip.  The other three are Sound Test menu rows,
whose result decides whether `un_80302E00` forwards the key to the parent
handler.  Two of those (`un_80300758`/`un_80300790`) look like they must return
0 — melee-pc patched them that way — but retail returns 4, because the `void`
callee `un_802FFCD0` never writes `r3` (G-161).  Five Sound Test sites remain,
each needing a per-path trace of a `switch`; tracked as P-711.  GC build still
`main.dol: OK` (100.00% matched); `ctest` 28/28.  Owner check queued as H-6.

**Checked and not applicable (2026-09-14, same review):** melee-pc's
`2957ebf` marks `Fighter_x2D0_t` `DISC_STRUCT` because Kirby/Purin alias
`fp->dat_attrs` through it and their access-time endian model byte-swapped only
the `ftCo_DatAttrs` view.  Our port converts the whole 0x424 `dat_attrs` blob
as dense `u32` at load (`hsd_convert.c:2540`), so both views see the same
converted bytes and the bug cannot occur here.  Its `6f363e7` (Venom arwing
`jobj` read uninitialised) predates its own decomp re-pin; our pin already has
`jobj = gobj->hsd_obj` at `grvenom.c:1145`.  The rest of melee-pc's crash fixes
are 64-bit pointer-width work (`b810dc7`, `4c7da38`, `93fc1c7`, `6a18a36`,
`393a4b5`), which ADR-0012 makes moot for our 32-bit targets.

**P-688 (2026-09-14):** host math now follows the console where upstream
diverges.  The MSL `sinf`/`cosf`/`tanf` tables and wrappers compile
(`src/MSL/trigf.c` + `math_data.c`; the one ADR-0011 MSL exception, ADR-0018,
owner decision) with `native/decomp/msl_port.c` supplying `fabsf__Ff` and
running `__sinit_trigf_c` from a constructor; the decompiled `atanf` body
compiles behind a `PORT_PC` fallback (`__fnmsubs = -fmaf(a, c, -b)`) instead
of silently linking glibc's (3.8% of 54,590,184 sampled inputs differed);
`__fabs` maps to `fabs` in the shim (G-143).  `ctest decomp_trig` proves the
atanf result bit-identical to an explicitly-rounded transcription; the
2400-frame frontend target changes on 0.17% of pixels (RMSE 0.20/255), all
deterministic tests pass and match game time is unchanged (1.81 ms vs
1.82 ms at frame 600).

**P-671/P-678/P-672 (2026-09-13):** the renderer-parity program (ADR-0017)
produced `learnings/gx_coverage_matrix.md` (the 100-function GX surface with
Aurora references and P-672..P-680 gap list).  `GXGetProjectionv` now returns
the SDK packed `{type,A..F}` form, fixing the particle-billboard axes
`psdisp.c` builds (P-678/G-131).  Indirect texturing now evaluates in the
fragment shader (Aurora `shader.cpp`/`GXBump.cpp`): Melee's refraction setup
(`lbrefract.c`, `ITF_8`/`ITB_ST`/`ITM_0`/`ITW_OFF`) offsets the sampled screen
copy, `GXSetTevDirect` clears the stage, `GX_TG_MTX3x4` keeps its q row and
divides (with the hardware q==0 clamp), texgen `normalize` runs between the
matrices, and `GX_TG_SRTG` toon coordinates sample the lit raster.  Mario's
cap/face reflection-map UVs change (19 pixels, RMSE 0.0009).  `ctest` 17/17,
ASan clean; see `learnings/gx_indirect_toon.md`.

**P-673 (2026-09-13):** light-object math now matches `GXLight.c`
(`GXInitLightDistAttn` MEDIUM/STEEP and the OFF guards; `GXInitLightSpot`
falls back to `GX_SP_OFF` for out-of-range cutoffs) and the `GX_AF_SPEC`
(enum value 0 — the SDK's specular default, easy to misread as "none")
channel accumulates `attn * light.color.rgb` with the hardware a/k
polynomial instead of a clamped grey average.  The character-select
screenshot is unchanged (its only light is white/infinite); regressions are
the `decomp_gx_direct` light cases and `decomp_efb` pass 6.  Learnings:
`gx_lighting_specular.md`; spot-light cones filed as P-681.

**P-674 (2026-09-13):** `GXCopyTex` now encodes every format the game uses
through one tiling table: I4/I8/IA4/IA8 with Aurora's ITU-R BT.601
`intensity()` + `quantize4()` (the old port copied red only), and RGB5A3
(result-screen portraits and menu snapshots, previously left untouched).
RGB565/R4/RGBA8 bytes are unchanged.  `ctest decomp_efb` pass 7 covers the
new formats with sensitivity flipped; Z24X8 depth snapshots and `GX_ZT_ADD`
are documented in `learnings/gx_efb_copy.md` (closed by P-682).

**P-677 (2026-09-13):** the parity harness is one command:
`native/tests/parity_matrix.sh [binary] [report]` renders 13 cases (7
characters, 4 stages including the toon stage `GrPu`, the `--direct`/`--efb`
fixtures) and writes a markdown pass/fail artifact with screenshot hashes.
`ctest decomp_parity` is the 18th test and skips cleanly without a disc.
Learning `gx_parity_harness.md`.  With P-677 closed, every row of
`learnings/gx_coverage_matrix.md` is EXACT, N/A or a documented deviation.

**P-676 (2026-09-13):** match-path batching: `gx_gl` remembers the last
applied draw state and skips the uniform/state/scissor calls when the next
draw's captured state is identical (19.4% of draws in a 900-frame match;
-2.0% total cycles, match frames 600/718 byte-identical).  Profiling notes,
the reverted `read_comp` experiment and remaining opportunities are in
`learnings/gx_match_perf.md`.  Benchmark with the `melee` product binary; the
`melee_decomp_viewer` target is stale.

**P-682 (2026-09-13):** `GXCopyTex(GX_TF_Z24X8)` depth snapshots work: the
EFB depth is blitted into a DEPTH_COMPONENT24 renderbuffer and read as
`GL_UNSIGNED_INT` (the EGL pbuffer itself cannot read depth), encoded into
the 64-byte 4x4 Z24X8 tile, and decoded back for the `GXSetZTexture`
sampler (`decode_z24x8`).  `GX_ZT_ADD` adds the incoming depth, `REPLACE`
ignores it, and the 24-bit bias applies to both.  `decomp_efb` pass 11
covers the snapshot bytes, ADD and bias; three sensitivity flips.  Learning
`gx_efb_copy.md`.

**P-681 (2026-09-13):** channel attenuation functions are evaluated per
channel: `GX_AF_SPOT` uses the hardware cosine polynomial
(`a.x+a.y·c+a.z·c²` over `k·(1,d,d²)` with `c = cos` against the negated
light travel axis), `GX_AF_NONE` = 1, and HSD's point lights (a=(1,0,0))
reduce to the existing distance falloff, so only `GrZebesRoute` cones
change.  The character-select screenshot is byte-identical; `decomp_efb`
pass 10 proves on/off-axis points.  Learning `gx_lighting_specular.md`.

**P-679 (2026-09-13):** fog now follows the hardware again: `GXSetFog`
stores the SDK's `A = f·n/((f−n)(e−s))`, `B = f/(f−n)`, `C = s/(e−s)` and
the shader evaluates `A/(B − z) − C` with all five `GX_FOG_*` families,
replacing the eye-distance linear/exp approximations.
`GXInitFogAdjTable`/`GXSetFogRangeAdj` are implemented (SDK 12-bit table +
Dolphin's per-pixel `sqrt(offset²+k²)/k` adjustment); the converter still
nulls `HSD_FogDesc.fogadjdesc`, so no retail scene reaches the range path
yet.  `decomp_gx_direct` checks the coefficients/table and `decomp_efb` pass
9 checks LIN/EXP2/range pixels; the character-select screenshot is
unchanged (its fog is far beyond the model).  Learning `gx_fog.md`.

**P-690 (2026-09-14):** P-679 fed the shader `gl_FragCoord.z`, but the GX TEU
evaluates fog on the viewport screen depth `far + z_ndc·(far−near)` (SDK
`GXProject`), which is `z_ndc + 1` for the usual `[0,1]` range — GL's
`(z_ndc+1)/2` made every retail fog half strength.  The shader now uses
`2·gl_FragCoord.z − u_depth_near` (`u_depth_near` = `GXSetViewport`'s
`nearz`), and `decomp_efb` pass 9 was rewritten so both sampled ends sit in
the fog ramp and the range-adjusted frame compares against the interpolated
SDK table `k`; reverting to `gl_FragCoord.z` fails all four pixels.  `ctest`
21/21.  Learning `gx_fog.md` (P-690 correction).

**P-691 (2026-09-14):** the title screen's background base is the game's dark
grey again.  HSD's screen erase draws a full-screen `GXSetZTexture(REPLACE)`
quad with `color_update` on; the port's dedicated Z-texture program wrote
only black (it was built for the depth-only shadow passes), so the erase
colour (38,38,38) never reached the EFB and every background layer composited
over black — the owner's "black bands" versus Dolphin.  The Z-texture program
now writes the vertex (raster) colour when the draw updates colour; the
title's `(320,60)` readback is `36,36,36` instead of `0,0,0`, `ctest` 21/21.
Gotcha G-138.

**P-703 (2026-09-14, missing fighter names):** the owner's Classic VS screen
showed both fighters but no names under them.  The name tables in
`gm_1601.c` are full-width literals stored in the source as **UTF-8**
(`Ｍ` = `EF BC AD`); the GameCube build pipes the source through **sjiswrap**
(`configure.py --sjiswrap`) so MWCC emits the console's Shift-JIS bytes
(`82 6C`), which is what `HSD_SisLib_803A67EC` looks up two bytes at a time.
Compiled as UTF-8 every lookup misses.  `melee_decomp_game` now builds with
`-fexec-charset=CP932` — GCC converts at codegen, after parsing, so the
backslash-as-trail-byte hazard sjiswrap exists to solve never arises, and
Shift-JIS being ASCII-compatible leaves every ordinary literal alone.  Use
**CP932, not SHIFT-JIS**: iconv's strict variant rejects characters the JP
name table uses and the build fails.  `ctest decomp_classic_names` checks the
first byte of `gm_80160980(0)` is a Shift-JIS lead byte (`sjis=1` with the
flag, `sjis=0` without) — a pixel probe is useless here because the broken
build renders *garbage kana* rather than nothing (927 vs 1054 white pixels).
`ctest` 27/27; the GameCube build is untouched (host compile flag).  Gotcha
G-150.

**P-704 (2026-09-14, Classic VS names):** P-703's encoding fix was necessary
but not sufficient.  `fn_80160DE8` (`gm_1601.c`) picks the US name width with
`lbl_803B75F8[ckind + 0x21]` (`+ 0x42`, `+ 0x63`), i.e. **past the end of the
33-entry table**.  Retail works only because the console linker placed
`lbl_803B767C`/`lbl_803B7700`/`lbl_803B7784` immediately after it; GCC's
`-fdata-sections` gives every array its own section, so the reads land in
padding and return 0.0.  A zero width makes `HSD_SisLib_803A7548` store an
x-scale of 0 (8.8 fixed, G-152), so every glyph drew at zero width — silent.
Only the US branch uses those offsets (the JP path reads in bounds), which is
why the JP harness rendered names while the owner's US save showed none.
Under `PORT_PC` the function now names the array each offset resolves to, as
`gm_80160B40`/`gm_80160C90` already did;
`patches/src/melee/gm/gm_1601.c.patch`.  `ctest decomp_intro_names` forces US
through the now-committed `MELEE_INTRO_US` harness switch and counts white in
the name row: **0 broken, 2457 fixed** — a non-black count does not flip, the
dark backdrop and the white "VS" logo keep it high either way.  `ctest` 28/28;
GameCube `ninja` 100.00% linked with the patch applied.  Gotchas G-154
(linker-adjacency reads) and G-155 (probe statistic must flip); the diagnostic
record stays in
[`handoffs/2026-09-14-P-704-classic-vs-names.md`](handoffs/2026-09-14-P-704-classic-vs-names.md).

**P-700 (2026-09-14, SIS text truncation):** the owner reported the 1P
character-select level reading `normar.a.`; headless it renders `VERY EASE..`
instead of `VERY EASY`.  The SIS text engine builds every string with
`vsnprintf(buffer, -1, fmt, args)` — `-1` means "unbounded" to the console's
MSL, but glibc documents sizes above `INT_MAX` as unsupported and writes one
byte fewer than asked (reproducible in ten lines, 32- and 64-bit alike).  The
engine's strings are Shift-JIS, two bytes per Latin letter, so the lost byte
truncates mid-character: `HSD_SisLib_803A67EC`'s SJIS lookup then finds
nothing for the orphaned lead byte, emits no glyph, and the renderer runs on
into the bytes that follow — the trailing `E..`.  All three call sites
(`hsd_3A64.c` x2, `textlib.c`) now pass `sizeof(buffer)` under `PORT_PC`;
every destination is a fixed local array.  `MELEE_CLASSIC_TEST` reaches the 1P
CSS and `ctest decomp_classic_text` probes the leading digit of the
right-aligned "TOTAL HIGH SCORE" value (**0 -> 159**) — a pixel probe on the
level text itself is useless because its box rescales to fit, so the broken
and correct strings occupy almost the same pixels.  `ctest` 26/26; `ninja`
100.00% matched.  The empty name plate is a separate bug (P-702).  Gotcha
G-149; the wider "console MSL was laxer than glibc" sweep is in
`learnings/decomp_port.md`.

**P-701 (2026-09-14, Classic splash decorations):** the owner's Dolphin
comparison showed the Classic "STAGE n" splash missing its row of stage-marker
models (only the thin chain between them survived, reading as a bare zigzag on
black) and the big red "VS" reduced to a few dark streaks.  It looked like the
collapsed geometry of G-147, and it is not: `--dump-draws` shows every marker
draw present with correct screen extents.  They were rendering with colour
writes disabled.  `conv_scene_desc` walked `SceneDesc.fogs` (and `.cameras`,
`.lights`) as "stop at 0 or out of range", which is not enough — the word
after the last entry can be unrelated archive data that still looks like an
offset.  In GmIntEz.dat it landed on an `HSD_PEDesc` and byte-swapped its
first word, so `flags = 0x29` read back as `0`, and `HSD_SetupPEMode` fed
`pe->flags & 1` to `GXSetColorUpdate`.  All three walks now gate each slot on
the relocation table (converter version 84), which is also G-002-correct.
New `MELEE_INTRO_TEST` reaches `GS_INTRO_EASY` through `gm_Mode_Debug_States`
state 6 — the 1P menus are otherwise the only route — and `ctest
decomp_intro_markers` counts non-black pixels across the marker row
(**553 -> 14411**).  `ctest` 25/25; `ninja` still 100.00% matched.  The text
glitches on those screens are a separate bug (P-700).  Gotcha G-148.

**P-698 (2026-09-14, "STAGE CLEAR" banner):** with P-695 the owner could
finally see the 1P clear screen, and reported a black box over its top
quarter.  A Dolphin capture settled what the port could not: the black band is
the banner's backdrop and is correct — what was missing is the "STAGE CLEAR"
artwork on it.  That banner is a `POBJ_SHAPEANIM` mesh, and `drawShapeAnim`
blends morph targets on the CPU (`get_shape_vertex_xyz` and friends `memcpy`
the `GX_F32` case and cast the 16-bit cases natively) from pools the port
deliberately leaves big-endian for the GX display-list decoder, so every
component decoded as a denormal and the mesh collapsed to `ndc x[0,0]`.  The
swap now happens in those three readers under `PORT_PC`, mirroring the
big-endian index reads HSD already does by hand a few lines above.  Fixing it
in the converter instead is wrong and was tried: the pools are shared with
sibling PObjs the HLE decodes big-endian, and swapping them in place removed
the SPECIAL BONUS frame and the TIME REMAINING/DAMAGE fills.  New `ctest
decomp_clear_banner` counts non-black pixels inside the banner (**0 -> 30690**);
`ctest` 24/24; `ninja` still 100.00% matched.  This also retires the shape-set
half of B-8.  Gotcha G-147.

**P-696 (2026-09-14, magnifier 400 ms frames):** the owner reported the match
dropping to ~2.5 fps whenever any part of their fighter left the camera, and
recovering the instant it came back (`render=405ms` with `draws` barely moving).
`convert_roots` dispatches on the archive's public symbol name and silently
ignores names no rule claims, so IfAll.dat's `lupe` (off-screen magnifier),
`tdsce` (countdown digits) and `Stc_rarwmdls` (rotating arrows) kept
big-endian sub-graphs.  `ifMagnify_802FBBDC` hands the magnifier's
`HSD_ImageDesc` to `HSD_ImageDescCopyFromEFB`, which passes `width`/`height`/
`format` straight to `GXSetTexCopySrc`/`GXSetTexCopyDst`: the 64x64 RGB5A3
target became a 0x4000 x 0x4000 copy in format 0x05000000, and the GL
encoder spun 268M no-op iterations per frame.  The three roots now route to
`conv_dynamic_models` (converter version 83) and `efb_copy_tex` rejects any
copy larger than the 640x480 EFB with a one-line warning.  Worst-case render
over a 300-frame match: **414.22 ms -> 13.57 ms**.  `ctest decomp_assets`
gained `check_ifall_hud_modelsets`, which fails with the rule disabled
(`IfAll `lupe` joint flags=00000010 want=10000000`); `ctest` 23/23.
`MELEE_ROOT_TRACE=1` now lists roots nothing claims — the remaining ones are
tabulated in `learnings/decomp_assets.md`.  Gotcha G-146.

**P-695 (2026-09-14, match-end crash + missing-`return` census):** the owner
reported a hard segfault one frame after the "GAME!!" announcer at the end of
any 1P stage — `lb_800138D8` with `gobj == 0`, from `gmvs.c`'s `fn_8016D634`
-> `gmregclear.c`'s `fn_80180630`.  `lb_800138EC` (`lbspdisplay.c`) is
declared `HSD_GObj*` and has **no `return` statement**: MWCC leaves the blur
GObj in `r3` across `GObj_SetupGXLinkMax` -> `GObj_GXReorder` (neither writes
`r3`, checked in `main.elf`), GCC returns `NULL`, and the very next call
dereferences it.  `return gobj;` under `PORT_PC` fixes it and the 1P clear
overlay now builds and runs.

Because `melee_decomp_game` compiles `src/` with `-w`, the whole class was
invisible, so the tree was swept with `-Wreturn-type`: **45 sites**.  Each was
instrumented and the real flows re-run; only two ever execute their
fall-through (`lb_800138EC`, and `extern/.../axfx/delay.c`'s `AXFXDelayInit`,
which GCC happens to tail-call into `AXFXDelaySettings` so it still returns
the console's `1`).  Five more sites were patched where the retail DOL proves
the value (`un_803224DC`/`un_80322598` return `un_8032201C`'s result) or where
a consumer would dereference the garbage (`ftAnim_8006F994`, `lb_8000CDC0`,
`fn_8017A318`); the remaining 38 are indeterminate in retail too and were
deliberately left alone rather than reinterpreted.  New `ctest
decomp_gameover` (`MELEE_GAMEOVER_TEST=1`) drives a real
`OUTCOME_ELIMINATION` into the clear overlay and segfaults without the patch;
`ctest` is 23/23 and `ninja` in `decomp/` still reports 100.00% matched,
1130/1130 linked.  Gotcha G-145, census table in `learnings/decomp_port.md`.

**P-692..P-694 (2026-09-14, GX HLE audit):** an audit of the HLE against the
Aurora reference closed three more silent gaps: vertex colours now expand by
bit replication (the HLE still had the old round-by-scaling formula;
`RGBX8` ignores its X byte as the reference does), a texture-coordinate
texgen source feeds `(u, v, 1)` so `GX_TG_MTX3x4` q rows see z, and the TEV
raster channel maps `COLOR1/ALPHA1/COLOR1A1` to rast1 and `ZERO/NULL` to
black.  Each has a regression (RGB565/RGBX8 decode, an MTX3x4 q row with a z
coefficient, an ALPHA1/NULL channel pixel) that fails before the fix.  The
title frame is byte-identical to P-691 (RMSE 0) and `ctest` is 21/21.
Gotchas G-139/G-140/G-141.

**P-736 (2026-09-15, stage projection + Stadium monitor):** GX map/coord 3
was silently folded onto map/coord 0 in GL.  Castle and Stadium materials use
two ordinary maps plus two live fighter-shadow maps; the second shadow stage
therefore projected the material's roof/Pokéball texture beneath whichever
fighter owned it, producing the moving duplicate and intermittent darkening.
The HLE and shaders now carry and bind all eight GX maps/coords, preserve
projective STQ until per-fragment division, and evaluate POS/NRM texgens from
the raw vertex through the explicitly selected matrix.  The Stadium monitor's
independent corruption came from 250-pixel RGB565 EFB copies advancing rows by
`width/4` tiles instead of `ceil(width/4)`; all 4-pixel copy formats now use
the rounded stride.  `GXCopyTex(clear=true)` also resolves before clearing the
source rectangle with the latched GX clear registers and write masks.  Direct
and EFB regressions cover all four semantics; Castle attract and Stadium match
captures are visually clean.  Gotchas G-173..G-175.

**P-680 (2026-09-13):** `GX_LINES`/`GX_LINESTRIP`/`GX_POINTS` now render.
`GxHleDraw` carries topology runs (consecutive same-mode groups merge, so
triangle-only draws keep one run and are byte-identical: `decomp_render`
RMSE 0 vs P-675), `GXSetLineWidth`/`GXSetPointSize` drive `glLineWidth`/
`gl_PointSize`, and `GXEnableTexOffsets` stays a documented no-op.
`ctest decomp_gx_direct` asserts the run modes and `decomp_efb` pass 8
checks a line and a 5 px point pixel; learning `gx_primitive_runs.md`.

**P-675 (2026-09-13):** texture channels now expand by bit replication
(5/6-bit, and the 3-bit RGB5A3 alpha) in both the image and TLUT paths, and
`GXTexObj` keeps per-object state keyed by the caller's pointer, so
`GXGetTexObj*`/`GXLoadTexObj` read the object the game passed (sobjlib and
lbspdisplay read stored texobjs long after initialization).  Model-region
screenshot delta from the rounding fix is RMSE 0.00056.  `ctest` 17/17;
learning `gx_texture_parity.md`.

## TL;DR

A playable two-player sandbox runs natively on Linux, rendering real disc
assets. Characters decode with correct bind-pose skinning, the `right` matrix
that attaches PObjs on non-root joints, the game's own part visibility tables
(neutral face, hidden alternate expressions) and full GX texture support
including CI4/CI8 + TLUT. An interactive 3D viewer with orbit/zoom/wireframe,
part isolation and a hidden-part toggle inspects any `Pl*Nr.dat`. Fighters now
play their real `Pl<Char>AJ.dat` FigaTree clips: the viewer can play, pause,
scrub and cycle clips, and the sandbox switches Wait/Walk/Dash/Jump/Fall clips
per fighter. Animation runs through a literal port of the engine's FObj state
machine, joint transforms and envelope/shared/rigid skinning. Movement uses
real Mario attributes read from the disc. Rendering is OpenGL 3.3 core with
GLSL shaders written in an ES3/WebGL2-portable subset (ADR-0009); the
fixed-function/display-list path is gone. Materials follow the decomp's
`MObjMakeTExp`/`TObjMakeTExp` state (channel raster, TEV colormap/alphamap and
lightmap phases, alpha test, XLU blend) and every fighter is scaled by its
`ftData.model_scaling` like `Fighter_UpdateModelScale` (Bowser 0.69, Kirby
0.92, Mario 1.10).

## Verified working

| Capability | Evidence |
|---|---|
| Disc read (CISO) | `--inspect` finds and extracts `PlMrNr.dat` (473,522 bytes) |
| FST lookup | Finds files in the root and one level deep |
| HSD joint/DObj/PObj walk | Mario: 68 PObjs, 6328 triangles |
| Bind-pose envelope skinning | Rendered Mario is a coherent T-pose; bounds `[-8.31 -0.31 -2.97]..[8.32 15.63 3.94]` (includes the game's 1.10 model scale) |
| Model scaling | `Fighter_UpdateModelScale` from `ftData.model_scaling`: Bowser 0.69, Kirby 0.92, Mario 1.10, Luigi 1.25; Mr. Game & Watch's `x34_scale.z` (0.01) flattening |
| Texture filtering | Per-TObj `HSD_TexLODDesc` min/mag filters, CI downgrade, LOD bias in the shader, anisotropy when the driver supports it |
| `right` matrix | Link's sword/scabbard/shield sit on his back instead of the floor (bounds y-min rose from -6.14 to -0.01) |
| Part visibility | Mario hides 16 of 59 DObjs, Link 32 of 83; faces render in neutral pose |
| CI4/CI8 + TLUT | Mario eye atlas (190x190 CI8, palette RGB565) decodes; 32 textures total (incl. one TEX1 map) |
| PObj types | SKIN (shared two-slot and rigid) and SHAPEANIM handled; Kirby, Link, Falcon, Game & Watch colors correct |
| Vertex colours | GX colour enum (RGB565/RGB8/RGBX8/RGBA4/RGBA6/RGBA8) decoded; fixes desync on coloured meshes |
| Per-PObj culling | GX cull modes + clockwise front faces; Master Hand renders solid |
| Hidden joints | `JOBJ_HIDDEN` skipped; fixes Mario's cap emblem/face smear and cuts most of Game & Watch's extra pieces |
| Material z-mode | `RENDER_ZMODE_ALWAYS` / `RENDER_NO_ZUPDATE` honoured per batch |
| Texture matrix | `MakeTextureMtx` (`repeat_s/t`, scale, rotate, translate) applied per batch; Mario's mirrored cap "M" is complete |
| Mipmaps | `glGenerateMipmap` on upload; filtering per TObj as above |
| Renderer | OpenGL 3.3 core, GLSL 330 in an ES3 subset; per-batch VAO/VBOs, no display lists/immediate mode |
| Render parity | Bind/anim/back-view screenshots RMSE <= 3.2e-6 vs the pre-rewrite build; scripted 88/1,024,000 pixels (HUD alpha) |
| Visibility slots | `FtPartsVis` slot semantics documented; viewer `B` / `--vis-slot N` cycles them |
| GX display lists | Strips/triangles/quads decoded; clean opcode histogram (only 0x80/0x90/0x98) |
| Textures | 32 textures for Mario (CMPR + CI8 + TEX1), correct cap/overalls/face/eyes |
| Materials | `MObjMakeTExp`/`TObjMakeTExp` common path: material constant/RAS initial stage, colormap/alphamap, RENDER_DIFFUSE lit stage |
| GX channels | `HSD_SetupChannelMode` case 4 lighting (`mat_ambient*ambient + light*N·L`), unlit vertex-colour default |
| Scene lights | `HSD_LightDesc` sets from `MnSlChr` (`--dump-lights`): white infinite + 0.4 ambient; shader evaluates the GX channel from real light objects |
| Fog | `HSD_FogDesc` from the same scene (linear 500-1000) evaluated in the shader |
| Alpha/blend/Z | `HSD_SetupPEMode`: XLU blend factors, alpha->0 discard, custom `PEDesc`, per-material Z func/update |
| Multi-texture | TEX0+TEX1 decoded; second TObj colormap/alphamap + its `MakeTextureMtx` uniformly sampled |
| Cross-character | Fox 6658 tris/34 tex, Pikachu 4989/10, Young Link 7381/42 |
| Model enumeration | `--list-models` finds 33 `Pl*Nr.dat`; `--all-models` finds 273 |
| 3D viewer | Orbit/zoom, ground grid, wireframe, culling, auto-spin, screenshots |
| Part isolation | 68 batches for Mario; `--list-parts`, `[`/`]`, `V` modes |
| Mario attributes | accel .080, friction .060, run 1.500, gravity .095, terminal 1.70, air .045, jump 2.30, 2 jumps |
| Sandbox | Move, jump, shield, attack, damage, stocks, respawn, CPU, camera follow |
| FigaTree clips | `--list-clips` finds 195 clips for `PlMrNr.dat` (Wait1 50 frames) |
| FObj playback | `hsd/aobj.c` matches a literal `fobj.c` transcription on 5661 samples (worst 6.4e-7) |
| Animation viewer | `--view --animate --clip Wait1`; `A`, `,`/`.`, `Z`/`X`, `M`; HUD shows clip/frame |
| Match animation | Both fighters pose independently; `Wait1`/`WalkMiddle`/`Dash`/`JumpF`/`Fall` by movement |
| Headless verify | `SDL_VIDEODRIVER=offscreen ... --frames N --screenshot` works |
| Sanitizers | 600-frame scripted run clean under ASan+UBSan (leaks disabled) |
| Unit tests | `ctest --test-dir build/native` (hand matrix math + compiled `HSD_MtxSRT` bitwise parity) |
| Compiled decomp math | `HSD_MtxSRT` built verbatim from `src/sysdolphin/baselib/mtx.c` behind `native/decomp/shim/`; SDK mtx/vec pairs are Metrowerks asm and stay hand-ported (P-301, `learnings/decomp_shim.md`). Bind/animate/scripted BMPs byte-identical |
| Compiled boot skeleton (S1) | `melee_decomp_boot` runs the decomp's `main()` for 10 frames under the platform stubs, reaches the game's own loading wait, and stops on the frame budget with a deterministic triage log (`logs/2026-09-11-S1-boot-triage.md`); `ctest decomp_boot` is the regression |
| Compiled HSD + GX HLE (S2) | `test_decomp_render` loads `PlMrNr.dat` through the compiled HSD display path and the `native/decomp/gx/` backend, applies the compiled `ftData` part visibility (16/59 hidden) and `Fighter_UpdateModelScale`, and renders with GLES3; world bounds equal the prototype's exactly, screenshot RMSE 10.94/255 (HUD excluded). `ctest decomp_render` is the regression; `logs/2026-09-12-S2-render.md` is the evidence |
| Interactive compiled viewer (P-611) | `melee_decomp_viewer` (SDL3 window + EGL/GLES3 via `gx_gl_attach`) renders the compiled scene with drag orbit, wheel zoom, `N`/`P` model cycle, `[`/`]` + `V` part isolation, `B` slot, `V`/`shift+V` variant, `Y` show-hidden, `L` lights, `T` textures, `W` wireframe, HUD (`H`), `F12` screenshot; `--frames N --hidden --shot F` is the non-interactive smoke path and its BMP matches `test_decomp_render` to RMSE 0.000 | Camera viewport/scissor stay in EFB space and the window size is polled per frame (G-111).
| GX HLE backend (S2) | `native/decomp/gx/gx_hle.c`: real GX state + `GXCallDisplayList` decode (68 lists, zero desync), XF/channel/texgen evaluation (hardware specular attenuation), per-draw snapshots; `gx_gl.c` evaluates up to 8 captured TEV stages with full KONST selects and all 8 texture maps/coordinates (projective STQ retained through rasterization), with textures/TLUTs from `native/gx/texture.c` on an EGL/GLES3 pbuffer. It honours scissor/dst-alpha and applies per-TObj LOD bias/min-max LOD/anisotropy. GX REG0/1/2 correctly map after PREV (G-108), combined `COLOR0A0` channel controls mirror into the paired alpha slot so vertex-alpha gradients fade (G-109), and stage map matanim/shapeanim arrays convert (G-110). Live R4 shadow copies remain GPU-resident; permanent VAO layout and per-primitive vertex decode planning remove repeated work (P-631 follow-up, G-107/G-173). |
| Direct-mode capture (P-608) | `native/decomp/shim/dolphin/gx/GXVert.h` routes the decomp's inline FIFO writers to `GXPortWGFifo*`; `GXBegin` + 4-vertex quad through the shim decodes to exactly 2 triangles / 6 vertices with the source pos/color/UV in `ctest decomp_gx_direct` |
| Turn-stability (P-610) | Viewer static vs `--spin 360 --no-hud` RMSE 0.003/255 (Master Hand; residue is HSD lookat float rounding), frame1 vs frame240 and `--cycle 33` both RMSE 0.0 |
| Viewer model switch (G-071) | Switching models after frames have rendered keeps the compiled GX lighting: switched vs direct-load screenshots are pixel-identical (Mario->Mewtwo, Falcon, Kirby, Giga Koopa, Luigi pairs); the old alpha/colour channel-slot aliasing is covered by the channel block in `ctest decomp_gx_direct` |
| Hand translucency (G-072) | With lights on, Master Hand/Crazy Hand show their translucent wrist-forearm connector (raster alpha now comes from the paired `GX_ALPHA0/1` channel instead of 0); Mario/Kirby/Giga Koopa/Link screenshots byte-identical, `v->ras[3]` covered in `ctest decomp_gx_direct` |
| Disc/ARAM backends (S3) | `melee_decomp_boot` mounts the disc (FST 1212 entries), loads `audio/us/main.ssm` and the other boot banks through the compiled DVDFS + DevCom + ARQ, and passes `HSD_SynthSFXWaitForLoadCompletion`; the stop is now the memory-card/pad wait (`gm_Scene_MemCard_OnFrame`, input is S4) |
| Host-endian converter (S3) | `native/decomp/assets/hsd_convert.c`; every archive's relocation targets convert (`reloc_valid == reloc_total`), content-hash + version disk cache, `MELEE_ASSET_CACHE`/`MELEE_NO_ASSET_CACHE` controls |
| Asset sweep (S3) | `test_decomp_assets` (ctest `decomp_assets`): 33/33 `Pl*Nr.dat` + `GrNBa.dat` + `MnSlChr.dat` + `IfAll.dat` + `NtMsgWin.dat` parse, pose and convert with 0 desyncs |
| Common scene assets (S3) | `NtMsgWin.dat` `SceneDesc` camera/light/fog + CObj descriptors convert; the boot loads the scene camera and only then waits for pad input |
| Stage viewer (P-619) | `Gr*.dat` `map_head` converts (`UnkStageDat`/`UnkStageDat_x8_t`: per-map joint tree, camera, lights, fog, light anims, shape sets; converter v9); `hsd_scene_load_stage_all` loads every map as a Ground GObj (platform + background layers) like the game, `hsd_scene_load_stage` isolates one; `melee_decomp_viewer --stage GrNBa.dat --fighter PlMrNr.dat` renders the full stage with a posed fighter and `--stage-cam` through the stage's own camera; `M` model/stage, `N`/`P` cycle, `,`/`.` map, `F` fighter, `K` camera; all 71 `Gr*.dat` load headless and a 70-stage cycle runs clean; ctest `decomp_stage`/`decomp_stage_cam` |
| Sanitizers (S3) | 32-bit ASan/UBSan: `test_decomp_assets` PASS and `melee_decomp_boot` clean with 0 sanitizer errors; fixed a DVD-cancel write into a dead stack frame (G-063) and the `.ssm` overlapping copy (PORT_PC memmove, G-064) |
| Stage shadows (P-640/P-736) | Per-draw viewports, frame-boundary engine cache invalidation, and all eight GX texture units/coordinates: two fighter shadows render on multi-map stage materials without map 3 aliasing the roof/Pokéball texture, and silhouettes align with the fighters (G-102..G-104/G-173) |
| Shadow maps (P-639) | `GXCopyTex` handles `GX_CTF_R4` (`shadow.c`): the EFB red channel becomes a 4-bit tiled texture.  Final Destination's platform renders its texture (and fighter shadows) instead of a black band; `ctest decomp_efb` pass 3 |
| EFB capture + Z-texture (P-615/P-736) | `GXCopyTex` reads the EFB at its point in the command stream, re-encodes GX tiled formats with rounded tile-row strides, then applies `GXCopyTex(clear=true)` using the latched clear registers/write masks. `GXSetZTexture(REPLACE/ADD)` runs a dedicated depth program (`gl_FragDepth` is ignored inside the big TEV shader on Mesa, G-068). Direct-mode quads flush at every draw-affecting setter (G-069); `ctest decomp_efb` covers partial-width RGB565 and clear semantics (G-174/G-175). |
| Bump texgen (P-612) | `GX_VA_NBT` keeps binormal/tangent; `GX_TG_BUMP0..7` implements the hardware emboss formula; Giga Koopa renders correctly (green/orange, previously magenta).  Indirect state is captured (P-617 evaluates it in S4).  `--direct` covers both |
| Owner visual checks | 180 Hz viewer animation speed confirmed correct; face texture artifact gone (2026-09-11) |
| Live match viewer (P-623) | `melee_decomp_viewer --match` runs the compiled game in-process (Link vs Mario, Final Destination): Ready countdown, both fighters walking/jumping, KO + `SCORE -1`, camera pan/zoom, respawn platforms; looping PAD script at 60 Hz until ESC; `--record -` piped to ffmpeg produces a 40 s H.264 of the same run |
| AX stack (S5) | The decomp's `axdriver.c` + SDK AX layer drive `native/audio/ax_mixer.c`: DSP-ADPCM 8-byte/14-sample frames, SRC, `AXPBMIX`, VE, ITD, loop/end/current write-back; `ctest audio` (synthetic fixture) and `decomp_audio` (two 300-frame matches byte-identical) pass |
| Audio assets (S5) | `.ssm` banks, `smash2.sem` command table and `.hps` streams convert in `platform/{ssm,sem,hps}.c`; boot/title HPS pages advance and loop; the scripted match plays character SFX throughout; ASan/UBSan clean |
| HPS stream ring (P-648) | The menu BGM sustains across all three ARAM ring slots: `ax_collapse_addr_sync` preserves the header `loopFlag` when the synchronous DevCom burst coalesces `AXSetVoiceAddr` with the address-field sync bits, the mixer's end test is crossing-based (a page handoff whose loop target sits above the lagging `endAddress` no longer re-wraps into a ~2.3 kHz buzz), and the page table's `AXPBADPCMLOOP` predictor contexts byte-swap so seams are sample-continuous (G-115..G-117).  Before/after energy probe: silence from ~13 s vs music through 22 s, zero buzz windows; `ctest` 15/15 |
| Audio output (S5) | `melee_decomp_boot --audio-dump out.wav` writes deterministic 32 kHz s16 stereo (peak -1.3 dBFS, no clipping) and logs `audio: frames=N hash=...`; `melee_decomp_viewer --match` plays through an SDL3 audio stream |
| Reverb (S5) | `reverb_std`'s asm `HandleReverb` transcribed to C in `native/decomp/axfx/axfx_port.c`; registered by `lbAudioAx_8002838C` as aux A |
| Match fighters visible (P-625) | The compiled fighters render fully textured in `--match`: `fighter.c`'s `x21FC_flag.byte = 1` sets the MWCC `b7` bit only via the `FtStatusFlags` PORT_PC union (G-091), and the GL texture cache evicts LRU instead of returning black when full (G-092).  `--dump-draws FRAME` lists a captured frame's draws/textures/NDC bounds |
| Fighter animations loop (P-626) | Walk/run cycles wrap instead of freezing at the clip end: `conv_waitanim_flags` now bit-reverses the top byte of `x10_animCurrFlags` into the low byte, so `x594_b1_loop` reads the console bit and `ftAnim_8006EBE8` sets `AOBJ_LOOP` (converter v57, G-093) |
| GPU channel evaluation (P-628) | The GX channel/specular lighting now runs in the GL vertex shader (uniforms for 4 channels + 8 lights) instead of per-vertex C: `ctest decomp_render`/`decomp_gx_direct` pass, match-frame RMSE <= 3.4/255 vs the CPU path, spikes 8.5/s -> 3.4/s and worst frame 68 ms -> 26 ms |
| Match pacing (P-626) | Interactive `--match` no longer fights vsync (it skips the manual 60 Hz delay when the swap already blocked) and re-anchors instead of burst-catching-up after a slow frame; `[match] frame N draws=... render=Xms` reports the per-frame render cost |
| Host FP fidelity (P-629) | `native/decomp/shim/placeholder.h` corrects the upstream host fallback from `sqrt(x)` to reciprocal square root for `__frsqrte`; every 32-bit decomp target uses SSE2 scalar FP so `float`/`double` expressions do not retain x87 80-bit intermediates. Sampled game work from frames 600–1200 is 1.4–6 ms instead of the old sustained ~29 ms plateau on poisoned NaN matrices. This did **not** by itself fix Link's legs (see P-627) |
| Link leg IK conversion (P-627) | `native/decomp/assets/hsd_convert.c` v58 now byte-swaps `ftData_x58_t`'s three f32 leg lengths (`x4`, `xC`, `x18`). On disc they are big-endian; the raw words read as `-490 / -1e27 / 7.7e35`, so `ft_80089B08` fed degenerate targets to `lbBgFlash_80021410`, whose `acos` outputs went NaN and poisoned Link's leg JObj matrices (parts 6–10, 12–16). Frame 720 now renders both legs, `--dump-draws 720` has zero non-finite NDC bounds, and frames 600–1200 stay under 10 ms game time |

## Known issues / gaps

Ordered by impact.

> Items 1–3 are prototype gaps superseded by the compiled engine (ADR-0010,
> S2/S4) and are parked. Item 4 (TEV) rolls into GX HLE; item 6 (character
> data) into compiled `ftData`; item 5 is the port plan itself. They stay
> listed because the prototype remains the fallback oracle until parity lands.

1. **Animation fidelity gaps.** Clips play and skin correctly, but
   `SETBYTE`/`SETFLOAT` channels (expressions, blinking, `ftParts_80074B0C`),
   IK joint resolution (`resolveIKJoint1/2`), material animation
   (`matanim`), shape sets and animation blending are not ported. Playback
   rate is fixed at the engine default 1.0 instead of per-action
   `frame_speed_mul`. See P-207..P-210.
2. **Animated expressions not implemented.** The neutral pose is correct, but
   blinking/damage expressions need the `SETBYTE` callbacks from item 1.
3. **Game & Watch residual slivers.** After honouring hidden joints he is
   recognisable, but a few thin edge-on pieces remain (x=0, y 13.6..21.9) that
   in-game are hidden through animation/joint state the port does not evaluate
   yet. P-201/P-412.
4. **TEV mostly ported.** The compiled path evaluates the captured GX TEV
   state generically (up to 8 stages, swap tables, full KONST selects,
   alpha test, scissor/dst-alpha and per-TObj LOD), the channel-1 specular
   uses the hardware attenuation function, `GX_TG_BUMPn` emboss is faithful
   (Giga Koopa fixed) and the Z-texture/EFB effects are in (P-615).
   **Indirect texturing and toon ramps landed in P-672** (indirect fragment
   evaluation, `GXSetTevDirect`, projective MTX3x4 + normalize, SRTG lit
   raster) — see `learnings/gx_indirect_toon.md`.  The remaining renderer
   gaps are tracked in `learnings/gx_coverage_matrix.md`: lighting/specular
   math (P-673), EFB copy formats and Z-texture edges (P-674), texture
   expansion/texobj state (P-675), fog math+range adj (P-679), lines/points
   (P-680), perf (P-676) and harness breadth (P-677).  Stage light lists
   (`src/melee/gr/*`) still supersede the viewer's stand-in lights at S4.
   See `learnings/hsd_tev_materials.md` and `decomp_s2_gx_hle.md`.
5. **Audio landed in S5; menus/items/results/netplay/WASM are not there yet.**
In-match and boot/title audio play; `AXFXReverbHi`/`AXFXChorus` are ported
but never registered by Melee, and the mixer's ITD ramps per 5 ms frame
(P-638).  The menu BGM page ring is fixed (P-648) and the owner confirmed
the game's music is "pretty much perfect" (2026-09-14), which closes the
stage-BGM/results audio flag; netplay/WASM are S7.
6. **Captain Falcon's eyes do not render** in the compiled path (P-616); the
   rest of the head now matches the prototype.  See TASKS.md.
7. **Non-Mario physics values** are demo defaults, not per-character data.
8. **Title state fixed (P-624).**  The skip-intro boot enters `GM_TITLE`
   with `gm_804D67EC == 0`, which now takes the retail branch: the logo
   starts at frame 400 and loops 400..1600 (`gmTitle_801A1630`).  The
   opening-movie transition still uses `fn_801A1498` with the movie frame
   count and reveals the logo as the movie ends.  `ctest decomp_title`
   asserts the title logo animation is in [400, 1600]; see G-090.
9. **Windows/macOS untested.** Linux + Mesa is the only verified target.

## Baseline commands

```sh
cmake -S native -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j4
./build/native/melee                          # the product: retail frontend
./build/native/melee --frontend --input native/tests/frontend_vs.txt \
    --no-items --frames 2400 --shot /tmp/fe.bmp   # headless frontend flow
ctest --test-dir build/native -R decomp_frontend  # the same, as a ctest
./build/native/melee_prototype --inspect      # prototype sandbox (dev tool)
./build/native/melee_prototype --list-models
./build/native/melee_prototype --list-clips
SDL_VIDEODRIVER=offscreen ./build/native/melee_prototype --view --frames 3 \
    --screenshot /tmp/viewer.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee_prototype --view --animate \
    --clip Wait1 --anim-frame 25 --frames 1 --screenshot /tmp/anim.bmp
SDL_VIDEODRIVER=offscreen ./build/native/melee --scripted --frames 240 \
    --screenshot /tmp/baseline.bmp
./build/native/melee_decomp_boot --boot-frames 10 --boot-timeout 10 \
    --boot-log /tmp/boot.log      # hangs in the memory-card/pad wait; the
                                 # watchdog gives a controlled SIGALRM stop
./build/native/test_decomp_assets   # 33 Pl*Nr + stage + common assets
SDL_VIDEODRIVER=offscreen ./build/native/melee --view --frames 1 --no-grid \
    --screenshot /tmp/viewer.bmp
./build/native/test_decomp_render --width 1280 --height 800 \
    --shot /tmp/compiled.bmp --dump
./build/native/test_decomp_render --direct   # direct-mode capture (P-608)
./build/native/test_decomp_render --no-gl   # asset bridge + GX capture only
./build/native/melee_decomp_viewer          # interactive (needs lib32-sdl3)
./build/native/melee_decomp_viewer --frames 1 --hidden --shot /tmp/v.bmp
./build/native/melee_decomp_viewer --match  # live match, 60 Hz, ESC quits
./build/native/melee_decomp_boot --boot-frames 600 --boot-timeout 90 \
    --boot-match 20 --audio-dump /tmp/melee.wav   # deterministic 32 kHz mix;
                                                  # logs frames=N hash=...
native/tests/audio_determinism.sh ./build/native/melee_decomp_boot /tmp/aud
./build/native/test_audio                      # disc-free mixer/ssm unit test
./build/native/melee_decomp_viewer --match --frames 2400 --record - 2>/dev/null \
    | ffmpeg -y -f image2pipe -framerate 60 -i - -c:v libx264 -crf 21 \
      -pix_fmt yuv420p /tmp/melee_match.mp4
```

Expected `--inspect` tail:

```
Decoded PlMrNr.dat: 6328 triangles, 32 textures; bounds [-8.31 -0.31 -2.97] to [8.32 15.63 3.94]
```

If those numbers move, say why in the commit and update this file.

## Environment assumptions

- Linux, GCC/Clang, CMake, pkg-config, SDL2 dev, Mesa (`libEGL_mesa`,
  `libGL`), OpenGL math. The renderer needs a GL 3.3 core driver.
- Compiled targets (32-bit): `lib32-gcc-libs`, `lib32-libglvnd`,
  `lib32-mesa` (EGL/GLESv2) and, for the viewer, `sdl3` headers +
  `lib32-sdl3` (ADR-0014). Exact Arch package list in `TESTING.md`.
- Disc image at `iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso` for
  default runs. `iso/` is locally excluded from git.
- `ACGC-PC-Port/` is a local, untracked reference checkout.
- The offscreen SDL driver + Mesa `radeonsi`/llvmpipe renders correctly (Mesa
  26.1.6 reports a 4.6 core context); there is no X11 server available to
  agents on this machine.

**P-712 (2026-09-14, strict aliasing vs. the decompilation's type puns):** the
`999sian/melee-pc` review's most transferable contribution was a *method*, not
a patch — a tree-wide `-Wuninitialized`/`-Wmaybe-uninitialized` compile, which
we had never run.  It reports three **definite** uninitialised reads
(`particle.c:539` `abs_z`, `gmresultplayer.c:609` `abs_stick_y`,
`ft_0892.c:44` `spC`), and none of them is a missing assignment: all three
clear a float's sign bit through an `int` lvalue, so the `f32` object is
written and the `s32` object GCC reads is not.  At `-O2` that entitles GCC to
drop the mask.  There are 72 punning sites tree-wide and `src/` is read-only,
so the fix is the flag, not the sites: ADR-0019 adds `-fno-strict-aliasing` to
every decomp-compiling target via `MELEE_DECOMP_UB_OPTIONS`.  It also makes
P-709's `*(u16*)` store/read pair safe rather than technically UB.  `ctest`
28/28; the GameCube build has no `src/` change to notice.  G-162.  The same
sweep leaves 181 `-Wmaybe-uninitialized` sites — a different class, genuinely
lost assignments, and the class melee-pc's Venom crash came from — triaged as
P-713.

**P-713 (2026-09-14, seven uninitialised reads):** triage of the 181
`-Wmaybe-uninitialized` sites ADR-0019's sweep left behind, cross-checked
against `999sian/melee-pc` `05919cf` — which triaged the same class with two
agents and confirmed the same seven.  All seven exist at our pin; each verdict
was re-derived here rather than transcribed, and one of theirs did not hold
(their `gmevent.c` fix cites sibling count loops our pin does not have, and
additionally removes a stale carry that is real retail behaviour rather than
undefined behaviour).  The reachable one is `ftCo_800AC5A0`: a CPU's DI/SDI
stick is read uninitialised whenever hitlag opens the window at ~0 knockback —
**every match with a CPU** — and retail is indeterminate there too
(`0x800ac6d0` reaches the `CpuCmd_SetLstickX` call with nothing having written
`r5`/`r30`), so the port now sends the neutral value the function's own
else-branch sends.  The rest: `HSD_GObjFree` through an uninitialised
`prev_link` on the Popo-string and yo-yo allocation-failure paths (three
sibling chain builders in the tree clear it; these two lost the assignment), a
wild float into `HSD_JObjReqAnimAll` on the Item Switch screen, an Event Match
CLEARED count decided by a stale register, and two latched-garbage values.  GC
build still `main.dol: OK` (100.00% matched); `ctest` 28/28.  Owner check
queued as H-7.  Full verdict table in `learnings/decomp_port.md`.

**Correction:** an earlier P-713 note in this session called the class
low-yield after three sampled sites turned out to be false positives.  That was
a bad generalisation from a sample of three; the real hit rate was seven, and
the pointer-ranked shortlist in `logs/` contained all of them.

**P-714 (2026-09-14, the layout contract was switched off twice over):** the
decompilation ships the console's layout contract inline — 189
`ASSERT_SIZE`/`ASSERT_OFFSET` declarations and a `#pragma pack(push, 1)` around
`TmData::x37[]` — all gated on `MUST_MATCH || LINT`, which the port defined
neither of; and `native/decomp/shim/Runtime/platform.h` `#undef`ed
`STATIC_ASSERT` on top of that, killing even the raw offset assertions outside
that gate.  So the port had no check at all that its structs match the console,
which is the bug class (`one object, many views`) behind several of
`999sian/melee-pc`'s crashes.  It was also hiding a live one: `sizeof(struct
TmData)` measured `0x5F8` against the console's `0x574` (`TmUnkMenuData` `0x14`
vs `0x12`), shifting every field after `x37[64]` in the live `gm_804771C4`
Tournament Mode global by 132 bytes.  ADR-0020 turns `LINT` on for all eight
decomp targets and deletes the shim's `#undef`; all 993 TUs pass, `ctest`
28/28.  The shim's stated reason was simply stale — it predates ADR-0012, which
made every compiled target 32-bit, where the documented PowerPC sizes are
correct.  melee-pc withdrew this same idea in `51965c2` for its 64-bit target,
correctly for them and not for us.  G-163.

**Cross-port review status (P-715):** 15 of the 63 `src/`-touching commits in
`999sian/melee-pc` have been reviewed.  The remaining 48 are listed and ranked
in P-715; the highest-value unread ones are its sign-extension and
bitfield-union sweeps, its missing-SFX diagnosis (bears on our open P-706), and
its white-quad/`GX_TEXMAP_NULL` TEV work.  **Treat that port as a source of
leads, not patches:** three of its conclusions have now turned out wrong for us
on re-derivation (`un_80300758`, `gmevent.c`, and the ASSERT_SIZE retraction),
each time because its 64-bit architecture changes the reasoning.

**P-716 (2026-09-14, the UB/bounds classes `-w` was hiding):** `src/` compiles
with `-w`, so the port only ever sees the warning classes it explicitly sweeps
for.  After the return-value (P-709/P-710) and uninitialised-read
(P-712/P-713) sweeps, this one covered the bounds and UB families.  Two sites
came back as `-Waggressive-loop-optimizations`, which is GCC saying it has
**proved** a loop runs out of bounds and is entitled to transform it:
`gm_1884.c:899` clears 27 entries of a 25-entry array (the two extra land on
the sibling `pad_6C[2]`, so nothing outside the struct is touched) and
`gm_1601.c:3251` reads one byte past `team_standings[5]`.

The second turned out to be more than UB.  That byte is
`player_standings[0].self_destructs`, a u16 of natively-stored runtime data at
`MatchEnd+0x62`: on the console's big-endian layout retail reads its **high**
half, so the branch needs 256+ self-destructs and effectively never fires,
while on a little-endian host the same address is the **low** half and it
fired on the first one — inflating `team_count`, which is added into
`is_big_loser`/`is_small_loser` on the results screen (G-164).  The aliasing is
now pinned with a `STATIC_ASSERT`, live since ADR-0020, and it earned its keep
immediately: offsets computed from the decomp's `/* 0xNN */` comments put the
fix on the wrong field, and the assertion caught it (`team_standings` is
commented `0x1B` but really sits at `0x1C`).

GC build still `main.dol: OK` (100.00% matched); `ctest` 28/28.  The rest of
the sweep is banked in `logs/` and triaged as P-717, with the classes already
judged (benign sequence points, a retail dead branch, and a `Mtx` prototype
artifact) written down so nobody redoes them.

**P-718 (2026-09-14, a second console-adjacency overlay):** the P-716 sweep's
`-Wstringop-overflow` reports led to `soundtest.c`, which reaches
`un_803FA258` by indexing off `un_803FA128` — a 304-byte array — at offsets up
to `0x227`.  The console linker placed the two symbols back to back and the
original code addresses both from one base; the port builds with
`-fdata-sections`, so GCC gives each object its own section and the write
lands 244 bytes past the array in whatever follows.  All five overlay users
touch only fields beyond `0x130`, i.e. every one of them is really reaching
the second symbol, and `un_802FFF2C` is the debug-menu match start
(`gmdebugmode.c:323`) that builds the entire `StartMeleeData` — rules, stage,
stocks, CPU kinds and levels — out of it.  `PORT_FA128_BASE` now rebases the
overlay onto the symbol that holds the fields, leaving every field access
unchanged, with `STATIC_ASSERT`s pinning the mapping.  The menu table at
`soundtest.c:2432` reaches the same field directly as `&un_803FA258.xF0`,
which confirms it independently.

This is the **second** instance of the class: G-154/P-704 was the same bug in
the four name-width tables `gm_1601.c` indexes past `lbl_803B75F8`.  Recorded
as G-166 with the tells to find the rest — console-linker adjacency is never
guaranteed here.

**P-719 (2026-09-14, the card work area's declaration disagreed with its
definition):** the same P-716 sweep reported 332 `-Warray-bounds` accesses
"outside array bounds of `u8[16]`" across `hsd_3A94.c` and `hsd_3B27.c`.  The
runtime layout was already right — P-646 defines `hsd_804D1138` as one
`0x1510` array and aliases `hsd_804D1148` (+0x10) and `hsd_804D2348` (+0x1210)
into it, because letting GCC allocate them separately splits the request queue
between its writers and the pump and deadlocks the first file command — but
`hsd_3A94.h` still declared the base as `u8[0x10]`.  Every card call site
therefore told GCC the object was sixteen bytes while the real accesses run to
`0x1510`, and object size is something the optimiser is entitled to act on.
Matching the declaration to the definition takes the count from 332 to 3.
`ctest` 28/28 including `decomp_frontend_card`; GC `main.dol: OK`.

The three that remain are a separate, real finding for P-717:
`state->file_sizes[file_idx]` at `hsd_3A94.c:2855`, `:3694` and `:4133`, where
GCC has proved `file_idx == 9` reaches an `int[9]` — that reads
`CardState::file_data[0]`, a pointer, as a file size.  In-struct, so wrong data
rather than corruption.

**P-720 (2026-09-14, a pointer read as a file size):** fixing P-719's
declaration let GCC prove three `state->file_sizes[file_idx]` accesses with
`file_idx == 9` on an `int[9]`.  Index 9 is not a mistake in the caller — the
file expects it (`fn_803AC6B8_blocks_before` opens `if (file_idx >= 9) return
0;`) — but the size reads are unguarded, so they land on
`CardState::file_data[0]`, a pointer, at offset `0x70`.  The console reads a
MEM1 address or NULL, which as a signed int is non-positive, so every
`file_sizes[...] <= 0` guard treats the file as empty and nothing happens.  A
32-bit host's pointer is usually below `0x80000000` and therefore positive, the
guard does not fire, and `fn_803AF3F0` computes a block count from an address
before running `for (i = 0; i < file_blocks; i++) block_map[i] = -1;` over a
64-entry **stack** array.  `port_file_size()` now returns 0 out of range — the
outcome retail reaches anyway — as an inline under `PORT_PC` and a macro
expanding to the original expression otherwise, so the GameCube build is
byte-identical.  G-167; this is the sign-of-a-pointer cousin of G-164.

**Cross-symbol overlays are now a named, recurring class (P-721 open).**  Four
instances have been fixed — the name-width tables (G-154/P-704), the card work
area (P-646, via `.set` symbol aliases), `soundtest.c` (G-166/P-718) and the
card declaration plus `file_sizes[9]` (P-719/P-720).  The pattern is always the
same: the console linker placed symbols back to back and the game addresses
several of them from one base, while `-fdata-sections` gives each its own
section here and lets the linker place them anywhere.

The P-716 sweep leaves 718 `-Warray-bounds` sites that are candidates for the
same thing, inventoried in `logs/2026-09-14-P721-overlay-candidates.txt`.  The
largest is `ty/toy.c`: `_Toy_804A26B8` is a 12-byte static, and the code casts
its address to `Toy26B8*` (0x196 bytes) reaching through two devtext buffers
into `Toy_804A284C[302]` — which `tylist.c` in turn overlays as `TyModeState*`.
**`ty/toy.c`, `gm/gmtoulib.c`, `gm/gm_1798.c` and `mn/mndiagram.c` are now done** (141 -> 5, 88 -> 80, 60 -> 0 and 49 -> 1).  The
trophy block got the `hsd_4D11.c` treatment — one object holding all five at
the console's offsets, `#define`s for the file-local names so the live
`ASSERT_SIZE`s still see real arrays, and `.set` aliases for the two names
`tylist.c` links against, verified in the linked binary.  `gmtoulib.c` was
sharper: `BracketData` overlays `lbl_80473AB8` (0x3700), `lbl_804771B8` (0xC)
and then `gm_804771C4` — the **TmData tournament state** whose layout P-714
had just fixed — and eight writes reached that third symbol through a base
`-fdata-sections` does not anchor.  They now name `gm_804771C4`.

**Triage note for the rest of the inventory:** most surviving
`-Warray-bounds` reports are *benign*.  GCC says "`X[0]` is partly outside
array bounds of `Y[n]`" whenever a struct is cast over a smaller array, even
when every actual member access is in bounds — 80 of `gmtoulib.c`'s 88 are
exactly that.  Only accesses that land **past the object** are defects.
**That one is deliberately not attempted yet:** the overlays interlock across
TUs and two of the four symbols are non-`static`, so it needs the `hsd_4D11.c`
alias treatment rather than a quick rebase, and getting it wrong would corrupt
the trophy gallery rather than fix it.

**Frame time, two texture-cache bugs (2026-09-16, P-813/P-814):** the owner
reported ~38 fps with a full house -- `game=13.8ms render=12.2ms draws=780
verts=145662` -- while two players held 60. Both causes were caches that had
stopped being caches, and neither had any symptom other than frame time.

`gx_hle_begin_frame` reset `frame_tcount` without clearing `frame_tex_hash`,
so the frame texture index kept every key its predecessors wrote. It stayed
*correct* -- `frame_tex_find` rejects `idx >= frame_tcount` -- while
degenerating into a scan, and because `GXLoadTexObj` only records a key when
the lookup returns an empty slot, insertions stopped happening as the table
filled. Separately, `GXInitTexObj` dropped the decoded GL texture for its
image on every call, and HSD re-inits a texobj every time it binds a material:
26,187 decodes over 300 frames, 18,198 of them CMPR art off the disc that
cannot have changed, against 166 LRU evictions. That one is now gated on
`gx_hle_image_is_asset()`, preserving P-685's opening-movie planes, which are
the writable buffers it was actually for.

Measured on a 900-frame headless match, pinned seed and core:
**66.04e9 -> 33.34e9 instructions, -49.5%**; two-player `render=` 4.0ms ->
1.0ms; decodes 26,187 -> 1,305. Captures are byte-identical against the old
behaviour, which `MELEE_GX_TEX_INVALIDATE=all` restores;
`MELEE_GX_TEX_STATS=1` reports hits/misses/evictions/decodes/invalidations.
ctest 33/33. **Not yet confirmed at four players** -- the match harness is
`MELEE_MATCH_P0`/`P1` only, so that measurement has to come from the owner.
G-200, G-201.

**Dotted/transparent stage backgrounds (2026-09-16 correction):** P-763's
display-copy filter and authored-mip work was valid parity work, but it did
not fix the reported Yoshi's Story/Yoshi's Island artifact. The shared cause
was in `rgb565()`: it initialized RGB but not alpha, while both the RGB565 and
CMPR decoders copied all four bytes. CMPR endpoint texels therefore received
stack-garbage alpha; the affected blended canopy, grass, wave and background
sprite textures appeared as sparse coloured dots. `rgb565()` now makes every
endpoint opaque and the CMPR transparent selector retains GX's interpolated
RGB while clearing alpha. Direct decoder tests pin both behaviors. Fresh
headless match captures on `GrSt` and `GrYt` show the solid artwork restored.

P-763's mipmap lead was secondary but real. Registered archive images now
decode their authored tiled mip levels instead of replacing them with
`glGenerateMipmap`; runtime buffers with unknown readable bounds retain the
safe generated fallback. `decomp_efb` pins both behaviors with alternating
scanlines (~128 after copy filtering) and a black base/white forced authored
LOD. Its display-copy and authored-mip regressions remain covered by
`decomp_efb` (G-189).

**Brinstar's acid (2026-09-16, P-796).** The bury descriptor Zebes keeps
immediately before its `yakumono_param` was converted five words deep, so
`element`, `sfx_severity` and `sfx_kind` stayed big-endian and the first
burial indexed a 42-entry sound table 400 million entries out. It converts
nine words now, which is what the archive's own layout says: the gap the
Zebes test keys on is `0x24`, the size of the nine-word `lbColl_80008D30_arg1`
view, not `DynamicsDesc`'s `0x14`. P-785 fixed this exact five-vs-nine split
in `conv_dynamics_desc` and missed the second copy of the walk.

**Crash reports now carry game state, not just a stack (P-796/P-797).**
`__assert`, `HSD_Panic` and the SIGSEGV handler all call
`match_boot_dump_fighters` through a dumper registered with `boot_triage`.
It prints every live fighter's kinematics -- `prev_pos` and `pos_delta` are
last frame's values, so an *integrated* runaway is distinguishable from a
*written* position without a second run -- and the camera bone's joint chain
from the bone to the root, marking the first level whose world translation is
out of range. The `lbvector.c` position-sanity family (P-772, P-781, and the
owner's `y` assert) has so far cost two hardware watchpoints per instance to
get from the backtrace to the guilty transform; this is that walk, printed.

**Particle command immediates were byte-reversed (2026-09-16, P-798).**
`psReadFloat` assembles each float in a particle script one byte at a time,
`bytes[0]` first, into a `union { f32; u8[4] }`. That byte is the most
significant one on PowerPC and the least significant one here, so every
immediate in every particle script -- size, position, velocity, gravity,
friction -- arrived reversed. A reversed size turned Bowser's fire breath into
a flat polygon across half the screen; the owner also saw it as "the right part
of the screen flashing white" on other characters' moves, which was the same
bug. The particle command bank is an opcode stream, so no converter can swap
the floats inside it without decoding every opcode; it has to be fixed at the
read, and it is, under `PORT_PC`.

Two coverage gaps closed with it. `MELEE_SPECIAL_TEST` holds B with the stick
neutral -- **no automated run had ever pressed B**, so no special move, and
with it most of the particle system, had been drawn outside the owner's own
play. `MELEE_BIG_PRIM` reports primitives whose view-space extent or NDC span
runs far past the viewport, filtered to direct-mode positions (models index
their positions through `GXSetArray`; effects and 2D submit them directly), so
it separates a wrong-geometry bug from a wrong-shading one without a second
run. That distinction is what a screenshot of a flat polygon cannot give.

**GX raster sizes are 1/6 pixel units (2026-09-16, P-799).**
`GXSetPointSize`/`GXSetLineWidth` take a `u8` where 6 is one pixel and 255 is
the 42.5 px maximum the register can express; `psdisp.c` names the unit at the
only place that sets it. The port passed the raw byte to `gl_PointSize` and
`glLineWidth`, so every point sprite was six times too wide in GX pixels and
wider again on a taller framebuffer. Fountain of Dreams' background twinkles
drew as ~10 px flat grey squares instead of 1-2 px points.

Worth carrying forward from how that one went: the shape of the artifact
argued for a texture-coordinate bug, `GXEnableTexOffsets` really is unimplemented,
and implementing it changed nothing. A census of primitive topologies is what
settled it -- **of ~7.1M point vertices in a 400-frame match, zero carry
`TEX0`** -- so no texcoord could be the problem and the size had to be. The
census cost one counter and replaced a day of plausible reasoning.

**Crash reports carry the faulting address and the running gobj
(2026-09-16, P-800).** Three gaps surfaced together in one owner report. The
P-796 fighter dump walks GX link 5 only, so a gobj mid-teardown -- exactly the
one that matters -- is invisible; `HSD_GObj_CurrentInvokedProcGObj` is the
engine's own record of what `HSD_GObj_RunProcs` was invoking and is printed
whether or not the link list still knows about it. Both signal handlers had
`si_addr` for free and discarded it; they take `SA_SIGINFO` now, and one
number separates a NULL base with a struct offset added, which names the
field, from a dangling or byte-swapped pointer. And the dump had nothing to
say about items, which is where that crash actually died, so every live item's
gobj, kind, entity and article are printed, with each fighter's `item_gobj`
alongside.

`MELEE_SPECIAL_TEST` cycles neutral/side/up/down. It pressed neutral only, and
the stick direction at the moment B goes down is what picks the move, so three
quarters of every character's specials -- and every article they spawn -- had
never run in an automated match.

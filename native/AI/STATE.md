# State of the port

Last updated: 2026-09-14 (S6 complete and owner-checked; S8/Aurora dropped by owner; parity program landed)

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
the scripted opponent's percent).  Item attributes now read the console bits:
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
| GX HLE backend (S2) | `native/decomp/gx/gx_hle.c`: real GX state + `GXCallDisplayList` decode (68 lists, zero desync), XF/channel/texgen evaluation (hardware specular attenuation), per-draw snapshots; `gx_gl.c` evaluates up to 8 captured TEV stages with full KONST selects and textures/TLUTs from `native/gx/texture.c` on an EGL/GLES3 pbuffer, honours scissor/dst-alpha and applies per-TObj LOD bias/min-max LOD/anisotropy. GX REG0/1/2 correctly map after PREV (G-108), combined `COLOR0A0` channel controls mirror into the paired alpha slot so vertex-alpha gradients fade (G-109), and stage map matanim/shapeanim arrays convert (G-110). Live R4 shadow copies remain GPU-resident; permanent VAO layout and per-primitive vertex decode planning remove repeated work (P-631 follow-up, G-107). |
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
| Stage shadows (P-640) | Per-draw viewports, frame-boundary engine cache invalidation and a third GL texture unit: two fighter shadows render on the stage, silhouettes align with the fighters (G-102..G-104); owner confirmed both shadows visible |
| Shadow maps (P-639) | `GXCopyTex` handles `GX_CTF_R4` (`shadow.c`): the EFB red channel becomes a 4-bit tiled texture.  Final Destination's platform renders its texture (and fighter shadows) instead of a black band; `ctest decomp_efb` pass 3 |
| EFB capture + Z-texture (P-615) | `GXCopyTex` reads the EFB back at its point in the command stream and re-encodes RGB565/RGBA8 tiled memory; `GXSetZTexture(REPLACE/ADD)` runs a dedicated depth-only program (`gl_FragDepth` is ignored inside the big TEV shader on Mesa, G-068).  Direct-mode quads now flush at every draw-affecting setter (G-069).  ctest `decomp_efb` |
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

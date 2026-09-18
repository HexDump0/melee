# Handoffs

A handoff is required whenever you stop with work in progress, discover
something the next agent must know, or finish a task that changes interfaces.

## Protocol

1. Copy `templates/handoff.md` to `handoffs/YYYY-MM-DD-<task-id>-<slug>.md`.
2. Fill every section. If a section does not apply, write "n/a" — do not delete
   the heading; the structure is what makes handoffs skimmable.
3. Update `TASKS.md` (status, files, blockers).
4. Commit the handoff together with the work (or as its own commit if you are
   stopping mid-task and the tree must stay buildable).
5. If the work stops mid-edit, make the tree build first. Never leave a broken
   build for the next agent without a `blocked` task row and a handoff.

## What makes a good handoff

- **Exact next action.** "Run X, expect Y, then do Z." Not "continue work".
- **State of the tree.** Commit hash, whether it builds, which tests fail.
- **Dead ends.** What you tried that did not work and why. This saves the most
  time.
- **Open questions.** Things only a human can answer, with the exact command to
  reproduce.
- **File claims.** Which files you touched so the next agent knows what to
  re-read.

## Index

| Date | Task | Agent | File |
|---|---|---|---|
| 2026-09-18 | **P-847 fixed: Master Hand's entry camera fed an uninitialised pitch into the camera position, and a fighter's on-screen test asserted at `lbvector.c:397`** | claude (opus-5 1M) | [`2026-09-18-P-847-boss-camera.md`](handoffs/2026-09-18-P-847-boss-camera.md) — G-205's fourth member; `MELEE_BOSSCAM` reproduces the eleventh Classic round on a VS match |
| 2026-09-18 | **P-847: an Unbound opening movie before Melee's own, played by the game's own MTH player** | claude (opus-5 1M) | [`2026-09-18-P-847-unbound-opening-movie.md`](handoffs/2026-09-18-P-847-unbound-opening-movie.md) — THP scan data is **not byte-stuffed** and the port's bit reader unescapes nothing; a stuffed JPEG decodes its flat-black first frame and returns -1 for every other, which `lbmthp.c` dereferences as a state pointer |
| 2026-09-18 | **P-845 fixed (Race to the Finish panicked on entry: `GrNPo.dat`'s `yakumono_param` was never converted) and P-846 fixed (Z-texture draws threw the TEV away, so the team-battle splash's sprites were black)** | claude (opus-5 1M) | [`2026-09-18-P-845-P-846-classic-rounds.md`](handoffs/2026-09-18-P-845-P-846-classic-rounds.md) — G-220, G-221; nine more stages still on the raw `yakumono_param` fallback |
| 2026-09-18 | **P-844: the opening movie is the default boot** — `OSGetResetCode` returns the cold-boot 0 instead of the console's reset-to-menu code | claude (opus-5 1M) | [`2026-09-18-P-844-opening-by-default.md`](handoffs/2026-09-18-P-844-opening-by-default.md) — `MELEE_NO_OPENING=1` is the opt-out and the whole suite takes it, because every harness counts frames from the boot |
| 2026-09-18 | **P-843 root-caused and fixed: the Classic team-battle VS splash's Z24X8 EFB copy overran its buffer by 607,904 bytes** | claude (opus-5 1M) | [`2026-09-18-P-843-efb-copy-overrun.md`](handoffs/2026-09-18-P-843-efb-copy-overrun.md) — one code path (`model_scale_kind == 4`) matched the owner's "only the middle round"; both 64-byte-tile encoders used a half-word index with a 128-byte stride. G-220 |
| 2026-09-16 | **Browser port is playable: gate zero, DMA alignment, 130 casts, the optimiser** | claude (opus-5, 1M) | [`2026-09-16-browser-playable.md`](handoffs/2026-09-16-browser-playable.md) — owner-confirmed boot to a match in Firefox; supersedes the two earlier browser notes; **read this first**, and note the P-806..P-810 / G-193..G-198 renumbering at the merge |
| 2026-09-16 | Session wind-up: P-796..P-800, three owner-reported bugs | claude (opus-5) | [`2026-09-16-P-800-windup.md`](handoffs/2026-09-16-P-800-windup.md) — Brinstar's acid, every particle float byte-reversed, GX raster sizes in 1/6 pixel units, and why the 754/754 matrix number should not be quoted as stability |
| 2026-09-16 | P-502 WASM/browser feasibility audit, pending review | codex (gpt-5) | [`2026-09-16-P-502-wasm-feasibility.md`](handoffs/2026-09-16-P-502-wasm-feasibility.md) — repo evidence, critical blockers, proposed W0-W5 gates, and exact independent-review checklist |
| 2026-09-16 | P-758 windup: burn-down + four crash fixes | claude (opus-5) | [`2026-09-16-P-758-windup.md`](handoffs/2026-09-16-P-758-windup.md) — coverage 76.87% -> 82.83%, P-778/P-771/P-770/P-755, and why four "biggest targets on the disc" were measurement artifacts |
| 2026-09-16 | P-758 head item landed; rest is orphan `HSD_AnimJoint` trees | claude (opus-5) | [`2026-09-16-P-758-head-item-and-the-orphan-animjoint-trees.md`](handoffs/2026-09-16-P-758-head-item-and-the-orphan-animjoint-trees.md) — coverage 76.87% -> 79.50%, the array bound a relocation check does not give, and why P-771 is not this chain |
| 2026-09-15 | Stability program (P-756..P-761) | claude (opus-5) | [`2026-09-15-stability-program.md`](handoffs/2026-09-15-stability-program.md) — three bug classes, the coverage denominator, and what is already settled |
| 2026-09-10 | P-107 bind-pose fix | follow-up | _this entry is the code itself; no separate note_ |
| 2026-09-10 | P-201 animation start | opencode (deepseek-flash) | [`2026-09-10-P-201-animation-start.md`](handoffs/2026-09-10-P-201-animation-start.md) |
| 2026-09-10 | P-201 animation landed | opencode (deepseek-flash) | [`2026-09-10-P-201-animation-done.md`](handoffs/2026-09-10-P-201-animation-done.md) |
| 2026-09-10 | P-211 renderer rewrite | opencode (deepseek-flash) | [`2026-09-10-P-211-renderer-core-profile.md`](handoffs/2026-09-10-P-211-renderer-core-profile.md) |
| 2026-09-10 | P-204 TEV materials (in progress) | opencode (deepseek-flash) | [`2026-09-10-P-204-tev-materials.md`](handoffs/2026-09-10-P-204-tev-materials.md) |
| 2026-09-11 | ADR-0010 architecture pivot | opencode (deepseek-flash) | [`2026-09-11-architecture-pivot.md`](handoffs/2026-09-11-architecture-pivot.md) |
| 2026-09-11 | P-301 shim experiment | opencode (deepseek-flash) | [`2026-09-11-P-301-shim-experiment.md`](handoffs/2026-09-11-P-301-shim-experiment.md) |
| 2026-09-11 | S0 complete | opencode (deepseek-flash) | [`2026-09-11-S0-complete.md`](handoffs/2026-09-11-S0-complete.md) |
| 2026-09-11 | S1 boot skeleton | opencode (deepseek-flash) | [`2026-09-11-S1-boot-skeleton.md`](handoffs/2026-09-11-S1-boot-skeleton.md) |
| 2026-09-12 | S2 compiled GX render | opencode (deepseek-flash) | [`2026-09-12-S2-compiled-gx-render.md`](handoffs/2026-09-12-S2-compiled-gx-render.md) |
| 2026-09-12 | P-611 compiled viewer | opencode (deepseek-flash) | [`2026-09-12-P-611-compiled-viewer.md`](handoffs/2026-09-12-P-611-compiled-viewer.md) |
| 2026-09-12 | P-608/P-610/P-612 renderer fixes | opencode (deepseek-flash) | [`2026-09-12-P-608-P-610-P-612.md`](handoffs/2026-09-12-P-608-P-610-P-612.md) |
| 2026-09-12 | P-609 S3 assets + DVD/ARQ | opencode (deepseek-flash) | [`2026-09-12-P-609-s3-assets.md`](handoffs/2026-09-12-P-609-s3-assets.md) |
| 2026-09-12 | P-615 + P-612 renderer work (P-616 open) | opencode (deepseek-flash) | [`2026-09-12-P-615-P-612-renderer.md`](handoffs/2026-09-12-P-615-P-612-renderer.md) |
| 2026-09-12 | P-618 GX channel slots + lit raster alpha | opencode (deepseek-flash) | [`2026-09-12-P-618-gx-channel-alpha.md`](handoffs/2026-09-12-P-618-gx-channel-alpha.md) |
| 2026-09-12 | P-620 S4 match runs frames; command-script bitfield repack next | opencode (deepseek-v4.1-flash) | [`2026-09-12-P-620-s4-scripts.md`](handoffs/2026-09-12-P-620-s4-scripts.md) |
| 2026-09-12 | P-620 S4 complete: deterministic scripted match + PAD backend | opencode (deepseek-v4.1-flash) | [`2026-09-12-P-620-s4-match.md`](handoffs/2026-09-12-P-620-s4-match.md) |
| 2026-09-12 | P-623 live match in the compiled viewer + PPM recording | opencode (deepseek-v4.1-flash) | [`2026-09-12-P-623-viewer-match.md`](handoffs/2026-09-12-P-623-viewer-match.md) |
| 2026-09-12 | P-625..P-628 fighters render/loop, GPU channel eval; P-629 slowdown open | opencode (deepseek-v4.1-flash) | [`2026-09-12-P-629-match-slowdown.md`](handoffs/2026-09-12-P-629-match-slowdown.md) |
| 2026-09-12 | P-629 host-FP slowdown fixed (pose claim corrected by P-627) | codex | [`2026-09-12-P-629-fixed.md`](handoffs/2026-09-12-P-629-fixed.md) |
| 2026-09-12 | P-627 fixed: byte-swap `ftData_x58_t` leg-IK lengths, converter v58 | codex | [`2026-09-12-P-627-ftdata-x58.md`](handoffs/2026-09-12-P-627-ftdata-x58.md) |
| 2026-09-12 | S5 audio stack + software mixer + asset formats | opencode (deepseek-v4.1-flash) | [`2026-09-12-S5-audio.md`](handoffs/2026-09-12-S5-audio.md) |
| 2026-09-13 | P-648 menu BGM stall (blocked note, resolved the same day: G-115..G-117, `logs/2026-09-13-P-648-hps-ring.md`) | Muse Spark / opencode (deepseek-v4.1-flash) | [`2026-09-13-P-648-menu-bgm.md`](handoffs/2026-09-13-P-648-menu-bgm.md) |
| 2026-09-14 | P-646 card pump deadlock fixed; P-662/P-658 converter work queued for the save-load match path | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-646-card-pump.md`](handoffs/2026-09-14-P-646-card-pump.md) |
| 2026-09-14 | P-662 stage `yakumono_param` layouts + `dynamicsdata_*` conversion (Castle crash) | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-662-stage-params.md`](handoffs/2026-09-14-P-662-stage-params.md) |
| 2026-09-14 | P-624 retail title state verified; `MELEE_TITLE_TEST` probe + `decomp_title` ctest | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-624-title-state.md`](handoffs/2026-09-14-P-624-title-state.md) |
| 2026-09-14 | P-658 unwalked public roots: stand/cut scenes, intro-easy table, 51 event levels, dbLoadCommonData | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-658-roots.md`](handoffs/2026-09-14-P-658-roots.md) |
| 2026-09-14 | P-638 reverb_hi/chorus ports + per-frame ITD ramp | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-638-axfx-itd.md`](handoffs/2026-09-14-P-638-axfx-itd.md) |
| 2026-09-14 | P-685 opening movie: host THP decoder, texture-cache invalidation, colanim/ftData endianness fixes | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-685-opening-movie.md`](handoffs/2026-09-14-P-685-opening-movie.md) |
| 2026-09-14 | P-686 fixed: fighter material templates used DOL-only data adjacency; decomp pin updated | Codex | [`2026-09-14-P-686-match-spawn.md`](handoffs/2026-09-14-P-686-match-spawn.md) |
| 2026-09-14 | P-687..P-689 non-Metrowerks divergences (upstream #3456) + task specs | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-687-native-divergences.md`](handoffs/2026-09-14-P-687-native-divergences.md) |
| 2026-09-14 | P-687..P-689 fixed: host fallbacks, console math, respawn platform | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-687-P-689-divergences-done.md`](handoffs/2026-09-14-P-687-P-689-divergences-done.md) |
| 2026-09-14 | P-692..P-694 GX HLE audit: vertex formats, texgen source z, TEV raster channel | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-692-gx-audit.md`](handoffs/2026-09-14-P-692-gx-audit.md) |
| 2026-09-14 | P-695/P-696/P-698 fixed (match-end crash, magnifier 400 ms frames, "STAGE CLEAR" shape-anim banner); P-699/P-700 filed | claude (opus-5) | [`2026-09-14-P-697-clear-screen-black-band.md`](handoffs/2026-09-14-P-697-clear-screen-black-band.md) |
| 2026-09-14 | P-700/P-701/P-703 fixed (SIS `vsnprintf` truncation, scene-desc fog walk, Shift-JIS name literals); P-704 filed — Classic VS names still absent, `xEF` indexes `x57C[3]` out of range | claude (opus-5) | [`2026-09-14-P-704-classic-vs-names.md`](handoffs/2026-09-14-P-704-classic-vs-names.md) |
| 2026-09-14 | P-704 fixed: US name-width table read one past `lbl_803B75F8`; VS splash names draw again (0 -> 2457 white pixels) | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-704-classic-vs-names.md`](handoffs/2026-09-14-P-704-classic-vs-names.md) |
| 2026-09-14 | P-706 opt-in `MELEE_SFX_DEBUG` recorder for the intermittent loud SFX burst; waiting on the owner's capture | Codex | [`2026-09-14-P-706-sfx-capture.md`](handoffs/2026-09-14-P-706-sfx-capture.md) |
| 2026-09-14 | P-707 fixed: `GrCs.dat` `yakumono_param` stayed big-endian so the Castle intro countdown never ran and the `0x53025` ambient looped for the whole match; owner confirmed the sound no longer glitches | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-706-sfx-capture.md`](handoffs/2026-09-14-P-706-sfx-capture.md) |
| 2026-09-14 | P-709 filed: GQR3 is a quantized-u16 store (corrects the P-687 `fn_80166A8C` float store) + `ftAnim_8006F3DC` return fix, both from the `999sian/melee-pc` cross-port review | opencode (deepseek-v4.1-flash) | [`2026-09-14-P-709-gqr3-u16-store.md`](handoffs/2026-09-14-P-709-gqr3-u16-store.md) |
| 2026-09-14 | P-709 fixed: `fn_80166A8C` now stores the clamped GQR3 u16 the caller reads back into `MatchPlayerData.xE` (`objdump`: 2-byte store, was `movss`), and `ftAnim_8006F3DC` returns a defined `0.0f` on its not-found path | claude (opus-5) | [`2026-09-14-P-709-gqr3-u16-store.md`](handoffs/2026-09-14-P-709-gqr3-u16-store.md) |
| 2026-09-14 | P-710 fixed: the P-695 census's "no caller reads the result" bucket was decided by a direct-call grep, so nine table-installed callbacks were written off as unread; four fixed (Chansey egg destruction, three Sound Test rows), five filed as P-711 | claude (opus-5) | [`2026-09-14-P-710-census-callback-tables.md`](handoffs/2026-09-14-P-710-census-callback-tables.md) |

## Template

See [`templates/handoff.md`](templates/handoff.md).

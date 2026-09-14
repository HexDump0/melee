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

## Template

See [`templates/handoff.md`](templates/handoff.md).

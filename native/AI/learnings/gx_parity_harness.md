# Renderer parity harness (P-677)

**Date:** 2026-09-13
**Agent:** opencode (deepseek-v4.1-flash)

## One command per slice

```sh
native/tests/parity_matrix.sh [render-binary] [report-path]
# defaults: ./build/native/test_decomp_render /tmp/melee-parity-report.md
MELEE_DISC=/path/to/game.iso native/tests/parity_matrix.sh   # non-default image
ctest --test-dir build/native -R decomp_parity               # CI form (18th test)
```

The harness renders 13 cases through the compiled HSD + GX HLE path and
writes a pass/fail markdown artifact (the report template):

| Slice | Cases |
|---|---|
| Characters | Mario, Fox, Pikachu, Kirby, Master Hand, Giga Bowser, Game & Watch |
| Stages | Final Destination (`GrNBa`), Yoshi's Story (`GrYt`), Fourside (`GrFs`), Pura/`GrPu` (toon path) |
| GX fixtures | `--direct` (direct-mode capture, texgen, indirect/light/texobj unit checks) and `--efb` (EFB copy formats, Z-texture, fog, lines/points, spot cones, Z24X8) |

Per case the harness requires: exit 0, `PASS` in the log, and no
`FAIL`/`decode failed`/`non-finite`/`GL_INVALID` line.  Screenshots and logs
go to a temp directory beside the report; nothing is committed.  The report
records the screenshot SHA-256 prefix (or the fixture summary), so a slice's
before/after can be diffed by regenerating and comparing hashes.

Disc-dependent cases SKIP cleanly without the image (`parity: PASS
(2/13 passed, 11 skipped)`), so the ctest is safe on machines without a
disc.  `decomp_parity` is the 18th ctest.

## Relationship to the unit tests

`decomp_gx_direct`/`decomp_efb` assert exact values (they are the
sensitivity-flipped regressions from P-672..P-682); the parity matrix is the
breadth gate that catches cross-character/stage crashes, decoder regressions
and non-finite geometry.  Screenshot hashes are internal consistency, not
retail parity (ADR-0017/`broken.md` policy).

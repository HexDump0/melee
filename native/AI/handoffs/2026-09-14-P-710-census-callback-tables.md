# Handoff: P-710 — the P-695 census's "unread" bucket was decided by the wrong grep

**Date:** 2026-09-14
**Agent:** claude (opus-5)
**Status:** done for 4 of 9 sites; the other 5 are P-711.
**Tree state:** clean; GC build 100.00% matched, `ctest` 28/28.

## Where this comes from

Continuing the `999sian/melee-pc` cross-port review that produced P-709.  That
port's `58cf731` ("Give every non-void function a defined return value")
patched **all 45** fall-off-the-end sites blindly.  Ours (P-695) triaged them
into 6 patched / 1 accidental / 38 left alone, which is the better method —
but re-auditing the triage found a hole in it.

## The hole

14 sites were bucketed as *"declared non-void but no caller reads the result
(fake return type)"*.  That verdict was reached by grepping for `name(` — i.e.
**direct calls only**.  Nine of those functions are never called directly at
all.  They are installed in callback tables and invoked through a pointer, and
the engine reads what comes back.  See G-160; the one-line check is

```sh
grep -rn "\b$fn\b" --include=*.c --include=*.h decomp/src | grep -v "$fn *("
```

The other five in that bucket were re-checked the same way and really are
unread.

## Fixed here (4) — all census outcome 1

| Site | Retail evidence | Fix |
|---|---|---|
| `it/kinds/itkyasarinegg.c:136` `itKyasarinegg_UnkMotion4_Anim` | `0x802efe08` is `mflr`/`bl it_802751D8`/epilogue; `r3` untouched | `return it_802751D8(gobj);` |
| `if/soundtest.c:812` `un_802FF934` | `0x802ff934`, same shape around `bl lbAudioAx_80024C08` | `return lbAudioAx_80024C08(un_804D6DBC);` |
| `if/soundtest.c:1264` `un_80300758` | see below | `return true;` / `return arg0 != 0;` |
| `if/soundtest.c:1271` `un_80300790` | see below | same |

**Why the egg one matters.**  `itKyasarinegg_UnkMotion4_Anim` is the `animated`
predicate of the Chansey egg's motion-state-4 `ItemStateTable` row
(`itkyasarinegg.c:21`).  `Item_80269528` (`item.c:1303`) does

```c
if (item_data->animated != NULL && item_data->animated(gobj)) {
    item_data->destroy_type = 0;
    Item_8026A8EC(gobj);      /* destroy */
    return;
}
```

so the port was deciding whether to destroy the egg from whatever `%eax` held.

**Why the two Sound Test ones are 4 and not 0.**  Their bodies end in
`un_802FFCD0(4, ptr)`, which is `void` — so the path *looks* indeterminate,
and melee-pc patched both to `return 0`.  But `un_802FFCD0` (`0x802ffcd0`)
reads `count` out of `r3` and does all its work in `r0/r5/r6/r7`; it never
writes `r3`.  `cmpwi r3, 1` does not write `r3` either.  So retail returns
`count` (4) on the `arg0 == 1` path and the incoming `arg0` on the other, both
fully determined.  That is G-161, and it is a general trap: a `void` callee is
not automatically an indeterminate `r3`.

The consumer is `un_80302E00` (`textlib_1.c:33`), which forwards the key to the
parent handler (`un_804D6E44->xC(arg1)`) **only** when the row's callback
returns 0.  Only zero-vs-non-zero reaches that test, so the `bool` spellings
are exact.  melee-pc's blanket `0` inverts the `arg0 == 1` decision.

## Verification

Patch series idempotent; `ninja` in `decomp/` 100.00% matched, 1130/1130
linked; `cmake --build build/native` clean; `ctest` 28/28 (unchanged).
No new ctest: neither the Chansey egg nor the Sound Test screen is reachable
from a harness today, and a test that cannot fail against the old patch proves
nothing.  Owner check queued as **H-6** in `TASKS.md`.

## Next (P-711)

Five Sound Test callbacks are still falling off the end into the same
consumer: `fn_80300CC8:1510`, `fn_80300DE0:1551`, `fn_80300ED0:1586`,
`fn_803011EC:1729`, `un_80301CE0:2176`.  Each ends in a multi-case `switch`
whose arms leave different things in `r3`, so each needs a per-path trace of
the retail disassembly rather than the single epilogue read that settled the
four above.  Do not copy melee-pc's blanket `return 0`.

## Also checked, not applicable

- melee-pc `2957ebf` (`Fighter_x2D0_t` must be `DISC_STRUCT`): their
  access-time endian model byte-swapped only the `ftCo_DatAttrs` view of the
  blob that Kirby/Purin also reach through `fp->x2D0`.  We convert the whole
  0x424 blob as dense `u32` at load (`hsd_convert.c:2540`), so both views see
  the same bytes.  No bug here.
- melee-pc `6f363e7` (Venom arwing `jobj` uninitialised): predates their own
  decomp re-pin.  Our pin already has `jobj = gobj->hsd_obj`
  (`grvenom.c:1145`).
- `b810dc7`, `4c7da38`, `93fc1c7`, `6a18a36`, `393a4b5`: 64-bit pointer-width
  crashes.  ADR-0012 keeps our compiled targets 32-bit, so these cannot occur.
- Everything after their `f163995` re-pin is Windows/Android/CI packaging.

## Worth mining next from that port

Their pre-re-pin history still has gameplay-relevant work we have not
compared: `ba3d332` (Classic team-intro splash cutouts), `e79cffd`
(animated-texture frame bounds), `4899bc7` (effect queue), `c213063` (black
stage floors from single-level shadow textures), `accc210`/`b040234`
(disc-vector misreads on the fighter bone-offset path).  The last pair is the
most likely to apply to us, since disc-data conversion is our converter's job
too.

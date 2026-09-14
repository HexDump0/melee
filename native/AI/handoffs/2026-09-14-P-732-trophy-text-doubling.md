# P-732: trophy description text renders doubled — open, narrowed by measurement

Status at handoff: **not fixed.** Four hypotheses tried and disproved, and the
bug is now boxed in tightly enough that the next session should not need to
guess. Everything below marked "measured" came from instrumenting the running
game; everything else is explicitly labelled.

## The repro (corrected by the owner)

Entering the close-up view alone is **not** enough. The sequence is:

1. Trophies -> Gallery -> open a trophy's description. It is correct.
2. Press **A** for the close-up view.
3. Come back.
4. **Move to the next trophy.** *That* description is glitched.

I spent several rounds testing "A and back" without the trophy change and kept
seeing a clean screen. The trigger needing the *next* trophy is the single most
important fact here: the damage is carried across a teardown into the next
trophy's setup, not produced by laying out any one message.

Once broken it stays broken, and each trophy is glitched in its own repeatable
way.

## What was measured (trust these)

Instrumentation lived in `hsd_3A76.c` and `gobj.c` under `PORT_PC`, gated on
`MELEE_TRACE_TEXT=1`, printed via `OSReport` and read with
`MELEE_LOG_REPORTS=1`. It was reverted before this handoff; recreate it from
the notes below.

- **The SIS layout is correct.** Each object emits exactly one copy's worth of
  glyphs: 17 for `"Super Mario Bros."`, 12 for `"Koopa Troopa"`, 5 for
  `"10/85"`, 308 for the Koopa Troopa description. Identical whether the
  `restart` path ran or not.
- **One quad per glyph.** The glyph branch has a single `GXBegin`/`GXEnd`; no
  shadow or outline pass exists to be misplaced.
- **One render per frame.** `[walk]` printed raw (no dedupe) showed the text
  GObj rendering once per frame for frames 61..82 steady-state.
- **One render pass per retrace.** `passes=1` in steady state.
- **Layout state is identical across trophies** on the clean path:
  `x88=1000 x80=700/700 x78=0/0 fs=3400/3300 kern=1 fit=1 align=0 lh=22400`
  for the description object (`0x806ea1a4`).
- `sizeof(HSD_Text)` is **160** on the host, matching retail's `li r3,160` at
  `803A5ACC`; `SisBlock` is 12. Both now pinned with `STATIC_ASSERT`, and the
  assertions were verified to fire when given a wrong value (G-163).

## Hypotheses disproved (do not re-run these)

1. **The trophy text archive is unconverted.** No. `SIS_ToyData` *is* an
   unclaimed converter root, but harmlessly: all 317 of `SdToy.dat`'s
   relocations are the message-pointer table (max offset 1264, table ends
   1268, **zero in-stream**). Checked `SdToyExp` too, which is where the
   affected font-3 text comes from: 902 relocations, max 3604, table ends
   3608, **zero in-stream**. Neither archive has embedded pointers, so the
   parser's `SIS_S32`-as-pointer sites (`hsd_3A76.c:324`, `:734`) never see a
   relocated value and the big-endian read is right.
2. **The font atlas is wrong.** No. `HSD_SisLib_FontAtlas` is real extracted
   data, 146944 bytes, 54598 non-zero.
3. **Text objects leak on cycling.** No. The cycle path only calls
   `HSD_SisLib_803A6368` to re-point one existing object; creation is guarded
   by `if (display->x144 == NULL)`.
4. **The stale SIS list head.** `HSD_SisLib_803A5E70` resets the text heap but
   left `HSD_SisLib_804D7978` pointing into it — a real latent bug, fixed in
   `0d96fed33`, but **it did not fix this artifact.**

## Two conclusions I published and had to retract

Both came from the same mistake: reading the *first* hit out of a deduplicated
trace as if it were the steady state.

- "The entire render pass runs twice per frame" — disproved by `passes=1`.
- "Each text object renders twice per frame" — disproved by the raw `[walk]`
  trace: frame 60 appears twice, frames 61..82 once each. The doubles occur
  only on loading frames, where `VIWaitForRetrace` does not tick and
  `boot_triage_frames()` merges two real frames into one key.

**Rule for the next session: print the distribution raw before concluding
anything from a deduped trace.** A dedupe key also has to cover every field
you might care about — mine keyed on `(obj, glyphs, x88)` and would have
swallowed a change in scale, font size or line height. It was widened to hash
all placement inputs just before the session ended; that widened trace has not
been run yet.

## Where to look next

The picture (owner's screenshot) shows the first ~6 lines of the description
clean and the lines below progressively piling on top of each other. Given the
glyph count is exactly one copy, that is **consecutive lines overlapping**,
not two horizontal copies of the string — which is how I had been reading it
for most of the session, and it invalidated every hypothesis built on it.

Vertical placement comes from `case 7` in `hsd_3A76.c`:

```c
y_offset = SIS_S16(sis_cursor + 3);
text->current_height = (f32) y_offset * text->font_size.y;
```

and each glyph is placed at

```c
glyph_y = (scale_y * (line_height_out - glyph_size))
        + (text->pos_y + text->current_height);
```

A `[line]` trace logging `y_offset`, `current_height`, `line_height_out` and
`line_width_out` per line was written and built but **never run** — that is
the first thing to do. If `y_offset` steps evenly while `current_height`
stops advancing, the fault is in `font_size.y`; if `y_offset` itself stops
stepping, it is the stream position, which ties back to the fact that the
trigger requires a *trophy change* after the close-up.

Also still unexplained and probably the same root cause: **P-730**, the
Trophy List showing ~5 empty row boxes for a split second before vanishing.

## Recreating the instrumentation

All of it was `#ifdef PORT_PC`, `getenv("MELEE_TRACE_TEXT")`, `OSReport`:

- `hsd_3A76.c` entry (after the `pass != 2` filter): object, gobj, font_idx,
  `sis_buffer`, kerning, fitting, font size, position.
- `hsd_3A76.c` at `render_done`: glyph count, restart count, and every
  placement input, deduped on a hash of all of them.
- `hsd_3A76.c` `case 7`: the per-line `[line]` trace described above.
- `gobj.c` in `HSD_GObj_80390ED0`'s inner loop: which GX-link list and which
  driving camera GObj each text render came through (this is what disproved
  the double-render theories). Print it **raw**, not deduped.
- `gobj.c` in `HSD_GObj_80390FC0`: render passes per retrace.

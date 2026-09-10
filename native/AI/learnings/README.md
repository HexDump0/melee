# Learnings

Durable technical knowledge about porting Melee. These files are the only
surviving memory of agent sessions; write for a reader who has never seen the
data.

## Rules

1. **Record the evidence.** "Offset +0x20" is not enough; say how you verified
   it (hex dump, identity check, screenshot, decompiled source line).
2. **Prefer a table of offsets** over prose.
3. **Separate fact from inference.** Mark hypotheses with `HYPOTHESIS:` and say
   how to falsify them.
4. **Update, don't fork.** If a fact is wrong, fix the file and mention the
   correction in the commit. Keep one source of truth.
5. **No game data.** Do not paste large binary blobs from the disc. A few hex
   bytes illustrating a struct are fine.

## Files

| File | Contents |
|---|---|
| [`hsd_archive_format.md`](hsd_archive_format.md) | `.dat` archive header, pointer base, reloc/public/extern tables |
| [`hsd_models_and_skinning.md`](hsd_models_and_skinning.md) | Joint/DObj/PObj layouts, vertex descriptors, envelope groups, bind matrices |
| [`gx_display_lists.md`](gx_display_lists.md) | Display list framing, primitive opcodes, attribute order |
| [`gx_textures.md`](gx_textures.md) | Texture descriptors, formats, tiling, palettes |
| [`disc_assets.md`](disc_assets.md) | CISO/ISO/GCM, FST, DOL, file naming, where characters live |
| [`fighter_data.md`](fighter_data.md) | `ftData`, `ftCo_DatAttrs`, attribute offsets |

## Writing a new entry

Use [`../templates/learning.md`](../templates/learning.md) and add the file to
the table above. Cross-link from `ARCHITECTURE.md` or `GOTCHAS.md` when
relevant.

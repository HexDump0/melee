# Fighter data (`ftData`, `ftCo_DatAttrs`)

Verified: `demo_attributes.c` reads Mario's movement values from `PlMr.dat`
(not `PlMrNr.dat`) and the demo prints plausible retail values.

## Where attributes live

`Pl<Char>.dat` exposes a public symbol `ftData<Char>` (for example
`ftDataMario`). Relocation makes the first field a pointer:

```
ftData (+0x00) -> ftCo_DatAttrs*
```

So the attribute block is reached as:
`attr = rb32(archive, data_base + ftData_offset)` then `file = data_base + attr`
(data-relative, see `hsd_archive_format.md`).

## ftCo_DatAttrs offsets (verified subset)

The full struct is `ftCo_DatAttrs` in `src/melee/ft/types.h` starting at
`fp+0x110`. Offsets used by the port:

| Offset | Field | Demo meaning |
|---|---|---|
| +0x04 | `walk_accel_base` | ground acceleration |
| +0x18 | `ground_friction` | ground friction |
| +0x34 | `ground_max_horizontal_velocity` | run speed |
| +0x40 | `jump_v_initial_velocity` | jump velocity |
| +0x58 | `max_jumps` (int) | air jumps |
| +0x5C | `gravity` | gravity |
| +0x60 | `terminal_velocity` | fall cap |
| +0x64 | `air_drift_stick_mul` | horizontal air accel |
| +0x70 | `aerial_friction` | air friction |
| +0x78 | `air_max_horizontal_velocity` | air speed cap |

Retail Mario values observed through the demo:

```
accel 0.080  friction 0.060  run 1.500  gravity 0.095
terminal 1.70  air 0.045  jump 2.30  jumps 2
```

The demo maps `air_drift_stick_mul` onto its single `air_accel` field; the real
formula also adds `aerial_drift_base` with a sign term
(`ftCommon_CalcSelfAccel_Drift`, `src/melee/ft/ftcommon.c`). That is a
documented approximation (P-302).

## `ftData` layout beyond x0

From `src/melee/ft/types.h`:

| Offset | Field |
|---|---|
| +0x00 | `ftCo_DatAttrs* x0` |
| +0x04 | `void* ext_attr` |
| +0x08 | `ftData_x8*` (parts descriptor: `FtPartsDesc`, part index, anim joints) |
| +0x1C | model/anim joint tables |
| +0x20 | `ftData_x20*` |
| +0x2C | `ftDynamics*` |
| +0x30 | hurtbox inits |
| +0x34 | `{Fighter_Part, float scale}` |
| +0x3C | camera data |
| +0x4C | `FtSFX*` |
| +0x58 | `ftData_x58_t*` |
| ... | see types.h |

Attributes sit at `fp+0x110` because `Fighter` embeds `co_attrs` after the
common fields; the demo avoids the giant `Fighter` struct entirely.

## Parts and visibility (needed for faces and animation)

- `ftData_x8` describes `Fighter_Part` mapping and animation joints.
- `ftParts.c` assigns DObjs to parts (`ftParts_80074194`) and sets visibility
  from `FtPartsVis` tables supplied by the **animation system**, e.g.
  `ftParts_80074B6C` toggles `DOBJ_HIDDEN` (bit 0) on `dobj_list` entries.
- Therefore model archives contain no "default visible" set: with no animation,
  every face expression mesh is visible at once and overlaps. This is why the
  bind-pose face looks smeared. Fix path: P-201 (animation) plus part
  visibility, or a viewer-side part filter (already implemented for debugging).

## HYPOTHESIS (not yet tested)

`ftData_x8`'s part table order corresponds to the DObj traversal order used by
`ftParts_80074194` (joint tree, DObj `next` chains). Falsify by loading Mario's
`ftDataMario` part table and comparing its length/index range with the 68 DObjs
the model parser finds.

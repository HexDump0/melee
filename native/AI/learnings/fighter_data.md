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

## Parts and visibility (verified, implemented)

The static visibility lives in `Pl<Char>.dat` and is implemented by
`native/demo_parts.c`.

### Locating it

```
ftData<Char> +0x08 -> ftData_x8          (data-relative)
    value 0 means data offset 0: the descriptor lives at the very start of the
    archive's data section for Mario.  Do NOT treat 0 as NULL here.
ftData_x8 +0x00 = FtPartsDesc {
    u32 model_num;              // number of visibility "models" (Mario: 1)
    void* (*vis_table)[4];      // data-relative
}
vis_table[costume][slot] -> FtPartsVisLookup[model_num]
FtPartsVisLookup { int variant_count; TempS* variants; }
TempS            { int count; u8* dobj_indices; }
```

`costume` is the costume id (0 = `Nr` normal). Slots 0..3; `FtPartsVis.xC[4]`
is always NULL.

### Runtime rules (from `src/melee/ft/ftparts.c`, `ftdrawcommon.c`)

- `ftParts_8007487C` (called from `ftParts_800749CC`) marks **every** DObj
  listed in any slot hidden (`HSD_DObjSetFlags(dobj, 1)`), then clears.
- Normal rendering (`ftDrawCommon_800805C8`) enables slot 0 and disables
  slots 1/2/4; metal fighters enable slot 2 instead.
- `ftParts_80074B6C(fp, vis, slot, list)` shows variant `x5F4_arr[i].idx` for
  each model `i` and hides the rest. The animation system sets that index.
- With no animation, the port shows variant 0 of slot 0 and leaves everything
  else hidden. That reproduces the neutral face.

### DObj indices are not PObj/batch indices

The vis tables index `fp->dobj_list`, built in joint-traversal order by
`ftParts_SetupParts`. A DObj can own several PObjs, so the port stores the
DObj index on every `DemoModelBatch` and tests visibility through
`demo_model_batch_visible`. Never use the batch index directly.

### Still missing

Animated expressions (blink, damage) need animation playback (P-201) to set
`x5F4_arr` indices; the tables themselves are already parsed.

## HYPOTHESIS (not yet tested)

`ftData_x8`'s part table order corresponds to the DObj traversal order used by
`ftParts_80074194` (joint tree, DObj `next` chains). Falsify by loading Mario's
`ftDataMario` part table and comparing its length/index range with the 68 DObjs
the model parser finds.

# HSD archive (`.dat`) format

Source of truth: `src/sysdolphin/baselib/archive.c/.h`. Verified against
`PlMrNr.dat` extracted from the retail disc.

## Header (file offset 0x00, 0x20 bytes)

| Offset | Type | Field |
|---|---|---|
| 0x00 | u32 BE | `file_size` — total archive size |
| 0x04 | u32 BE | `data_size` — size of the data section |
| 0x08 | u32 BE | `nb_reloc` — relocation entry count |
| 0x0C | u32 BE | `nb_public` — public symbol count |
| 0x10 | u32 BE | `nb_extern` — extern symbol count |
| 0x14 | u8[4] | `version` (`/2.0`, etc.) |
| 0x18 | u32[2] | padding |

Data section starts at **0x20**.

## THE pointer rule

All pointers inside the data section are stored as `u32` **offsets from the
start of the data section**, not absolute values:

```
host_file_offset = stored_u32 + 0x20
```

`HSD_ArchiveParse` performs this addition in place for every entry in the
relocation table. Consequences:

- `stored == 0` usually means NULL.
- `stored == 0` **can also mean "points at data offset 0"** for array-base
  fields (vertex arrays, display lists). Resolve ambiguity per field: use a
  NULL-aware accessor for link pointers and a base-aware one for arrays.
- Never reinterpret these as host pointers; keep them as offsets and bounds
  check (ADR-0002).

## Trailing tables

```
data section: [0x20 .. 0x20 + data_size)
relocation:   nb_reloc * 4 bytes    (u32 data-relative offsets of pointer fields)
public:       nb_public * 8 bytes   (u32 data_offset, u32 symbol_name_offset)
extern:       nb_extern * 8 bytes   (u32 data_offset, u32 symbol_name_offset)
strings:      NUL-terminated symbol names, offset from strings base
```

Public entry layout (verified against `PlMrNr.dat`, which has 2 publics):

| Offset | Field |
|---|---|
| +0 | `data_offset` — offset from data base of the symbol |
| +4 | `symbol` — offset into the strings blob |

Root-selection heuristic used by `hsd/model.c`: pick the first public whose
name ends in `_joint` and does not contain `matanim`. For `PlMrNr.dat` that is
`PlyMario5K_Share_joint @ 0x19400`.

## Verified example

`PlMrNr.dat`: `file_size 0x739B2`, `data_size 0x72310`, `nb_reloc 1423`,
`nb_public 2`, `nb_extern 0`.

Public symbols:
- `PlyMario5K_Share_joint` @ `0x19400`
- `PlyMario5K_Share_matanim_joint` @ `0x44590`

Root joint lives at file offset `0x20 + 0x19400 = 0x19420`.

## Pitfalls

- The `matanim_joint` root is animation data, not geometry. Do not pick it.
- `data_size` counts only the data section; the relocation table starts right
  after it. Do not assume 4-byte alignment of the following tables without
  checking.
- Symbol names are Shift-JIS in general. Character names are ASCII, but do not
  assume it for other archives.

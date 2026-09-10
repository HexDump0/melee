# Disc images and asset locations

Verified against `Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso`.

## Container formats

`demo_assets.c` accepts CISO, ISO and GCM.

- **CISO**: 0x8000 header. `u32 LE` magic `CISO` at 0, `u32 LE` block size at
  4 (0x200000 for this disc), then a 0x7FF8-byte block map at 8. Map entry 0
  means "unused block → zeros"; nonzero entries are ordinal indices of packed
  blocks in the file.
- **ISO/GCM**: raw disc image, offsets are file offsets.

Speed note: the map is a sparse ordinal table, so translating a virtual offset
to a physical one requires a prefix count. `disc_open` precomputes a
`block_ordinal[]` prefix array once; do not reintroduce the O(blocks) scan per
read (it made FST enumeration unusably slow).

## Disc header

| Offset | Meaning |
|---|---|
| 0x00 | game id string (`GALE01`) |
| 0x1C | `u32 BE` GC magic `0xC2339F3D` |
| 0x420 | `u32 BE` DOL offset |
| 0x424 | `u32 BE` FST offset |

## FST (file system table)

- At `fst`: entry count `u32 BE` at +8; entries start at +0x0C.
- Each entry is 12 bytes: `typeAndName u32 BE`, `parentOrOffset u32 BE`,
  `sizeOrNext u32 BE`.
  - `typeAndName`: bit 24+ = name offset into the string table, bit 0 = 1 means
    directory.
  - For files: `parentOrOffset` = disc offset, `sizeOrNext` = byte length.
  - For directories: `parentOrOffset` = parent entry index, `sizeOrNext` =
    index one past the directory's subtree.
- String table base = `fst + entries * 12`; names are NUL-terminated.
- `demo_asset_list(prefix, suffix)` enumerates all **file** entries and filters
  by name; it does not need directory reconstruction.

Observed: 1209 files, 273 `Pl*.dat`, 71 `Gr*.dat`.

## Character model naming

```
Pl<Char><Costume>.dat
```

- `<Char>`: verified from public symbols / renders: Mr Mario, Lg Luigi, Dr Dr.
  Mario, Pe Peach, Kp Bowser (Koopa), Ys Yoshi, Dk Donkey Kong, Ca Captain
  Falcon, Gn Ganondorf, Fx Fox, Fc Falco, Cl Young Link, Lk Link, Zd Zelda,
  Ns Ness, Pk Pikachu, Pc Pichu, Ms Marth, Fe Roy, Gw Mr. Game & Watch, Mt
  Mewtwo, Mh Master Hand, Pp Popo and Nn Nana (Ice Climbers), Kb Kirby, Ss
  Samus, Pr Jigglypuff (Purin), Sk Sheik (unverified), Sb Sandbag
  (unverified). Other prefixes (Bo, Ch, Co, Gk, Gl) are unverified — do not
  assert a mapping without checking the archive's public symbol string.
- `<Costume>`: `Nr` normal, `Gr` green, `Bu` blue, `Re` red, `Ye` yellow,
  `Bk` black, `Wh` white, `La` lavender, `Or` orange, `Pi` pink, `Aq`
  aqua, `AJ` alternate/anim-only archives, `DViWaitAJ` special.
- The reliable base-model set is every `Pl*Nr.dat` that does not contain `Cp`
  (Kirby copy abilities): 33 archives today.

`Pl<Char>.dat` (without costume) contains `ftData<Char>`, including the
`ftCo_DatAttrs` attributes; `Pl<Char>Nr.dat` is the model.

## Other useful root files

- `PlMrAJ.dat`, `Pl*AJ.dat`: animation-only archives.
- `Gr*.dat`: stage assets (not yet supported by the parser).
- `GALE01` system files, menus, audio banks: untouched.

## Safety rules

- Never trust an offset without `range_ok`; the parser is fuzzable and must not
  read wild memory (ADR-0002).
- CISO virtual offsets can exceed the physical file size because unused blocks
  are omitted. Clamp name/string reads against `d->file_size` and treat a short
  read as corruption.
- Asset bytes are user-supplied; never commit them (ADR-0005).

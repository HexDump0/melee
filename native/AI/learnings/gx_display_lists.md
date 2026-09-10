# GX display lists (PObj geometry)

A PObj's `display` block is a GameCube FIFO command stream containing only
primitive draw commands. `n_display` is in 32-byte blocks; the byte length is
`n_display * 32`.

## Frame layout

```
[opcode: u8] [vertex count: u16 BE] [vertex data ...]
```

- The opcode's low 3 bits encode the vertex format (`vtxfmt`); the primitive is
  `opcode & 0xF8`. All PObjs observed in `PlMrNr.dat` use `vtxfmt = 0`.
- `count` counts **vertices**, not primitives.
- `opcode == 0x00` terminates the list (padding).
- The vertex data length is not encoded. You must consume attributes in
  descriptor order until `count` vertices are read, or you will desync.

## Primitives (`opcode & 0xF8`)

| Value | Primitive | Triangle rule |
|---|---|---|
| 0x80 | QUADS | `(0,1,2)`, `(0,2,3)` per 4 |
| 0x90 | TRIANGLES | `(0,1,2)`, `(3,4,5)`, ... |
| 0x98 | TRIANGLESTRIP | for k>=2: `(k-2,k-1,k)` with parity swap: `a=(k&1)?k-1:k-2`, `b=(k&1)?k-2:k-1` |
| 0xA0 | TRIANGLEFAN | for k>=2: `(0,k-1,k)` |
| 0xA8..0xB8 | LINES/STRIP/POINTS | skip; still consume `count` vertices |

Melee fighters are almost entirely `TRIANGLESTRIP` (e.g. 282 of 304 draws in
`PlMrNr.dat`).

## Attribute order

The vertex stream follows the PObj's `HSD_VtxDescList` order exactly, including
matrix indices. For the common 4-entry set:

```
[PNMTXIDX: 1 byte direct]
[POS:   u16 index]
[NRM:   u16 index]
[TEX0:  u16 index]
```

Rules:
- **Matrix-index attributes are always 1 byte inline**, regardless of the
  `comp_type` in the descriptor (it says `GX_F32`, ignore it).
- Indexed attributes read `u8` (GX_INDEX8) or `u16` (GX_INDEX16) big-endian,
  then fetch `base + index * stride` from the descriptor's array.
- Direct attributes read `comp_count * comp_type_size` bytes inline, big-endian
  for 16-bit and float.
- Apply `frac`: integer values are fixed point; decode as `value / 2^frac`.

## Debugging desync

Symptoms of stream desync: implausible indices, "shards" or long thin
triangles, wildly out-of-range positions. Checks:

1. Histogram the first byte of every record; only `0x80/0x90/0x98/0xA0/0x00`
   should appear. Any other opcode means you desynced or missed a command.
2. Verify per-attribute byte consumption sums to the expected vertex stride.
3. Dump one primitive's raw bytes and decode it by hand once.
4. Compare positions of consecutive strip vertices: they should be spatially
   close for a coherent mesh.

`native/demo_model.c` exposes no debug histogram; use a Python probe (see
`workflows/inspect_an_asset.md`) or add a temporary one and delete it before
commit.

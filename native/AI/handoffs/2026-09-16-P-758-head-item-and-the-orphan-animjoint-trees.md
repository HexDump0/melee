# Handoff: P-758 — head item landed; the rest of the `PlXx.dat` gap is orphan `HSD_AnimJoint` trees

**Date:** 2026-09-16
**Agent:** claude (opus-5)
**Commits:** `266aa9bc6`, `12b2fa4ae`, `d3a41c862`
**Tree state:** builds clean, `ctest --test-dir build/native` **32/32**, converter **v105**.
The only uncommitted file in the tree is `native/tests/test_decomp_render.c`,
which belongs to the codex graphics session — I did not touch it.

## What I did

- **Landed P-758's head item.** `conv_ft_part_anim` (`/* DWARF: ftData_x1C */`)
  now walks each `ftData->x1C` entry and follows `x8`, the `HSD_AnimJoint*`
  array `ftAnim_ApplyPartAnim` (`ftanim.c:1281`) indexes with
  `Fighter_x8B0_t.x11`. `x2` bounds `x4`, **not** `x8`, so nothing in the
  archive records `x8`'s length — that is why it was never followed.
- **Coverage 76.87% → 79.50%**, +5,512 descriptors. The 34 `PlXx.dat` went
  **20.2% → 39.9%** (gap 22,271 → 16,759). `MELEE_COVERAGE_FLOOR` → 79.50,
  `decomp_layout` floor 49 → **51** (it was already at 50 at HEAD; the ratchet
  was one behind).
- **Fixed an over-walk I introduced in my own first commit.** See the dead-ends
  section — it is the most reusable thing in this note.
- **Corrected two claims in the P-758 row.** It scoped the head item at
  ~20,600 descriptors; it recovered 5,512. And it predicted **P-771** was the
  same root: it is not.

## Exact next action

The remaining `PlXx.dat` gap is **16,759 descriptors / 67,022 cold words**, and
**15,071 of them are the same 0x14-byte shape** (`words=5 ptr=1|2|3 conv=0`).
That shape is **`HSD_AnimJoint`** — and `conv_anim_joint` already exists and is
correct. As with the head item, the *reference chain* is what is broken.

Start here, in `PlMr.dat`:

```sh
IMG="iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"
MELEE_UNWALKED=2 MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets "$IMG" 2>&1 >/dev/null \
  | awk '/^\[unwalked-file\]/{f=$2} f=="PlMr.dat"{print}' > /tmp/plmr.txt
```

The run 0xa1f0 → 0xa204 → … → 0xa290 is nine unwalked nodes, each 0x14, chained
at +0x00, ending **immediately before 0xa2a4** — which my new walker *does*
reach, as `x8[0]` of `x1C` entry 0 (`[walked] 0x00a2a4 <- field 0x008d14`).

The head of that run is **0xa1dc**, and **nothing in the archive points at it**:

```sh
MELEE_UNWALKED=1 MELEE_FIND_PTR=0xa1dc MELEE_NO_ASSET_CACHE=1 \
    ./build/native/test_decomp_assets "$IMG" 2>&1 >/dev/null \
  | awk '/^\[unwalked-file\]/{f=$2} f=="PlMr.dat" && /^\[findptr\]/{print}'
# -> no output
```

So 0xa1dc is the workflow's *"descriptor with no referrer — usually an element
of an array whose base is the real root"* case. **Find that base.** Do not add
a scan-and-guess walker until you have; `conv_orphan_matanim_trees` is the
precedent for scanning, but it pays for a shape heuristic and P-765 is the
record of a shape heuristic being wrong.

**The type is confirmed, so you do not need to re-derive it.** A *walked*
sibling at 0xa36c is `HSD_AnimJoint` field for field —
`{child=0, next=0, aobjdesc=0xa1cc, robj_anim=0, flags=1}` — and 0xa1cc is an
`HSD_AObjDesc` `{flags=0, end_frame=10.0f, fobjdesc=0xa190, obj_id=0}` whose
FObj chain 0xa190 → 0xa1a4 → 0xa1b8 is already walked. The unwalked nodes have
exactly that shape with `aobjdesc = 0`, which is normal for an unanimated
joint. DWARF has `HSD_AnimJoint` at size 20 = 0x14.

**Sizing the job before you start it:** of the 16,759, only **3,205 are chain
heads** — the other 13,554 are reached from another unwalked descriptor, so
they come free once a head is reached. (Caveat: `MELEE_UNWALKED` prints only
the *first* referrer per target, so treat 3,205 as an upper bound.) Script:

```sh
# see the "Verification run" section for the exact one I used
```

Worst files after the head item, by remaining gap: `PlGn` 1209, `PlGk` 978,
`PlDr` 971, `PlKp` 958, `PlPe` 943, `PlZd` 916, `PlFx` 775, `PlFe` 757.

## What I tried that did not work

**1. A `c->reloc[slot]` bound does not end the `x1C` table — and this is the
general lesson, not a detail of this walker.** In `PlMr.dat` the table at
0x2534 holds three entries, and the word straight after it, 0x2540, is
`ftData->x20` — an `ftData_x20 { HSD_Joint** x0; f32 x8; }`. Its `x0` is a
relocation field *exactly like the table slots are*, so `!c->reloc[slot]` never
fires and the walk reads an `HSD_Joint**` array as a fourth part-animation
descriptor. That is the P-739 shape.

The over-run predates my walker; it was inert only because `mark()` had already
claimed 0x8e60 and swapping two u16 of a zero word changes nothing. **Following
`entry+0x08` made it live.** The fix is `next_pointed_at_after()` as a second
bound: `ftData->x20` points at 0x2540, so the table ends there, at three.

> If you bound an array by relocation evidence, ask what the *next struct
> field* after the array is. If it is a pointer, your bound does not exist.

I proved the fix costs nothing by diffing `MELEE_COVERAGE_JSON` per file:
**zero of 861 archives differ** before and after, which says both that the
over-run reached nothing and that the tighter bound cuts no real entry.

**2. `HSD_MAX_DEPTH` is not truncating these trees.** `conv_anim_joint`
recurses on `next` as well as `child`, so a long sibling run accumulates depth,
and 256 looked like a plausible cap for a 130-bone fighter. Raised it to 4096
as a probe: **coverage did not move one descriptor** (166368 either way).
Reverted. Do not spend time here.

**3. P-771 is not this chain.** The P-758 row predicted the Corneria
`HSD_JObjAddAnim` crash was probably the same root. Stage 7 × all 26 fighters
fails **the same 6 runs with the same assertion** before and after the head
item. P-765 was already fixed separately at v102.

**4. Reading the `x1C` entry base off a dump is easy to get wrong.** The
entries in `PlMr.dat` are at 0x2510/0x251c/0x2528 and the *table* is at 0x2534,
after them. My first reading had the entry base 4 bytes low, which made the
`u16` pair land on a pointer. The check that settles it: `ftData` is at 0x8de4
(`ftData+0x1C` = 0x8e00 holds 0x2534), and `ftData+0x0C`/`+0x14` hold 0x6f20
and 0x8b88, which the unwalked report independently shows as the two
partially-converted `Fighter_WaitAnimData` arrays.

## Open questions

- **What array holds the orphan `HSD_AnimJoint` roots?** Nothing points at
  0xa1dc. The game must reach it by pointer arithmetic from a base — `ftanim.c`
  does exactly that a few lines up from the `x1C` consumers
  (`tracks = &tracks[*nodes]`, `ftanim.c:1267`), so start by reading what feeds
  `tracks` and `nodes`. Needs a human? **No.**
- **Is any of it dead data?** ~15,000 orphan nodes across 34 files is a lot to
  be unreachable, so I doubt it — but a walker that converts genuinely dead
  bytes raises coverage without fixing anything, which the workflow calls out
  as gaming the metric. Confirm reachability from `ftanim.c` before walking.
  Needs a human? **No.**

## Files touched / claimed

- `native/decomp/assets/hsd_convert.c` — `conv_ft_part_anim`,
  `FT_DATA_X1C_SIZE`, the `x1C` table bound, `HSD_CONVERTER_VERSION` → 105
- `native/tests/test_decomp_assets.c` — `MELEE_COVERAGE_FLOOR` → 79.50
- `native/CMakeLists.txt` — `decomp_layout` floor → 51
- `native/AI/TASKS.md` — P-758 row
- `AI/agent_communication.md` — claim added, then released

Not touched: `patches/src/**`, `decomp/src/**`, `src/**`,
`native/decomp/boot/**`, `native/tests/soak.sh`, `native/tests/test_decomp_render.c`.

## Verification run

```sh
IMG="iso/Super Smash Bros. Melee (USA) (En,Ja) (Rev 2).ciso"

cmake --build build/native -j8
MELEE_NO_ASSET_CACHE=1 ./build/native/test_decomp_assets "$IMG" 2>/dev/null | grep 'coverage desc'
# before: descriptors=209261 walked=160856 (76.87%)
# after:  descriptors=209261 walked=166368 (79.50%)

ctest --test-dir build/native
# 100% tests passed out of 32

# targeted soak, before vs after, built from HEAD and from the working tree
for g in 7 11 31; do
  MELEE_SOAK_JOBS=4 MELEE_SOAK_SEEDS=1 MELEE_SOAK_FIGHTERS=all MELEE_SOAK_STAGES=$g \
    native/tests/soak.sh ./build/native/melee_decomp_boot /tmp/soak-758-g$g
done
# stage 7:  7/26 fail, identical signatures and identical runs before and after
#           (6x SIGSEGV in HSD_JObjAddAnim, 1x SIGSEGV in HSD_JObjGetFlags)
# stage 11: 1/26 fail, identical (SIGSEGV in it_8026EC54)
# stage 31: 0/26 fail, both
```

Chain-head count (the script referenced above):

```python
# feed it the stderr of: MELEE_UNWALKED=1 ... 2>unwalked.txt
import re, collections, bisect
f=None; per=collections.defaultdict(list)
for ln in open("unwalked.txt"):
    m=re.match(r'\[unwalked-file\] (\S+)',ln)
    if m: f=m.group(1); continue
    m=re.match(r'\[unwalked\] 0x(\w+) <- field 0x(\w+) words=(\d+) ptr=(\d+) conv=(\d+) cold=(\d+)',ln)
    if m and f:
        g=m.groups(); per[f].append((int(g[0],16),int(g[1],16),int(g[2]),int(g[5])))
heads=body=0
for k in (k for k in per if re.fullmatch(r'Pl..\.dat',k)):
    iv=sorted((t,t+w*4) for t,fld,w,c in per[k]); starts=[a for a,b in iv]
    for t,fld,w,c in per[k]:
        i=bisect.bisect_right(starts,fld)-1
        if i>=0 and iv[i][0]<=fld<iv[i][1]: body+=1
        else: heads+=1
print(heads, body)   # -> 3205 13554
```

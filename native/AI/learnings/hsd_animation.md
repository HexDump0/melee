# HSD fighter animation (FigaTree)

Sources of truth: `src/melee/lb/lbanim.c`, `src/sysdolphin/baselib/fobj.c`
(`aobj.c`, `jobj.c`, `mtx.c`, `spline.c`), `src/melee/ft/ftanim.c` and
`src/melee/ft/ftparts.c`.  Verified against `PlMrAJ.dat` / `PlKbAJ.dat` and
renders from `PlMrNr.dat`, `PlKbNr.dat`, `PlFxNr.dat`, `PlMsNr.dat`.

The port is intentionally a **literal transcription** of the engine functions;
do not replace them with a "clean" equivalent (see `AGENTS.md` §0.1).

## 1. Clip container: `Pl<Char>AJ.dat`

- The file is **not one HSD archive**.  It is a sequence of independent HSD
  sub-archives, each 0x20-aligned, one per clip.  Walk: read `file_size` at
  offset 0, advance to `(off + file_size + 0x1F) & ~0x1F`, repeat.
- Each sub-archive exposes one public symbol named
  `Ply<Char>5K_Share_ACTION_<State>_figatree`.  The display name is the part
  between `_ACTION_` and `_figatree` (`Wait1`, `WalkMiddle`, `Dash`, ...).
  Mario has 195 clips; Kirby 335.
- Archive table order (settled, differs from an early assumption):
  `data section` -> `relocation table (nb_reloc * 4)` -> `public table
  (nb_public * 8)` -> `extern table` -> `strings`.  `data_size` covers the
  data section only.
- Pointers inside a sub-archive are data-relative **to that sub-archive's**
  data base (`container_offset + 0x20 + value`).  Do not resolve a clip
  pointer against the whole file base.

## 2. FigaTree -> FObj

```c
struct FigaTree {   // 0x14 bytes
    int type;       // bit 0 -> JOBJ_CLASSICAL_SCALE on every bound joint
    u32 flags;      // HSD_AObjSetFlags (AOBJ_LOOP etc.)
    f32 frames;     // AObj end_frame
    s8* nodes;      // per-joint track counts, 0xFF terminated
    FigaTrack* tracks;
};
struct FigaTrack {  // 12 bytes
    u16 length;     // byte length of the FObj stream
    u16 startframe;
    u8 obj_type;    // HSD_A_J_* channel
    u8 frac_value;  // low 5 bits: shift, high 3 bits: format
    u8 frac_slope;
    u8 _pad;
    u8* ad_head;    // data-relative
};
```

`nodes[i]` is the number of tracks for the i-th joint; `tracks` are consumed
in order.  `lbAnim_InitFrames` creates one `HSD_FObj` per track (no filtering);
`fn_8001E60C` is the blending variant used by `ftAnim_8006E28` and skips
channels 5/6/7, so do not use it for normal playback.

## 3. FObj stream playback (`fobj.c`)

- Stream layout: `[cmd] v0 wait0 v1 wait1 v2 ... waitN-1 vN`.
  `cmd` low nibble = `HSD_A_OP_*`, bits 4-6 = count-1, bit 7 = varint extension.
- Segment over `wait_i` interpolates `v_i -> v_i+1`:
  - `CON` holds `v_i`, `LIN` lerps, `SPL0/SPL/SLP` use `splGetHelmite`,
    `KEY` steps to `v_i`, and the final value `v_N` is held after the last
    wait.
- `bytes` are little-endian.  Fixed point: `denom = 1 << (frac & 0x1F)`,
  format `frac & 0xE0`: `0x00` = f32 LE, `0x20` = s16, `0x40` = u16,
  `0x60` = s8, `0x80` = u8.  Sign-extend before dividing; shifting a negative
  `s8` is UB in C, so mask to `u16` first and cast the result to `s16`.
- Playback semantics that matter:
  - `ReqAnim(frame)` resets the stream and sets `time = startframe + frame`.
  - The next `Interpret(rate=0)` fast-forwards to that time and emits the
    current value (this is how `HSD_JObjReqAnimAll` seeks).
  - `AOBJ_LOOP` wrap is handled by the caller in the port (`demo_anim`).
- A stateless "parse the stream once, evaluate at t" rewrite is tempting but
  was wrong in practice (end-of-stream garbage, KEY boundaries).  Port the
  state machine.

## 4. Node -> joint binding

- `ftAnim_8006F4C8` / `ftAnim_8006F7C8` walk `fp->parts[]`, skipping NULL
  part slots, and consume exactly one node per real part.  `ftParts` inserts
  **phantom slots** for `Fighter_804D6540` entries (hat/attach points), so the
  i-th node still drives the **i-th joint in HSD joint-traversal order**.
  Verified numerically: `JumpF` node 2 `TRAY = 5.0` equals joint 2's bind Y,
  and `Wait1` node 5 `TRAY = 2.6` equals joint 5's bind Y.
- Do **not** apply the skip list as a joint filter; it only shifts part
  indices for item attachment.
- `JObjUpdateFunc` writes **absolute** local values: `ROTX/Y/Z` -> rotation,
  `TRAX/Y/Z` -> position, `SCAX/Y/Z` -> scale (near-zero clamped to 1e-3),
  `NODE`/`BRANCH` -> `JOBJ_HIDDEN`.
- Runtime rate is `frame_speed_mul` from the action data; Wait uses `1.0`
  (`ftwaitanim.c`), i.e. 60 animation frames per second at 60 Hz.

## 5. Pose and re-skin

- Joints are evaluated exactly like `HSD_JObjMakeMatrix`: local `HSD_MtxSRT`
  with the parent's accumulated scale, then `world = parent.world * local`.
  All fighter joints carry `JOBJ_CLASSICAL_SCALE`.
- Envelope PObjs (`POBJ_ENVELOPE`): per group, rigid (first weight `>= 1`) is
  `M_j`; blended is `sum(w * M_j * E_j)`.  Non-root joints additionally
  concat the engine `right` matrix from `_HSD_mkEnvelopeModelNodeMtx`, which
  is **dynamic** under animation (inverse of the skeleton root's current
  world, etc.), not the bind-time shortcut.
- `POBJ_SKIN` rigid/shared: `current.world`, or the shared joint's `world`
  for `PNMTXIDX == 3`.
- The port keeps a raw copy of every vertex (6 floats: position, normal) plus
  a selector byte, and re-skins into `DemoModel::vertices` per evaluated
  frame.  The static display lists remain the bind-pose fast path.

## 6. Verification

- Differential test: a literal Python transcription of `fobj.c` was compared
  against `native/demo_aobj.c` for every track of Mario `Wait1`
  (111 tracks x 51 frames = 5661 samples): 0 mismatches, worst absolute
  difference 6.4e-7 (float rounding).  Repeat this when touching the player.
  The Python probe lives in `/tmp/opencode/anim/` (not committed).
- Expected poses: Kirby's Melee idle turns him around to look back
  (SmashWiki: "Hops a bit to look back"), so that is not a bug.

## 7. Debugging an animated pose that "looks wrong"

Checklist from the Bowser `Wait1` hair investigation (2026-09-10), all
verified on the retail Rev 2 `PlKpNr.dat`:

1. **Does the clip actually move that piece?** `--dump-clip Wait1` and filter
   by joint/type. Bowser's mohawk joints (42, 46) have *constant* rotations
   (`3.141`, `1.047`); only the body/neck moves. The bind-pose (`--view`) and
   animated pose therefore differ mainly by the hunched spine.
2. **NODE/BRANCH tracks?** Bowser's `Wait1` has none (types 11/12 absent), so
   `hidden_dyn` visibility is not involved.
3. **Node -> joint mapping.** `PlCo.dat`'s `ftPartsTable[kind].parts_num` is
   76 for Bowser and the model has 76 joints; `flags_b0/b5` (skip bones) are
   only set by runtime part swaps (`ftParts_800753D4`), so the port's
   node i -> joint i mapping is valid for normal play.
4. **Envelope `right` matrix.** Disabling it changed Bowser's frame 15 render
   by 0 RMSE, and its formula matches `_HSD_EnvelopeModelNodeMtx`; not the
   cause here.
5. **Playback timing.** `demo_fobj_req_anim` uses `time = startframe + frame`
   and `demo_anim_apply` resets to bind first; matches `HSD_JObjReqAnimAll`.
6. **Then it is probably the authored pose.** `Wait1` bows the spine so the
   shell rim and swept-back mohawk occupy the same screen area; a close
   three-quarter view reads as overlap. Compare against a console/Dolphin
   capture before "fixing" anything.

Useful flags added for this: `--no-grid` (removes the viewer floor grid from
screenshots) and `--dump-joints` (index, parent, flags, bind SRT).

## 8. Not yet ported

- Expression/visibility events.  Contrary to first guess, fighters do **not**
  get expressions from `SETBYTE`/`SETFLOAT` FObj channels: `jobj.c`'s
  `ufc_callbacks` list has no registration API in the decomp.  Expressions come
  from action code calling `ftParts_80074B0C`/`ftParts_80074A4C` and the
  per-kind `ftData_UnkIntBoolFunc0.model_events` table.  Port that path.
- `HSD_A_J_PATH` (spline joint attachment), IK (`resolveIKJoint1/2`).
- Material animation (`HSD_MatAnimJoint`), shape sets (`POBJ_SHAPEANIM` data).
- Animation blending (`x8A4_animBlendFrames`), per-action rate tables.

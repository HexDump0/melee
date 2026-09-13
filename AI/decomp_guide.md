# 📖 Melee Decompilation Technical Guide

This guide compiles essential knowledge about the PowerPC 750 (Gekko) architecture, CodeWarrior MWCC compiler behaviors, and Super Smash Bros. Melee engine idioms.

---

## 1. Architecture: Gekko (PowerPC 750CXe @ 486 MHz)

### Register Conventions
* **`r0`**: Volatile register, often used as scratch or to hold return addresses (`mflr r0 / stw r0, 4(r1)`).
* **`r1`**: Stack Pointer (SP). Grows downward. Must remain 8-byte aligned.
* **`r2`**: Small Data Area 2 (SDA2) base. Points to read-only constants and float literals (`.sdata2`).
* **`r3` - `r10`**: Argument registers & return values:
  * `r3`: 1st argument and integer/pointer return value.
  * `r4` - `r10`: Remaining integer/pointer arguments.
  * All volatile (caller-saved).
* **`r11` - `r12`**: Volatile scratch registers (often used in trampolines and jump tables).
* **`r13`**: Small Data Area (SDA) base. Points to small read/write global data (`.sdata`).
* **`r14` - `r31`**: Non-volatile (callee-saved) registers. Allocated backwards by MWCC (from `r31` down to `r14`).
* **`f0`**: Volatile floating-point scratch register.
* **`f1` - `f8`**: Floating-point argument registers:
  * `f1`: 1st argument and return float/double.
  * `f2` - `f8`: Subsequent float arguments.
  * All volatile.
* **`f9` - `f13`**: Volatile float scratch registers.
* **`f14` - `f31`**: Non-volatile (callee-saved) float registers (allocated `f31` down to `f14`).
* **`lr`**: Link Register (holds return address for `blr`).
* **`ctr`**: Count Register (loop counter and indirect call destination `bctrl`).

---

## 2. CodeWarrior Compiler (MWCC 1.2.5n) Quirks

Melee was compiled primarily using Metrowerks CodeWarrior for GameCube **1.2.5n** with flags `-O4,p -nodefaults -fp hard -Cpp_exceptions off -inline auto`.

### Register Allocation Strategy
* **Top-down assignment**: MWCC assigns local variables to non-volatile registers starting from `r31` downward (`r31`, `r30`, `r29`...).
* If a register allocation does not match:
  * **Declaration order**: In C, declare variables in the order they are first used or initialized.
  * **Temporary variables**: Introducing or eliminating a temporary variable (e.g. `Fighter* fp = GET_FIGHTER(gobj);` vs repeated `GET_FIGHTER(gobj)`) changes register pressure and lifetime intervals.
  * **Scope nesting**: Putting code inside inner blocks `{ ... }` can free up registers earlier.

### Floating-Point Literals & Small Data Areas
* Constant floats like `0.0f`, `1.0f`, `-1.0f` are placed in `.sdata2` and referenced via `r2`:
  ```asm
  lfs f1, -0x7FA0(r2)   # loads a float constant from sdata2
  ```
* The exact offset depends on the deduplication and file compilation order. If your float loads from a different SDA offset, check what other floats are referenced in the file.
* Beware of single (`float`, `1.0f`) vs double (`double`, `1.0`):
  * `1.0f` produces `lfs` (load float single).
  * `1.0` produces `lfd` (load float double) and consumes 8 bytes in `.rodata` or `.sdata2`.

### Inline Functions
* Small functions (especially accessors or math helpers) were frequently written as `inline` in header files.
* If you see repeated sequences of instructions (e.g. vector normalization, matrix multiplication, entity lookup), look for an existing inline helper in headers like `lbvector.h`, `jobj.h`, or `fighter.h`.

### Branching & Boolean Expressions
* **Order of conditions**:
  ```c
  if (a && b) // Emits sequential conditional branches (beq, bne)
  ```
* **Bitwise vs Logical**:
  Developers sometimes wrote `if (a & b)` instead of `if (a && b)` for flags.
* **Ternary vs If/Else**:
  ```c
  x = cond ? a : b;
  ```
  MWCC sometimes compiles ternaries with branchless instructions (`subfe`, `addze`) or specific branch patterns.

### Struct Member Ordering & Alignment
* Melee structs are generally aligned to 4 bytes (or 8 bytes if they contain `f64` / `u64`).
* Always check the offset in the assembly:
  ```asm
  lwz r0, 0x21BC(r31)
  ```
  This indicates an access to offset `0x21BC` from the base struct in `r31`. Compare this with struct definitions in `src/melee/ft/fighter.h` or relevant headers.

---

## 3. Common Melee Engine Concepts

### Game Objects (`HSD_GObj`)
The central entity container for players, items, effects, and camera:
```c
struct HSD_GObj {
    u16 classifier;
    s8 p_link;
    s8 gx_link;
    u8 p_priority;
    u8 render_priority;
    u8 obj_type;
    u8 flags;
    HSD_GObj* next;
    HSD_GObj* prev;
    HSD_GObj* next_gx;
    HSD_GObj* prev_gx;
    HSD_GObjProc* proc;
    void (*render_cb)(HSD_GObj* gobj, int code);
    u64 gxlink_prios;
    void* user_data;     // Points to Fighter, Item, etc.
};
```
Helper macros:
* `GET_FIGHTER(gobj)` -> `((Fighter*) (gobj)->user_data)`
* `GET_ITEM(gobj)` -> `((Item*) (gobj)->user_data)`

### Fighter Structure (`Fighter` / `fp`)
Located at `src/melee/ft/fighter.h`. Contains:
* Player index, character kind (`ftKind`).
* Position, velocity (`fp->self_vel`, `fp->kb_vel`).
* State callbacks (`fp->accessory4_cb`, `fp->death2_cb`, etc.).
* Collision data (`fp->coll_data`).
* Fighter attributes (`fp->x110_attr`).

### SysDolphin (HSD)
Melee's graphics engine:
* `HSD_JObj`: Joint object (skeletal hierarchies, bones).
* `HSD_DObj`: Display object (polygons, meshes).
* `HSD_MObj`: Material object (textures, lighting properties).
* `HSD_AObj`: Animation object (tracks, keyframes).
* `HSD_MemAlloc`: Memory allocator for engine objects.

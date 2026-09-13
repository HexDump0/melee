# 🥊 Fighter System (`ft/`) Learnings & Insights

The Fighter subsystem (`src/melee/ft/`) governs player characters, states, actions, physics, and animations.

---

## 1. Structure Overview

* **`HSD_GObj* gobj`**: The outer Game Object.
* **`Fighter* fp = GET_FIGHTER(gobj)`**: The inner character state struct.
* **`Fighter_GObj*`**: Typed wrapper around `HSD_GObj` where `user_data` is known to be `Fighter*`.

### Common Pointer Casts
```c
Fighter* fp = GET_FIGHTER(gobj);
// In older or lower-level files, you may also see:
Fighter* fp = gobj->user_data;
```
Always prefer the canonical `GET_FIGHTER(gobj)` macro unless decompilation requires a raw access pattern.

---

## 2. Action States & Callbacks

Fighter actions (e.g. `SpecialHi`, `Walk`, `Jump`) typically register 5 callbacks:
1. **Anim**: Animation frame updates (`cb.anim`).
2. **IASA**: Interruptible As Soon As (input polling / state changes) (`cb.iasa`).
3. **Phys**: Physics updates (gravity, velocity, friction) (`cb.phys`).
4. **Coll**: Environmental collision detection (ledges, platforms, walls) (`cb.coll`).
5. **Accessory / GFX**: Particle effects or sound effects (`fp->accessory4_cb`).

---

## 3. Motion Vars & Character-Specific Data

* `fp->motion_vars`: A union of structs containing per-state temporary variables (timers, flags, speeds).
* When decompiling character-specific files (`src/melee/ft/chara/ft<Name>/...`), inspect the character's header (e.g., `ftFox.h`, `ftMewtwo.h`) to find the corresponding `motion_vars` typedef rather than doing raw pointer offset arithmetic.

---

## 4. Input Handling

* Player controller inputs are stored in `fp->input`:
  * `fp->input.held_inputs`: Buttons currently held.
  * `fp->input.down_inputs`: Buttons pressed on this exact frame.
  * `fp->input.lstick_x`, `fp->input.lstick_y`: Analog stick values normalized between `-1.0f` and `1.0f`.

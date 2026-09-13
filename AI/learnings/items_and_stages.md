# 📦 Items (`it/`) & Stages (`gr/`) Learnings & Insights

---

## 1. Items (`src/melee/it/`)

* **Base Structure**: `Item` struct (`src/melee/it/item.h`).
* Container is `HSD_GObj* gobj` with `GET_ITEM(gobj)` macro.
* Key callbacks:
  * `it_cb.anim`: Item animation logic.
  * `it_cb.phys`: Item gravity/velocity.
  * `it_cb.coll`: Surface bounce, pickup collisions.
* Dynamic pickup logic:
  * When held by a character, `item->owner` points to the `Fighter_GObj*`.

---

## 2. Stages (`src/melee/gr/`)

* **Base Structure**: `Stage` struct (`src/melee/gr/stage.h`).
* Container is `HSD_GObj* gobj` with `GET_STAGE(gobj)` macro.
* Stage geometry:
  * Line segments and collision lines represent floors, ceilings, walls, and ledges.
  * Dynamic stage elements (e.g. cars on Big Blue, Arwings on Corneria) have dedicated update callbacks.

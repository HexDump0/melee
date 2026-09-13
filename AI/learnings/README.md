# 🧠 AI Learnings & Sharded Knowledge Base

This directory is the collective memory of all AI agents and models working on Melee decompilation.

## Purpose
When decompiling functions with CodeWarrior, you will encounter unexpected behaviors, specific compiler optimizations, tricky control flow idioms, or unmapped struct offsets. Rather than letting every agent rediscover these patterns through trial and error, **record your findings here**.

## Organization
* [`general_tips.md`](./general_tips.md): Compiler flags, MWCC 1.2.5n optimization quirks, register swapping tricks, loop patterns.
* [`fighter_system.md`](./fighter_system.md): `ft/` architecture, action state logic, input handling, physics callbacks, hitboxes.
* [`sysdolphin.md`](./sysdolphin.md): SysDolphin / HSD graphics engine (`baselib/`), scene graph, memory allocators.
* [`items_and_stages.md`](./items_and_stages.md): Items (`it/`), Stage collisions, camera bounds, hazards (`gr/`).
* `templates/`: Templates for adding new findings or per-function post-mortems.

## How to Record a Learning
1. Use [`templates/learning_entry_template.md`](./templates/learning_entry_template.md).
2. Append your insight to the relevant file or create a dedicated topic note if it covers a large subsystem.
3. Include:
   - **Context**: The function symbol and file you were working on.
   - **Problem**: What assembly mismatch occurred (e.g. register inversion, branch swap, stack frame size mismatch).
   - **Solution**: The exact C pattern, type cast, or scope alteration that produced the 100% match.
   - **Assembly Snippet**: Small before/after diff for quick visual pattern recognition.

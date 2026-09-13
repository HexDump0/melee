# 💡 General Learnings & MWCC Compiler Tips

This document tracks general compiler quirks, register tricks, and idioms observed across Melee decompilation.

---

## 1. Register Swapping & Ordering

### Problem: Registers Inverted (e.g. `r30` and `r31` swapped)
* **Symptom**: Function body is identical, but variable $A$ uses `r31` instead of `r30` and variable $B$ uses `r30` instead of `r31`.
* **Fix**:
  1. Swap the declaration or first-initialization order of the variables.
  2. If they are initialized in the same statement (e.g., arguments passed to another call), evaluate the order in which arguments are evaluated (in C, argument evaluation order is unspecified, but MWCC evaluates them right-to-left or left-to-right depending on calling context).
  3. Change the scope: wrap one variable's usage in a localized `{ ... }` scope.

---

## 2. Stack Frame & Spills

### Problem: Stack Frame Too Big (e.g., `stwu r1, -0x28(r1)` instead of `-0x18(r1)`)
* **Symptom**: MWCC reserved more stack space than the original code.
* **Fix**:
  1. Check for unused or lingering local variables.
  2. Avoid passing structs by value; pass by pointer.
  3. Inlined functions: An inlined function might declare local variables that reserve stack space even if unused.
  4. Non-volatile register usage: If you accidentally touch an extra callee-saved register (`r29` through `r14`), MWCC will save and restore it on the stack, expanding the stack frame by 4 bytes per register (plus 8-byte alignment padding).

---

## 3. Conditionals & Branch Order

### Problem: Branches Inverted (`beq` vs `bne`)
* **Fix**:
  - Invert the `if` condition: `if (!cond)` instead of `if (cond)`.
  - Check for short-circuit logic:
    ```c
    // Might produce different branches:
    if (a && b) { ... }
    
    // Versus:
    if (a) {
        if (b) { ... }
    }
    ```

### Problem: Branchless Logic
* CodeWarrior will generate `subfe`, `addze`, or `cntlzw` for certain boolean conversions:
  ```c
  // Can trigger branchless evaluation:
  bool result = (val != 0);
  ```

---

## 4. Floats & `.sdata2` Ordering

### Problem: Incorrect Float Constant Offset
* If the assembly loads a float from `@offset(r2)`, but your code emits a different offset:
  - Check if the literal is a `float` (`1.0f`) or a `double` (`1.0`). Double uses 8 bytes.
  - Check if negative floats are written as `-1.0f` vs `-(1.0f)`.
  - The order of float literals within a translation unit determines their sequential layout in `.sdata2`. Moving a helper function above or below another can change float offsets.

---

## 5. Loops (`for` vs `while` vs `do-while`)
* In PowerPC, loops are frequently compiled into `do-while` style with an initial jump or check:
  ```asm
      cmpwi r3, 0
      ble .L_exit
  .L_loop:
      ...
      bdnz .L_loop  # or subic. / bne
  .L_exit:
  ```
* If you see `bdnz`, the loop was likely a countdown loop using `for (i = count; i > 0; i--)` or `while (count--)`.

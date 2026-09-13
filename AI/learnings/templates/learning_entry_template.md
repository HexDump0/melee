# Learning Entry: [Topic / Function Symbol]

**Date**: YYYY-MM-DD  
**Agent / Model**: [Agent Name or Model Version]  
**Target File**: `src/melee/...`  
**Symbol(s)**: `[symbol_name]`  

---

### 1. Problem Description
Describe what mismatch occurred between your compiled C code and the original assembly.
* E.g.: "MWCC assigned r30 where r31 was expected."
* E.g.: "Branch condition inverted (beq vs bne)."
* E.g.: "Stack frame allocated 0x28 bytes instead of 0x18."

---

### 2. Root Cause
Why did MWCC generate that code?
* E.g.: "Variable `fp` was declared before `gobj`, causing the register allocator to prioritize `fp`."
* E.g.: "Missing `inline` keyword or header order issue."

---

### 3. Solution / Matching Code Pattern
Show the exact C code structure that produced the 100% match:

```c
// Paste matching C code here
```

---

### 4. Key Takeaway for Future Agents
One or two sentences summarizing the heuristic or lesson learned.

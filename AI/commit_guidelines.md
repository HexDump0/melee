# 📝 AI Agent Git Commit Guidelines

To keep the git history clean, maintainable, and identical to upstream human contributors, **every agent must adhere to these commit standards**.

---

## 🎯 The Golden Rule: Atomic "Human" Commits

> **Commit early, commit atomically, commit like a human.**

* **Never batch multiple functions into one mega-commit**:
  When you finish decompiling a function or translation unit and verify that it matches 100%, **commit it immediately**.
* **Do not leave uncommitted changes sitting in the working tree**:
  Leaving dirty files risks other agents accidentally modifying, overwriting, or staging unrelated work.
* **Write commits that look like human software engineers**:
  Upstream Melee contributors write clean, concise, imperative commit messages. Avoid verbose, robotic, or AI-generated boilerplate.

---

## ✍️ Commit Title Conventions

Use the exact standard prefixes established by upstream `doldecomp/melee`:

| Pattern | When to Use | Examples |
| :--- | :--- | :--- |
| `Match <Symbol>` | A single function was decompiled to 100% match | `Match ftMt_SpecialHi_CreateGFX`<br>`Match mnDiagram_InputProc` |
| `Match and link <File/TU>` | A complete translation unit was matched & linked | `Match and link lbsnap`<br>`Match and link mndiagram`<br>`Match and link ifstock` |
| `Improve <Symbol> matching` | Function match percentage was improved | `Improve JPEG coefficient encoder matching`<br>`Improve lbSnap_8001DA5C` |
| `Clean up <Component>` | Formatting, renaming, or refactoring | `Clean up camera headers`<br>`Assorted naming and cleanup` |
| `Document <Feature>` | Adding comments, docstrings, or types | `Document fighter and ground alloc bug` |
| `Fix <Issue>` | Correcting types, prototypes, or macros | `Fix Item_8026AD20 argument` |

---

## 🚫 What NOT to Do (Anti-Patterns)

### ❌ Robotic / AI Boilerplate Commit Messages
```text
# BAD:
[AI-AGENT-GEMINI] Decompiled ftMt_SpecialHi_CreateGFX with 100% objdiff match
feat(ftMewtwo): autonomous decompilation completed for 3 functions
automated commit: progress 98.68% -> 98.69%
WIP: some functions decompiled

# GOOD:
Match ftMt_SpecialHi_CreateGFX
```

### ❌ Mega-Commits Combining Unrelated Work
```text
# BAD:
git commit -m "Matched SpecialHi, updated camera structs, and cleaned up bigblue"

# GOOD:
Commit 1: Match ftMt_SpecialHi_CreateGFX
Commit 2: Clean up camera structs
Commit 3: Match grBigBlue_Init
```

### ❌ Staging Everything Recklessly (`git add .` or `git add -A`)
Never run `git add .` indiscriminately! You might accidentally stage:
* Unrelated work in other files
* Scratch files or dumps
* Internal notes or logs

Always stage **only the specific files** modified for that function:
```sh
git add src/melee/ft/chara/ftMewtwo/ftMewtwo_SpecialHi.c src/melee/ft/chara/ftMewtwo/ftMewtwo_SpecialHi.h
```

---

## 📋 The Pre-Commit Checklist for Agents

Before running `git commit`:

1. **Verify 100% Byte Match with `ninja`**:
   ```sh
   ninja
   ```
   Output must say:
   ```text
   build/GALE01/main.dol: OK
   ```
2. **Run the Style Checker**:
   ```sh
   python3 tools/check/main.py src/path/to/file.c
   ```
3. **Check What Changed**:
   ```sh
   git status
   ```
4. **Stage Only Your Target Files**:
   ```sh
   git add src/path/to/modified.c src/path/to/modified.h
   # If you fully linked a new translation unit:
   git add configure.py config/GALE01/splits.txt
   ```
5. **Inspect the Staged Diff**:
   ```sh
   git diff --cached
   ```
   Ensure no leftover debug prints, temporary comments, or accidental whitespace changes are included.
6. **Commit with a Clean Human Message**:
   ```sh
   git commit -m "Match ftMt_SpecialHi_CreateGFX"
   ```
7. **Mark the Task Completed in Claims Register**:
   ```sh
   python3 AI/scripts/claim.py complete ftMt_SpecialHi_CreateGFX --notes "100% matched, committed"
   ```

---

## 💡 Optional Commit Body

If the decompilation required a non-obvious solution (e.g. an obscure compiler bug workaround, unusual cast, or struct discovery), add a brief 1–2 sentence explanation in the commit body:

```text
Match ftMt_SpecialHi_CreateGFX

Fix register inversion between r30 and r31 by scoping the
Fighter pointer assignment within an inner block.
```

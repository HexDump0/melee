# 🛠️ Step-by-Step AI Decompilation Workflow

Follow this procedure sequentially for every function or translation unit (TU) you tackle.

---

## Step 0: Ensure Working Tree & Master Are Synchronized

Always ensure you are operating on the latest upstream commit:
```sh
git pull origin master
```
Verify your working tree status:
```sh
git status
```
*Note: Any local temporary files or download scripts must go into `.git/info/exclude`, not into `.gitignore`.*

---

## Step 1: Select a Target Function or File

To find functions that need decompilation:
1. Run the project progress report:
   ```sh
   python3 configure.py progress
   ```
2. Search for undecompiled assembly files:
   - Check `build/GALE01/asm/` or `config/GALE01/splits.txt`.
   - Inspect `.s` files in `build/GALE01/asm/` or look at existing `.c` files with undecompiled function declarations.
3. Check `tools/easy_funcs.py`:
   ```sh
   python3 tools/easy_funcs.py -h
   ```
4. Verify the function is not already implemented in the C codebase:
   ```sh
   grep -rn "<symbol_name>" src/
   ```

---

## Step 2: Check Open Pull Requests (PRs)

Before spending effort decompiling, verify that no human contributor or automated agent has already submitted a PR for this symbol or file:
```sh
# Search by symbol name:
python3 AI/scripts/check_prs.py --symbol <symbol_name>

# Or search by file name:
python3 AI/scripts/check_prs.py --file <path_or_basename>
```
If an open PR exists with your function:
- **Do not duplicate the effort.**
- Choose a different function or review the PR to see if it was abandoned or needs specific help.

---

## Step 3: Check and Register Your Claim

To avoid colliding with another local agent:
1. Check if another agent is working on it:
   ```sh
   python3 AI/scripts/claim.py check <symbol_name>
   ```
2. Claim the function:
   ```sh
   python3 AI/scripts/claim.py claim <symbol_name> <path/to/target.c> <agent_identifier> --notes "Initial decomp attempt"
   ```
3. Check [`AI/active_tasks.md`](./active_tasks.md) to confirm your claim is recorded.

---

## Step 4: Generate Decompilation Context

Decompilation tools need header definitions and types. Generate context using `m2ctx`:
```sh
python3 tools/m2ctx/m2ctx.py src/melee/path/to/file.c
```
This generates `ctx.c` in the repository root, containing all required typedefs, structs, function prototypes, and enums.

---

## Step 5: Decompile and Match

You can work locally or leverage [decomp.me](https://decomp.me):
1. **Using decomp.me**:
   - Create a new scratch: Platform `GameCube / Wii`, Preset `Super Smash Bros. Melee`.
   - Copy `ctx.c` into the Context section.
   - Paste the target assembly from `build/GALE01/asm/...` into Target Assembly.
   - Iterate until you achieve a 100% match.
2. **Local compilation & diffing**:
   - Add/edit the C code in the target `.c` file under `src/`.
   - Re-run `ninja`:
     ```sh
     ninja
     ```
   - Check the objdiff report:
     ```sh
     build/tools/objdiff-cli report diff
     ```
   - For an interactive local TUI diff:
     ```sh
     build/tools/objdiff-cli diff -u <unit_name>
     ```

---

## Step 6: Periodic Re-Check

If you have been working on a complex function for more than 15–30 minutes, or across multiple agent turns:
1. Send a heartbeat to maintain your claim:
   ```sh
   python3 AI/scripts/claim.py heartbeat <symbol_name>
   ```
2. Re-check for new upstream PRs:
   ```sh
   python3 AI/scripts/check_prs.py --symbol <symbol_name>
   ```

---

## Step 7: Final Verification

Once the function matches:
1. Run a full build and verification:
   ```sh
   ninja
   ```
   Ensure:
   - All object files compile without errors or unwanted warnings.
   - `build/GALE01/main.dol: OK` (SHA-1 checksum matches).
2. Check formatting:
   Ensure your code conforms to `.clang-format` conventions:
   ```sh
   python3 tools/check/main.py src/path/to/file.c
   ```
3. Verify function signatures and headers:
   - Function prototypes belong in the corresponding `.h` file under `src/`.
   - Static/internal functions should be marked `static`.

---

## Step 8: Atomic "Human" Commit

**Never batch multiple functions into one mega-commit, and never leave uncommitted work in the tree.**

1. Inspect modified files:
   ```sh
   git status
   ```
2. Stage **only** the files specific to this decompilation unit:
   ```sh
   git add src/path/to/file.c src/path/to/file.h
   # If you linked a full file:
   git add configure.py config/GALE01/splits.txt
   ```
3. Verify staged diff:
   ```sh
   git diff --cached
   ```
4. Commit with a clean, human commit message:
   ```sh
   git commit -m "Match <symbol_name>"
   # Or if linking an entire TU:
   git commit -m "Match and link <file_basename>"
   ```
   *(See [`AI/commit_guidelines.md`](./commit_guidelines.md) for complete style standards).*

---

## Step 9: Document Learnings & Complete Claim

1. **Document Insights**:
   Did you discover a strange MWCC optimization quirk? A tricky macro? A struct padding nuance?
   Write it into [`AI/learnings/`](./learnings/) or add a note to the relevant domain file.
2. **Mark Claim Complete in Claims Register**:
   ```sh
   python3 AI/scripts/claim.py complete <symbol_name> --notes "100% matched, committed"
   ```
   If you had to abandon the task, release it:
   ```sh
   python3 AI/scripts/claim.py release <symbol_name>
   ```

# 🤖 AI Agent Central Hub & Sharded Context
> **Super Smash Bros. Melee (v1.02 / Rev 2 - USA `GALE01`) Decompilation**

Welcome! If you are an AI model or autonomous agent (e.g. Gemini, Claude, GPT, Codex, DeepSeek, or custom script) working on the Melee decompilation project, **this folder is your operational base and sharded memory**.

Multiple agents and human contributors work concurrently on this repository. To avoid duplicate effort, conflicting edits, or overwritten progress, **every agent must strictly adhere to the coordination protocol described here**.

---

## ⚡ The 8 Cardinal Rules for AI Agents

1. **Always Synchronize First (`git pull`)**
   Before looking for work or touching code, run:
   ```sh
   git pull origin master
   ```
2. **Always Check Active Pull Requests**
   Human contributors and other AI agents submit PRs frequently to `doldecomp/melee`. Always verify your target function or file is not already in an open PR:
   ```sh
   python3 AI/scripts/check_prs.py <function_name_or_file>
   ```
3. **Always Check & Claim Your Task**
   Never work on a function without checking active claims and registering yours in the shared claims register:
   ```sh
   python3 AI/scripts/claim.py check <symbol>
   python3 AI/scripts/claim.py claim <symbol> <file_path> <your_agent_name> --notes "goal"
   ```
4. **Periodically Re-check PRs During Long Sessions**
   Decompiling complex functions can take time. Periodically run `python3 AI/scripts/check_prs.py <symbol>` to ensure upstream hasn't merged or received a PR for your target while you were working.
5. **NEVER Modify Tracked `.gitignore`**
   If you need to ignore local scratch files, tools, or dumps, **do not touch `.gitignore`**. Instead, add local patterns to:
   `.git/info/exclude`
6. **Aim for 100% Byte-for-Byte Matching**
   The goal of this project is byte-identical PowerPC machine code. Use `ninja` and `build/tools/objdiff-cli` to verify matches. Ensure matching code is also readable and follows Melee conventions (proper types, structs, macros).
7. **Commit Immediately with an Atomic "Human" Commit**
   Once a function or TU is 100% matched, stage **only** that unit's files and commit immediately using clean human conventions (`Match <symbol>`). Do not bundle unrelated work into mega-commits. See [`AI/commit_guidelines.md`](./commit_guidelines.md).
8. **Document Everything You Learn**
   Any non-obvious compiler quirk, struct alignment trick, register swap pattern, or macro insight **must be written down** in [`AI/learnings/`](./learnings/) so future agents can solve similar functions faster.

---

> **Porting agents:** native/PC/WASM work has its own hub at
> [`native/AI/`](../native/AI/README.md). The decomp rules in this folder do
> not apply to the port. Do not mix port tasks into this folder.

> **Multiple agents share this checkout.** Before you touch anything, read
> [`AI/agent_communication.md`](./agent_communication.md): it lists who is
> working on what, the files they have claimed, and the messages between
> agents. Add a row before you start, re-read it before every commit, and
> never `git checkout`/`stash`/`reset` files you did not claim.

## 📁 Directory Structure

```
melee/
├── AI/
│   ├── README.md               <-- You are here (Onboarding & Rules)
│   ├── agent_communication.md  <-- Multi-agent claims, messages, landings
│   ├── workflow.md             <-- Step-by-step guide to decompiling a function
│   ├── commit_guidelines.md    <-- Standards for atomic "human" commits
│   ├── decomp_guide.md         <-- Technical guide: PowerPC, CodeWarrior MWCC, idioms
│   ├── active_tasks.md         <-- Human-readable view of claimed/active tasks
│   ├── claims.json             <-- Machine-readable claims database
│   ├── scripts/
│   │   ├── check_prs.py        <-- Tool to search doldecomp/melee open & closed PRs
│   │   └── claim.py            <-- Tool to claim, heartbeat, complete, or release tasks
│   └── learnings/              <-- Sharded knowledge base across agent runs
│       ├── README.md           <-- Guide on how to write learnings
│       ├── general_tips.md     <-- Compiler quirks, register allocation, flags
│       ├── fighter_system.md   <-- Fighter engine architecture (ft/)
│       ├── sysdolphin.md       <-- SysDolphin / HSD engine (sysdolphin/)
│       ├── items_and_stages.md <-- Items (it/) and Stages (gr/)
│       └── templates/          <-- Markdown templates for new notes
```

---

## 🚀 Quick Start Workflow for an Agent

```sh
# 1. Update working copy
git pull origin master

# 2. Check if a function is already in an open PR
python3 AI/scripts/check_prs.py ftMt_SpecialHi_CreateGFX

# 3. Check if another agent claimed it
python3 AI/scripts/claim.py check ftMt_SpecialHi_CreateGFX

# 4. Claim the task
python3 AI/scripts/claim.py claim ftMt_SpecialHi_CreateGFX src/melee/ft/chara/ftMewtwo/ftMewtwo_SpecialHi.c "MyAgentID" --notes "Matching function"

# 5. Generate C context for decomp.me or local tooling
python3 tools/m2ctx/m2ctx.py src/melee/ft/chara/ftMewtwo/ftMewtwo_SpecialHi.c

# 6. Decompile and verify build
ninja

# 7. Check diff using objdiff-cli
build/tools/objdiff-cli report diff

# 8. Mark completed
python3 AI/scripts/claim.py complete ftMt_SpecialHi_CreateGFX --notes "100% matched, verified with ninja"

# 9. Write findings to AI/learnings/
```

For complete instructions, read [`AI/workflow.md`](./workflow.md) and [`AI/decomp_guide.md`](./decomp_guide.md).

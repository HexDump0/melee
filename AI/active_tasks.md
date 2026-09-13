# 📋 Active AI Agent Task Claims

This file is automatically updated by `AI/scripts/claim.py`.
Agents must check and claim their target function before starting decompilation.

| Symbol / Function | Target File | Claimed By (Agent) | Status | Claimed At | Last Active | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `hsd_803B3408` | `src/sysdolphin/baselib/hsd_3B34.c` | **MuseSpark** | 🟡 IN_PROGRESS | 2026-09-07T06:17:13 | 2026-09-07T07:57:12 | Round 2 brute force scoping/shifts |

## How to Claim or Release
```sh
# Check if a symbol is free:
python3 AI/scripts/claim.py check <symbol>

# Claim a function:
python3 AI/scripts/claim.py claim <symbol> <file_path> <agent_id> --notes "Matching func"

# Mark complete:
python3 AI/scripts/claim.py complete <symbol> --notes "100% matched"

# Release/abandon:
python3 AI/scripts/claim.py release <symbol>
```

#!/usr/bin/env python3
"""
Doldecomp Melee - Find Work Helper
Inspects build/GALE01/report.json and cross-references active PRs and AI claims
to give agents ready-to-tackle targets.

Usage:
    python3 AI/scripts/find_work.py
"""

import json
import os
import sys

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO_DIR = os.path.dirname(BASE_DIR)
REPORT_PATH = os.path.join(REPO_DIR, "build", "GALE01", "report.json")
CLAIMS_PATH = os.path.join(BASE_DIR, "claims.json")


def load_json(path):
    if not os.path.exists(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        print(f"Warning: Could not read {path}: {e}", file=sys.stderr)
        return None


def main():
    report = load_json(REPORT_PATH)
    if not report:
        print(f"Report file not found at {REPORT_PATH}. Run 'ninja' first.")
        sys.exit(1)

    claims_data = load_json(CLAIMS_PATH) or {}
    active_claims = {
        sym: c for sym, c in claims_data.get("claims", {}).items()
        if c.get("status") == "in_progress"
    }

    units = report.get("units", [])
    unlinked_units = [u for u in units if not u.get("metadata", {}).get("complete", False)]

    print("=" * 80)
    print("🏆 MELEE DECOMPILATION: ACTIVE TARGET OVERVIEW")
    print("=" * 80)
    print(f"Total Translation Units: {len(units)} | Unlinked: {len(unlinked_units)}")
    print("-" * 80)

    print("\n📁 Unlinked Translation Units:")
    for u in unlinked_units:
        name = u.get("name", "")
        funcs = u.get("functions", [])
        matched_funcs = sum(1 for f in funcs if float(f.get("fuzzy_match_percent", 0)) >= 100.0)
        total_funcs = len(funcs)
        pct = (matched_funcs / total_funcs * 100) if total_funcs else 100.0
        print(f"  • {name:<35} -> {matched_funcs}/{total_funcs} funcs matched ({pct:.1f}%)")

    unmatched_funcs = []
    for u in units:
        for f in u.get("functions", []):
            fuzzy = float(f.get("fuzzy_match_percent", 0.0))
            if fuzzy < 100.0:
                name = f.get("name", "")
                size = int(f.get("size", 0))
                unmatched_funcs.append({
                    "name": name,
                    "unit": u.get("name", ""),
                    "size": size,
                    "fuzzy": fuzzy,
                    "claimed": name in active_claims,
                    "agent": active_claims.get(name, {}).get("agent", "") if name in active_claims else None,
                })

    unmatched_funcs.sort(key=lambda x: x["size"])

    print(f"\n🎯 Remaining Unmatched Functions ({len(unmatched_funcs)} total):")
    print(f"{'SIZE':<8} {'MATCH %':<10} {'STATUS':<15} {'FUNCTION SYMBOL':<30} {'UNIT'}")
    print("-" * 95)
    for f in unmatched_funcs:
        status_str = f"LOCKED ({f['agent']})" if f["claimed"] else "AVAILABLE"
        print(f"{f['size']:<8} {f['fuzzy']:<10.1f} {status_str:<15} {f['name']:<30} {f['unit']}")

    print("\n💡 Suggested Next Step for an Agent:")
    available = [f for f in unmatched_funcs if not f["claimed"]]
    if available:
        pick = available[0]
        print(f"  1. Check PRs: python3 AI/scripts/check_prs.py --symbol {pick['name']}")
        print(f"  2. Claim:     python3 AI/scripts/claim.py claim {pick['name']} <file_path> <agent_id>")
    else:
        print("  All remaining functions are currently claimed or matched!")
    print("=" * 80)


if __name__ == "__main__":
    main()

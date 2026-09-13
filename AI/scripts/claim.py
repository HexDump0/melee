#!/usr/bin/env python3
"""
Doldecomp Melee - Multi-Agent Task Claiming & Coordination Tool
Prevents multiple autonomous agents / models from duplicating work or conflicting.

Usage:
    python3 AI/scripts/claim.py list                      # List all active claims
    python3 AI/scripts/claim.py check <symbol>            # Check if a symbol is claimed
    python3 AI/scripts/claim.py claim <symbol> <file> <agent> [--notes "info"]
    python3 AI/scripts/claim.py complete <symbol> [--notes "100% match"]
    python3 AI/scripts/claim.py release <symbol>
    python3 AI/scripts/claim.py heartbeat <symbol>
"""

import argparse
import datetime
import json
import os
import sys

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLAIMS_FILE = os.path.join(BASE_DIR, "claims.json")
ACTIVE_TASKS_MD = os.path.join(BASE_DIR, "active_tasks.md")


def load_claims():
    if not os.path.exists(CLAIMS_FILE):
        return {"version": 1, "claims": {}}
    try:
        with open(CLAIMS_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        print(f"Warning: Could not read {CLAIMS_FILE}: {e}", file=sys.stderr)
        return {"version": 1, "claims": {}}


def save_claims(data):
    with open(CLAIMS_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    sync_markdown(data)


def sync_markdown(data):
    lines = [
        "# 📋 Active AI Agent Task Claims",
        "",
        "This file is automatically updated by `AI/scripts/claim.py`.",
        "Agents must check and claim their target function before starting decompilation.",
        "",
        "| Symbol / Function | Target File | Claimed By (Agent) | Status | Claimed At | Last Active | Notes |",
        "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |",
    ]
    claims = data.get("claims", {})
    if not claims:
        lines.append("| *(None)* | - | - | - | - | - | No active tasks claimed |")
    else:
        for sym, c in sorted(claims.items(), key=lambda x: x[1].get("claimed_at", ""), reverse=True):
            status = c.get("status", "in_progress")
            status_badge = {
                "in_progress": "🟡 IN_PROGRESS",
                "completed": "🟢 COMPLETED",
                "released": "⚪ RELEASED",
                "blocked": "🔴 BLOCKED",
            }.get(status, status)
            lines.append(
                f"| `{sym}` | `{c.get('file', '')}` | **{c.get('agent', '')}** | {status_badge} | {c.get('claimed_at', '')[:19]} | {c.get('last_active', '')[:19]} | {c.get('notes', '')} |"
            )

    lines.append("")
    lines.append("## How to Claim or Release")
    lines.append("```sh")
    lines.append("# Check if a symbol is free:")
    lines.append("python3 AI/scripts/claim.py check <symbol>")
    lines.append("")
    lines.append("# Claim a function:")
    lines.append('python3 AI/scripts/claim.py claim <symbol> <file_path> <agent_id> --notes "Matching func"')
    lines.append("")
    lines.append("# Mark complete:")
    lines.append('python3 AI/scripts/claim.py complete <symbol> --notes "100% matched"')
    lines.append("")
    lines.append("# Release/abandon:")
    lines.append("python3 AI/scripts/claim.py release <symbol>")
    lines.append("```\n")

    with open(ACTIVE_TASKS_MD, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def now_iso():
    return datetime.datetime.now().astimezone().isoformat()


def cmd_claim(args):
    data = load_claims()
    claims = data.setdefault("claims", {})
    sym = args.symbol

    if sym in claims and claims[sym].get("status") == "in_progress":
        existing = claims[sym]
        if existing.get("agent") != args.agent:
            print(f"❌ ERROR: Symbol '{sym}' is already claimed by agent '{existing.get('agent')}' since {existing.get('claimed_at')}!")
            print(f"Notes: {existing.get('notes')}")
            sys.exit(1)
        else:
            print(f"ℹ️ You ({args.agent}) already hold the claim for '{sym}'. Updating heartbeat/notes.")

    claims[sym] = {
        "symbol": sym,
        "file": args.file,
        "agent": args.agent,
        "status": "in_progress",
        "claimed_at": claims.get(sym, {}).get("claimed_at", now_iso()),
        "last_active": now_iso(),
        "notes": args.notes or "",
    }
    save_claims(data)
    print(f"✅ Successfully claimed '{sym}' for agent '{args.agent}'.")
    print(f"   Target file: {args.file}")


def cmd_complete(args):
    data = load_claims()
    claims = data.setdefault("claims", {})
    sym = args.symbol
    if sym not in claims:
        print(f"⚠️ Warning: Symbol '{sym}' was not in claims registry, recording as completed.")
        claims[sym] = {
            "symbol": sym,
            "file": "",
            "agent": "unknown",
            "claimed_at": now_iso(),
        }
    claims[sym]["status"] = "completed"
    claims[sym]["last_active"] = now_iso()
    if args.notes:
        claims[sym]["notes"] = args.notes
    save_claims(data)
    print(f"🎉 Marked '{sym}' as COMPLETED!")


def cmd_release(args):
    data = load_claims()
    claims = data.setdefault("claims", {})
    sym = args.symbol
    if sym in claims:
        claims[sym]["status"] = "released"
        claims[sym]["last_active"] = now_iso()
        save_claims(data)
        print(f"🔓 Released claim on '{sym}'.")
    else:
        print(f"Symbol '{sym}' was not claimed.")


def cmd_heartbeat(args):
    data = load_claims()
    claims = data.setdefault("claims", {})
    sym = args.symbol
    if sym in claims:
        claims[sym]["last_active"] = now_iso()
        save_claims(data)
        print(f"💓 Heartbeat updated for '{sym}'.")
    else:
        print(f"Symbol '{sym}' is not claimed.")


def cmd_check(args):
    data = load_claims()
    claims = data.get("claims", {})
    sym = args.symbol
    if sym in claims and claims[sym].get("status") == "in_progress":
        c = claims[sym]
        print(f"🔒 CLAIMED: '{sym}' is currently IN PROGRESS by '{c.get('agent')}' (claimed: {c.get('claimed_at')})")
        print(f"   File:  {c.get('file')}")
        print(f"   Notes: {c.get('notes')}")
        sys.exit(1)
    else:
        print(f"🟢 AVAILABLE: '{sym}' is not actively claimed.")


def cmd_list(args):
    data = load_claims()
    claims = data.get("claims", {})
    if not claims:
        print("No active claims found.")
        return

    print(f"{'STATUS':<12} {'SYMBOL':<30} {'AGENT':<20} {'LAST ACTIVE':<20} {'FILE'}")
    print("-" * 105)
    for sym, c in claims.items():
        if not args.all and c.get("status") != "in_progress":
            continue
        status = c.get("status", "unknown")
        print(f"{status:<12} {sym:<30} {c.get('agent', ''):<20} {c.get('last_active', '')[:19]:<20} {c.get('file', '')}")


def main():
    parser = argparse.ArgumentParser(description="Multi-agent claim coordinator for Melee decomp")
    subparsers = parser.add_subparsers(dest="command", required=True)

    p_list = subparsers.add_parser("list", help="List claims")
    p_list.add_argument("--all", "-a", action="store_true", help="Show all claims including released/completed")

    p_check = subparsers.add_parser("check", help="Check if a symbol is claimed")
    p_check.add_argument("symbol", help="Symbol/function name")

    p_claim = subparsers.add_parser("claim", help="Claim a symbol")
    p_claim.add_argument("symbol", help="Symbol/function name")
    p_claim.add_argument("file", help="File path being edited")
    p_claim.add_argument("agent", help="Agent or model identifier")
    p_claim.add_argument("--notes", "-n", default="", help="Optional notes or plan")

    p_comp = subparsers.add_parser("complete", help="Mark a symbol as complete")
    p_comp.add_argument("symbol", help="Symbol/function name")
    p_comp.add_argument("--notes", "-n", default="", help="Optional notes")

    p_rel = subparsers.add_parser("release", help="Release a symbol claim")
    p_rel.add_argument("symbol", help="Symbol/function name")

    p_hb = subparsers.add_parser("heartbeat", help="Send heartbeat for a claim")
    p_hb.add_argument("symbol", help="Symbol/function name")

    args = parser.parse_args()
    if args.command == "list":
        cmd_list(args)
    elif args.command == "check":
        cmd_check(args)
    elif args.command == "claim":
        cmd_claim(args)
    elif args.command == "complete":
        cmd_complete(args)
    elif args.command == "release":
        cmd_release(args)
    elif args.command == "heartbeat":
        cmd_heartbeat(args)


if __name__ == "__main__":
    main()

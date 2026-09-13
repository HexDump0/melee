#!/usr/bin/env python3
"""
Doldecomp Melee - PR Checker for AI Agents
Queries open (and recently closed) Pull Requests on doldecomp/melee
to prevent agents from duplicating work already in progress or merged upstream.

Usage:
    python3 AI/scripts/check_prs.py                      # List all open PRs
    python3 AI/scripts/check_prs.py <query>              # Search PRs for query string
    python3 AI/scripts/check_prs.py --symbol <symbol>    # Search PRs for a function/symbol
    python3 AI/scripts/check_prs.py --file <file>        # Search PRs for a file path
    python3 AI/scripts/check_prs.py --all                # Include closed/merged PRs
"""

import argparse
import json
import subprocess
import sys
import urllib.request
import urllib.error

REPO = "doldecomp/melee"
API_URL = f"https://api.github.com/repos/{REPO}/pulls"


def fetch_prs_gh(state="open", limit=100):
    try:
        cmd = ["gh", "pr", "list", "--repo", REPO, "--state", state, "--limit", str(limit), "--json", "number,title,state,url,author,updatedAt,headRefName"]
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        data = json.loads(result.stdout)
        prs = []
        for item in data:
            prs.append({
                "number": item.get("number"),
                "title": item.get("title", ""),
                "state": item.get("state", "").lower(),
                "url": item.get("url", ""),
                "author": item.get("author", {}).get("login", "") if isinstance(item.get("author"), dict) else str(item.get("author", "")),
                "branch": item.get("headRefName", ""),
                "updated_at": item.get("updatedAt", "")[:10],
            })
        return prs
    except Exception:
        return None


def fetch_prs_api(state="open", per_page=100):
    url = f"{API_URL}?state={state}&per_page={per_page}"
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "MeleeDecomp-Agent-PRChecker/1.0",
            "Accept": "application/vnd.github.v3+json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            prs = []
            for item in data:
                prs.append({
                    "number": item.get("number"),
                    "title": item.get("title", ""),
                    "state": item.get("state", "").lower(),
                    "url": item.get("html_url", ""),
                    "author": item.get("user", {}).get("login", "") if isinstance(item.get("user"), dict) else "",
                    "branch": item.get("head", {}).get("ref", "") if isinstance(item.get("head"), dict) else "",
                    "updated_at": (item.get("updated_at") or "")[:10],
                })
            return prs
    except urllib.error.URLError as e:
        print(f"Warning: Failed to fetch PRs via GitHub API: {e}", file=sys.stderr)
        return []


def get_prs(state="open"):
    prs = fetch_prs_gh(state=state)
    if prs is None:
        prs = fetch_prs_api(state=state)
    return prs


def main():
    parser = argparse.ArgumentParser(description="Check doldecomp/melee pull requests")
    parser.add_argument("query", nargs="?", default="", help="Keyword to search in PR title or branch")
    parser.add_argument("--symbol", "-s", help="Symbol or function name to search")
    parser.add_argument("--file", "-f", help="File name or path to search")
    parser.add_argument("--all", "-a", action="store_true", help="Include open and closed PRs")
    args = parser.parse_args()

    search_terms = []
    if args.query:
        search_terms.append(args.query.lower())
    if args.symbol:
        search_terms.append(args.symbol.lower())
    if args.file:
        # e.g., src/melee/ft/chara/ftFox/ftFox_SpecialN.c -> ftFox_SpecialN
        clean_file = args.file.replace("\\", "/").split("/")[-1].replace(".c", "").replace(".h", "").replace(".s", "")
        search_terms.append(clean_file.lower())

    state = "all" if args.all else "open"
    print(f"[*] Fetching {state} pull requests from {REPO}...")
    prs = get_prs(state=state)

    if not prs:
        print("No pull requests found or unable to connect to GitHub.")
        sys.exit(0)

    matches = []
    for pr in prs:
        haystack = f"{pr['title']} {pr['branch']} {pr['author']}".lower()
        if not search_terms:
            matches.append(pr)
        elif any(term in haystack for term in search_terms):
            matches.append(pr)

    print(f"\nFound {len(matches)} matching pull request(s) (out of {len(prs)} total {state} PRs):\n")
    if not matches:
        print("  --> No active PRs found matching your query. Safe to proceed!")
        print("  --> Remember to claim your task in AI/claims.json before starting work.")
        return

    for pr in matches:
        state_badge = f"[{pr['state'].upper()}]"
        print(f"  {state_badge} #{pr['number']:<5} {pr['title']}")
        print(f"          Author: @{pr['author']} | Branch: {pr['branch']} | Updated: {pr['updated_at']}")
        print(f"          Link:   {pr['url']}\n")

    if any(term for term in search_terms):
        print("⚠️  WARNING: Existing PR(s) may already touch or implement this code!")
        print("⚠️  Inspect the PR links above before starting work to avoid duplicate effort.")


if __name__ == "__main__":
    main()

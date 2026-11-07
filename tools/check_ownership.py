#!/usr/bin/env python3
"""Fail when a stream branch changes files outside its stream's paths.

usage: check_ownership.py <branch> <changed-file>...
       git diff --name-only origin/main...HEAD | xargs tools/check_ownership.py "$BRANCH"
Branches: s<N>/<task>, contract/<name>, int/<task>. Other branches (main, dependabot, ...) are not checked.
"""
import json
import sys
from pathlib import Path

STREAMS = json.loads((Path(__file__).parent / "streams.json").read_text())["streams"]


def owned(paths: list[str], f: str) -> bool:
    return any(p == "*" or f == p or (p.endswith("/") and f.startswith(p)) for p in paths)


def check(branch: str, files: list[str]) -> list[str]:
    prefix = branch.split("/", 1)[0]
    if prefix not in STREAMS or "/" not in branch:
        return []
    allowed = STREAMS[prefix]["paths"]
    return [f for f in files if f and not owned(allowed, f)]


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    branch, files = argv[1], argv[2:]
    bad = check(branch, files)
    if bad:
        name = STREAMS[branch.split("/", 1)[0]]["name"]
        print(f"{branch} ({name}) may not change:")
        for f in bad:
            print(f"  {f}")
        print("Ask the owning stream, or open a contract/<name> PR for interface changes. See AGENTS.md.")
        return 1
    print(f"ownership ok for {branch} ({len(files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

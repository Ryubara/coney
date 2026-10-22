# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools stamp ...`: read and verify a unit's stamp log (docs/guides/stamps.md).

Read only. There is deliberately no command that appends a stamp: the stage commands are the only writers, and each
derives what it covers from files and Ghidra itself, so a stamp cannot be typed in.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from coney_tools import stamps, unit
from coney_tools.config import find_repo_root
from coney_tools.conventions import load_conventions


def add_parser(groups: Any) -> None:
    """Register `coney-tools stamp verify|show`."""
    group = groups.add_parser("stamp", help="verify or list a unit's stamp log (read only)")
    commands = group.add_subparsers(dest="command", required=True)
    for name, text in (
        ("verify", "check the chain, every field and the signatures of a stamp log"),
        ("show", "list the stamps of a log, one line each"),
    ):
        sub = commands.add_parser(name, help=text)
        sub.add_argument("--task", help="the task (default: CONEY_TASK, else the branch)")
        sub.add_argument("--dir", type=Path, help="a stamp log folder (default: the task's, in the scratch folder)")
    verify = commands.choices["verify"]
    verify.add_argument(
        "--release",
        action="store_true",
        help="a release check (what `pr open` and CI do): the signed steps must carry valid signatures",
    )
    verify.add_argument("--signers", type=Path, help="the allowed-signers file (default: the repository's)")


def run(args: argparse.Namespace) -> int:
    """Run `stamp verify` or `stamp show`."""
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    folder = args.dir or unit.Unit.of(root, unit.task_name(root, args.task)).stamps
    rules = conventions.stamps
    if args.command == "show":
        report = stamps.verify(folder, rules)
        for stamp in report.stamps:
            signed = "signed" if stamp.sig else "unsigned"
            print(
                f"#{stamp.seq:<3} {stamp.time} {stamp.step:<12} {stamp.result:<5} {stamp.subject or '-':<14} "
                f"{stamp.commit[:10] or '-':<10} {signed}"
            )
        print(f"{len(report.stamps)} stamp(s) in {folder}")
        return 0
    signers = args.signers or root / rules.signers_file
    report = stamps.verify(
        folder, rules, signers=signers if signers.is_file() else None, require_signatures=args.release
    )
    for problem in report.problems:
        print(problem)
    print(
        f"stamp verify: {'ok' if report.ok else 'FAILED'}: {len(report.stamps)} stamp(s), "
        f"{sum(p.severity == 'error' for p in report.problems)} error(s), "
        f"{sum(p.severity == 'warning' for p in report.problems)} warning(s); head {report.head[:12]}"
    )
    return 0 if report.ok else 1

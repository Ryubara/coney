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
        ("verify", "check the chain and every field of a stamp log"),
        ("show", "list the stamps of a log, one line each"),
    ):
        sub = commands.add_parser(name, help=text)
        sub.add_argument("--task", help="the task (default: CONEY_TASK, else the branch)")
        sub.add_argument(
            "--dir",
            type=Path,
            help="a stamp log folder (default: the task's, in the scratch folder); with --task, it must be that task's",
        )
    commands.choices["verify"].add_argument(
        "--require-log", action="store_true", help="a missing or empty log is an error, not just a start"
    )


def run(args: argparse.Namespace) -> int:
    """Run `stamp verify` or `stamp show`."""
    root = find_repo_root(Path.cwd())
    rules = load_conventions(root).stamps
    task = unit.task_name(root, args.task) if args.dir is None or args.task else None
    folder = args.dir or unit.Unit.of(root, task or "").stamps
    report = stamps.verify(folder, rules, require_stamps=getattr(args, "require_log", False), task=task)
    if args.command == "show":
        return _show(report, folder)
    for problem in report.problems:
        print(problem)
    print(
        f"stamp verify: {'ok' if report.ok else 'FAILED'}: {len(report.stamps)} stamp(s), "
        f"{sum(p.severity == 'error' for p in report.problems)} error(s), "
        f"{sum(p.severity == 'warning' for p in report.problems)} warning(s); head {report.head[:12]}"
    )
    return 0 if report.ok else 1


def _show(report: stamps.Report, folder: Path) -> int:
    """`stamp show`: one line per stamp; 1 when the log has errors."""
    for stamp in report.stamps:
        print(
            f"#{stamp.seq:<3} {stamp.time} {stamp.step:<12} {stamp.result:<5} {stamp.subject or '-':<14} "
            f"{stamp.commit[:10] or '-':<10} {stamp.who.get('source', '-')}"
        )
    for problem in report.problems:
        print(problem)
    print(f"{len(report.stamps)} stamp(s) in {folder}")
    return 0 if report.ok else 1

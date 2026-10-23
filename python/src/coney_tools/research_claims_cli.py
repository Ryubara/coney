# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools research claims`: the graded blocks a branch changed on the research pages, for the reviewer
(docs/guides/coney-tools-fidelity.md#research). The listing is coney_tools.research_claims."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from coney_tools import research_claims, unit
from coney_tools.config import find_repo_root

DEFAULT_BASE = "origin/main"


def add_commands(commands: Any) -> None:
    """Register `research claims` under the research group's subcommands."""
    claims = commands.add_parser(
        "claims",
        help="list the graded blocks the branch changed on the research pages, with what backs each (for review)",
    )
    claims.add_argument(
        "--base", default=DEFAULT_BASE, help=f"the branch point to compare with (default {DEFAULT_BASE})"
    )
    claims.add_argument(
        "--task", help="the task whose stamp log backs the code claims (default: CONEY_TASK, else the branch)"
    )
    claims.add_argument("--stamps", type=Path, help="a stamp log folder to use instead of the task's")
    claims.add_argument(
        "--require-study",
        action="store_true",
        help="an error for a confirmed (code) claim whose addresses were not studied in the task (maintainer decision)",
    )
    claims.add_argument("--json", action="store_true", help="print the report as JSON")


def stamp_folder(root: Path, task: str | None, explicit: Path | None) -> Path | None:
    """The stamp log that backs the code claims: `explicit`, else the task's in the scratch folder, else the one a pull
    request publishes under research/units/; None when there is none."""
    if explicit is not None:
        return explicit
    name = unit.task_name(root, task)
    for folder in (unit.Unit.of(root, name).stamps, root / "research" / "units" / name / "stamps"):
        if folder.is_dir():
            return folder
    return None


def run(args: Any) -> int:
    """`research claims`: print the report; exit 1 when a claim has an error."""
    root = find_repo_root(Path.cwd())
    report = research_claims.build(
        root,
        base=args.base,
        stamp_folder=stamp_folder(root, args.task, args.stamps),
        require_study=args.require_study,
    )
    print(research_claims.render_json(report) if args.json else research_claims.render(report))
    return 1 if report.errors else 0

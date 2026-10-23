# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools conclave snapshot`: the one read model of the Claude Code mod (docs/guides/conclave.md)."""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Callable
from pathlib import Path

from coney_tools import conclave_snapshot
from coney_tools.conclave_snapshot import Sources, default_sources

__all__ = ["add_parser", "default_sources", "run"]


def add_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Register `coney-tools conclave snapshot`."""
    group = groups.add_parser("conclave", help="the Claude Code mod's read model (docs/guides/conclave.md)")
    commands = group.add_subparsers(dest="command", required=True)
    snapshot = commands.add_parser(
        "snapshot",
        help="units, stamps, builds, claims, pull requests and the machine, as one JSON object",
        description=(
            "Prints what the Conclave mod draws: the hook settings' roles and helpers (config), every unit with "
            "its stage and stamps (units), the build slots (builds), free disk and the PCSX2 copies (machine) and, "
            "with --github, the claim cards (claims), the pull requests (prs) and the issues they name (issues). "
            "Read-only. A section that cannot be read carries its error; the exit code is 0."
        ),
    )
    snapshot.add_argument("--json", action="store_true", help="print the snapshot as JSON (the mod's input)")
    snapshot.add_argument("--github", action="store_true", help="also read GitHub through gh (slower)")
    snapshot.add_argument("--unit", metavar="TASK", help="keep only this unit's row")


def _stderr(line: str) -> None:
    """Print one line to stderr."""
    print(line, file=sys.stderr)


def run(
    args: argparse.Namespace,
    sources: Sources | None = None,
    out: Callable[[str], None] = print,
    err: Callable[[str], None] = _stderr,
) -> int:
    """Run `conclave snapshot`; 2 when `--unit` names no unit."""
    report = conclave_snapshot.collect(sources or default_sources(Path.cwd()), args.github, args.unit)
    units = (report["units"] or {}).get("units")
    if args.unit is not None and units is not None and not units:
        err(f"conclave snapshot: no unit {args.unit!r} (a scratch folder with a task file)")
        return 2
    if args.json:
        out(json.dumps(report, indent=1, ensure_ascii=False))
    else:
        for line in conclave_snapshot.format_text(report):
            out(line)
    return 0

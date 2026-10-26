# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools standards ...` commands: scan, init, prune, mark and check the legacy marks and the touch-it rule.

See standards_check for what the marks are and docs/guides/coney-tools-standards.md#standards for the commands.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from coney_tools import docs_legacy, standards_check
from coney_tools.config import find_repo_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.research_functions import load_all
from coney_tools.research_legacy import load_legacy
from coney_tools.standards_file import format_legacy_files, load_legacy_files
from coney_tools.standards_scan import RULES, Finding

#: The legacy file is written with LF whatever the system, as the repository's text files are.
NEWLINE = chr(10)


def add_parser(groups: Any) -> None:
    """Register `coney-tools standards ...`."""
    group = groups.add_parser(
        "standards",
        help="the legacy marks (functions, source and test files) and the standards a touched file is held to",
    )
    commands = group.add_subparsers(dest="command", required=True)
    scan = commands.add_parser("scan", help="count what is below the standard: functions, source files, tests, pages")
    scan.add_argument("--files", action="store_true", help="also list every finding, one per line")
    commands.add_parser(
        "init", help="write the legacy file of source and test files from a scan (refuses if it exists)"
    )
    commands.add_parser(
        "prune", help="take the files that pass out of the legacy file and lower the counts; never adds"
    )
    commands.add_parser(
        "mark",
        help="give every ported function with no status the status legacy (refused for a new port: record it compared)",
    )
    check = commands.add_parser(
        "check", help="the marks are consistent and the files and functions the change touches meet the standard"
    )
    check.add_argument("--base", default="origin/main", help="the branch the change is measured from")


def _lines(findings: dict[str, list[Finding]]) -> list[str]:
    """One `path:line: rule: message` line per finding, in path and line order."""
    return [f"{f.path}:{f.line}: {f.rule}: {f.message}" for path in sorted(findings) for f in findings[path]]


def _rule_counts(files: dict[str, list[Finding]]) -> str:
    """`comment 4, tag 9`: the findings of the given files counted by rule."""
    totals = {rule: sum(1 for found in files.values() for f in found if f.rule == rule) for rule in RULES}
    return ", ".join(f"{rule} {count}" for rule, count in totals.items() if count)


def run_scan(root: Path, conventions: Conventions, files: bool) -> int:
    """Print what is below the standard, by kind; with `files`, every finding too."""
    scan = standards_check.scan_tree(root, conventions)
    counted = standards_check.function_counts(load_all(root, Path(conventions.research.files.functions)))
    print(
        f"ported functions: {counted.ported} (legacy {counted.legacy}, compared {counted.compared}, "
        f"no status {len(counted.unmarked)})"
    )
    print(
        f"source files: {len(scan.code)} of {scan.code_files} below the standard ({_rule_counts(scan.code) or 'none'})"
    )
    print(
        f"test files: {len(scan.tests)} of {scan.test_files} below the standard ({_rule_counts(scan.tests) or 'none'})"
    )
    listed = load_legacy(root, conventions.research, docs_legacy.LEGACY_RULES)
    print(f"research pages: {len(listed.pages)} listed in {conventions.research.files.legacy}")
    if files:
        for line in _lines(scan.findings()):
            print(line)
    return 0


def run_check(root: Path, conventions: Conventions, base: str) -> int:
    """Print the problems and what the touched files owe; exit 1 when any is an error."""
    reports = standards_check.check(root, conventions, base)
    errors = 0
    for report in reports:
        label = "" if report.error else "warning: "
        print(f"{report.path}:{report.line}: {label}{report.rule}: {report.message}")
        errors += report.error
    warnings = len(reports) - errors
    print(
        f"standards check: {errors} error{'s' if errors != 1 else ''}, {warnings} warning{'s' if warnings != 1 else ''}"
    )
    return 1 if errors else 0


def run(args: argparse.Namespace) -> int:
    """Dispatch to the chosen command; ConfigError reaches the caller, which prints it and exits 2."""
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    name = conventions.standards.legacy_file
    if args.command == "scan":
        return run_scan(root, conventions, args.files)
    if args.command == "check":
        return run_check(root, conventions, args.base)
    if args.command == "mark":
        functions = Path(conventions.research.files.functions)
        waiting = len(standards_check.function_counts(load_all(root, functions)).unmarked)
        changed = standards_check.mark_unmarked(root, conventions)
        plural = "" if waiting == 1 else "s"
        print(f"{waiting} function{plural} marked legacy" + (f" in {', '.join(changed)}" if changed else ""))
        return 0
    scan = standards_check.scan_tree(root, conventions)
    if args.command == "init":
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(
            standards_check.initial_text(root, conventions, scan), encoding="utf-8", newline=NEWLINE
        )
        print(f"wrote {name}")
        return 0
    before = load_legacy_files(root, conventions.standards)
    left = standards_check.pruned(root, conventions, scan)
    (root / name).write_text(format_legacy_files(left), encoding="utf-8", newline=NEWLINE)
    print(f"{name}: {len(before.files)} files listed before, {len(left.files)} now")
    return 0

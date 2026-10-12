# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools repo audit` command line: pick the scopes, read what they need, run the checks, print the report."""

from __future__ import annotations

import argparse
import json
from collections.abc import Sequence
from pathlib import Path

from coney_tools import audit_files, audit_github, audit_local
from coney_tools.audit import (
    SCOPES,
    Report,
    format_text,
    run_checks,
    sort_findings,
    specs_in,
    to_json,
)
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import CheckSpec, Conventions, load_conventions

#: Findings of one check printed in the text report before "... N more" (the JSON and `--show 0` list them all).
DEFAULT_SHOWN = 10
#: Checks that need the default branch's files; without them they do not run and a notice says so.
NEEDS_TREE = frozenset({"issue.links"})


def add_audit_parser(repo_commands: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add `repo audit` and its options to the `repo` command group."""
    audit = repo_commands.add_parser(
        "audit",
        help="report where the repository departs from .github/conventions.yml (read only)",
        description=(
            "Reports where the repository departs from .github/conventions.yml and changes nothing. --files reads "
            "the checkout (CI runs it), --github reads issues, pull requests, labels and settings through gh, "
            "--local reads the workspace. Exit code 1 when a finding has severity error."
        ),
    )
    audit.add_argument("--files", action="store_true", help="check the checkout (the default when no scope is given)")
    audit.add_argument("--github", action="store_true", help="check issues, pull requests, labels and settings (gh)")
    audit.add_argument("--local", action="store_true", help="check the workspace: disk, worktrees, scratch folders")
    audit.add_argument("--json", action="store_true", help="print the report as JSON instead of text")
    audit.add_argument(
        "--only", action="append", default=[], metavar="CHECK", help="run only this check or group (issue)"
    )
    audit.add_argument("--strict", action="store_true", help="exit 1 on a warning too")
    audit.add_argument(
        "--show", type=int, default=DEFAULT_SHOWN, metavar="N", help="findings printed per check; 0 for all (text only)"
    )
    audit.add_argument(
        "--snapshot", type=Path, metavar="FILE", help="with --github: read this saved snapshot, not GitHub"
    )
    audit.add_argument("--save-snapshot", type=Path, metavar="FILE", help="with --github: also write the snapshot read")
    audit.add_argument(
        "--workspace", type=Path, help="with --local: the workspace folder (default: found from the checkout)"
    )
    audit.add_argument("--list", action="store_true", help="list the checks and exit")


def _list_checks(conventions: Conventions) -> int:
    """Print the registry of checks."""
    for spec in conventions.hygiene.checks.values():
        print(f"{spec.id:<24} {spec.scope:<7} {spec.severity:<6} fix: {spec.fix:<6} {spec.summary}")
    return 0


def _github_snapshot(args: argparse.Namespace) -> audit_github.Snapshot:
    """The GitHub data to audit: a saved snapshot or a fresh read; ConfigError when it cannot be had."""
    if args.snapshot is not None:
        try:
            raw = json.loads(args.snapshot.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise ConfigError(f"{args.snapshot}: cannot be read as a snapshot ({error})") from error
    else:
        try:
            raw = audit_github.fetch_raw()
        except (OSError, ValueError, KeyError) as error:
            raise ConfigError(f"cannot read GitHub with gh ({error}); sign in with `gh auth login`") from error
    if args.save_snapshot is not None:
        args.save_snapshot.write_text(json.dumps(raw, indent=1, ensure_ascii=False), encoding="utf-8")
    return audit_github.parse_snapshot(raw)


def build_report(
    root: Path,
    conventions: Conventions,
    scopes: Sequence[str],
    only: Sequence[str],
    args: argparse.Namespace,
    cwd: Path,
) -> Report:
    """Run the checks of `scopes` and return the report; reads GitHub and the workspace only when asked."""
    specs: list[CheckSpec] = specs_in(conventions, scopes, only)
    report = Report(tuple(scopes))
    contexts: dict[str, object] = {}
    if "files" in scopes:
        contexts["files"] = audit_files.FilesContext(root, conventions)
    if "github" in scopes:
        snapshot = _github_snapshot(args)
        tree = audit_github.git_tree(root, f"origin/{snapshot.default_branch}")
        report.notices.extend(snapshot.notices)
        if tree is None and any(spec.id in NEEDS_TREE for spec in specs):
            report.notices.append(
                f"origin/{snapshot.default_branch} is not in this checkout (git fetch origin): issue.links did not run"
            )
            specs = [spec for spec in specs if spec.id not in NEEDS_TREE]
        if snapshot.settings is None and snapshot.rulesets is None:
            specs = [spec for spec in specs if spec.id != "gh.ruleset"]
        contexts["github"] = audit_github.GithubContext(conventions, snapshot, tree, root)
    if "local" in scopes:
        local = audit_local.build_context(cwd, args.workspace, conventions)
        report.notices.extend(local.notices)
        contexts["local"] = local
    run_checks(specs, contexts, report)
    report.findings = sort_findings(report.findings, [spec.id for spec in specs])
    return report


def run_audit(args: argparse.Namespace) -> int:
    """Run `coney-tools repo audit`; 0 when clean (or only warnings), 1 on an error finding."""
    cwd = Path.cwd()
    root = find_repo_root(cwd)
    conventions = load_conventions(root)
    if args.list:
        return _list_checks(conventions)
    scopes = [scope for scope in SCOPES if getattr(args, scope)] or ["files"]
    for option, scope in (("snapshot", "github"), ("save_snapshot", "github"), ("workspace", "local")):
        if getattr(args, option) is not None and scope not in scopes:
            raise ConfigError(f"--{option.replace('_', '-')} needs --{scope}")
    report = build_report(root, conventions, scopes, args.only, args, cwd)
    if args.json:
        print(to_json(report))
    else:
        for line in format_text(report, args.show or None):
            print(line)
    return 1 if report.failed(args.strict) else 0

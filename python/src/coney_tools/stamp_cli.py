# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools stamp ...`: read and verify a unit's stamp log (docs/guides/stamps.md).

Read only. There is deliberately no command that appends a stamp: the stage commands are the only writers, and each
derives what it covers from files and Ghidra itself, so a stamp cannot be typed in.
"""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from coney_tools import stamps, unit
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Stamps, load_conventions, load_conventions_at


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
        sub.add_argument(
            "--dir",
            type=Path,
            help="a stamp log folder (default: the task's, in the scratch folder); with --task, it must be that task's",
        )
    verify = commands.choices["verify"]
    verify.add_argument(
        "--release",
        action="store_true",
        help="a release check (what `pr open` and CI will run): the signed steps must carry valid signatures and the "
        "log must not be missing or empty",
    )
    anchor = verify.add_mutually_exclusive_group()
    anchor.add_argument(
        "--signers", type=Path, help="the allowed-signers file (default: the working tree's .github/stamp-signers)"
    )
    anchor.add_argument(
        "--signers-ref",
        metavar="REF",
        help="read .github/stamp-signers and the stamp rules from this commit (CI will pass the pull request's "
        "base, such as origin/main), not from the working tree under review; not with --signers",
    )


def _trusted(root: Path, ref: str) -> tuple[Stamps, Path | None, tempfile.TemporaryDirectory[str]]:
    """The stamp rules and the allowed-signers file as the commit `ref` has them, the file copied to a temporary
    folder (returned so the caller keeps it alive): what a release check trusts instead of the change under review."""
    rules = load_conventions_at(root, ref).stamps
    folder = tempfile.TemporaryDirectory()
    done = subprocess.run(
        ["git", "-C", str(root), "show", f"{ref}:{rules.signers_file}"], capture_output=True, check=False
    )
    if done.returncode != 0:
        folder.cleanup()
        raise ConfigError(
            f"{ref}:{rules.signers_file}: cannot be read ({done.stderr.decode('utf-8', 'replace').strip()})"
        )
    path = Path(folder.name) / "stamp-signers"
    path.write_bytes(done.stdout)
    return rules, path, folder


def run(args: argparse.Namespace) -> int:
    """Run `stamp verify` or `stamp show`."""
    root = find_repo_root(Path.cwd())
    rules = load_conventions(root).stamps
    task = unit.task_name(root, args.task) if args.dir is None or args.task else None
    folder = args.dir or unit.Unit.of(root, task or "").stamps
    signers: Path | None = getattr(args, "signers", None) or root / rules.signers_file
    keep: tempfile.TemporaryDirectory[str] | None = None
    if getattr(args, "signers_ref", None):
        rules, signers, keep = _trusted(root, args.signers_ref)
    try:
        signers = signers if signers is not None and signers.is_file() else None
        release = getattr(args, "release", False)
        report = stamps.verify(folder, rules, signers=signers, require_signatures=release, task=task)
        if args.command == "show":
            return _show(report, folder, signers is not None)
        for problem in report.problems:
            print(problem)
        print(
            f"stamp verify: {'ok' if report.ok else 'FAILED'}: {len(report.stamps)} stamp(s), "
            f"{sum(p.severity == 'error' for p in report.problems)} error(s), "
            f"{sum(p.severity == 'warning' for p in report.problems)} warning(s); head {report.head[:12]}"
        )
        return 0 if report.ok else 1
    finally:
        if keep is not None:
            keep.cleanup()


def _show(report: stamps.Report, folder: Path, can_check: bool) -> int:
    """`stamp show`: one line per stamp, a signature marked as checked, unchecked or bad; 1 when the log has errors."""
    bad = {p.where for p in report.problems if p.severity == "error" and "signature" in p.text}
    for stamp in report.stamps:
        where = f"{folder.name}: seq {stamp.seq}"
        if not stamp.sig:
            signed = "unsigned"
        elif where in bad:
            signed = "BAD signature"
        else:
            signed = "signed, verified" if can_check else "signed, NOT verified (no signers file)"
        print(
            f"#{stamp.seq:<3} {stamp.time} {stamp.step:<12} {stamp.result:<5} {stamp.subject or '-':<14} "
            f"{stamp.commit[:10] or '-':<10} {signed}"
        )
    for problem in report.problems:
        print(problem)
    print(f"{len(report.stamps)} stamp(s) in {folder}")
    return 0 if report.ok else 1

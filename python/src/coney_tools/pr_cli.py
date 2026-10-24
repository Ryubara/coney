# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pr verify` and `pr text`: the read-only half of releasing a pull request (docs/guides/pr.md).

`pr verify` runs steps 1-4 (the stamp log, Check, the compared functions, Review) and lists every stage that does not
hold, with the command to redo. `pr text` runs step 5: it generates the pull request's title and description from the
engineer's file, the task and the stamps, checks them with the checks CI runs, and writes `PR-TITLE.txt` and
`PR-BODY.md` next to the file. Neither pushes, opens or edits anything on GitHub, so they are safe for an engineer to
run at any time; `pr open` (a later command) runs the same code before it pushes.

Exit codes: 0 everything holds, 1 something is refused, 2 the command cannot run (a bad argument, no unit, an unknown
base, unreadable rules).
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from coney_tools import pr_proof, pr_text
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import load_conventions
from coney_tools.pr_proof import ProofResult, Refusal, Subject
from coney_tools.repo_checks import branch_trailer_models
from coney_tools.task_file import TaskFile, TaskFileError, parse_task_file
from coney_tools.unit import Unit, task_name

#: What the generated text is written to, next to the engineer's file.
TITLE_FILE = "PR-TITLE.txt"
BODY_FILE = "PR-BODY.md"
#: The engineer's file.
TEXT_FILE = "PR.md"


def add_pr_commands(commands: Any) -> None:
    """Add `verify` and `text` to the `pr` group's subparsers."""
    for name, text in (
        (
            "verify",
            "steps 1-4 of a release, read only: the stamp log, Check, the compared functions and Review hold for "
            "this change (docs/guides/pr.md#verify)",
        ),
        (
            "text",
            "step 5: generate the pull request's title and description from PR.md, the task and the stamps, check "
            "them and write PR-TITLE.txt and PR-BODY.md (docs/guides/pr.md#text)",
        ),
    ):
        sub = commands.add_parser(name, help=text)
        sub.add_argument("task", nargs="?", metavar="TASK", help="the task (default: CONEY_TASK, else the branch)")
        sub.add_argument("--worktree", type=Path, help="the unit's worktree (default: the current checkout)")
        sub.add_argument("--remote", default="origin", help="the git remote whose default branch is the base")
        sub.add_argument("--json", action="store_true", help="print the result as one JSON object")
        if name == "text":
            sub.add_argument("--text", type=Path, metavar="FILE", help="the engineer's file (default: PR.md)")
            sub.add_argument("--dry-run", action="store_true", help="check and print; write no file")
        else:
            sub.add_argument("--text", type=Path, metavar="FILE", help="the file Review covered (default: PR.md)")


def _load(args: argparse.Namespace) -> tuple[Subject, TaskFile, dict[str, Any], Path]:
    """The subject, the task file, its optional keys and the engineer's file for the unit the arguments name."""
    root = find_repo_root(args.worktree if args.worktree is not None else Path.cwd())
    task = task_name(root, args.task)
    own = Unit.of(root, task)
    conventions = load_conventions(root)
    path = own.dir / conventions.tasks.file
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as error:
        raise ConfigError(
            f"pr: {task}: no {conventions.tasks.file} at {path} ({error.strerror}); run task create"
        ) from error
    try:
        task_file = parse_task_file(raw, conventions.tasks)
    except TaskFileError as error:
        raise ConfigError(f"pr: {path}: {error}") from error
    keys = pr_text.optional_task_keys(raw)
    # A fix task continues its parent's unit: the log, the branch and the text are the parent's (D6).
    owner = str(keys.get("parent") or task)
    unit = Unit.of(root, owner)
    subject = pr_proof.load_subject(root, unit, owner, task_file.kind, remote=args.remote)
    return subject, task_file, keys, args.text if args.text is not None else unit.dir / TEXT_FILE


def _print_refusals(refusals: list[Refusal]) -> None:
    """One block per refusal: the message, then what to redo."""
    for refusal in refusals:
        print(f"  {refusal.text}")
        print(f"      redo: {refusal.redo}")


def _facts(subject: Subject) -> dict[str, Any]:
    """The fields both commands report about the change."""
    return {
        "task": subject.task,
        "base": subject.base,
        "tip": subject.tip,
        "remote_ref": subject.remote_ref,
        "diff_sha256": subject.diff_sha256,
        "profile": subject.profile.name,
        "kind": subject.kind,
    }


def _refusal_json(refusal: Refusal) -> dict[str, str]:
    return {"stage": refusal.stage, "text": refusal.text, "redo": refusal.redo}


def run_verify(args: argparse.Namespace) -> int:
    """`pr verify`: print every stage that does not hold; 0 when all hold."""
    subject, _task, _keys, text_path = _load(args)
    try:
        hashes = pr_text.hashes_of(pr_text.read_engineer_file(text_path, subject.conventions))
    except ConfigError:
        hashes = None  # `pr text` says the file is unreadable; Review's three text hashes are then not compared
    result: ProofResult = pr_proof.verify(subject, hashes)
    if args.json:
        print(
            json.dumps(
                {
                    **_facts(subject),
                    "ok": result.ok,
                    "refusals": [_refusal_json(r) for r in result.refusals],
                    "notes": result.notes,
                },
                indent=2,
            )
        )
        return 0 if result.ok else 1
    print(
        f"pr verify {subject.task}: change {subject.diff_sha256[:12]} since {subject.remote_ref} "
        f"({subject.base[:12]}), tip {subject.tip[:12]}, profile {subject.profile.name}"
    )
    for note in result.notes:
        print(f"  note: {note}")
    if result.ok:
        print("ok: the log, Check, the compared functions and Review all hold for this change")
        return 0
    count = len(result.refusals)
    print(f"refused: {count} problem{'s' if count != 1 else ''}")
    _print_refusals(result.refusals)
    return 1


def run_text(args: argparse.Namespace) -> int:
    """`pr text`: generate and check the title and description, and write them unless a check fails or --dry-run."""
    subject, task_file, keys, text_path = _load(args)
    text = pr_text.read_engineer_file(text_path, subject.conventions)
    models = branch_trailer_models(subject.root, subject.base, subject.conventions.commits.trailers)
    generated = pr_text.generate(subject, task_file, text, keys=keys, trailer_models=models)
    folder = text_path.parent
    written: list[Path] = []
    if generated.ok and not args.dry_run:
        folder.mkdir(parents=True, exist_ok=True)
        (folder / TITLE_FILE).write_text(generated.title + "\n", encoding="utf-8", newline="\n")
        (folder / BODY_FILE).write_text(generated.body, encoding="utf-8", newline="\n")
        written = [folder / TITLE_FILE, folder / BODY_FILE]
    if args.json:
        print(
            json.dumps(
                {
                    **_facts(subject),
                    "ok": generated.ok,
                    "title": generated.title,
                    "title_sha256": generated.text_hashes.title_sha256,
                    "paragraph_sha256": generated.text_hashes.paragraph_sha256,
                    "images_sha256": generated.text_hashes.images_sha256,
                    "facts_sha256": generated.facts_sha256,
                    "written": [str(p) for p in written],
                    "refusals": [_refusal_json(r) for r in generated.refusals],
                },
                indent=2,
            )
        )
        return 0 if generated.ok else 1
    if not generated.ok:
        count = len(generated.refusals)
        print(f"pr text {subject.task}: refused: {count} problem{'s' if count != 1 else ''}")
        _print_refusals(list(generated.refusals))
        return 1
    print(f"pr text {subject.task}: ok: {generated.title}")
    for path in written:
        print(f"  wrote {path}")
    if args.dry_run:
        print("  (dry run: nothing written)")
    missing = [step for step in ("checked", "reviewed") if subject.report.latest(step) is None]
    if missing:
        print(
            f"  note: no {' or '.join(missing)} stamp yet, and the facts say so; `pr verify` lists what a release needs"
        )
    return 0

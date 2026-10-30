# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the `pr verify` and `pr text` tests: a made-up unit that passes every stage, and ways to break each one.

`Built` makes a checkout on a branch `work` that left `main` (pushed to a bare `origin`), with an `@orig` function and a
test that failed first, and the unit's scratch folder holding the task file and a stamp log in which every stage has
passed: assigned, claimed, studied, compared, fails-first, checked (with its build record) and reviewed, plus the
engineer's `PR.md`. Git is real; Ghidra, the build and the reviewer are not (their results are written as the stamps a
stage command would write), and no network is used.
"""

import atexit
import dataclasses
import hashlib
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import yaml
from conventions_support import REPO, changed, write_checkout
from git_support import git
from test_orig_compare import SOURCE
from test_orig_study import ADDRESS, FakeGhidra

from coney_tools import build_record as build_record_module
from coney_tools import fails_first, orig_compare, orig_copy, orig_pairs, orig_study, pr_diff, pr_proof, stamps
from coney_tools.conventions import CONVENTIONS_FILE, Conventions, load_conventions
from coney_tools.task_file import TaskFile, render_task_file
from coney_tools.unit import Unit

TEST_NAME = "Widget ready frame"
TEST_FILE = "tests/ai/widget_test.cpp"
SOURCE_FILE = "src/ai/widget.cpp"
NOW = datetime(2026, 10, 23, 12, 0, 0, tzinfo=UTC)


def agent(kind: str, number: int = 1, model: str = "claude-opus-5-5") -> dict[str, Any]:
    """The `who` of a stamp written by a command an agent of type `kind` ran, as the hook identified it."""
    return {
        "kind": "tool",
        "tool": "coney-tools test",
        "agent": {"id": f"{kind}-{number}", "type": kind, "model": model},
        "source": "hook",
    }


ENGINEER = agent("engineer")
REVIEWER = agent("reviewer")
#: A stamp written by a command the hook did not vouch for.
UNVERIFIED = {
    "kind": "tool",
    "tool": "coney-tools test",
    "agent": {"id": "unknown", "type": "unknown", "model": "unknown"},
    "source": "unverified",
}
PARAGRAPH = "The widget now waits for its ready frame before it updates, as the original does."
DOCS_TEXT = f"""docs: Document the widget

## Summary

{PARAGRAPH}

Docs: none

## Issues affected

None (no open issue is affected)

## Screenshots

Screenshots: none (no visible change)
"""
PR_TEXT = f"""gameplay: Fix the widget ready frame

## Summary

{PARAGRAPH}

Docs: none

## Issues affected

None (no open issue is affected)

## Testing

- Runtime: trace diff widget60, 600 updates, 0 outside tolerance

## Screenshots

Screenshots: none (no visible change)
"""


class Clock:
    """A clock that moves a second per reading, from just after the commits' dates."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 22, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(seconds=1)
        return self.now


def sha(data: bytes | str) -> str:
    """SHA-256 of `data` as hex."""
    return hashlib.sha256(data.encode("utf-8") if isinstance(data, str) else data).hexdigest()


def write(root: Path, name: str, text: str) -> None:
    """Write a file of the checkout (creating its folder), LF endings."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="")


def commit_all(root: Path, message: str) -> str:
    """Commit everything; returns the commit."""
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", message)
    return git(root, "rev-parse", "HEAD").strip()


@dataclass
class Built:
    """A made-up unit; see the module docstring. Every part is a field so a test can break exactly one."""

    root: Path
    origin: Path
    unit: Unit
    task: str
    kind: str
    clock: Clock
    conventions: Conventions
    ghidra: FakeGhidra
    test_commit: str
    findings: list[orig_copy.Finding]

    @property
    def tip(self) -> str:
        """The branch's tip."""
        return git(self.root, "rev-parse", "HEAD").strip()

    @property
    def base(self) -> str:
        """Where the branch left main."""
        return pr_diff.change_of(self.root, "origin/main", "HEAD").base

    @property
    def diff(self) -> str:
        """The change hash now."""
        return pr_diff.change_of(self.root, "origin/main", "HEAD").sha256

    @property
    def pairs_path(self) -> Path:
        """The pairs file of the function."""
        return self.unit.mapping(ADDRESS)

    # --- stamps ---------------------------------------------------------------------------------------------------

    def stamp(
        self,
        step: str,
        *,
        subject: str = "",
        result: str = "pass",
        covers: dict[str, str] | None = None,
        detail: dict[str, Any] | None = None,
        who: dict[str, Any] | None = None,
        commit: str | None = None,
    ) -> stamps.Stamp:
        """Append one stamp to the unit's log."""
        return stamps.append(
            self.unit.stamps,
            rules=self.conventions.stamps,
            task=self.task,
            step=step,
            subject=subject,
            result=result,
            commit=commit if commit is not None else self.tip,
            covers=covers or {},
            detail=detail or {},
            who=who or ENGINEER,
            clock=self.clock,
        )

    def study(self) -> None:
        """Study the function (the fake Ghidra serves it) and stamp it."""
        orig_study.study(
            self.unit,
            ADDRESS,
            self.ghidra,
            rules=self.conventions.stamps,
            commit=self.tip,
            who=ENGINEER,
            clock=self.clock,
        )

    def fill_pairs(self) -> None:
        """Complete the pairs file the first compare made (the same pairs `test_orig_compare` fills in)."""
        file = orig_pairs.load(self.pairs_path)
        where = {
            "1-3": (f"{SOURCE_FILE}:5", "same", ""),
            "4-6": (f"{SOURCE_FILE}:8", "same", ""),
            "7": (f"{SOURCE_FILE}:12", "stand-in", "the sound plays later"),
            "8": (f"{SOURCE_FILE}:13", "same", ""),
            "9": (f"{SOURCE_FILE}:14", "same", ""),
        }
        file.pairs = [
            orig_pairs.Pair(p.lines, p.kind, where[p.lines][0], where[p.lines][1], where[p.lines][2])
            for p in file.pairs
        ]
        orig_pairs.write(self.pairs_path, file)

    def compare(self, key: str | None = None) -> orig_compare.CompareResult:
        """Run compare (copy-check answers with `self.findings`) and stamp it; `key` picks the Coney function when
        several carry the address."""
        return orig_compare.compare(
            self.unit,
            self.root,
            ADDRESS,
            self.conventions.stamps,
            commit=self.tip,
            who=ENGINEER,
            clock=self.clock,
            key=key,
            copy_check=lambda root, base: self.findings,
        )

    def fails_first(self, name: str = TEST_NAME, result: str = "pass", **detail: Any) -> stamps.Stamp:
        """Stamp that the test failed before the fix and passes after, as `orig fails-first` writes it; `result`
        `keeps` is the stamp of a test that passes before it too (a refactor guard)."""
        return self.stamp(
            "fails-first",
            subject=name,
            result=result,
            covers={
                "test_commit": self.test_commit,
                "fix_commit": self.tip,
                fails_first.TEST_COVER: fails_first.test_sha256(self.root, self.tip, name, [TEST_FILE]),
                "red_log_sha256": sha("red"),
                "green_log_sha256": sha("green"),
            },
            detail={
                "red": "passed" if result == "keeps" else "failed",
                "green": "passed",
                "test_files": [TEST_FILE],
                "stubs": [],
                **detail,
            },
        )

    def record_fields(self, **more: Any) -> dict[str, Any]:
        """What `coney-tools build` gives `build_record.make` for a passing full run at the tip, with `more` over it;
        the run's log is written as a file in the unit, as the record's `log` names it."""
        log = self.unit.dir / "build-logs" / "ci.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_bytes(b"log")
        fields: dict[str, Any] = {
            "task": self.unit.task,
            "agent": ENGINEER["agent"]["id"],
            "tip": self.tip,
            "dirty": False,
            "preset": "ci",
            "filters": [],
            "command_line": ["build", "--preset", "ci", "--test"],
            "exit_code": 0,
            "ran": ["configure", "build", "test"],
            "tests": {"total": 412, "passed": 412, "failed": 0, "skipped": 0, "skipped_disc": 0},
            "disc": "env",
            "tool_revision": "test",
            "log": str(log),
            "log_sha256": sha(b"log"),
            "started": "2026-10-22T12:00:00Z",
            "ended": "2026-10-22T12:20:00Z",
        }
        fields.update(more)
        return fields

    def build_record(self, **more: Any) -> str:
        """Write a build record of a passing full run at the tip, made by `build_record.make` and `write` as
        `coney-tools build` makes it; returns its hash."""
        record = build_record_module.make(**self.record_fields(**more))
        return build_record_module.write(self.unit.dir / build_record_module.FOLDER, record).stem

    def check(
        self, profile: str = "full", checks: list[str] | None = None, record: str | None = None, **more: Any
    ) -> None:
        """Stamp a passing Check of the change as it is now (and, for `full`, its build record)."""
        rules = self.conventions.proof
        covers = {"diff_sha256": self.diff, "checks_sha256": sha(profile)}
        tests = {"total": 412, "passed": 412, "failed": 0, "skipped": 0, "skipped_disc": 0}
        if profile == "full":
            covers["build_record_sha256"] = record if record is not None else self.build_record()
        listed = checks if checks is not None else list(next(p.checks for p in rules.profiles if p.name == profile))
        detail = {"profile": profile, "checks": listed, "tests": tests, "preset": "ci", "minutes": 21}
        detail.update(more)
        # `tests` stands for the build's counts; a `more` key of None (e.g. `tests=None`) leaves that key out, as a
        # profile without a build (`docs`, `tooling`) has no `detail.tests`. Pytest's counts are `more["pytest"]`.
        self.stamp("checked", covers=covers, detail={k: v for k, v in detail.items() if v is not None})

    def text_hashes(self) -> pr_proof.TextHashes:
        """The hashes of the engineer's PR.md as it is now."""
        from coney_tools import pr_text

        return pr_text.hashes_of(self.unit.dir / "PR.md")

    def review(
        self, functions: dict[str, str] | None = None, reviewer: dict[str, str] | None = None, **detail: Any
    ) -> None:
        """Stamp a passing Review of the change, the pairs and the text as they are now."""
        text = self.text_hashes()
        pairs = pr_proof.pairs_set_sha256(self.unit, [ADDRESS] if self.pairs_path.is_file() else [])
        info: dict[str, Any] = {
            "verdict": "pass",
            "functions": functions if functions is not None else {ADDRESS: "clean"},
            "pages": [],
            "reviewer": reviewer or REVIEWER["agent"],
            "blockers": 0,
            "notes": 0,
        }
        info.update(detail)
        self.stamp(
            "reviewed",
            covers={
                "diff_sha256": self.diff,
                "pairs_set_sha256": pairs,
                "title_sha256": text.title_sha256,
                "paragraph_sha256": text.paragraph_sha256,
                "images_sha256": text.images_sha256,
            },
            detail=info,
            who=REVIEWER,
        )

    # --- the repository -------------------------------------------------------------------------------------------

    def edit(self, name: str, text: str, message: str = "edit") -> str:
        """Write a file and commit it on the branch; returns the commit."""
        write(self.root, name, text)
        return commit_all(self.root, message)

    def edit_rules(self, *changes: tuple[str, object], message: str = "ci: edit the rules") -> str:
        """The branch commits its own `.github/conventions.yml` with `changes` applied: a branch that edits the rules it
        is released under. Returns the commit."""
        return self.edit(CONVENTIONS_FILE, yaml.safe_dump(changed(*changes)), message)

    def set_base_rules(self, *changes: tuple[str, object]) -> None:
        """`origin/main` moves to hold `.github/conventions.yml` with `changes` applied, and this checkout fetches it:
        the rules the branch is released under, while its own copy of the file stays as `make` wrote it."""
        other = self.root.parent.parent / "other"
        if not other.exists():
            subprocess.run(["git", "clone", "-q", str(self.origin), str(other)], check=True)
            own_identity(other)
        git(other, "checkout", "-q", "main")
        git(other, "pull", "-q", "origin", "main")
        write(other, CONVENTIONS_FILE, yaml.safe_dump(changed(*changes)))
        commit_all(other, "ci: change the rules")
        git(other, "push", "-q", "origin", "main")
        git(self.root, "fetch", "-q", "origin", "main")

    def subject(self, **more: Any) -> pr_proof.Subject:
        """The subject `pr verify` reads, at `NOW`."""
        return pr_proof.load_subject(self.root, self.unit, self.task, self.kind, now=NOW, **more)

    def verify(self, text: bool = True, **more: Any) -> pr_proof.ProofResult:
        """Run steps 1-4 on the unit as it is now."""
        return pr_proof.verify(
            self.subject(**more), self.text_hashes() if text else None, copy_check=lambda root, base: self.findings
        )


_TEMPLATES: dict[tuple[str, frozenset[str], bool], tuple[Path, datetime]] = {}
_TEMPLATE_ROOT = Path(tempfile.mkdtemp(prefix="coney-pr-unit-"))
atexit.register(shutil.rmtree, _TEMPLATE_ROOT, ignore_errors=True)


def own_identity(root: Path) -> None:
    """Give the checkout `root` its own committer identity and no signing, so the git that `pr open` runs (a rebase
    writes commits) works the same on a machine whose global git config is empty, as on CI."""
    for key, value in (("user.name", "T"), ("user.email", "t@example.org"), ("commit.gpgsign", "false")):
        git(root, "config", key, value)


def make(tmp_path: Path, shape: str = "feature", skip: frozenset[str] = frozenset(), start: bool = True) -> Built:
    """A unit in which every stage has passed (see `build`), copied from one built once per kind of unit, because
    building one runs a dozen git commands and the tests make hundreds."""
    key = (shape, skip, start)
    if key not in _TEMPLATES:
        folder = _TEMPLATE_ROOT / str(len(_TEMPLATES))
        folder.mkdir()
        built = build(folder, shape, skip, start)
        _TEMPLATES[key] = (folder, built.clock.now)
    folder, moment = _TEMPLATES[key]
    shutil.copytree(folder, tmp_path, dirs_exist_ok=True)
    root = tmp_path / "repos" / "coney"
    git(root, "remote", "set-url", "origin", str(tmp_path / "origin.git"))
    clock = Clock()
    clock.now = moment
    template = Built(
        root,
        tmp_path / "origin.git",
        Unit("work", tmp_path / "scratch" / "work"),
        "work",
        "",
        clock,
        load_conventions(root),
        FakeGhidra(),
        "",
        [],
    )
    original = _KINDS[shape]
    return dataclasses.replace(template, kind=original, test_commit=_TEST_COMMITS[folder])


_KINDS = {"feature": "feature", "docs": "docs", "tooling": "tooling", "no-test": "feature"}
_TEST_COMMITS: dict[Path, str] = {}


def build(tmp_path: Path, shape: str = "feature", skip: frozenset[str] = frozenset(), start: bool = True) -> Built:
    """A unit in which every stage has passed. Everything is committed; the worktree is clean.

    `shape` is what the branch changes: "feature" (an `@orig` function and a test that failed first), "docs" (a guide),
    "tooling" (a Python file) or "no-test" (a gameplay file that carries no `@orig` and has no test). `skip` leaves
    out stages (`study`, `compare`, `fails-first`, `checked`, `reviewed`); `start=False` leaves out `assigned` and
    `claimed`.
    """
    root = tmp_path / "repos" / "coney"
    origin = tmp_path / "origin.git"
    origin.mkdir(parents=True)
    subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(origin)], check=True)
    root.mkdir(parents=True)
    write_checkout(root)
    (root / ".github" / "pull_request_template.md").write_text(
        (REPO / ".github" / "pull_request_template.md").read_text(encoding="utf-8"), encoding="utf-8"
    )
    write(root, "src/ai/placeholder.cpp", "// placeholder\n")
    write(root, "tests/ai/placeholder_test.cpp", "// placeholder\n")
    git(root, "init", "-q", "-b", "main")
    own_identity(root)
    commit_all(root, "base")
    git(root, "remote", "add", "origin", str(origin))
    git(root, "push", "-q", "-u", "origin", "main")
    # As `git clone` leaves it: origin/HEAD names the default branch, which pr verify reads its rules from.
    git(root, "remote", "set-head", "origin", "main")
    git(root, "checkout", "-q", "-b", "work")
    unit = Unit("work", tmp_path / "scratch" / "work")
    kind = _KINDS[shape]
    test_commit = git(root, "rev-parse", "HEAD").strip()
    if shape == "feature":
        write(root, TEST_FILE, f'TEST_CASE("{TEST_NAME}") {{}}' + chr(10))
        test_commit = commit_all(root, "test: widget ready frame")
        write(root, SOURCE_FILE, SOURCE)
        commit_all(root, "fix: widget ready frame")
    elif shape == "docs":
        write(root, "docs/guides/widget.md", "# The widget" + chr(10) * 2 + "It waits." + chr(10))
        commit_all(root, "docs: widget")
    elif shape == "tooling":
        write(root, "python/src/coney_tools/widget.py", '"""A widget."""' + chr(10))
        commit_all(root, "tools: widget")
    else:
        write(
            root,
            "src/ai/other.cpp",
            "// @coney-only a helper of ours" + chr(10) + "int other() { return 1; }" + chr(10),
        )
        commit_all(root, "feature: other")
    built = Built(root, origin, unit, "work", kind, Clock(), load_conventions(root), FakeGhidra(), test_commit, [])
    task = TaskFile(
        task="work",
        issues=(757,),
        kind=kind,
        claimant="alice",
        claimed_at=datetime(2026, 10, 22, 11, 0, 0, tzinfo=UTC),
        sections={
            "Issues": "- #757",
            "Goal": "Port the widget update.",
            "Done": "- The widget waits for its ready frame." + chr(10) + "- A test proves it.",
        },
    )
    unit.dir.mkdir(parents=True)
    (unit.dir / "TASK.md").write_text(render_task_file(task), encoding="utf-8")
    (unit.dir / "PR.md").write_text(PR_TEXT if shape in ("feature", "no-test") else DOCS_TEXT, encoding="utf-8")
    if start:
        built.stamp("assigned", detail={"issues": [757]}, who=agent("coordinator"))
        built.stamp("claimed", detail={"issues": [757]}, who=ENGINEER)
    if shape == "feature":
        if "study" not in skip:
            built.study()
        if "compare" not in skip:
            built.compare()
            built.fill_pairs()
            built.compare()
        if "fails-first" not in skip:
            built.fails_first()
    elif "fails-first" not in skip:
        built.stamp("fails-first", result="none", detail={"reason": "documentation and tools, no game behaviour"})
    profile = {"feature": "full", "no-test": "full", "docs": "docs", "tooling": "tooling"}[shape]
    if "checked" not in skip:
        built.check(profile)
    if "reviewed" not in skip:
        built.review(functions={ADDRESS: "clean"} if shape == "feature" else {})
    _TEST_COMMITS[tmp_path] = built.test_commit
    return built

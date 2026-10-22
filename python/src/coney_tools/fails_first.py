# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools original fails-first`: a test that fails on the test commit and passes on the fix commit.

The workflow writes the test first and commits it alone (docs/guides/stamps.md#fails-first); the test must fail
(proving it detects the problem) and then pass once the fix is committed. This runs the named tests twice through
`coney-tools build ... --test -R` (the only way C++ tests run), on the test commit and on the fix commit, checks
out each in the unit's worktree and puts the branch back, and appends one `fails-first` stamp per test.

The verdict is a pass when the build succeeded at both commits, the test failed on the first and passed on the second. A
test that passes on the first (it detects nothing) or still fails on the second is a `fail` stamp. A build that does not
compile, a queued build or a test that does not exist is an error with no stamp: the log records verdicts, not
infrastructure faults. The test commit may carry the minimal stubs a new feature's test needs to compile, and the stamp
lists them; a stub must not implement the behaviour (return a default or an empty result). The test files must be
byte-identical in both commits: a test that had to change to pass is a question for the coordinator (`unit ask`).
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Stamps
from coney_tools.unit import Unit

#: `build` exits with this when it gave up waiting for a slot (build_run.EXIT_QUEUED).
EXIT_QUEUED = 75
#: Where a test file lives.
TEST_DIR = "tests/"


class FailsFirstError(ConfigError):
    """The tests cannot be run or the commits are not fit for it (exit 2): nothing is stamped."""


@dataclass(frozen=True)
class BuildOutcome:
    """What one `coney-tools build --test -R` run said."""

    built: bool  # configure and build succeeded
    ok: bool  # the whole run succeeded: every selected test passed
    failed: tuple[str, ...]  # names of the failed tests
    no_tests: bool  # the filter matched no test
    queued: bool  # the build gave up waiting for a slot
    output: str  # the summary the build printed

    @property
    def tests_ran(self) -> bool:
        """Whether tests were run at all."""
        return self.built and not self.no_tests and not self.queued


#: Runs the named tests in a checkout: (root, preset, names) -> outcome.
Runner = Callable[[Path, str, tuple[str, ...]], BuildOutcome]


def parse_build_output(text: str, exit_code: int) -> BuildOutcome:
    """Read the summary lines `coney-tools build` prints (`result: ok`, `result: FAILED at test: a, b`, ...)."""
    failed_at = re.search(r"^result: FAILED at (\w+)", text, re.MULTILINE)
    names: list[str] = []
    for line in text.splitlines():
        if line.startswith("  failed: "):
            names = [n.strip() for n in line[len("  failed: ") :].split(", ") if n.strip() and not n.startswith("...")]
    return BuildOutcome(
        built=exit_code != EXIT_QUEUED and (failed_at is None or failed_at.group(1) == "test"),
        ok=exit_code == 0 and bool(re.search(r"^result: ok", text, re.MULTILINE)),
        failed=tuple(names),
        no_tests="no tests match" in text,
        queued=exit_code == EXIT_QUEUED or "result: QUEUED" in text,
        output=text,
    )


def name_regex(names: Sequence[str]) -> str:
    """The CMake regular expression (`ctest -R`) that matches exactly these test names."""
    escaped = [re.sub(r"([.+*?()\[\]|^$\\-])", r"\\\1", name) for name in names]
    return "^(" + "|".join(escaped) + ")$"


def build_runner(agent: str) -> Runner:
    """A Runner that calls `coney-tools build --agent <agent> --preset <p> --test -R <names>` in the checkout."""

    def run(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        command = [
            sys.executable,
            "-c",
            "import sys; from coney_tools.cli import main; sys.exit(main(sys.argv[1:]))",
            "build",
            "--agent",
            agent,
            "--preset",
            preset,
            "--test",
            "-R",
            name_regex(names),
        ]
        done = subprocess.run(command, cwd=root, capture_output=True, encoding="utf-8", errors="replace", check=False)
        return parse_build_output(done.stdout + done.stderr, done.returncode)

    return run


# --- git ---------------------------------------------------------------------------------------------------------


def _git(root: Path, *args: str, check: bool = True) -> str:
    """Run git in `root`; its stdout (FailsFirstError with the message when it fails and `check`)."""
    done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, encoding="utf-8", check=False)
    if check and done.returncode != 0:
        raise FailsFirstError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout if done.returncode == 0 else ""


def _commit(root: Path, ref: str) -> str:
    """The full hash `ref` names; FailsFirstError when it is no commit."""
    done = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}"],
        capture_output=True,
        encoding="utf-8",
        check=False,
    )
    if done.returncode != 0:
        raise FailsFirstError(f"{ref!r} is not a commit")
    return done.stdout.strip()


def _is_ancestor(root: Path, older: str, newer: str) -> bool:
    """Whether `older` is `newer` or one of its ancestors."""
    done = subprocess.run(["git", "-C", str(root), "merge-base", "--is-ancestor", older, newer], check=False)
    return done.returncode == 0


def _changed_files(root: Path, commit: str) -> list[str]:
    """The paths a commit changes (against its first parent; all of a root commit)."""
    out = _git(root, "diff-tree", "--root", "--no-commit-id", "--name-only", "-r", "-z", commit)
    return sorted(p for p in out.split("\0") if p)


def _blob(root: Path, commit: str, path: str) -> str:
    """The blob hash of `path` at `commit`, "" when the file is not there."""
    return _git(root, "rev-parse", "--verify", "--quiet", f"{commit}:{path}", check=False).strip()


def _plan(root: Path, test_commit: str, fix_commit: str | None) -> tuple[str, str, list[str], list[str]]:
    """(test commit, fix commit, the test files, the other files) after the checks the run depends on."""
    head = _commit(root, "HEAD")
    test = _commit(root, test_commit)
    fix = _commit(root, fix_commit) if fix_commit else head
    if not _is_ancestor(root, test, fix):
        raise FailsFirstError(f"the test commit {test[:12]} is not an ancestor of the fix commit {fix[:12]}")
    if not _is_ancestor(root, fix, head):
        raise FailsFirstError(f"the fix commit {fix[:12]} is not on the checked-out branch")
    changed = _changed_files(root, test)
    tests = [p for p in changed if p.startswith(TEST_DIR)]
    if not tests:
        raise FailsFirstError(f"the test commit {test[:12]} changes no file under {TEST_DIR}")
    for path in tests:
        if _blob(root, test, path) != _blob(root, fix, path):
            raise FailsFirstError(
                f"{path} changed between the test commit and the fix commit; a test that had to change to pass is a "
                "decision point: ask the coordinator (unit ask)"
            )
    return test, fix, tests, [p for p in changed if not p.startswith(TEST_DIR)]


def _checkout(root: Path, ref: str, detach: bool = True) -> None:
    """Check `ref` out: detached for a commit, as the branch itself when it is the branch to come back to."""
    _git(root, "checkout", "--quiet", *(["--detach"] if detach else []), ref)


def _original_ref(root: Path) -> tuple[str, bool]:
    """(the branch checked out, or the commit when detached; whether it is a branch), to come back to."""
    branch = _git(root, "rev-parse", "--abbrev-ref", "HEAD").strip()
    return (_commit(root, "HEAD"), False) if branch == "HEAD" else (branch, True)


# --- the run -----------------------------------------------------------------------------------------------------


def run(
    unit: Unit,
    root: Path,
    *,
    tests: Sequence[str],
    test_commit: str,
    fix_commit: str | None,
    preset: str,
    runner: Runner,
    rules: Stamps,
    who: Mapping[str, object],
    sign: stamps.Sign | None = None,
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> list[stamps.Stamp]:
    """Run `tests` on `test_commit` and `fix_commit` and append a `fails-first` stamp for each; returns the stamps.

    Raises FailsFirstError (nothing stamped, the branch restored) for a dirty tree, commits out of order, a test file
    that changed between the commits, or a run that could not build or find the tests.
    """
    names = tuple(tests)
    if not names:
        raise FailsFirstError("name at least one test (--test NAME)")
    dirty = _git(root, "status", "--porcelain", "--untracked-files=no").strip()
    if dirty:
        raise FailsFirstError("the worktree has uncommitted changes; commit or stash them first")
    test, fix, test_files, stubs = _plan(root, test_commit, fix_commit)
    original = _original_ref(root)
    outcomes: dict[str, BuildOutcome] = {}
    try:
        for label, commit in (("red", test), ("green", fix)):
            _checkout(root, commit)
            outcomes[label] = runner(root, preset, names)
            _need_a_run(label, commit, outcomes[label])
    finally:
        _checkout(root, original[0], detach=not original[1])
    red, green = outcomes["red"], outcomes["green"]
    folder = unit.dir / "fails-first"
    folder.mkdir(parents=True, exist_ok=True)
    digests = {}
    for label, outcome in outcomes.items():
        data = outcome.output.encode("utf-8")
        (folder / f"{label}-{fix[:12]}.log").write_bytes(data)
        digests[label] = stamps.sha256_hex(data)
    tests_hash = stamps.sha256_hex(
        "".join(f"{path}\n{_blob(root, fix, path)}\n" for path in test_files).encode("utf-8")
    )
    written = []
    for name in names:
        failed_red = name in red.failed
        passed_green = green.ok or (bool(green.failed) and name not in green.failed)
        result = "pass" if failed_red and passed_green else "fail"
        written.append(
            stamps.append(
                unit.stamps,
                rules=rules,
                task=unit.task,
                step="fails-first",
                subject=name,
                result=result,
                commit=fix,
                covers={
                    "test_commit": test,
                    "fix_commit": fix,
                    "tests_sha256": tests_hash,
                    "red_log_sha256": digests["red"],
                    "green_log_sha256": digests["green"],
                },
                detail={
                    "red": "failed" if failed_red else "passed",
                    "green": "passed" if passed_green else "failed",
                    "test_files": test_files,
                    "stubs": stubs,
                },
                who=dict(who),
                sign=sign,
                clock=clock,
            )
        )
    return written


def _need_a_run(label: str, commit: str, outcome: BuildOutcome) -> None:
    """Raise unless the build at `commit` ran the tests (a verdict needs a run)."""
    if outcome.queued:
        raise FailsFirstError(f"the build at {commit[:12]} stayed queued for a slot; run it again later")
    if not outcome.built:
        raise FailsFirstError(f"the {label} commit {commit[:12]} did not build; fix the commit (see the build output)")
    if outcome.no_tests:
        raise FailsFirstError(f"no test matched the names at {commit[:12]}; check the spelling of --test")


def record_none(
    unit: Unit,
    root: Path,
    reason: str,
    *,
    rules: Stamps,
    who: Mapping[str, object],
    sign: stamps.Sign | None = None,
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> stamps.Stamp:
    """Stamp that a task has no fails-first test (docs, tooling), with the reason; nothing is run."""
    if not reason.strip():
        raise FailsFirstError("--none needs a reason")
    return stamps.append(
        unit.stamps,
        rules=rules,
        task=unit.task,
        step="fails-first",
        subject="",
        result="none",
        commit=_commit(root, "HEAD"),
        covers={},
        detail={"reason": reason.strip()},
        who=dict(who),
        sign=sign,
        clock=clock,
    )

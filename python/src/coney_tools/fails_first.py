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

import json
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
#: The longest reason `--none` takes (it goes in a stamp).
MAX_REASON = 200
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
    failed_truncated: bool  # the list ended in `... and N more`: a name missing from it is not known to pass
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
    truncated = False
    for line in text.splitlines():
        if line.startswith("  failed: "):
            parts = [n.strip() for n in line[len("  failed: ") :].split(", ") if n.strip()]
            truncated = any(part.startswith("...") for part in parts)
            names = [part for part in parts if not part.startswith("...")]
    return BuildOutcome(
        built=exit_code != EXIT_QUEUED and (failed_at is None or failed_at.group(1) == "test"),
        ok=exit_code == 0 and bool(re.search(r"^result: ok", text, re.MULTILINE)),
        failed=tuple(names),
        failed_truncated=truncated,
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


def _states(
    root: Path, preset: str, runner: Runner, names: tuple[str, ...], outcome: BuildOutcome, commit: str, logs: list[str]
) -> dict[str, str]:
    """Each name's state at the checked-out commit, "failed" or "passed". A name the run's own words settle is settled;
    one that a cut-off failed list leaves unknown (or a run that failed without saying which) is run alone."""
    states: dict[str, str] = {}
    for name in names:
        if name in outcome.failed:
            states[name] = "failed"
        elif outcome.ok:
            states[name] = "passed"
        elif outcome.failed and not outcome.failed_truncated:
            states[name] = "passed"  # a whole list of failures, and not this name among them
        elif len(names) == 1:
            states[name] = "failed"  # alone already, and it did not pass
        else:
            alone = runner(root, preset, (name,))
            logs.append(alone.output)
            _need_a_run("rerun", commit, alone)
            states[name] = "passed" if alone.ok else "failed"
    return states


def _marker(unit: Unit) -> Path:
    """The file that remembers which branch to come back to while the worktree is detached."""
    return unit.dir / "fails-first" / "restore.json"


#: The inside of a string or character literal: runs of plain characters and escapes, matched possessively so a
#: failed match never backtracks into them.
_STRING_BODY = r'(?:[^"\\\n]++|\\.)*+'
#: One string literal: its contents, escapes still raw.
_LITERAL = '"(' + _STRING_BODY + ')"'
#: A block comment or a line comment. A line comment runs to the end of its line whatever it contains.
_COMMENT = r"/\*[\s\S]*?\*/|//[^\n]*+"
#: What a scan for comments finds, left to right: a raw string (replaced by a non-literal, since a name written as
#: one is not handled), a string or character literal (kept whole: a `//`, `/*` or quote inside one is text), or
#: a comment (replaced by a space).
_RAW = r'R"(?P<delim>[^()\\\s]{0,16})\((?s:.*?)\)(?P=delim)"'
_KEEP = '"' + _STRING_BODY + '"|' + r"'(?:[^'\\\n]++|\\.)*+'"
_COMMENTS = re.compile("(?P<raw>" + _RAW + ")|(?P<keep>" + _KEEP + ")|" + _COMMENT)
#: `TEST_CASE(` and its first argument: one string literal or several adjacent ones (C++ joins them), then `)` or `,`.
#: It runs on text without comments, so the only gaps are white space, matched possessively.
_TEST_CASE = re.compile(r"TEST_CASE\s*+\(\s*+((?:" + _LITERAL + r"\s*+)+)[,)]")


def _without_comments(text: str) -> str:
    """`text` as the compiler sees it: each comment replaced by one space, each raw string by `0`, literals whole.
    One scan, left to right, so a quote inside a comment and a comment marker inside a literal are both text."""

    def one(match: re.Match[str]) -> str:
        if match.group("keep") is not None:
            return match.group(0)
        return "0" if match.group("raw") is not None else " "

    return _COMMENTS.sub(one, text)


#: The escapes that stand for one character each.
_SIMPLE_ESCAPES = {
    "'": "'",
    '"': '"',
    "?": "?",
    "\\": "\\",
    "a": "\a",
    "b": "\b",
    "f": "\f",
    "n": "\n",
    "r": "\r",
    "t": "\t",
    "v": "\v",
}
#: One escape: `\u` or `\U` and its hex digits, `\x` and its hex digits, up to three octal digits, or one character.
_ESCAPE = re.compile(r"\\(?:u([0-9A-Fa-f]{4})|U([0-9A-Fa-f]{8})|x([0-9A-Fa-f]+)|([0-7]{1,3})|(.))", re.DOTALL)


def _unescape(text: str) -> str:
    r"""The characters a string literal's contents stand for: its C++ escapes (`\"`, `\\`, `\n`, `\x41`, `\101`,
    `\u00e9`, ...) replaced by what they mean, as in the name Catch2 registers and ctest lists. An escape the language
    does not define, or a code point beyond Unicode, is left as written."""

    def one(match: re.Match[str]) -> str:
        for group in (1, 2, 3):  # a code point in hex
            if match.group(group):
                value = int(match.group(group), 16)
                return chr(value) if value <= 0x10FFFF else match.group(0)
        if match.group(4):
            return chr(int(match.group(4), 8))
        return _SIMPLE_ESCAPES.get(match.group(5), match.group(0))

    return _ESCAPE.sub(one, text)


def _joined(literals: str) -> str:
    """The name a run of adjacent string literals makes: each literal unescaped, then joined."""
    return "".join(_unescape(contents) for contents in re.findall(_LITERAL, literals))


def _declared_names(text: str) -> set[str]:
    """The names of the `TEST_CASE`s declared in `text` outside comments, as ctest lists them."""
    return {_joined(match.group(1)) for match in _TEST_CASE.finditer(_without_comments(text))}


def _check_names(root: Path, commit: str, test_files: list[str], names: Sequence[str]) -> None:
    """Every name must be a whole test name (`TEST_CASE("<name>"`, then `)` or `,` for tags) in a test file the test
    commit adds or changes: a typo, only part of a longer name, or a SECTION's name or any other quoted string would
    run nothing under ctest. Adjacent string literals are joined with the comments between them dropped and their
    escapes unescaped, as C++ does; a name a macro builds does not match."""
    text = "".join(_git(root, "show", f"{commit}:{path}") for path in test_files)
    declared = _declared_names(text)
    missing = [name for name in names if name not in declared]
    if missing:
        where = ", ".join(test_files)
        raise FailsFirstError(f"{', '.join(missing)}: not found in the test files of {commit[:12]} ({where})")


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
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> list[stamps.Stamp]:
    """Run `tests` on `test_commit` and `fix_commit` and append a `fails-first` stamp for each; returns the stamps.

    Raises FailsFirstError (nothing stamped, the branch restored) for a dirty tree, a worktree an earlier run left
    detached, commits out of order, a test file that changed between the commits, names the test commit's files do not
    hold, or a run that could not build or find the tests. The branch to come back to is written to scratch before the
    first checkout; if putting it back fails, the error says so and how to do it by hand.
    """
    names = tuple(tests)
    if not names:
        raise FailsFirstError("name at least one test (--test NAME)")
    dirty = _git(root, "status", "--porcelain", "--untracked-files=no").strip()
    if dirty:
        raise FailsFirstError("the worktree has uncommitted changes; commit or stash them first")
    original = _original_ref(root)
    marker = _marker(unit)
    if marker.is_file():
        if not original[1]:
            try:
                left = json.loads(marker.read_text(encoding="utf-8")).get("ref", "the branch")
            except (OSError, json.JSONDecodeError):
                left = "the branch"
            raise FailsFirstError(
                f"an earlier fails-first run left this worktree detached; put it back with `git checkout {left}` "
                f"(and delete {marker}), then run again"
            )
        marker.unlink()
    test, fix, test_files, stubs = _plan(root, test_commit, fix_commit)
    _check_names(root, test, test_files, names)
    marker.parent.mkdir(parents=True, exist_ok=True)
    note = {"ref": original[0], "branch": original[1], "head": _commit(root, "HEAD")}
    marker.write_text(json.dumps(note), encoding="utf-8")
    runs: dict[str, tuple[BuildOutcome, dict[str, str], list[str]]] = {}
    try:
        for label, commit in (("red", test), ("green", fix)):
            _checkout(root, commit)
            first = runner(root, preset, names)
            _need_a_run(label, commit, first)
            logs = [first.output]
            runs[label] = (first, _states(root, preset, runner, names, first, commit, logs), logs)
    finally:
        try:
            _checkout(root, original[0], detach=not original[1])
        except FailsFirstError as error:
            raise FailsFirstError(
                f"COULD NOT RESTORE the worktree: run `git checkout {original[0]}` in {root} yourself ({error})"
            ) from error
        marker.unlink(missing_ok=True)
    folder = unit.dir / "fails-first"
    digests = {}
    for label, (_, _, logs) in runs.items():
        data = "".join(logs).encode("utf-8")
        (folder / f"{label}-{test[:12]}-{fix[:12]}.log").write_bytes(data)
        digests[label] = stamps.sha256_hex(data)
    tests_hash = stamps.sha256_hex(
        "".join(f"{path}" + chr(10) + _blob(root, fix, path) + chr(10) for path in test_files).encode("utf-8")
    )
    entries = []
    for name in names:
        red, green = runs["red"][1][name], runs["green"][1][name]
        entries.append(
            {
                "step": "fails-first",
                "subject": name,
                "result": "pass" if red == "failed" and green == "passed" else "fail",
                "commit": fix,
                "covers": {
                    "test_commit": test,
                    "fix_commit": fix,
                    "tests_sha256": tests_hash,
                    "red_log_sha256": digests["red"],
                    "green_log_sha256": digests["green"],
                },
                "detail": {"red": red, "green": green, "test_files": test_files, "stubs": stubs},
                "who": dict(who),
            }
        )
    return stamps.append_many(unit.stamps, rules=rules, task=unit.task, entries=entries, clock=clock)


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
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> stamps.Stamp:
    """Stamp that a task has no fails-first test (docs, tooling), with the reason; nothing is run."""
    if not reason.strip() or len(reason.strip()) > MAX_REASON or len(reason.strip().splitlines()) > 1:
        raise FailsFirstError(f"--none needs a reason of one line and at most {MAX_REASON} characters")
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
        clock=clock,
    )

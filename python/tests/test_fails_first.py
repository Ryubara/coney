# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `original fails-first`: a test red on the test commit and green on the fix commit, with a fake build."""

from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest
from git_support import git
from stamp_support import rules

from coney_tools import fails_first, stamps
from coney_tools.fails_first import BuildOutcome, FailsFirstError
from coney_tools.unit import Unit

WHO = {"kind": "tool", "tool": "test"}
RED_OUTPUT = """log: /tmp/x.log
build: ok in 5s
test: FAILED (exit 8) in 3s (main lane): 0 passed, 1 failed of 1
  failed: Widget ready frame
result: FAILED at test: Widget ready frame
"""
GREEN_OUTPUT = """log: /tmp/x.log
build: ok in 5s
test: ok in 3s (main lane): 1 passed, 0 failed of 1
result: ok
"""


class Clock:
    """A clock that moves a second per reading."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 22, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(seconds=1)
        return self.now


class Repo:
    """A git repository with a base commit, a test commit (a test and a stub) and a fix commit."""

    def __init__(self, tmp_path: Path) -> None:
        self.root = tmp_path / "repos" / "coney"
        (self.root / "src").mkdir(parents=True)
        (self.root / "tests").mkdir()
        (self.root / "coney.local.example.toml").write_text("", encoding="utf-8")
        git(self.root, "init", "-q", "-b", "work")
        (self.root / "src" / "widget.cpp").write_text("int ready() { return 0; }\n", encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "base")
        self.base = git(self.root, "rev-parse", "HEAD").strip()
        (self.root / "tests" / "widget_test.cpp").write_text('TEST_CASE("Widget ready frame") {}\n', encoding="utf-8")
        (self.root / "src" / "widget.h").write_text("int ready();\n", encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "test")
        self.test = git(self.root, "rev-parse", "HEAD").strip()
        (self.root / "src" / "widget.cpp").write_text("int ready() { return 7; }\n", encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "fix")
        self.fix = git(self.root, "rev-parse", "HEAD").strip()
        self.unit = Unit("t", tmp_path / "scratch" / "t")
        self.seen: list[tuple[str, tuple[str, ...]]] = []
        self.red = RED_OUTPUT
        self.green = GREEN_OUTPUT

    def runner(self, root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        """A fake build: red on the test commit, green on the fix commit."""
        head = git(root, "rev-parse", "HEAD").strip()
        self.seen.append((head, names))
        text = self.red if head == self.test else self.green
        return fails_first.parse_build_output(text, 0 if "result: ok" in text else 1)

    def run(self, **more: object) -> list[stamps.Stamp]:
        """Run fails-first for the one test."""
        arguments: dict[str, object] = {
            "tests": ("Widget ready frame",),
            "test_commit": self.test,
            "fix_commit": self.fix,
            "preset": "dev",
            "runner": self.runner,
            "rules": rules(),
            "who": WHO,
            "clock": Clock(),
        }
        arguments.update(more)
        return fails_first.run(self.unit, self.root, **arguments)  # type: ignore[arg-type]


@pytest.fixture
def repo(tmp_path: Path) -> Repo:
    """A repository ready for fails-first."""
    return Repo(tmp_path)


def test_red_then_green_is_a_pass_and_the_branch_is_restored(repo: Repo) -> None:
    [stamp] = repo.run()
    assert (stamp.step, stamp.subject, stamp.result, stamp.commit) == (
        "fails-first",
        "Widget ready frame",
        "pass",
        repo.fix,
    )
    assert stamp.covers["test_commit"] == repo.test and stamp.covers["fix_commit"] == repo.fix
    assert len(stamp.covers["tests_sha256"]) == 64 and len(stamp.covers["red_log_sha256"]) == 64
    assert stamp.detail["red"] == "failed" and stamp.detail["green"] == "passed"
    assert stamp.detail["stubs"] == ["src/widget.h"]
    assert [h for h, _ in repo.seen] == [repo.test, repo.fix]
    assert git(repo.root, "rev-parse", "--abbrev-ref", "HEAD").strip() == "work"
    assert stamps.verify(repo.unit.stamps, rules()).ok


def test_the_names_are_anchored_for_ctest(repo: Repo) -> None:
    assert fails_first.name_regex(("a.b", "c (d)")) == r"^(a\.b|c \(d\))$"
    repo.run()
    assert repo.seen[0][1] == ("Widget ready frame",)


def test_a_test_that_passes_on_the_test_commit_is_a_fail_stamp(repo: Repo) -> None:
    repo.red = GREEN_OUTPUT
    [stamp] = repo.run()
    assert stamp.result == "fail" and stamp.detail["red"] == "passed"


def test_a_test_that_still_fails_on_the_fix_is_a_fail_stamp(repo: Repo) -> None:
    repo.green = RED_OUTPUT
    [stamp] = repo.run()
    assert stamp.result == "fail" and stamp.detail["green"] == "failed"


def test_each_named_test_gets_its_own_stamp(repo: Repo) -> None:
    repo.red = RED_OUTPUT.replace("failed: Widget ready frame", "failed: Widget ready frame, Widget other")
    [first, second] = repo.run(tests=("Widget ready frame", "Widget other"))
    assert (first.result, second.result) == ("pass", "pass") and second.subject == "Widget other"
    repo.red = RED_OUTPUT
    [third, fourth] = repo.run(tests=("Widget ready frame", "Widget other"))
    assert (third.result, fourth.result) == ("pass", "fail")


def test_a_build_error_is_an_error_and_writes_no_stamp(repo: Repo) -> None:
    repo.red = "build: FAILED (exit 1) in 2s\nresult: FAILED at build\n"
    with pytest.raises(FailsFirstError, match="did not build"):
        repo.run()
    assert not repo.unit.stamps.exists()
    assert git(repo.root, "rev-parse", "--abbrev-ref", "HEAD").strip() == "work"


def test_no_matching_test_is_an_error(repo: Repo) -> None:
    repo.red = "build: ok in 1s\ntest: FAILED: no tests match -R x\nresult: FAILED at test\n"
    with pytest.raises(FailsFirstError, match="no test"):
        repo.run()


def test_a_queued_build_is_an_error(repo: Repo) -> None:
    def queued(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        return fails_first.parse_build_output("result: QUEUED at build (exit 75)\n", 75)

    with pytest.raises(FailsFirstError, match=r"queued|slot"):
        repo.run(runner=queued)


def test_a_dirty_tree_is_refused_before_anything_is_checked_out(repo: Repo) -> None:
    (repo.root / "src" / "widget.cpp").write_text("changed\n", encoding="utf-8")
    with pytest.raises(FailsFirstError, match="uncommitted"):
        repo.run()
    assert repo.seen == []


def test_the_test_must_not_change_between_the_commits(repo: Repo) -> None:
    (repo.root / "tests" / "widget_test.cpp").write_text(
        'TEST_CASE("Widget ready frame") { /* weaker */ }\n', encoding="utf-8"
    )
    git(repo.root, "commit", "-q", "-am", "weaken")
    with pytest.raises(FailsFirstError, match="changed"):
        repo.run(fix_commit=git(repo.root, "rev-parse", "HEAD").strip())


def test_the_commits_must_be_in_order_and_on_the_branch(repo: Repo) -> None:
    with pytest.raises(FailsFirstError, match="ancestor"):
        repo.run(test_commit=repo.fix, fix_commit=repo.test)
    with pytest.raises(FailsFirstError, match="not a commit"):
        repo.run(test_commit="nonsense")


def test_a_test_commit_without_a_test_file_is_refused(repo: Repo) -> None:
    with pytest.raises(FailsFirstError, match="tests/"):
        repo.run(test_commit=repo.fix, fix_commit=repo.fix)


def test_fix_commit_defaults_to_head(repo: Repo) -> None:
    [stamp] = repo.run(fix_commit=None)
    assert stamp.covers["fix_commit"] == repo.fix


def test_none_records_that_there_is_no_test_to_run(repo: Repo) -> None:
    stamp = fails_first.record_none(repo.unit, repo.root, "docs only", rules=rules(), who=WHO, clock=Clock())
    assert (stamp.step, stamp.result, stamp.subject) == ("fails-first", "none", "") and stamp.detail[
        "reason"
    ] == "docs only"
    with pytest.raises(FailsFirstError, match="reason"):
        fails_first.record_none(repo.unit, repo.root, " ", rules=rules(), who=WHO, clock=Clock())


def test_the_build_output_is_read_for_its_verdict() -> None:
    red = fails_first.parse_build_output(RED_OUTPUT, 1)
    assert red.built and not red.ok and red.failed == ("Widget ready frame",) and not red.no_tests
    green = fails_first.parse_build_output(GREEN_OUTPUT, 0)
    assert green.built and green.ok and green.failed == ()
    broken = fails_first.parse_build_output("configure: FAILED (exit 1)\nresult: FAILED at configure\n", 1)
    assert not broken.built
    many = fails_first.parse_build_output(
        "build: ok\n  failed: a, b, ... and 3 more (see the log)\nresult: FAILED at test: a\n", 1
    )
    assert many.failed == ("a", "b")
    assert fails_first.parse_build_output("result: QUEUED at build (exit 75); x\n", 75).queued

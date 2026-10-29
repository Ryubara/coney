# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `orig fails-first`: a test red on the test commit and green on the fix commit, with a fake build."""

import ast
import subprocess
import sys
from collections.abc import Sequence
from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest
from git_support import git
from stamp_support import rules

from coney_tools import fails_first, stamps
from coney_tools.fails_first import BuildOutcome, FailsFirstError
from coney_tools.unit import Unit

WHO = {"kind": "tool", "tool": "test"}
ONE = ("Widget ready frame",)
BOTH = ("Widget ready frame", "Widget other")
#: The longest line a build summary may have (build_run.MAX_LINE) and how many failed names it lists (MAX_FAILED_TESTS).
MAX_LINE = 220
MAX_LISTED = 8


def write_log(folder: Path, ran: Sequence[str], failed: Sequence[str]) -> Path:
    """A build log with ctest's own result for `ran`: a line per test, the count and the list of the failed ones."""
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"build-{len(list(folder.iterdir()))}.log"
    lines = ["==== test: ctest --output-on-failure"]
    for number, name in enumerate(ran, start=1):
        state = "***Failed" if name in failed else "Passed"
        lines.append(f"{number:>2}/{len(ran)} Test #{number}: {name} {'.' * 12}   {state}    0.01 sec")
    percent = 100 * (len(ran) - len(failed)) // len(ran)
    lines += ["", f"{percent}% tests passed, {len(failed)} tests failed out of {len(ran)}"]
    if failed:
        lines += ["", "The following tests FAILED:"]
        lines += [f"\t{ran.index(name) + 1:>2} - {name} (Failed)" for name in failed]
        lines.append("Errors while running CTest")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def build_output(log: Path, ran: Sequence[str], failed: Sequence[str]) -> str:
    """What `coney-tools build --test -R` prints for a run of `ran` in which `failed` failed: the summary lines, with
    the failed list joined by ", " and cut the way the build cuts it, and the path of the log that holds the rest."""
    counts = f"{len(ran) - len(failed)} passed, {len(failed)} failed of {len(ran)}"
    if not failed:
        lines = [f"log: {log}", "build: ok in 5s", f"test: ok in 3s (main lane): {counts}", "result: ok"]
    else:
        shown = ", ".join(failed[:MAX_LISTED]).rstrip()
        shown = shown if len(shown) <= MAX_LINE else shown[: MAX_LINE - 3] + "..."
        more = f", ... and {len(failed) - MAX_LISTED} more (see the log)" if len(failed) > MAX_LISTED else ""
        lines = [
            f"log: {log}",
            "build: ok in 5s",
            f"test: FAILED (exit 8) in 3s (main lane): {counts}",
            f"  failed: {shown}{more}",
            f"result: FAILED at test: {failed[0]}",
        ]
    return "\n".join(lines) + "\n"


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
        self.logs = tmp_path / "logs"
        self.red = self.out(ONE, ONE)
        self.green = self.out(ONE, ())

    def out(self, ran: Sequence[str], failed: Sequence[str]) -> str:
        """The build's output for a run of `ran` in which `failed` failed, with its log written to disk."""
        return build_output(write_log(self.logs, ran, failed), ran, failed)

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
    repo.red = repo.out(ONE, ())
    [stamp] = repo.run()
    assert stamp.result == "fail" and stamp.detail["red"] == "passed"


def test_a_test_that_still_fails_on_the_fix_is_a_fail_stamp(repo: Repo) -> None:
    repo.green = repo.out(ONE, ONE)
    [stamp] = repo.run()
    assert stamp.result == "fail" and stamp.detail["green"] == "failed"


def test_each_named_test_gets_its_own_stamp(repo: Repo) -> None:
    first, second = two_tests(repo)

    def runner_for(red: str) -> fails_first.Runner:
        def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
            text = red if git(root, "rev-parse", "HEAD").strip() == first else repo.out(BOTH, ())
            return fails_first.parse_build_output(text, 0 if "result: ok" in text else 1)

        return runner

    [one, two] = run_two(repo, first, second, runner_for(repo.out(BOTH, BOTH)))
    assert (one.result, two.result) == ("pass", "pass") and two.subject == "Widget other"
    [three, four] = run_two(repo, first, second, runner_for(repo.out(BOTH, ("Widget ready frame",))))
    assert (three.result, four.result) == ("pass", "fail") and four.detail["red"] == "passed"


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


def test_the_build_output_is_read_for_its_verdict(tmp_path: Path) -> None:
    logs = tmp_path / "logs"
    red = fails_first.parse_build_output(build_output(write_log(logs, ONE, ONE), ONE, ONE), 1)
    assert red.built and not red.ok and red.failed == ONE and red.failed_known and not red.no_tests
    green = fails_first.parse_build_output(build_output(write_log(logs, ONE, ()), ONE, ()), 0)
    assert green.built and green.ok and green.failed == () and green.failed_known
    broken = fails_first.parse_build_output("configure: FAILED (exit 1)\nresult: FAILED at configure\n", 1)
    assert not broken.built
    assert fails_first.parse_build_output("result: QUEUED at build (exit 75); x\n", 75).queued


def test_a_name_with_a_comma_is_read_whole_from_the_log(tmp_path: Path) -> None:
    name = "CameraReset right after a teleport puts the camera behind the new place, read from the target"
    ran = (name, "Camera other")
    outcome = fails_first.parse_build_output(build_output(write_log(tmp_path, ran, (name,)), ran, (name,)), 1)
    assert outcome.failed == (name,) and outcome.failed_known


def long_names(count: int) -> tuple[str, ...]:
    """Test names long enough that the build's summary line is cut in the middle of one."""
    return tuple(
        f"Camera placement case {n} with a rather long descriptive name so that the line is cut" for n in range(count)
    )


def test_a_failed_list_the_build_cut_mid_name_loses_no_name(tmp_path: Path) -> None:
    ran = long_names(12)
    text = build_output(write_log(tmp_path, ran, ran), ran, ran)
    assert "long desc..., ... and 4 more" in text  # the summary really is cut in the middle of a name
    outcome = fails_first.parse_build_output(text, 1)
    assert outcome.failed == ran and outcome.failed_known


def test_a_log_that_cannot_be_read_or_disagrees_with_its_count_leaves_the_names_unknown(tmp_path: Path) -> None:
    ran = ("a", "b", "c")
    text = build_output(write_log(tmp_path, ran, ("a", "b")), ran, ("a", "b"))
    gone = fails_first.parse_build_output(text.replace(str(tmp_path), str(tmp_path / "missing")), 1)
    assert not gone.failed_known and gone.failed == ()
    log = Path(text.splitlines()[0].removeprefix("log: "))
    counted_two = [
        " 1/3 Test #1: a ....   ***Failed    0.01 sec",
        "33% tests passed, 2 tests failed out of 3",
        "The following tests FAILED:",
        "\t 1 - a (Failed)",
    ]
    log.write_text("\n".join(counted_two) + "\n", encoding="utf-8")  # ctest counted two failures, the log shows one
    short = fails_first.parse_build_output(text, 1)
    assert not short.failed_known and short.failed == ()
    nothing = "log: x\nbuild: ok\ntest: FAILED\nresult: FAILED at test\n"
    assert not fails_first.parse_build_output(nothing, 1).failed_known


def two_tests(repo: Repo) -> tuple[str, str]:
    """A test commit holding two tests and a fix commit after it, on a fresh branch of the repository."""
    git(repo.root, "checkout", "-q", "-B", "work", repo.base)
    (repo.root / "tests").mkdir(exist_ok=True)
    test_file = repo.root / "tests" / "widget_test.cpp"
    test_file.write_text('TEST_CASE("Widget ready frame") {}\nTEST_CASE("Widget other") {}\n', encoding="utf-8")
    (repo.root / "src" / "widget.h").write_text("int ready();\n", encoding="utf-8")
    git(repo.root, "add", "-A")
    git(repo.root, "commit", "-q", "-m", "two tests")
    first = git(repo.root, "rev-parse", "HEAD").strip()
    (repo.root / "src" / "widget.cpp").write_text("int ready() { return 9; }\n", encoding="utf-8")
    git(repo.root, "commit", "-q", "-am", "fix two")
    return first, git(repo.root, "rev-parse", "HEAD").strip()


def run_two(repo: Repo, first: str, second: str, runner: fails_first.Runner, **more: object) -> list[stamps.Stamp]:
    """Run fails-first for both tests."""
    arguments: dict[str, object] = {
        "tests": BOTH,
        "test_commit": first,
        "fix_commit": second,
        "preset": "dev",
        "runner": runner,
        "rules": rules(),
        "who": WHO,
        "clock": Clock(),
    }
    arguments.update(more)
    return fails_first.run(repo.unit, repo.root, **arguments)  # type: ignore[arg-type]


def commit_tests(repo: Repo, source: str, label: str) -> None:
    """Replace the test file on a fresh branch from the base, commit it as the test commit and then a fix after it."""
    git(repo.root, "checkout", "-q", "-B", "work", repo.base)
    (repo.root / "tests").mkdir(exist_ok=True)
    (repo.root / "tests" / "widget_test.cpp").write_text(source, encoding="utf-8")
    git(repo.root, "add", "-A")
    git(repo.root, "commit", "-q", "-m", label)
    repo.test = git(repo.root, "rev-parse", "HEAD").strip()
    (repo.root / "src" / "widget.cpp").write_text(f"int ready() {{ return {len(label)}; }}\n", encoding="utf-8")
    git(repo.root, "commit", "-q", "-am", f"fix {label}")


def test_a_test_whose_name_has_a_comma_is_graded_by_the_run_not_by_the_summary(repo: Repo) -> None:
    name = "Widget ready, then drawn"
    commit_tests(repo, f'TEST_CASE("{name}") {{}}\n', "comma")
    repo.red, repo.green = repo.out((name,), (name,)), repo.out((name,), ())
    [stamp] = repo.run(tests=(name,), test_commit=repo.test, fix_commit=None)
    assert (stamp.result, stamp.detail["red"], stamp.detail["green"]) == ("pass", "failed", "passed")
    assert [names for _, names in repo.seen] == [(name,), (name,)]  # settled by the log: no rerun


def test_a_cut_summary_does_not_turn_failed_tests_into_passed_ones(repo: Repo) -> None:
    names = long_names(5)
    ran = (*names, "Other test one", "Other test two")
    commit_tests(repo, "".join(f'TEST_CASE("{name}") {{}}\n' for name in names), "five tests")
    repo.red, repo.green = repo.out(ran, names), repo.out(ran, ())
    assert repo.red.split("failed: ")[1].splitlines()[0].endswith("...")  # the summary ends in the middle of a name
    written = repo.run(tests=names, test_commit=repo.test, fix_commit=None)
    assert [(s.subject, s.result) for s in written] == [(name, "pass") for name in names]
    assert len(repo.seen) == 2  # one run per commit: the log settled every name


def test_several_names_in_one_run_are_each_graded_by_the_log(repo: Repo) -> None:
    first, second = two_tests(repo)
    red = repo.out(BOTH, ("Widget other",))  # "Widget ready frame" passes on the test commit: it detects nothing
    runs: list[tuple[str, ...]] = []

    def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        runs.append(names)
        text = red if git(root, "rev-parse", "HEAD").strip() == first else repo.out(BOTH, ())
        return fails_first.parse_build_output(text, 0 if "result: ok" in text else 1)

    written = run_two(repo, first, second, runner)
    assert [(s.subject, s.result, s.detail["red"]) for s in written] == [
        ("Widget ready frame", "fail", "passed"),
        ("Widget other", "pass", "failed"),
    ]
    assert runs == [BOTH, BOTH]


def test_names_the_log_does_not_settle_are_run_alone(repo: Repo) -> None:
    first, second = two_tests(repo)
    calls: list[tuple[str, tuple[str, ...]]] = []

    def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        head = git(root, "rev-parse", "HEAD").strip()
        calls.append((head, names))
        if len(names) > 1:  # a run whose log is not there says only that something failed
            text = "build: ok\ntest: FAILED (exit 8)\nresult: FAILED at test\n"
        else:
            text = repo.out(names, names if head == first else ())
        return fails_first.parse_build_output(text, 0 if "result: ok" in text else 1)

    written = run_two(repo, first, second, runner)
    assert [(s.subject, s.result) for s in written] == [("Widget ready frame", "pass"), ("Widget other", "pass")]
    for commit in (first, second):
        for name in BOTH:
            assert (commit, (name,)) in calls


def test_a_name_with_a_comma_that_is_run_alone_without_a_log_still_fails(repo: Repo) -> None:
    # Alone, a run that did not pass is this test failing, whatever the cut or split summary says.
    name = "Widget ready, then drawn"
    text = f"build: ok\ntest: FAILED (exit 8)\n  failed: {name}\nresult: FAILED at test: {name}\n"
    outcome = fails_first.parse_build_output(text, 1)
    assert fails_first._states(repo.root, "dev", repo.runner, (name,), outcome, "abc", []) == {name: "failed"}


def test_the_original_ref_is_written_down_before_the_first_checkout_and_removed_after(repo: Repo) -> None:
    marker = repo.unit.dir / "fails-first" / "restore.json"
    seen: list[bool] = []

    def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        seen.append(marker.is_file())
        return repo.runner(root, preset, names)

    repo.run(runner=runner)
    assert seen == [True, True] and not marker.exists()


def test_a_run_that_left_the_worktree_detached_is_refused_with_the_way_back(repo: Repo) -> None:
    marker = repo.unit.dir / "fails-first" / "restore.json"
    marker.parent.mkdir(parents=True)
    marker.write_text('{"ref": "work", "branch": true}', encoding="utf-8")
    git(repo.root, "checkout", "-q", "--detach")
    with pytest.raises(FailsFirstError, match="git checkout work"):
        repo.run()
    assert repo.seen == []
    git(repo.root, "checkout", "-q", "work")
    repo.run()  # a stale marker with the branch checked out is just removed
    assert not marker.exists()


def test_a_restore_that_fails_is_reported_loudly_and_keeps_the_marker(
    repo: Repo, monkeypatch: pytest.MonkeyPatch
) -> None:
    real = fails_first._checkout
    calls = {"n": 0}

    def flaky(root: Path, ref: str, detach: bool = True) -> None:
        calls["n"] += 1
        if calls["n"] == 3:  # red, green, then the restore
            raise FailsFirstError("git checkout work: cannot")
        real(root, ref, detach)

    monkeypatch.setattr(fails_first, "_checkout", flaky)
    with pytest.raises(FailsFirstError, match=r"COULD NOT RESTORE.*git checkout work"):
        repo.run()
    assert (repo.unit.dir / "fails-first" / "restore.json").is_file()


def test_logs_are_named_for_the_pair_of_commits(repo: Repo) -> None:
    repo.run()
    folder = repo.unit.dir / "fails-first"
    assert (folder / f"red-{repo.test[:12]}-{repo.fix[:12]}.log").is_file()
    assert (folder / f"green-{repo.test[:12]}-{repo.fix[:12]}.log").is_file()


def test_a_name_that_is_in_no_test_file_of_the_test_commit_is_refused_before_any_build(repo: Repo) -> None:
    with pytest.raises(FailsFirstError, match="Widget misspelt"):
        repo.run(tests=("Widget ready frame", "Widget misspelt"))
    assert repo.seen == []


def test_a_name_must_match_a_whole_test_name_not_part_of_one(repo: Repo) -> None:
    # "Widget ready" is only the start of "Widget ready frame": ctest would run nothing under that exact name.
    with pytest.raises(FailsFirstError, match="Widget ready:"):
        repo.run(tests=("Widget ready",))
    assert repo.seen == []


def test_a_section_name_is_not_a_test_name(repo: Repo) -> None:
    # Only TEST_CASE("NAME" counts: a SECTION, a string in a CHECK or a comment is no test ctest could run.
    (repo.root / "tests" / "widget_test.cpp").write_text(
        'TEST_CASE("Widget ready frame") {\n  SECTION("Only a section") {}\n  CHECK(name == "A quoted string");\n}\n',
        encoding="utf-8",
    )
    git(repo.root, "commit", "-q", "-am", "sections")
    sections = git(repo.root, "rev-parse", "HEAD").strip()
    for name in ("Only a section", "A quoted string"):
        with pytest.raises(FailsFirstError, match=f"{name}:"):
            fails_first._check_names(repo.root, sections, ["tests/widget_test.cpp"], (name,))
    fails_first._check_names(repo.root, sections, ["tests/widget_test.cpp"], ("Widget ready frame",))


def test_a_test_case_with_tags_or_spacing_still_matches(repo: Repo) -> None:
    (repo.root / "tests" / "widget_test.cpp").write_text(
        'TEST_CASE( "Widget tagged" , "[widget]") {}\nTEST_CASE("Widget plain") {}\n', encoding="utf-8"
    )
    git(repo.root, "commit", "-q", "-am", "tags")
    tagged = git(repo.root, "rev-parse", "HEAD").strip()
    fails_first._check_names(repo.root, tagged, ["tests/widget_test.cpp"], ("Widget tagged", "Widget plain"))


def test_a_name_written_as_adjacent_string_literals_matches_when_joined(repo: Repo) -> None:
    (repo.root / "tests" / "widget_test.cpp").write_text(
        'TEST_CASE("Widget "\n          "split name") {}\nTEST_CASE("Widget " "other", "[tag]") {}\n',
        encoding="utf-8",
    )
    git(repo.root, "commit", "-q", "-am", "adjacent")
    adjacent = git(repo.root, "rev-parse", "HEAD").strip()
    files = ["tests/widget_test.cpp"]
    fails_first._check_names(repo.root, adjacent, files, ("Widget split name", "Widget other"))
    with pytest.raises(FailsFirstError, match="Widget:"):
        fails_first._check_names(repo.root, adjacent, files, ("Widget",))
    with pytest.raises(FailsFirstError, match="split name:"):  # only the whole joined name is a test name
        fails_first._check_names(repo.root, adjacent, files, ("split name",))


def test_none_needs_a_short_one_line_reason(repo: Repo) -> None:
    for bad in ("two\nlines", "x" * 300):
        with pytest.raises(FailsFirstError, match="reason"):
            fails_first.record_none(repo.unit, repo.root, bad, rules=rules(), who=WHO, clock=Clock())


def _declare(repo: Repo, source: str) -> tuple[str, list[str]]:
    """Commit `source` as the test file; (the commit, the test files) to check names against."""
    (repo.root / "tests" / "widget_test.cpp").write_text(source, encoding="utf-8")
    git(repo.root, "commit", "-q", "-am", "names")
    return git(repo.root, "rev-parse", "HEAD").strip(), ["tests/widget_test.cpp"]


def test_a_comment_between_adjacent_literals_is_not_part_of_the_name(repo: Repo) -> None:
    # The compiler joins "A " and "B" whatever comment sits between them; so does the check.
    commit, files = _declare(
        repo,
        'TEST_CASE("Block " /* why */ "comment") {}\n'
        'TEST_CASE("Line "  // why\n          "comment") {}\n'
        'TEST_CASE( /* first */ "Around" /* last */ , "[tag]") {}\n'
        'TEST_CASE("Both " /* a */ // b\n /* c */ "kinds") {}\n'
        'TEST_CASE("Quoted " /* say "no" */ "comment") {}\n',
    )
    names = ("Block comment", "Line comment", "Around", "Both kinds", "Quoted comment")
    fails_first._check_names(repo.root, commit, files, names)
    with pytest.raises(FailsFirstError, match="Block why comment:"):  # the comment's words are no part of a name
        fails_first._check_names(repo.root, commit, files, ("Block why comment",))


def test_comment_markers_inside_a_literal_are_text(repo: Repo) -> None:
    commit, files = _declare(repo, 'TEST_CASE("a /* b */ c // d") {}\n')
    fails_first._check_names(repo.root, commit, files, ("a /* b */ c // d",))


def test_escapes_in_a_literal_are_the_characters_ctest_registers(repo: Repo) -> None:
    commit, files = _declare(
        repo,
        r'TEST_CASE("say \"hi\"") {}' + "\n"
        r'TEST_CASE("path C:\\dir") {}' + "\n"
        r'TEST_CASE("tab\there \x41\101\u00e9") {}' + "\n"
        r'TEST_CASE("q\"" "\\") {}' + "\n",
    )
    names = ('say "hi"', "path C:\\dir", "tab\there AA\u00e9", 'q"\\')
    fails_first._check_names(repo.root, commit, files, names)
    with pytest.raises(FailsFirstError, match="say"):  # the raw spelling is not the registered name
        fails_first._check_names(repo.root, commit, files, (r"say \"hi\"",))


def test_unescaping_follows_the_c_plus_plus_rules() -> None:
    assert fails_first._unescape(r"\' \? \a\b\f\n\r\t\v") == "' ? \a\b\f\n\r\t\v"
    assert fails_first._unescape(r"\x41\x4a\0\101\1012") == "AJ\x00AA2"
    assert fails_first._unescape(r"\u00e9\U0001F600") == "\u00e9\U0001f600"
    assert fails_first._unescape(r"\\n") == "\\n"  # an escaped backslash, then a plain n
    assert fails_first._unescape(r"\q\d") == r"\q\d"  # an escape the language does not define stays as written


def _declared_within(text: str, seconds: float = 20.0) -> set[str]:
    """`declared_names(text)`, run in a child process so that a pattern that never finishes fails the test."""
    code = (
        "import sys; from coney_tools import fails_first; print(sorted(fails_first.declared_names(sys.stdin.read())))"
    )
    try:
        done = subprocess.run(
            [sys.executable, "-c", code],
            input=text,
            capture_output=True,
            encoding="utf-8",
            timeout=seconds,
            check=True,
        )
    except subprocess.TimeoutExpired:
        pytest.fail(f"the name scan did not finish in {seconds} s")
    return set(ast.literal_eval(done.stdout))


def test_a_declaration_that_does_not_match_finishes_quickly() -> None:
    # A macro-built name after long runs of comment text must be refused, not hang the regular expression.
    assert _declared_within('TEST_CASE("a" ' + "/" * 80 + "\nMACRO)") == set()
    assert _declared_within('TEST_CASE("a" ' + "/* x */ " * 40 + "MACRO)") == set()
    assert _declared_within('TEST_CASE("a" ' + "// x\n" * 40 + "MACRO)") == set()
    assert _declared_within("TEST_CASE(" + "/* x */ " * 40 + "MACRO)") == set()


def test_a_comment_does_not_end_a_name_early() -> None:
    # What follows the comment is a macro, so the name is not a literal: neither `,` nor `)` inside the comment ends it.
    assert _declared_within('TEST_CASE("Foo" // note, see\n NAME_MACRO)') == set()
    assert _declared_within('TEST_CASE("Foo" // done)\n NAME_MACRO)') == set()
    assert _declared_within('TEST_CASE("Foo" /* a, b) */ NAME_MACRO)') == set()
    assert _declared_within('TEST_CASE("Foo" // note, see\n, "[tag]")') == {"Foo"}


def test_a_test_case_inside_a_comment_is_not_declared() -> None:
    source = '// TEST_CASE("Ghost") {}\n/* TEST_CASE("Block ghost") {} */\nTEST_CASE("Real") {}\n'
    assert _declared_within(source) == {"Real"}
    # A quote that is a character, and comment markers inside a string, do not hide or invent anything.
    assert _declared_within('auto q = \'"\'; TEST_CASE("After char") {}\n') == {"After char"}
    assert _declared_within('auto s = "// TEST_CASE(\\"x\\")"; TEST_CASE("Next") {}\n') == {"Next"}
    raw = 'auto r = R"x(// "TEST_CASE("Inside raw")" )x"; TEST_CASE("After raw") {}\n'
    assert _declared_within(raw) == {"After raw"}


# --- live progress (issue 1135) ----------------------------------------------------------------------------------


def test_each_run_says_when_it_starts_and_when_it_ends_as_it_happens(repo: Repo) -> None:
    said: list[str] = []
    seen_while_running: list[list[str]] = []
    original = repo.runner

    def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        seen_while_running.append(list(said))
        return original(root, preset, names)

    repo.run(runner=runner, say=said.append)
    assert [("start the red run" in line) or ("end the red run" in line) for line in said[:2]] == [True, True]
    assert "start the green run" in said[2] and "end the green run" in said[3]
    # While the red run was running only its start had been said; while the green one ran, the red run's end too.
    assert len(seen_while_running[0]) == 1 and len(seen_while_running[1]) == 3
    assert said[0].startswith("[") and "tests failed" in said[1] and "tests passed" in said[3]


def test_the_build_runners_lines_reach_the_caller_while_the_build_runs(tmp_path: Path) -> None:
    import sys

    lines: list[str] = []
    code = "import sys, time\nprint('log: x', flush=True)\ntime.sleep(0.3)\nprint('result: ok', flush=True)\n"
    exit_code, output = fails_first.stream_lines([sys.executable, "-c", code], tmp_path, lines.append)
    assert exit_code == 0 and lines == ["log: x", "result: ok"] and output == "log: x\nresult: ok\n"
    assert fails_first.stream_lines(["no-such-program-1135"], tmp_path, lines.append)[0] == 127

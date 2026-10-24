# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `original fails-first`: a test red on the test commit and green on the fix commit, with a fake build."""

import ast
import subprocess
import sys
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
    first, second = two_tests(repo)

    def runner_for(red: str) -> fails_first.Runner:
        def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
            text = red if git(root, "rev-parse", "HEAD").strip() == first else GREEN_OUTPUT
            return fails_first.parse_build_output(text, 0 if "result: ok" in text else 1)

        return runner

    both = RED_OUTPUT.replace("failed: Widget ready frame", "failed: Widget ready frame, Widget other")
    [one, two] = run_two(repo, first, second, runner_for(both))
    assert (one.result, two.result) == ("pass", "pass") and two.subject == "Widget other"
    [three, four] = run_two(repo, first, second, runner_for(RED_OUTPUT))
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


TRUNCATED = chr(10).join(
    [
        "build: ok",
        "test: FAILED (exit 8)",
        "  failed: Widget ready frame, ... and 11 more (see the log)",
        "result: FAILED at test",
        "",
    ]
)
BOTH = ("Widget ready frame", "Widget other")


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


def test_a_truncated_failed_list_is_unknown_so_the_test_is_run_alone(repo: Repo) -> None:
    first, second = two_tests(repo)
    calls: list[tuple[str, tuple[str, ...]]] = []

    def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        head = git(root, "rev-parse", "HEAD").strip()
        calls.append((head, names))
        red = head == first
        if len(names) > 1:
            text = TRUNCATED if red else TRUNCATED.replace("Widget ready frame", "Widget third")
        else:
            text = RED_OUTPUT.replace("Widget ready frame", names[0]) if red else GREEN_OUTPUT
        return fails_first.parse_build_output(text, 0 if "result: ok" in text else 1)

    written = run_two(repo, first, second, runner)
    assert [(s.subject, s.result) for s in written] == [("Widget ready frame", "pass"), ("Widget other", "pass")]
    assert (first, ("Widget other",)) in calls  # not in the cut-off list: asked alone
    assert (second, ("Widget ready frame",)) in calls  # a failed run with a cut-off list does not say it passed
    assert (second, ("Widget other",)) in calls


def test_a_test_passes_on_the_fix_only_when_the_run_was_ok_or_it_ran_alone_and_was_ok(repo: Repo) -> None:
    first, second = two_tests(repo)

    def runner(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        head = git(root, "rev-parse", "HEAD").strip()
        if head == first:
            return fails_first.parse_build_output(RED_OUTPUT.replace("Widget ready frame", ", ".join(BOTH)), 1)
        text = "build: ok\ntest: FAILED\nresult: FAILED at test\n"  # failed with no names: stopped from outside
        return fails_first.parse_build_output(
            text if len(names) > 1 else GREEN_OUTPUT.replace("ok in 3s", "ok"), 1 if len(names) > 1 else 0
        )

    assert [s.result for s in run_two(repo, first, second, runner)] == ["pass", "pass"]


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

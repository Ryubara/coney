# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the per-pull-request clang-tidy job (`ci tidy-plan`, `ci tidy-run`): made-up diffs, trees, clang-tidy."""

import argparse
import json
import re
from pathlib import Path

import pytest
from conventions_support import REPO, write_checkout

from coney_tools import ci_cli
from coney_tools.ci_tidy import (
    Finding,
    TidyPlan,
    build_setup_files,
    changed_lines,
    make_plan,
    parse_findings,
    run_plan,
    summary_lines,
    to_annotations,
    to_sarif,
    touched_files,
)
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions

SETTINGS = load_conventions(REPO).ci.tidy


def write(root: Path, name: str, text: str = "int x;\n") -> None:
    """Write a file below `root`, making its folders."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


@pytest.fixture
def tree(tmp_path: Path) -> Path:
    """A small tree: brain.h is included by brain.cpp and walk.h; walk.h by walk.cpp and a test; other.cpp is apart."""
    write(tmp_path, "src/ai/brain.h", "// brain\n")
    write(tmp_path, "src/ai/brain.cpp", '#include "ai/brain.h"\n')
    write(tmp_path, "src/ai/walk.h", '#include "ai/brain.h"\n')
    write(tmp_path, "src/ai/walk.cpp", '#include "ai/walk.h"\n')
    write(tmp_path, "src/ai/lonely.h", "// no includer\n")
    write(tmp_path, "src/other.cpp", "int x;\n")
    write(tmp_path, "tests/ai/walk_test.cpp", '#include "ai/walk.h"\n')
    write(tmp_path, "docs/page.md", "# page\n")
    write(tmp_path, "_deps/lib/src/x.cpp", '#include "ai/brain.h"\n')
    return tmp_path


# --- reading the diff -------------------------------------------------------------------------------------------------

DIFF = """\
diff --git a/src/ai/brain.cpp b/src/ai/brain.cpp
index 1..2 100644
--- a/src/ai/brain.cpp
+++ b/src/ai/brain.cpp
@@ -3 +3,2 @@ void f()
-old
+new one
+new two
@@ -10,2 +11,0 @@ void g()
-gone
-gone too
@@ -20,0 +22,3 @@ void h()
+a
+b
+c
diff --git a/docs/page.md b/docs/page.md
--- a/docs/page.md
+++ b/docs/page.md
@@ -1 +1 @@
-x
+y
diff --git a/src/old.cpp b/src/old.cpp
deleted file mode 100644
--- a/src/old.cpp
+++ /dev/null
@@ -1,2 +0,0 @@
-a
-b
diff --git a/src/new.cpp b/src/new.cpp
new file mode 100644
--- /dev/null
+++ b/src/new.cpp
@@ -0,0 +1,4 @@
+a
+b
+c
+d
"""


def test_the_changed_lines_are_the_added_ones_of_each_file() -> None:
    assert changed_lines(DIFF) == {
        "src/ai/brain.cpp": [(3, 4), (22, 24)],
        "docs/page.md": [(1, 1)],
        "src/new.cpp": [(1, 4)],
    }


def test_a_pure_deletion_and_a_deleted_file_have_no_changed_lines() -> None:
    assert changed_lines(DIFF).get("src/old.cpp") is None
    assert changed_lines("") == {}


def test_the_touched_files_include_a_deleted_one_and_list_each_once() -> None:
    assert touched_files(DIFF) == ["src/ai/brain.cpp", "docs/page.md", "src/old.cpp", "src/new.cpp"]
    assert touched_files("") == []


def test_a_path_with_a_space_or_quotes_is_read_whole() -> None:
    diff = "--- a/src/a b.cpp\n+++ b/src/a b.cpp\n@@ -1 +1 @@\n-x\n+y\n"
    assert changed_lines(diff) == {"src/a b.cpp": [(1, 1)]}
    diff = '--- "a/src/\\303\\251.cpp"\n+++ "b/src/\\303\\251.cpp"\n@@ -1 +1 @@\n-x\n+y\n'
    assert changed_lines(diff) == {"src/é.cpp": [(1, 1)]}


# --- choosing what to check -------------------------------------------------------------------------------------------


def names(plan: TidyPlan) -> list[str]:
    """The sources the plan runs."""
    return [item.source for item in plan.units]


def lines_of(plan: TidyPlan) -> dict[str, list[tuple[int, int]]]:
    """The line filter of the plan as {file: ranges}."""
    return {
        str(entry["name"]): [(pair[0], pair[1]) for pair in entry["lines"]]  # type: ignore[attr-defined]
        for entry in plan.line_filter()
    }


def test_a_changed_source_is_checked_itself_on_its_changed_lines(tree: Path) -> None:
    plan = make_plan({"src/other.cpp": [(2, 3)]}, tree, SETTINGS)
    assert names(plan) == ["src/other.cpp"]
    assert lines_of(plan) == {"src/other.cpp": [(2, 3)]}


def test_a_changed_header_is_checked_through_one_includer_with_its_own_lines(tree: Path) -> None:
    plan = make_plan({"src/ai/brain.h": [(1, 5)]}, tree, SETTINGS)
    # brain.cpp is the header's own source (the same name): the one includer, not walk.cpp and the test as well
    assert names(plan) == ["src/ai/brain.cpp"]
    assert lines_of(plan) == {"src/ai/brain.h": [(1, 5)]}


def test_a_header_without_a_same_named_source_is_checked_through_the_nearest_includer(tree: Path) -> None:
    plan = make_plan({"src/ai/walk.h": [(2, 2)]}, tree, SETTINGS)
    assert names(plan) == ["src/ai/walk.cpp"]


def test_a_header_included_only_through_another_header_is_found(tree: Path) -> None:
    write(tree, "src/ai/deep.h", "// deep\n")
    write(tree, "src/ai/mid.h", '#include "ai/deep.h"\n')
    write(tree, "src/ai/mid_user.cpp", '#include "ai/mid.h"\n')
    plan = make_plan({"src/ai/deep.h": [(1, 1)]}, tree, SETTINGS)
    assert names(plan) == ["src/ai/mid_user.cpp"]


def test_a_test_may_be_the_includer(tree: Path) -> None:
    write(tree, "src/ai/only_tests.h", "// x\n")
    write(tree, "tests/ai/only_tests_test.cpp", '#include "ai/only_tests.h"\n')
    plan = make_plan({"src/ai/only_tests.h": [(1, 1)]}, tree, SETTINGS)
    assert names(plan) == ["tests/ai/only_tests_test.cpp"]


def test_a_header_no_source_includes_is_noted_and_not_checked(tree: Path) -> None:
    plan = make_plan({"src/ai/lonely.h": [(1, 1)]}, tree, SETTINGS)
    assert names(plan) == []
    assert plan.notes == ["src/ai/lonely.h: no source includes it, so only the nightly pass checks it"]


def test_a_source_and_a_header_that_share_a_unit_merge_their_lines(tree: Path) -> None:
    plan = make_plan({"src/ai/brain.cpp": [(1, 1)], "src/ai/brain.h": [(2, 3)]}, tree, SETTINGS)
    assert names(plan) == ["src/ai/brain.cpp"]
    assert lines_of(plan) == {"src/ai/brain.cpp": [(1, 1)], "src/ai/brain.h": [(2, 3)]}


def test_files_outside_the_folders_and_other_extensions_are_left_alone(tree: Path) -> None:
    plan = make_plan(
        {"docs/page.md": [(1, 1)], "_deps/lib/src/x.cpp": [(1, 1)], "src/data.txt": [(1, 1)], "src/ai/brain.hpp": []},
        tree,
        SETTINGS,
    )
    assert names(plan) == [] and plan.notes == []


def test_a_deleted_source_is_not_checked(tree: Path) -> None:
    assert names(make_plan({"src/gone.cpp": [(1, 2)]}, tree, SETTINGS)) == []


@pytest.mark.parametrize(
    "path", ["CMakeLists.txt", "cmake/deps.cmake", "cmake/ffmpeg/CMakeLists.txt", "CMakePresets.json", ".clang-tidy"]
)
def test_a_build_setup_change_checks_the_whole_codebase(tree: Path, path: str) -> None:
    plan = make_plan({path: [(1, 1)]}, tree, SETTINGS)
    assert plan.everything
    assert any("every file" in note and path in note for note in plan.notes)


def test_a_deleted_build_setup_file_counts_though_it_adds_no_line(tree: Path) -> None:
    diff = "--- a/cmake/old.cmake\n+++ /dev/null\n@@ -1,2 +0,0 @@\n-a\n-b\n"
    plan = make_plan(changed_lines(diff), tree, SETTINGS, touched_files(diff))
    assert plan.everything


def test_every_other_change_is_checked_on_its_changed_lines_only(tree: Path) -> None:
    changed = {
        "src/other.cpp": [(1, 1)],
        "docs/page.md": [(1, 1)],
        "src/CMakeLists.txt": [(1, 1)],
        "tests/cmake/x.cpp": [],
    }
    plan = make_plan(changed, tree, SETTINGS, touched_files("--- a/README.md\n+++ b/README.md\n@@ -1 +1 @@\n-a\n+b\n"))
    assert not plan.everything and plan.notes == []


def test_a_build_setup_change_still_plans_its_own_sources_and_lines(tree: Path) -> None:
    plan = make_plan({"CMakeLists.txt": [(1, 1)], "src/other.cpp": [(2, 3)]}, tree, SETTINGS)
    assert plan.everything and names(plan) == ["src/other.cpp"] and lines_of(plan) == {"src/other.cpp": [(2, 3)]}


def test_the_build_setup_files_are_listed_sorted_and_once() -> None:
    found = build_setup_files(
        ["src/a.cpp", ".clang-tidy", "cmake/b.cmake", "cmake/b.cmake", "CMakeLists.txt"], SETTINGS
    )
    assert found == [".clang-tidy", "CMakeLists.txt", "cmake/b.cmake"]
    assert build_setup_files(["src/CMakeLists.txt", "docs/cmake/x.md", "CMakeLists.txt.bak"], SETTINGS) == []


def test_the_sources_come_out_sorted_and_once_each(tree: Path) -> None:
    plan = make_plan(
        {"src/other.cpp": [(1, 1)], "src/ai/walk.cpp": [(1, 1)], "src/ai/brain.cpp": [(1, 1)]}, tree, SETTINGS
    )
    assert names(plan) == ["src/ai/brain.cpp", "src/ai/walk.cpp", "src/other.cpp"]


def test_the_plan_round_trips_through_json(tree: Path) -> None:
    plan = make_plan({"src/ai/brain.h": [(1, 5)], "src/other.cpp": [(2, 3)]}, tree, SETTINGS)
    again = TidyPlan.from_json(json.loads(json.dumps(plan.as_json())))
    assert names(again) == names(plan) and again.line_filter() == plan.line_filter() and not again.everything
    whole = make_plan({"CMakeLists.txt": [(1, 1)]}, tree, SETTINGS)
    assert TidyPlan.from_json(json.loads(json.dumps(whole.as_json()))).everything


# --- reading clang-tidy's output --------------------------------------------------------------------------------------

OUTPUT = """\
2 warnings generated.
/work/coney/src/ai/brain.cpp:12:5: error: use of a moved-from object [bugprone-use-after-move]
   12 |     x.f();
      |     ^
/work/coney/src/ai/brain.cpp:9:9: note: move occurred here
/work/coney/src/ai/brain.h:3:1: warning: redundant expression [misc-redundant-expression]
Suppressed 1 warnings (1 in non-user code).
"""


def test_findings_are_read_from_clang_tidys_text() -> None:
    found = parse_findings(OUTPUT, Path("/work/coney"))
    assert found == [
        Finding("src/ai/brain.cpp", 12, 5, "error", "bugprone-use-after-move", "use of a moved-from object"),
        Finding("src/ai/brain.h", 3, 1, "warning", "misc-redundant-expression", "redundant expression"),
    ]


def test_a_windows_path_is_made_relative_too() -> None:
    text = r"C:\work\coney\src\a.cpp:1:2: error: bad [bugprone-x]" + "\n"
    assert parse_findings(text, Path("C:/work/coney"))[0].path == "src/a.cpp"


def test_a_path_outside_the_checkout_is_kept_as_it_is() -> None:
    assert (
        parse_findings("/usr/include/x.h:1:1: error: bad [bugprone-x]\n", Path("/work/coney"))[0].path
        == "/usr/include/x.h"
    )


def test_notes_and_chatter_are_not_findings() -> None:
    assert parse_findings("/w/a.cpp:1:1: note: here\nSuppressed 3 warnings\n", Path("/w")) == []


# --- SARIF and annotations --------------------------------------------------------------------------------------------


def test_sarif_lists_each_finding_with_its_rule_and_place() -> None:
    findings = [
        Finding("src/a.cpp", 12, 5, "error", "bugprone-use-after-move", "use after move"),
        Finding("src/b.h", 3, 1, "warning", "misc-redundant-expression", "redundant"),
        Finding("src/a.cpp", 20, 2, "error", "bugprone-use-after-move", "use after move again"),
    ]
    sarif = to_sarif(findings, "clang-tidy 19")
    assert sarif["version"] == "2.1.0" and "$schema" in sarif
    run = sarif["runs"][0]
    assert run["tool"]["driver"]["name"] == "clang-tidy"
    assert [rule["id"] for rule in run["tool"]["driver"]["rules"]] == [
        "bugprone-use-after-move",
        "misc-redundant-expression",
    ]
    first = run["results"][0]
    assert first["ruleId"] == "bugprone-use-after-move" and first["ruleIndex"] == 0 and first["level"] == "error"
    assert first["message"]["text"] == "use after move"
    place = first["locations"][0]["physicalLocation"]
    assert place["artifactLocation"] == {"uri": "src/a.cpp", "uriBaseId": "%SRCROOT%"}
    assert place["region"] == {"startLine": 12, "startColumn": 5}
    assert run["results"][1]["level"] == "warning" and run["results"][2]["ruleIndex"] == 0


def test_an_empty_sarif_is_still_a_valid_run() -> None:
    run = to_sarif([], "clang-tidy 19")["runs"][0]
    assert run["results"] == [] and run["tool"]["driver"]["rules"] == []


def test_annotations_are_workflow_commands_with_escaped_text() -> None:
    finding = Finding("src/a.cpp", 12, 5, "error", "bugprone-x", "100% bad: line\nbreak, really")
    assert to_annotations([finding]) == [
        "::error file=src/a.cpp,line=12,col=5,title=clang-tidy bugprone-x::100%25 bad: line%0Abreak, really"
    ]
    warning = Finding("a,b.cpp", 1, 1, "warning", "x-y", "m")
    assert to_annotations([warning])[0].startswith("::warning file=a%2Cb.cpp,line=1,col=1,")


def test_the_summary_counts_and_lists_up_to_the_limit() -> None:
    findings = [Finding("src/a.cpp", n, 1, "error", "bugprone-x", f"problem {n}") for n in range(1, 6)]
    lines = summary_lines(findings, ["src/a.cpp", "src/b.cpp"], limit=2, notes=["a note"])
    text = "\n".join(lines)
    assert "clang-tidy: 5 findings on the lines this change touches (2 files checked)" in text
    assert "src/a.cpp:1:1" in text and "src/a.cpp:2:1" in text and "src/a.cpp:3:1" not in text
    assert "3 more" in text and "a note" in text


def test_the_summary_of_a_clean_run_says_so() -> None:
    assert (
        summary_lines([], ["src/a.cpp"], 40, [])[0]
        == "clang-tidy: clean on the lines this change touches (1 file checked)"
    )
    assert "no file to check" in summary_lines([], [], 40, [])[0]


# --- running it -------------------------------------------------------------------------------------------------------


class FakeTidy:
    """A clang-tidy stand-in: the output and exit code per source, and a record of every command."""

    def __init__(self, outputs: dict[str, tuple[int, str]] | None = None) -> None:
        self.outputs = outputs or {}
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], cwd: Path) -> tuple[int, str]:
        self.commands.append(command)
        return self.outputs.get(command[-1], (0, ""))


def a_plan(tree: Path) -> TidyPlan:
    """A plan of two units, one reached through a header."""
    return make_plan({"src/other.cpp": [(1, 2)], "src/ai/brain.h": [(1, 5)]}, tree, SETTINGS)


def test_each_unit_runs_with_the_line_filter_and_the_build_folder(tree: Path) -> None:
    fake = FakeTidy()
    result = run_plan(a_plan(tree), tree, tree / "build" / "ci", "clang-tidy-19", 2, fake)
    assert result.ok and result.findings == []
    assert sorted(command[-1] for command in fake.commands) == ["src/ai/brain.cpp", "src/other.cpp"]
    command = fake.commands[0]
    assert command[0] == "clang-tidy-19" and "-p" in command and str(tree / "build" / "ci") in command
    assert "--quiet" in command
    filters = [arg for arg in command if arg.startswith("--line-filter=")]
    assert len(filters) == 1
    assert json.loads(filters[0].split("=", 1)[1]) == a_plan(tree).line_filter()


def test_findings_from_every_unit_are_collected_and_duplicates_dropped(tree: Path) -> None:
    both = f"{tree}/src/ai/brain.h:3:1: error: bad [bugprone-x]\n"
    fake = FakeTidy(
        {
            "src/ai/brain.cpp": (1, both),
            "src/other.cpp": (1, f"{tree}/src/other.cpp:2:1: error: worse [bugprone-y]\n" + both),
        }
    )
    result = run_plan(a_plan(tree), tree, tree / "build", "clang-tidy-19", 1, fake)
    assert not result.ok
    assert [(item.path, item.line) for item in result.findings] == [("src/ai/brain.h", 3), ("src/other.cpp", 2)]


def test_a_failure_with_no_finding_is_a_tool_failure_not_a_pass(tree: Path) -> None:
    fake = FakeTidy({"src/other.cpp": (1, "Error while processing src/other.cpp.\nerror: no compilation database\n")})
    result = run_plan(a_plan(tree), tree, tree / "build", "clang-tidy-19", 1, fake)
    assert not result.ok and result.findings == []
    assert result.failures == ["src/other.cpp: clang-tidy exited 1 with no finding: error: no compilation database"]


def test_a_missing_clang_tidy_is_a_tool_failure(tree: Path) -> None:
    fake = FakeTidy({"src/other.cpp": (127, "clang-tidy-19: no such file")})
    result = run_plan(a_plan(tree), tree, tree / "build", "clang-tidy-19", 1, fake)
    assert not result.ok and "127" in result.failures[0]


def test_an_empty_plan_runs_nothing_and_passes(tree: Path) -> None:
    fake = FakeTidy()
    result = run_plan(make_plan({}, tree, SETTINGS), tree, tree / "build", "clang-tidy-19", 4, fake)
    assert result.ok and fake.commands == []


# --- the commands -----------------------------------------------------------------------------------------------------


def command(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, *flags: str, tidy: FakeTidy | None = None) -> int:
    """Run `ci <flags>` in a checkout holding the real conventions and the `tree` files."""
    write_checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    parser = argparse.ArgumentParser()
    ci_cli.add_ci_parser(parser.add_subparsers(dest="group"))
    return ci_cli.run_ci(parser.parse_args(["ci", *flags]), tidy_runner=tidy or FakeTidy())


def test_tidy_plan_writes_the_plan_for_a_diff(
    tree: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tree / "change.diff").write_text(
        "--- a/src/other.cpp\n+++ b/src/other.cpp\n@@ -1 +1,2 @@\n-a\n+b\n+c\n", encoding="utf-8"
    )
    assert command(tree, monkeypatch, "tidy-plan", "--diff", "change.diff", "--out", "plan.json") == 0
    assert capsys.readouterr().out.splitlines()[0] == "clang-tidy will check 1 file:"
    plan = TidyPlan.from_json(json.loads((tree / "plan.json").read_text(encoding="utf-8")))
    assert names(plan) == ["src/other.cpp"] and lines_of(plan) == {"src/other.cpp": [(1, 2)]}


def test_tidy_plan_of_a_change_with_nothing_to_check_says_so(
    tree: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tree / "change.diff").write_text("--- a/docs/page.md\n+++ b/docs/page.md\n@@ -1 +1 @@\n-a\n+b\n", encoding="utf-8")
    assert command(tree, monkeypatch, "tidy-plan", "--diff", "change.diff", "--out", "plan.json") == 0
    assert "nothing for clang-tidy to check" in capsys.readouterr().out.lower()


def test_tidy_plan_reports_to_the_github_output_file(tree: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tree / "change.diff").write_text(
        "--- a/src/other.cpp\n+++ b/src/other.cpp\n@@ -1 +1 @@\n-a\n+b\n", encoding="utf-8"
    )
    output = tree / "github-output.txt"
    monkeypatch.setenv("GITHUB_OUTPUT", str(output))
    assert command(tree, monkeypatch, "tidy-plan", "--diff", "change.diff", "--out", "plan.json") == 0
    assert output.read_text(encoding="utf-8").splitlines() == ["run=true", "all=false"]


def test_tidy_plan_of_a_build_setup_change_sends_the_job_to_the_whole_pass(
    tree: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tree / "change.diff").write_text("--- a/cmake/old.cmake\n+++ /dev/null\n@@ -1 +0,0 @@\n-a\n", encoding="utf-8")
    output = tree / "github-output.txt"
    monkeypatch.setenv("GITHUB_OUTPUT", str(output))
    assert command(tree, monkeypatch, "tidy-plan", "--diff", "change.diff", "--out", "plan.json") == 0
    assert "every file" in capsys.readouterr().out
    assert output.read_text(encoding="utf-8").splitlines() == ["run=true", "all=true"]
    assert TidyPlan.from_json(json.loads((tree / "plan.json").read_text(encoding="utf-8"))).everything


def test_tidy_plan_of_an_ordinary_change_does_not_ask_for_the_whole_pass(
    tree: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    (tree / "change.diff").write_text("--- a/docs/page.md\n+++ b/docs/page.md\n@@ -1 +1 @@\n-a\n+b\n", encoding="utf-8")
    output = tree / "github-output.txt"
    monkeypatch.setenv("GITHUB_OUTPUT", str(output))
    assert command(tree, monkeypatch, "tidy-plan", "--diff", "change.diff", "--out", "plan.json") == 0
    assert output.read_text(encoding="utf-8").splitlines() == ["run=false", "all=false"]


def test_tidy_run_refuses_a_plan_for_every_file(tree: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    plan = make_plan({"CMakeLists.txt": [(1, 1)]}, tree, SETTINGS)
    (tree / "plan.json").write_text(json.dumps(plan.as_json()), encoding="utf-8")
    fake = FakeTidy()
    with pytest.raises(ConfigError, match="every file"):
        command(tree, monkeypatch, "tidy-run", "--plan", "plan.json", "--build-dir", "build", tidy=fake)
    assert fake.commands == []


def test_tidy_run_writes_sarif_and_the_summary_and_fails_on_a_finding(
    tree: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    plan = a_plan(tree)
    (tree / "plan.json").write_text(json.dumps(plan.as_json()), encoding="utf-8")
    bad = f"{tree}/src/other.cpp:2:1: error: bad [bugprone-x]\n"
    fake = FakeTidy({"src/other.cpp": (1, bad)})
    flags = (
        "tidy-run",
        "--plan",
        "plan.json",
        "--build-dir",
        "build/ci",
        "--sarif",
        "out.sarif",
        "--summary",
        "sum.md",
    )
    assert command(tree, monkeypatch, *flags, "--jobs", "1", tidy=fake) == 1
    sarif = json.loads((tree / "out.sarif").read_text(encoding="utf-8"))
    assert sarif["runs"][0]["results"][0]["ruleId"] == "bugprone-x"
    assert "1 finding" in (tree / "sum.md").read_text(encoding="utf-8")
    out = capsys.readouterr().out
    assert "src/other.cpp:2:1: error: bad [bugprone-x]" in out


def test_tidy_run_annotates_when_asked(
    tree: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tree / "plan.json").write_text(json.dumps(a_plan(tree).as_json()), encoding="utf-8")
    fake = FakeTidy({"src/other.cpp": (1, f"{tree}/src/other.cpp:2:1: error: bad [bugprone-x]\n")})
    code = command(
        tree, monkeypatch, "tidy-run", "--plan", "plan.json", "--build-dir", "build/ci", "--annotate", tidy=fake
    )
    assert code == 1 and re.search(r"^::error file=src/other.cpp,line=2,col=1,", capsys.readouterr().out, re.MULTILINE)


def test_tidy_run_passes_on_a_clean_plan_and_still_writes_an_empty_sarif(
    tree: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    (tree / "plan.json").write_text(json.dumps(a_plan(tree).as_json()), encoding="utf-8")
    flags = ("tidy-run", "--plan", "plan.json", "--build-dir", "build/ci", "--sarif", "out.sarif")
    assert command(tree, monkeypatch, *flags) == 0
    assert json.loads((tree / "out.sarif").read_text(encoding="utf-8"))["runs"][0]["results"] == []


def test_tidy_run_with_an_unreadable_plan_is_an_error(tree: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    with pytest.raises(ConfigError, match=r"plan\.json"):
        command(tree, monkeypatch, "tidy-run", "--plan", "plan.json", "--build-dir", "build/ci")


# --- the workflow -----------------------------------------------------------------------------------------------------


def build_workflow() -> dict[str, object]:
    """The build workflow, parsed."""
    import yaml

    loaded: dict[str, object] = yaml.safe_load(
        (REPO / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
    )
    return loaded


def test_the_workflow_uploads_sarif_under_the_category_of_the_conventions() -> None:
    jobs = build_workflow()["jobs"]
    steps = jobs["clang-tidy"]["steps"]  # type: ignore[index]
    upload = next(step for step in steps if str(step.get("uses", "")).startswith("github/codeql-action/upload-sarif@"))
    assert upload["with"]["category"] == SETTINGS.sarif_category
    # pinned to a full commit, never a tag
    assert re.fullmatch(r"[0-9a-f]{40}", upload["uses"].split("@", 1)[1].split()[0])


def test_the_pull_request_job_is_the_required_check_and_the_nightly_run_checks_everything() -> None:
    job = build_workflow()["jobs"]["clang-tidy"]  # type: ignore[index]
    assert job["permissions"]["security-events"] == "write"
    commands = "\n".join(str(step.get("run", "")) for step in job["steps"])
    assert "ci tidy-plan" in commands and "ci tidy-run" in commands and "run-clang-tidy-19" in commands
    # the whole-codebase pass runs when the plan says all (always for the nightly and manual runs, which set it), and
    # the changed-lines run and the SARIF upload then stand aside: there is no SARIF file for a whole pass
    steps = job["steps"]
    whole = next(step for step in steps if "run-clang-tidy-19" in str(step.get("run", "")))
    assert whole["if"] == "steps.plan.outputs.all == 'true'"
    plan = next(step for step in steps if step.get("id") == "plan")
    assert 'echo "all=true"' in plan["run"] and "--diff-filter" not in plan["run"]
    lines = next(step for step in steps if "ci tidy-run" in str(step.get("run", "")))
    upload = next(step for step in steps if str(step.get("uses", "")).startswith("github/codeql-action/upload-sarif@"))
    assert "steps.plan.outputs.all != 'true'" in lines["if"] and "steps.plan.outputs.all != 'true'" in upload["if"]


def test_a_push_is_tidied_on_its_changed_lines_and_only_nightly_and_manual_runs_take_the_whole_pass() -> None:
    workflow = build_workflow()
    # PyYAML reads the `on:` key as True
    triggers = set(workflow.get("on", workflow.get(True)))  # type: ignore[call-overload]
    assert "push" in triggers and "schedule" in triggers and "workflow_dispatch" in triggers
    steps = workflow["jobs"]["clang-tidy"]["steps"]  # type: ignore[index]
    plan = next(step for step in steps if step.get("id") == "plan")["run"]
    # the diff branch takes pull requests and pushes; only the else branch (schedule, workflow_dispatch) sets all=true
    diff_branch, whole_branch = plan.split("else", 1)
    assert '"$EVENT" = pull_request' in diff_branch and '"$EVENT" = push' in diff_branch
    assert "ci tidy-plan" in diff_branch and "all=true" not in diff_branch and "all=true" in whole_branch
    # the changed-lines run is not limited to pull requests, and the fork flag is false outside a pull request
    lines = next(step for step in steps if "ci tidy-run" in str(step.get("run", "")))
    assert "pull_request" not in lines["if"]
    assert "github.event_name == 'pull_request' &&" in lines["env"]["FORK"]


# --- a filter too big for one argument -----------------------------------------------------------------------------


def test_only_findings_on_the_changed_lines_are_kept_even_when_clang_tidy_reported_more(tree: Path) -> None:
    text = (
        f"{tree}/src/other.cpp:2:1: error: on a changed line [bugprone-x]\n"
        f"{tree}/src/other.cpp:50:1: error: not changed [bugprone-x]\n"
        f"{tree}/src/ai/brain.h:9:1: error: not changed either [bugprone-y]\n"
    )
    result = run_plan(a_plan(tree), tree, tree / "build", "clang-tidy", 1, FakeTidy({"src/other.cpp": (1, text)}))
    assert [(item.path, item.line) for item in result.findings] == [("src/other.cpp", 2)]
    # clang-tidy exited 1 for findings the plan does not want: that is neither a failure nor a pass-with-findings
    assert result.failures == []


def test_a_unit_that_does_not_compile_is_a_failure_even_off_the_changed_lines(tree: Path) -> None:
    text = f"{tree}/src/other.cpp:50:10: error: 'gone.h' file not found [clang-diagnostic-error]\n"
    result = run_plan(a_plan(tree), tree, tree / "build", "clang-tidy", 1, FakeTidy({"src/other.cpp": (1, text)}))
    # line 50 is not a changed line, so it is no finding, but the unit was never analysed: that is no pass
    assert result.findings == []
    assert len(result.failures) == 1 and "does not compile" in result.failures[0] and "gone.h" in result.failures[0]


def test_a_huge_filter_is_left_off_the_command_line_and_applied_afterwards(tree: Path) -> None:
    plan = a_plan(tree)
    plan.lines = {f"src/file{n}.cpp": [(i, i) for i in range(1, 400, 2)] for n in range(400)} | plan.lines
    fake = FakeTidy({"src/other.cpp": (1, f"{tree}/src/other.cpp:2:1: error: bad [bugprone-x]\n")})
    result = run_plan(plan, tree, tree / "build", "clang-tidy", 1, fake)
    assert not any(arg.startswith("--line-filter=") for command in fake.commands for arg in command)
    assert [(item.path, item.line) for item in result.findings] == [("src/other.cpp", 2)]
    assert max(len(arg) for command in fake.commands for arg in command) < 100_000


def test_a_small_filter_goes_to_clang_tidy(tree: Path) -> None:
    fake = FakeTidy()
    run_plan(a_plan(tree), tree, tree / "build", "clang-tidy", 1, fake)
    assert all(any(arg.startswith("--line-filter=") for arg in command) for command in fake.commands)

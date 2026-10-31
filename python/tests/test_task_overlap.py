# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the pending work that overlaps a unit (task_overlap, task_cli's `task overlap`).

Invented paths, titles and addresses only. GitHub is a canned `gh`, the units are records in a temporary scratch folder
and what a unit changes is injected, so nothing reaches the network or another worktree.
"""

import argparse
import json
from dataclasses import replace
from datetime import UTC, datetime
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from git_support import git

from coney_tools import stamps, task_cli, task_overlap
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.task_file import TaskFile, parse_task_file, render_task_file
from coney_tools.task_overlap import Change, OverlapEnv, Pending, Wanted
from coney_tools.unit_record import UnitRecord, read_record, write_record

NOW = datetime(2026, 10, 31, 12, 0, tzinfo=UTC)
USER = "Ryubara"
WHO = {"kind": "tool", "tool": "test", "agent": {"id": "a", "type": "engineer", "model": "m"}, "source": "hook"}
RULES = CONVENTIONS.tasks.overlap
TASK = "929-add-the-pending-work"
PATCH = """diff --git a/src/ai/brain.cpp b/src/ai/brain.cpp
--- a/src/ai/brain.cpp
+++ b/src/ai/brain.cpp
@@ -1,1 +1,2 @@
+// @orig 0x001490b8 Brain_Think (Brain.cpp)
diff --git a/tests/test_brain.cpp b/tests/test_brain.cpp
--- a/tests/test_brain.cpp
+++ b/tests/test_brain.cpp
@@ -1,1 +1,2 @@
+// invented fixture 0x00222222
"""


def pr(number: int, title: str, branch: str, *paths: str, draft: bool = False) -> dict[str, object]:
    """One entry of `gh pr list --json`."""
    return {
        "number": number,
        "title": title,
        "headRefName": branch,
        "isDraft": draft,
        "files": [{"path": path, "additions": 1, "deletions": 0} for path in paths],
    }


class Scene:
    """A scratch folder, a canned gh and the changes the other units make."""

    def __init__(self, tmp_path: Path) -> None:
        self.scratch = tmp_path / "scratch"
        self.scratch.mkdir()
        self.prs: list[dict[str, object]] = []
        self.patches: dict[int, str] = {}
        self.gh_down = False
        self.calls: list[list[str]] = []
        self.changes: dict[str, Change] = {}
        self.fail_change: set[str] = set()
        self.conventions: Conventions = CONVENTIONS

    def gh(self, arguments: list[str]) -> str:
        """The canned `gh`: the open pull requests, and a pull request's diff."""
        self.calls.append(arguments)
        if self.gh_down:
            raise OSError("gh could not run: offline")
        if arguments[:2] == ["pr", "list"]:
            return json.dumps(self.prs)
        if arguments[:2] == ["pr", "diff"]:
            return self.patches.get(int(arguments[2]), "")
        raise AssertionError(arguments)

    def change_of(self, record: UnitRecord) -> Change:
        """What the unit `record` changes, as the test set it."""
        if record.task in self.fail_change:
            raise ConfigError("git diff: failed")
        return self.changes.get(record.task, Change())

    def env(self, gh: bool = True) -> OverlapEnv:
        """The reads, with the canned gh unless `gh` is off."""
        return OverlapEnv(self.scratch, self.conventions, self.gh if gh else None, lambda: NOW, self.change_of)

    def task(self, name: str = TASK, scope: tuple[str, ...] = (), goal: str = "", kind: str = "tooling") -> Path:
        """A task file; returns its path."""
        file = TaskFile(name, (929,), kind, USER, NOW, scope, (), {"Goal": goal})
        path = self.scratch / name / "TASK.md"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(render_task_file(file), encoding="utf-8")
        return path

    def unit(self, name: str, *steps: str, parent: str = "", tree: bool = True, claimant: str = "Other") -> UnitRecord:
        """A started unit: its task file, record and a log with `assigned`, `claimed` and `steps`."""
        self.task(name)
        worktree = self.scratch.parent / "worktrees" / name
        if tree:
            worktree.mkdir(parents=True, exist_ok=True)
        record = UnitRecord(name, "feature", (12,), claimant, name, worktree, "origin/main", "b" * 40, NOW, parent)
        write_record(self.scratch / name / "unit.json", record)
        for step in ("assigned", "claimed", *steps):
            stamps.append(
                self.scratch / name / "stamps",
                rules=self.conventions.stamps,
                task=name,
                step=step,
                subject="",
                result="pass",
                commit="b" * 40,
                covers={},
                detail={},
                who=WHO,
                clock=lambda: NOW,
            )
        return record

    def compute(self, name: str = TASK) -> task_overlap.Result:
        """The result for the task `name`, with its unit record when it has one."""
        path = self.scratch / name / "TASK.md"
        task = parse_task_file(path.read_text(encoding="utf-8"), self.conventions.tasks)
        record_path = self.scratch / name / "unit.json"
        return task_overlap.compute(self.env(), task, read_record(record_path) if record_path.is_file() else None)


def with_overlap(conventions: Conventions, **changes: int) -> Conventions:
    """The conventions with `tasks.overlap` changed."""
    tasks = conventions.tasks
    return replace(conventions, tasks=replace(tasks, overlap=replace(tasks.overlap, **changes)))


@pytest.fixture
def scene(tmp_path: Path) -> Scene:
    return Scene(tmp_path)


# --- what the unit wants ------------------------------------------------------------------------------------------


def test_a_task_wants_its_scope_and_the_paths_and_addresses_its_issue_writes() -> None:
    task = TaskFile(
        "t",
        (1,),
        "feature",
        USER,
        NOW,
        ("src/ai/", "0x001490B8", "docs/*.md"),
        (),
        {
            "Goal": "Change `docs/guides/agents.md#slots` and 0x00322b78.",
            "Scope": "not stated",
            "Where to look": "src/x.cpp",
        },
    )
    wanted = task_overlap.wanted_of_task(task)
    assert wanted.paths == ("src/ai/", "docs/*.md", "docs/guides/agents.md")
    assert wanted.addresses == ("0x001490b8", "0x00322b78")


def test_a_wanted_path_covers_itself_what_is_inside_a_folder_and_what_a_glob_fits() -> None:
    wanted = Wanted(("docs/guides/a.md", "src/ai/", "tests/*.cpp"))
    assert wanted.covers("docs/guides/a.md")
    assert wanted.covers("src/ai/x/y.cpp")
    assert wanted.covers("tests/t.cpp")
    assert not wanted.covers("docs/guides/ab.md")
    assert not wanted.covers("src/air/x.cpp")
    assert not wanted.covers("docs/guides/a.md.bak")


def test_the_overlaps_come_in_the_order_of_what_they_share() -> None:
    wanted = Wanted(("src/ai/", "docs/a.md"), ("0x001490b8",))
    pending = [
        Pending("unit", "u1", "t", "s", "u1", Change(frozenset({"docs/a.md"}))),
        Pending("pr", "#9", "t", "s", "b9", Change(frozenset({"docs/a.md"}))),
        Pending("pr", "#5", "t", "s", "b5", Change(frozenset({"src/ai/x.cpp", "docs/a.md"}))),
        Pending("pr", "#7", "t", "s", "b7", Change(frozenset(), frozenset({"0x001490b8"}))),
        Pending("pr", "#8", "t", "s", "b8", Change(frozenset({"src/core/x.cpp"}), frozenset({"0x00000001"}))),
    ]
    found = task_overlap.overlaps(wanted, pending)
    assert [o.pending.key for o in found] == ["#7", "#5", "#9", "u1"]
    assert found[1].paths == ("docs/a.md", "src/ai/x.cpp")


# --- the open pull requests ---------------------------------------------------------------------------------------


def test_the_open_pull_requests_are_read_with_their_state_and_files(scene: Scene) -> None:
    scene.prs = [pr(5, "tools: Add a thing", "b5", "a.py", "b.py"), pr(6, "docs: Fix it", "b6", draft=True)]
    found = task_overlap.read_open_prs(scene.gh, RULES)
    assert [p.key for p, _ in found] == ["#5", "#6"]
    assert found[0][0].change.paths == {"a.py", "b.py"}
    assert found[1][0].state == "draft pull request" and found[0][0].state == "open pull request"
    assert scene.calls[0][:5] == ["pr", "list", "--state", "open", "--limit"]


def test_a_reply_that_is_not_a_list_of_pull_requests_is_refused(scene: Scene) -> None:
    for text in ("{}", "[{}]", "nonsense"):
        with pytest.raises(ConfigError, match=r"malformed|not a JSON list"):
            task_overlap.read_open_prs(lambda arguments, text=text: text, RULES)  # type: ignore[misc]


def test_an_open_pull_request_that_touches_a_wanted_path_overlaps(scene: Scene) -> None:
    scene.task(scope=("python/src/coney_tools/task_start.py",))
    scene.prs = [
        pr(1, "tools: Add one", "b1", "python/src/coney_tools/task_start.py", "docs/x.md"),
        pr(2, "tools: Add two", "b2", "docs/y.md"),
    ]
    result = scene.compute()
    assert [o.pending.key for o in result.overlaps] == ["#1"]
    assert result.overlaps[0].paths == ("python/src/coney_tools/task_start.py",)
    assert result.read_prs == 2 and not result.prs_failed


def test_the_units_own_pull_request_is_not_pending_work(scene: Scene) -> None:
    scene.task(scope=("docs/",))
    scene.prs = [pr(1, "tools: Add", TASK, "docs/a.md"), pr(2, "tools: Add", "other", "docs/a.md")]
    assert [o.pending.key for o in scene.compute().overlaps] == ["#2"]


def test_the_addresses_a_pull_request_touches_are_read_from_its_diff_only_when_the_unit_names_one(scene: Scene) -> None:
    scene.prs = [pr(1, "core: Port it", "b1", "src/ai/brain.cpp", "tests/test_brain.cpp")]
    scene.patches = {1: PATCH}
    scene.task(scope=("0x001490b8", "0x00222222"))
    result = scene.compute()
    assert result.overlaps[0].addresses == ("0x001490b8",)  # the test file's invented value is not counted
    assert result.overlaps[0].paths == ()
    assert ["pr", "diff", "1"] in scene.calls
    scene.calls.clear()
    scene.task(scope=("src/ai/",))
    scene.compute()
    assert all(call[:2] != ["pr", "diff"] for call in scene.calls)


def test_only_the_configured_number_of_diffs_is_read(scene: Scene) -> None:
    scene.conventions = with_overlap(scene.conventions, diff_prs=1)
    scene.prs = [pr(n, "t", f"b{n}", "src/ai/x.cpp") for n in (1, 2, 3)]
    scene.patches = {1: PATCH, 2: PATCH, 3: PATCH}
    scene.task(scope=("0x001490b8",))
    result = scene.compute()
    assert [call for call in scene.calls if call[:2] == ["pr", "diff"]] == [["pr", "diff", "1"]]
    assert any("only the first 1 open pull requests" in notice for notice in result.notices)


def test_a_failed_read_of_the_pull_requests_is_a_note_and_the_units_are_still_listed(scene: Scene) -> None:
    scene.task(scope=("docs/",))
    scene.unit("u1")
    scene.changes["u1"] = Change(frozenset({"docs/a.md"}))
    scene.gh_down = True
    result = scene.compute()
    assert result.prs_failed and "open pull requests were not read" in result.notices[0]
    assert [o.pending.key for o in result.overlaps] == ["u1"]
    no_gh = task_overlap.compute(
        scene.env(gh=False), parse_task_file(scene.task(scope=("docs/",)).read_text("utf-8"), CONVENTIONS.tasks), None
    )
    assert no_gh.prs_failed


# --- the other units ----------------------------------------------------------------------------------------------


def test_a_unit_in_progress_that_changes_a_wanted_path_overlaps(scene: Scene) -> None:
    scene.task(scope=("docs/guides/",))
    scene.unit("1194-other")
    scene.changes["1194-other"] = Change(frozenset({"docs/guides/a.md", "src/x.cpp"}))
    result = scene.compute()
    [found] = result.overlaps
    assert (found.pending.kind, found.pending.key, found.paths) == ("unit", "1194-other", ("docs/guides/a.md",))
    assert "claimed by Other" in found.pending.state and result.read_units == 1


@pytest.mark.parametrize("case", ["finished", "pr-opened", "a fix task", "no worktree", "the unit itself"])
def test_a_unit_that_is_not_pending_work_is_left_out(scene: Scene, case: str) -> None:
    scene.task(scope=("docs/",))
    if case == "the unit itself":
        scene.unit(TASK)
        scene.changes[TASK] = Change(frozenset({"docs/a.md"}))
    elif case == "a fix task":
        scene.unit("other-fix", parent="other")
        scene.changes["other-fix"] = Change(frozenset({"docs/a.md"}))
    elif case == "no worktree":
        scene.unit("other", tree=False)
        scene.changes["other"] = Change(frozenset({"docs/a.md"}))
    else:
        scene.unit("other", case)
        scene.changes["other"] = Change(frozenset({"docs/a.md"}))
    assert scene.compute().overlaps == []


def test_a_unit_with_an_open_pull_request_is_listed_once_as_the_pull_request(scene: Scene) -> None:
    scene.task(scope=("docs/",))
    scene.unit("other")
    scene.changes["other"] = Change(frozenset({"docs/a.md"}))
    scene.prs = [pr(3, "docs: Add a", "other", "docs/a.md")]
    assert [(o.pending.kind, o.pending.key) for o in scene.compute().overlaps] == [("pr", "#3")]


def test_a_unit_whose_changes_cannot_be_read_is_a_note(scene: Scene) -> None:
    scene.task(scope=("docs/",))
    scene.unit("broken")
    scene.fail_change.add("broken")
    result = scene.compute()
    assert result.overlaps == [] and "unit broken: its changes were not read" in result.notices[0]


def test_what_the_units_own_branch_changes_counts_as_wanted(scene: Scene) -> None:
    scene.task(scope=())
    scene.unit(TASK)
    scene.changes[TASK] = Change(frozenset({"src/core/a.cpp"}), frozenset({"0x00000042"}))
    scene.prs = [pr(4, "core: Other", "b4", "src/core/a.cpp")]
    assert [o.pending.key for o in scene.compute().overlaps] == ["#4"]


# --- the task file section ----------------------------------------------------------------------------------------


def test_the_section_is_added_replaced_and_leaves_the_task_file_valid(scene: Scene) -> None:
    path = scene.task()
    text = path.read_text(encoding="utf-8")
    assert task_overlap.get_section(text) is None
    first = task_overlap.set_section(text, "- one\n")
    second = task_overlap.set_section(first, "- two\n")
    assert task_overlap.get_section(second) == "- two"
    assert task_overlap.entries_of(task_overlap.get_section(second) or "") == ["- two"]
    assert second.count("## Pending work that overlaps") == 1
    parsed = parse_task_file(second, CONVENTIONS.tasks)
    assert parsed.sections["Goal"] == parse_task_file(text, CONVENTIONS.tasks).sections["Goal"]
    # A section that is not last is replaced in place and what follows it is kept.
    middle = second + "\n## Later\n\nkept\n"
    replaced = task_overlap.set_section(middle, "- three\n")
    assert "- three" in replaced and "- two" not in replaced and replaced.rstrip().endswith("kept")
    assert replaced.index("- three") < replaced.index("## Later")


def test_the_section_body_says_when_it_was_read_and_what_could_not_be(scene: Scene) -> None:
    scene.task(scope=("docs/",))
    scene.prs = [pr(1, "t", "b1", "docs/a.md")]
    result = scene.compute()
    result.notices.append("a thing failed")
    body = task_overlap.section_body(result, RULES, NOW)
    assert "Read 2026-10-31 12:00 UTC: 1 open pull request(s) and 0 other unit(s)" in body
    assert "- PR #1 (open pull request) `t`: shares docs/a.md" in body
    assert "Not read: a thing failed." in body
    empty = task_overlap.section_body(task_overlap.Result(), RULES, NOW)
    assert "None: nothing pending" in empty and task_overlap.entries_of(empty) == []


def test_the_list_is_cut_to_the_configured_size_and_counts_the_rest() -> None:
    rules = replace(RULES, max_listed=2, max_paths=1)
    items = [
        task_overlap.Overlap(Pending("pr", f"#{n}", "t", "s", "b", Change()), ("a.md", "b.md", "c.md"), ())
        for n in (1, 2, 3)
    ]
    lines = task_overlap.entry_lines(task_overlap.Result(items), rules)
    assert len(lines) == 3 and "a.md and 2 more path(s)" in lines[0] and "and 1 more" in lines[2]


# --- the stage commands -------------------------------------------------------------------------------------------


def test_a_stage_prints_the_list_when_it_changed_since_the_section_was_written(scene: Scene) -> None:
    path = scene.task(scope=("docs/",))
    scene.prs = [pr(1, "t", "b1", "docs/a.md")]
    said: list[str] = []
    task_overlap.stage_report(scene.env(), TASK, said.append)
    assert "not recorded at the start" in said[0] and any("PR #1" in line for line in said)
    assert "- PR #1" in path.read_text(encoding="utf-8")
    # Unchanged: silent.
    said.clear()
    task_overlap.stage_report(scene.env(), TASK, said.append)
    assert said == []
    # A pull request appears meanwhile, another one goes.
    scene.prs = [pr(2, "t2", "b2", "docs/b.md")]
    task_overlap.stage_report(scene.env(), TASK, said.append)
    assert "changed since it was last recorded (1 new or changed, 1 gone)" in said[0]
    assert any("PR #2" in line for line in said) and "never build on them unstacked" in said[-1]
    assert "- PR #2" in path.read_text(encoding="utf-8") and "- PR #1" not in path.read_text(encoding="utf-8")


def test_a_stage_that_cannot_read_the_pull_requests_changes_nothing_and_does_not_fail(scene: Scene) -> None:
    path = scene.task(scope=("docs/",))
    before = path.read_text(encoding="utf-8")
    scene.gh_down = True
    said: list[str] = []
    task_overlap.stage_report(scene.env(), TASK, said.append)
    assert said and "not checked" in said[0]
    assert path.read_text(encoding="utf-8") == before


def test_a_stage_without_a_task_file_or_with_a_broken_one_says_so_and_goes_on(scene: Scene) -> None:
    said: list[str] = []
    task_overlap.stage_report(scene.env(), "missing", said.append)
    assert said and "not read" in said[0]
    (scene.scratch / "broken").mkdir()
    (scene.scratch / "broken" / "TASK.md").write_text("nothing", encoding="utf-8")
    task_overlap.stage_report(scene.env(), "broken", said.append)
    assert len(said) == 2


def test_a_dry_stage_prints_without_writing(scene: Scene) -> None:
    path = scene.task(scope=("docs/",))
    before = path.read_text(encoding="utf-8")
    scene.prs = [pr(1, "t", "b1", "docs/a.md")]
    said: list[str] = []
    task_overlap.stage_report(scene.env(), TASK, said.append, write=False)
    assert said and path.read_text(encoding="utf-8") == before


# --- task start's report and the command --------------------------------------------------------------------------


def test_the_start_report_lists_the_overlaps_and_writes_the_section(scene: Scene) -> None:
    path = scene.task(scope=("docs/",))
    scene.prs = [pr(1, "t", "b1", "docs/a.md")]
    task = parse_task_file(path.read_text(encoding="utf-8"), CONVENTIONS.tasks)
    lines = task_overlap.start_lines(scene.env(), task, None, True, path)
    assert lines[0] == "pending work that overlaps: 1 (written to TASK.md)" and "PR #1" in lines[1]
    assert "- PR #1" in path.read_text(encoding="utf-8")
    again = task_overlap.start_lines(scene.env(), task, None, False, path)
    assert again[0] == "pending work that overlaps: 1"


def test_the_start_report_says_none_when_nothing_overlaps(scene: Scene) -> None:
    path = scene.task(scope=("docs/",))
    task = parse_task_file(path.read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert task_overlap.start_lines(scene.env(), task, None, True, path) == [
        "pending work that overlaps: none (written to TASK.md)"
    ]
    assert "None: nothing pending" in path.read_text(encoding="utf-8")


def parse(*arguments: str) -> argparse.Namespace:
    """The `task` command line parsed as the real parser does."""
    parser = argparse.ArgumentParser()
    task_cli.add_task_parser(parser.add_subparsers(dest="group"))
    return parser.parse_args(["task", *arguments])


def test_the_overlap_command_prints_the_list_and_writes_only_when_asked(
    scene: Scene, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    repo = tmp_path / "ws" / "repos" / "coney"
    repo.mkdir(parents=True)
    monkeypatch.chdir(repo)
    monkeypatch.setattr(task_cli, "scratch_folder", lambda root: scene.scratch)
    monkeypatch.setattr(task_cli, "find_repo_root", lambda start: repo)
    monkeypatch.setattr(task_cli, "load_conventions", lambda root: CONVENTIONS)
    path = scene.task(scope=("docs/",))
    scene.prs = [pr(1, "tools: Add one", "b1", "docs/a.md")]
    before = path.read_text(encoding="utf-8")
    assert task_cli.run_task(parse("overlap", TASK), run=scene.gh, now=lambda: NOW) == 0
    out = capsys.readouterr().out
    assert "1 pending change(s) overlap" in out and "PR #1" in out and "shares docs/a.md" in out
    assert path.read_text(encoding="utf-8") == before
    assert task_cli.run_task(parse("overlap", TASK, "--write"), run=scene.gh, now=lambda: NOW) == 0
    assert "- PR #1" in path.read_text(encoding="utf-8")


def test_the_committed_rules_hold_sensible_limits() -> None:
    assert RULES.open_prs >= RULES.diff_prs >= 1 and RULES.max_listed >= 1 and RULES.max_paths >= 1


def test_what_a_unit_changes_is_read_from_its_diff_and_its_uncommitted_files(tmp_path: Path) -> None:
    repo = tmp_path / "wt"
    repo.mkdir()
    git(repo, "init", "-q", "-b", "main")
    (repo / "src").mkdir()
    (repo / "docs").mkdir()
    (repo / "src/old.cpp").write_text("int a;\n", encoding="utf-8")
    (repo / "docs/page.md").write_text("# Page\n", encoding="utf-8")
    (repo / "tests").mkdir()
    (repo / "tests/t.cpp").write_text("int t;\n", encoding="utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "base")
    base = git(repo, "rev-parse", "HEAD").strip()
    git(repo, "mv", "src/old.cpp", "src/new.cpp")
    (repo / "src/new.cpp").write_text("int a;\n// @orig 0x001490B8 Brain_Think (Brain.cpp)\n", encoding="utf-8")
    (repo / "tests/t.cpp").write_text("int t;\n// invented 0x00222222\n", encoding="utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "work")
    (repo / "docs/page.md").write_text("# Page\n\nMore.\n", encoding="utf-8")
    (repo / "docs/fresh.md").write_text("# Fresh\n", encoding="utf-8")
    record = UnitRecord("t", "feature", (1,), USER, "main", repo, "origin/main", base, NOW)
    change = task_overlap.unit_change(record)
    assert change.paths == {"src/old.cpp", "src/new.cpp", "tests/t.cpp", "docs/page.md", "docs/fresh.md"}
    assert change.addresses == {"0x001490b8"}


def test_a_branch_whose_changes_main_has_changes_nothing_that_is_pending(tmp_path: Path) -> None:
    repo = tmp_path / "wt"
    repo.mkdir()
    git(repo, "init", "-q", "-b", "main")
    (repo / "a.md").write_text("one\n", encoding="utf-8")
    (repo / "b.md").write_text("one\n", encoding="utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "base")
    base = git(repo, "rev-parse", "HEAD").strip()
    git(repo, "checkout", "-q", "-b", "work")
    (repo / "a.md").write_text("two\n// @orig 0x001490b8 Brain_Think (Brain.cpp)\n", encoding="utf-8")
    (repo / "b.md").write_text("two\n", encoding="utf-8")
    git(repo, "commit", "-q", "-am", "work")
    record = UnitRecord("t", "feature", (1,), USER, "work", repo, "main", base, NOW)
    # Main moves on: it takes a.md exactly as the branch has it (a squash merge) and not b.md.
    git(repo, "checkout", "-q", "main")
    (repo / "a.md").write_text("two\n// @orig 0x001490b8 Brain_Think (Brain.cpp)\n", encoding="utf-8")
    (repo / "c.md").write_text("later\n", encoding="utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "squash of the branch's a.md, and c.md")
    git(repo, "checkout", "-q", "work")
    change = task_overlap.unit_change(record)
    assert change.paths == {"b.md"} and change.addresses == frozenset()

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools task check`: which checks a change needs, the `checked` stamp it writes (bound to the change,
with the build record), and every way it refuses. The checks and the build are fakes; the repository, the stamp log and
the build records are real files in a temporary folder."""

import hashlib
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
import yaml
from conventions_support import write_checkout
from git_support import git

from coney_tools import (
    build_record,
    build_run,
    local_gate,
    pr_diff,
    stamps,
    task_check,
    task_check_steps,
    unit,
    unit_record,
)
from coney_tools.build_claims import SlotTimeout
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.local_gate import StepResult

TASK = "757-fix-widget"
TESTS = {"total": 412, "passed": 412, "failed": 0, "skipped": 0, "skipped_disc": 0}


class Clock:
    """A clock that moves one minute per reading."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 23, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(minutes=1)
        return self.now


class World:
    """A repository on branch `work`, the fakes `task check` is given, and a record of what they were asked."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.calls: list[str] = []
        self.failing: dict[str, list[str]] = {}
        self.unavailable: set[str] = set()
        self.builds: list[tuple[str, str, str]] = []
        self.build_exit = 0
        self.build_tests: dict[str, int] | None = dict(TESTS)
        self.build_preset: str | None = None
        self.build_filters: list[str] = []
        self.no_disc: str | None = None
        self.on_step: dict[str, Callable[[], None]] = {}
        self.counts: dict[str, dict[str, int]] = {}
        self.clock = Clock()
        self.said: list[str] = []
        #: The (agent, what) of each check slot the run held, and the wait that gives up (None: every slot is free).
        self.holds: list[tuple[str, str]] = []
        self.hold_timeout: SlotTimeout | None = None
        self.contexts: list[task_check_steps.StepContext] = []
        self.streamed_into: list[Path] = []

    def step(self, name: str) -> Callable[[task_check_steps.StepContext], StepResult]:
        def run(context: task_check_steps.StepContext) -> StepResult:
            self.calls.append(name)
            self.contexts.append(context)
            if name == "pytest" and context.pytest_scope.whole:
                with context.hold("pytest"):
                    pass  # the real step runs the whole suite inside the hold, a scoped run without one
            if name in self.unavailable:
                raise task_check_steps.CheckUnavailable(f"{name} is not installed")
            if name in self.on_step:
                self.on_step[name]()
            if name in self.failing:
                return StepResult(name, False, "failed", output=tuple(self.failing[name]))
            return StepResult(name, True, "fine", counts=tuple(self.counts.get(name, {}).items()))

        return run

    def build(self, root: Path, agent: str, preset: str, task: str) -> build_run.Result:
        self.builds.append((agent, preset, task))
        result = build_run.Result(ok=self.build_exit == 0, exit_code=self.build_exit)
        if self.build_exit == 75:
            return result
        tip = unit.head_commit(root)
        counts = build_run.TestCounts(**self.build_tests) if self.build_tests is not None else None
        result.ran = ("configure", "build", "test") if counts is not None else ("configure", "build")
        result.tests = counts
        record = build_record.make(
            task=task,
            agent=agent,
            tip=tip,
            dirty=False,
            preset=self.build_preset or preset,
            filters=self.build_filters,
            command_line=["coney-tools", "build"],
            exit_code=self.build_exit,
            ran=result.ran,
            tests=self.build_tests,
            disc="the environment",
            log_sha256=hashlib.sha256(b"log").hexdigest(),
            started="2026-10-23T12:00:00Z",
            ended="2026-10-23T12:30:00Z",
        )
        result.record = build_record.write(unit.Unit.of(root, task).dir / build_record.FOLDER, record)
        return result

    @contextmanager
    def hold(self, root: Path, agent: str, what: str) -> Iterator[None]:
        if self.hold_timeout is not None:
            raise self.hold_timeout
        self.holds.append((agent, what))
        yield

    def stream(self, logs: Path) -> Any:
        self.streamed_into.append(logs)
        return lambda command, cwd: (0, "")

    def env(self, *_: object) -> task_check.Env:
        names = {name for p in load_conventions(self.root).proof.profiles for name in p.checks}
        return task_check.Env(
            steps={name: self.step(name) for name in names},
            build=self.build,
            disc=lambda root: self.no_disc,
            clock=self.clock,
            say=self.said.append,
            who=lambda: {"kind": "tool", "tool": "coney-tools task check", "agent": {"id": "lane-a"}, "source": "hook"},
            stream=self.stream,
            hold=self.hold,
            workers=lambda root: 3,
        )

    def request(self, **more: Any) -> task_check.Request:
        values: dict[str, Any] = {"task": TASK, "worktree": self.root, "base": "main", "agent": "lane-a"}
        values.update(more)
        return task_check.Request(**values)

    def check(self, **more: Any) -> int:
        return task_check.run_check(self.request(**more), self.env())

    def edit(self, name: str, text: str, message: str = "change") -> None:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", message)

    @property
    def log(self) -> Any:
        return stamps.verify(unit.Unit.of(self.root, TASK).stamps, load_conventions(self.root).stamps)

    @property
    def stamp(self) -> Any:
        report = self.log
        assert report.ok and report.stamps
        return report.stamps[-1]

    def change(self) -> pr_diff.Change:
        return pr_diff.change_of(self.root, "main", "HEAD")


@pytest.fixture
def world(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> World:
    root = tmp_path / "repos" / "coney"
    root.mkdir(parents=True)
    write_checkout(root)
    git(root, "init", "-q", "-b", "main")
    git(root, "config", "core.autocrlf", "false")
    (root / "docs" / "guides").mkdir(parents=True)
    (root / "docs" / "guides" / "widget.md").write_text("# Widget\n\nOne.\n", encoding="utf-8")
    (root / ".gitignore").write_text("build/\n", encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    git(root, "checkout", "-q", "-b", "work")
    return World(root)


# --- what runs -----------------------------------------------------------------------------------------------------


def test_a_documentation_change_runs_the_docs_profile_and_stamps_it(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check() == 0
    assert world.calls == ["docs-check", "mkdocs", "pre-commit", "pytest"]
    stamp = world.stamp
    change = world.change()
    assert (stamp.step, stamp.result, stamp.subject, stamp.commit) == ("checked", "pass", "", change.tip)
    assert stamp.covers[stamps.CHANGE] == change.sha256
    assert stamp.detail["profile"] == "docs" and stamp.detail["checks"] == world.calls
    assert "build_record_sha256" not in stamp.covers and world.builds == []
    assert stamp.covers["checks_sha256"] == stamps.sha256_hex(stamps.canonical(sorted(world.calls)))
    assert stamp.who["agent"]["id"] == "lane-a"


def test_a_tooling_change_runs_the_tooling_profile_without_the_build(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    assert world.check() == 0
    assert world.calls == ["docs-check", "mkdocs", "pre-commit", "ruff", "mypy", "pytest"]
    assert world.stamp.detail["profile"] == "tooling" and world.builds == []
    assert "tests" not in world.stamp.detail and "pytest" not in world.stamp.detail


def test_the_stamp_records_the_pytest_counts_so_the_facts_can_show_them(world: World) -> None:
    world.counts["pytest"] = {"passed": 7276, "skipped": 11, "deselected": 1}
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    assert world.check() == 0
    assert world.stamp.detail["pytest"] == {"passed": 7276, "skipped": 11, "deselected": 1}
    assert "tests" not in world.stamp.detail


@pytest.mark.parametrize(
    ("name", "recorded", "said"),
    [
        ("docs/guides/widget.md", ["docs_only"], "pytest: only the tests marked as reading docs_only"),
        ("src/ai/brain.cpp", ["engine"], "pytest: only the tests marked as reading engine"),
        (
            "python/src/coney_tools/widget.py",
            "whole",
            "pytest: the whole suite (python/src/coney_tools/widget.py runs everything)",
        ),
        (".github/conventions.yml", "whole", "pytest: the whole suite (.github/conventions.yml runs everything)"),
    ],
)
def test_the_output_and_the_stamp_say_which_python_tests_ran(
    world: World, name: str, recorded: object, said: str
) -> None:
    text = (world.root / name).read_text(encoding="utf-8") + "\n" if (world.root / name).is_file() else "x\n"
    world.edit(name, text)
    assert world.check() == 0
    assert world.stamp.detail["pytest_scope"] == recorded
    assert f"  {said}" in world.said
    assert world.contexts[-1].workers == 3  # a check slot's share of the cores, from the environment


def test_a_profile_without_pytest_records_no_scope(world: World) -> None:
    data = yaml.safe_load((world.root / ".github" / "conventions.yml").read_text(encoding="utf-8"))
    data["proof"]["profiles"][0]["checks"] = ["docs-check", "mkdocs", "pre-commit", "actionlint"]
    (world.root / ".github" / "conventions.yml").write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
    git(world.root, "commit", "-qam", "no pytest for docs")
    git(world.root, "branch", "-f", "main")
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check() == 0
    assert "pytest" not in world.calls and "pytest_scope" not in world.stamp.detail


def test_a_workflow_change_also_runs_actionlint(world: World) -> None:
    world.edit(".github/workflows/widget.yml", "name: widget\n")
    assert world.check() == 0
    assert world.calls[-1] == "actionlint" and "actionlint" in world.stamp.detail["checks"]


def test_an_engine_change_builds_with_the_ci_preset_once_and_covers_the_build_record(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int main() { return 0; }\n")
    assert world.check() == 0
    assert world.builds == [("lane-a", "ci", TASK)]
    stamp = world.stamp
    assert stamp.detail["profile"] == "full" and stamp.detail["preset"] == "ci"
    assert stamp.detail["checks"][-2:] == ["msvc-ci-build", "ctest-full-disc"]
    assert stamp.detail["tests"] == TESTS
    [record] = (unit.Unit.of(world.root, TASK).dir / "build-runs").glob("*.json")
    assert stamp.covers["build_record_sha256"] == record.stem == hashlib.sha256(record.read_bytes()).hexdigest()
    assert int(stamp.detail["minutes"]) >= 0


def test_a_bigger_profile_than_needed_is_allowed_and_a_smaller_one_is_refused(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check(profile="full") == 0 and world.stamp.detail["profile"] == "full"
    world.calls.clear()
    world.edit("src/ai/brain.cpp", "int x;\n")
    assert world.check(profile="docs") == 2
    assert world.calls == [] and any("needs full" in line for line in world.said)


def test_nothing_to_check_without_a_change(world: World) -> None:
    assert world.check() == 2
    assert any("changes no file" in line for line in world.said) and not world.log.stamps


# --- failing -------------------------------------------------------------------------------------------------------


def test_a_failing_check_stops_the_run_and_stamps_a_fail(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    world.failing["ruff"] = ["widget.py:1: E501"]
    assert world.check() == 1
    assert world.calls == ["docs-check", "mkdocs", "pre-commit", "ruff"]
    stamp = world.stamp
    assert stamp.result == "fail" and stamp.detail["failed"] == ["ruff"] and stamp.detail["checks"] == world.calls
    assert stamp.covers[stamps.CHANGE] == world.change().sha256
    assert any("widget.py:1: E501" in line for line in world.said)


def test_keep_going_runs_every_check_and_lists_every_failure(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    world.failing["ruff"] = ["a"]
    world.failing["mypy"] = ["b"]
    assert world.check(keep_going=True) == 1
    assert world.calls == ["docs-check", "mkdocs", "pre-commit", "ruff", "mypy", "pytest"]
    assert world.stamp.detail["failed"] == ["ruff", "mypy"]


def test_a_failed_build_fails_the_build_check_and_runs_no_test_check(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    world.build_exit = 1
    world.build_tests = None
    assert world.check() == 1
    stamp = world.stamp
    assert stamp.detail["failed"] == ["msvc-ci-build"] and "ctest-full-disc" not in stamp.detail["checks"]


def test_failed_tests_fail_the_test_check(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    world.build_tests = {**TESTS, "passed": 410, "failed": 2}
    assert world.check() == 1
    assert world.stamp.detail["failed"] == ["ctest-full-disc"]
    assert any("2 tests failed" in line for line in world.said)


def test_skipped_disc_tests_fail_the_test_check_with_the_reason(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    world.build_tests = {"total": 412, "passed": 371, "failed": 0, "skipped": 41, "skipped_disc": 41}
    assert world.check() == 1
    assert any("41 disc tests skipped; Check needs the disc" in line for line in world.said)


@pytest.mark.parametrize(
    ("setting", "text"),
    [({"build_preset": "dev"}, "preset dev, Check needs ci"), ({"build_filters": ["-R", "Widget"]}, "filtered run")],
)
def test_a_record_that_is_not_the_whole_ci_suite_fails_the_test_check(
    world: World, setting: dict[str, Any], text: str
) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    for key, value in setting.items():
        setattr(world, key, value)
    assert world.check() == 1
    assert any(text in line for line in world.said)


# --- not stamping --------------------------------------------------------------------------------------------------


def test_a_build_that_gave_up_waiting_stamps_nothing_and_exits_75(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    world.build_exit = 75
    assert world.check() == 75
    assert not world.log.stamps and any("queued" in line or "slot" in line for line in world.said)


def test_a_check_that_cannot_run_stamps_nothing(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    world.unavailable.add("mypy")
    assert world.check() == 2
    assert not world.log.stamps and any("mypy is not installed" in line for line in world.said)


def test_a_dirty_worktree_is_refused_before_anything_runs(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    (world.root / "notes.txt").write_text("loose", encoding="utf-8")
    assert world.check() == 2
    assert world.calls == [] and any("uncommitted" in line and "notes.txt" in line for line in world.said)


def test_no_disc_is_refused_before_the_build_is_started(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    world.no_disc = "CONEY_DISC is not set and coney.local.toml has no game_dir"
    assert world.check() == 2
    assert world.builds == [] and world.calls == [] and any("CONEY_DISC" in line for line in world.said)
    docs = world.root / "docs" / "guides" / "widget.md"
    git(world.root, "reset", "-q", "--hard", "HEAD~1")
    world.edit("docs/guides/widget.md", docs.read_text(encoding="utf-8") + "More.\n")
    assert world.check() == 0  # a docs change needs no disc


def test_a_worktree_that_changes_while_the_checks_run_gets_no_stamp(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    world.on_step["pytest"] = lambda: world.edit("python/src/coney_tools/widget.py", "x = 2\n")
    assert world.check() == 2
    assert not world.log.stamps and any("changed while" in line for line in world.said)


def test_a_check_that_dirties_the_worktree_gets_no_stamp(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")

    def hook() -> None:
        (world.root / "fixed.txt").write_text("by a hook", encoding="utf-8")

    world.on_step["pre-commit"] = hook
    assert world.check() == 2
    assert not world.log.stamps and any("fixed.txt" in line for line in world.said)


def test_an_unknown_base_is_a_usage_error(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check(base="nowhere") == 2 and any("nowhere" in line for line in world.said)


def test_dry_run_prints_the_plan_and_changes_nothing(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    assert world.check(dry_run=True) == 0
    text = "\n".join(world.said)
    assert "profile full" in text and "msvc-ci-build" in text and "src/ai/brain.cpp" in text
    assert world.calls == [] and world.builds == [] and not world.log.stamps


# --- the stamp is bound to the change ------------------------------------------------------------------------------


def test_the_stamp_survives_a_clean_rebase_and_goes_stale_with_an_edit(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check() == 0
    git(world.root, "checkout", "-q", "main")
    world.edit("docs/guides/other.md", "# Other\n", "main moves")
    git(world.root, "checkout", "-q", "work")
    git(world.root, "rebase", "-q", "main")
    now = world.change().sha256
    assert world.log.current("checked", "", {stamps.CHANGE: now}) is not None
    world.edit("docs/guides/widget.md", "# Widget\n\nThree.\n")
    assert world.log.current("checked", "", {stamps.CHANGE: world.change().sha256}) is None


def test_a_second_check_appends_a_second_stamp_and_the_newest_decides(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    world.failing["mkdocs"] = ["WARNING"]
    assert world.check() == 1
    world.failing.clear()
    assert world.check() == 0
    assert [s.result for s in world.log.stamps] == ["fail", "pass"]
    assert world.log.current("checked", "", {stamps.CHANGE: world.change().sha256}) is not None


def test_the_task_is_named_by_the_branch_when_none_is_given(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check(task=None) == 0
    assert (unit.Unit.of(world.root, "work").stamps / "0001.jsonl").is_file()


# --- the command line ----------------------------------------------------------------------------------------------


def test_the_command_is_wired_into_the_task_group(
    world: World, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    monkeypatch.chdir(world.root)
    monkeypatch.setattr(task_check, "default_env", world.env)
    assert main(["task", "check", TASK, "--agent", "lane-a", "--base", "main", "--dry-run"]) == 0
    assert any("profile docs" in line for line in world.said)
    assert main(["task", "check", TASK, "--agent", "lane-a", "--base", "main"]) == 0
    assert world.stamp.result == "pass"


def test_a_cli_error_is_exit_2_with_the_message(
    world: World, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(world.root)
    monkeypatch.setattr(task_check, "default_env", world.env)
    assert main(["task", "check", TASK, "--agent", "lane-a", "--base", "nowhere"]) == 2
    assert any("nowhere" in line for line in world.said)


def test_the_agent_must_be_named_when_nobody_can_say(world: World) -> None:
    with pytest.raises(ConfigError, match="--agent"):
        task_check.agent_for(None, {"agent": {"id": "unknown"}}, lambda: None)
    assert task_check.agent_for("x", {"agent": {"id": "unknown"}}, lambda: None) == "x"
    assert task_check.agent_for(None, {"agent": {"id": "a1"}}, lambda: None) == "a1"
    assert task_check.agent_for(None, {"agent": {"id": "unknown"}}, lambda: "alice") == "alice"


# --- the round after review ----------------------------------------------------------------------------------------


def test_a_profile_that_builds_nothing_needs_no_agent_id(world: World, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(task_check, "_person", lambda: None)
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    env = world.env()
    env.who = lambda: {"kind": "tool", "tool": "coney-tools task check", "agent": {"id": "unknown"}, "source": "hook"}
    assert task_check.run_check(world.request(agent=None), env) == 0
    world.edit("src/ai/brain.cpp", "int x;\n")
    assert task_check.run_check(world.request(agent=None), env) == 2
    assert any("--agent" in line for line in world.said)


def test_a_failed_build_stamp_still_covers_its_record_and_preset(world: World) -> None:
    world.edit("src/ai/brain.cpp", "int x;\n")
    world.build_exit = 1
    world.build_tests = None
    assert world.check() == 1
    stamp = world.stamp
    [record] = (unit.Unit.of(world.root, TASK).dir / "build-runs").glob("*.json")
    assert stamp.result == "fail" and stamp.detail["failed"] == ["msvc-ci-build"]
    assert stamp.covers["build_record_sha256"] == record.stem and stamp.detail["preset"] == "ci"


def test_a_branch_cannot_shrink_its_profile_by_editing_the_rules(world: World) -> None:
    import yaml

    path = world.root / ".github" / "conventions.yml"
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    for profile in data["proof"]["profiles"]:
        if profile["name"] == "docs":
            profile["classes"] = ["docs_only", "tools_only", "engine", "unknown"]
    world.edit(".github/conventions.yml", yaml.safe_dump(data))
    world.edit("src/ai/brain.cpp", "int x;\n")
    assert world.check() == 0
    assert world.stamp.detail["profile"] == "full"  # the base's rules, not the branch's


def _main_edits(world: World, change: Callable[[dict[str, Any]], None]) -> None:
    """Commit a change to the conventions file on `main` (a base that moved on), and return to the branch."""
    import yaml

    git(world.root, "checkout", "-q", "main")
    path = world.root / ".github" / "conventions.yml"
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    change(data)
    path.write_text(yaml.safe_dump(data), encoding="utf-8", newline="")
    git(world.root, "commit", "-qam", "main moves on")
    git(world.root, "checkout", "-q", "work")


def test_a_base_without_a_proof_section_gets_the_branchs_rules_and_the_stamp_says_so(world: World) -> None:
    _main_edits(world, lambda data: data.pop("proof"))
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    world.edit(".github/pull_request_template.md", "x\n")
    assert world.check() == 0
    assert any("no proof section yet" in line for line in world.said)
    assert world.stamp.detail["rules"] == "branch"


def test_the_stamp_records_that_the_rules_came_from_the_base(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check() == 0 and world.stamp.detail["rules"] == "base"


def test_a_base_that_gained_a_section_or_a_key_is_still_read_and_cannot_be_shrunk(world: World) -> None:
    import yaml

    def grow(data: dict[str, Any]) -> None:
        data["newsection"] = {}
        data["proof"]["newkey"] = 1
        data["paths"]["newpaths"] = []

    _main_edits(world, grow)
    path = world.root / ".github" / "conventions.yml"
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    for profile in data["proof"]["profiles"]:
        if profile["name"] == "docs":
            profile["classes"] = ["docs_only", "tools_only", "engine", "unknown"]
    world.edit(".github/conventions.yml", yaml.safe_dump(data))
    world.edit("src/ai/brain.cpp", "int x;\n")
    assert world.check() == 0
    assert world.stamp.detail["profile"] == "full" and world.stamp.detail["rules"] == "base"
    assert world.builds == [("lane-a", "ci", TASK)]


def test_a_base_whose_proof_rules_do_not_validate_stops_the_run_without_a_stamp(world: World) -> None:
    def break_proof(data: dict[str, Any]) -> None:
        data["proof"]["profiles"] = []

    _main_edits(world, break_proof)
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check() == 2
    assert not world.log.stamps and any("main:.github/conventions.yml" in line for line in world.said)
    assert not any("using this branch" in line for line in world.said)


def test_a_base_without_the_conventions_file_stops_the_run_without_a_stamp(world: World) -> None:
    git(world.root, "checkout", "-q", "main")
    git(world.root, "rm", "-q", ".github/conventions.yml")
    git(world.root, "commit", "-qm", "no rules")
    git(world.root, "checkout", "-q", "work")
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    assert world.check() == 2 and not world.log.stamps


def test_a_disc_variable_that_names_nothing_is_a_machine_fault(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    root = tmp_path / "repo"
    root.mkdir()
    monkeypatch.setenv("CONEY_DISC", str(tmp_path / "no-such-disc"))
    assert "does not exist" in str(task_check._no_disc_reason(root))
    here = tmp_path / "disc"
    here.mkdir()
    monkeypatch.setenv("CONEY_DISC", str(here))
    assert task_check._no_disc_reason(root) is None


# --- check slots and live progress (issue 1135) --------------------------------------------------------------------

DOC = "# Widget\n\nTwo.\n"
PY = "x = 1\n"


def test_the_pytest_check_holds_a_check_slot_for_the_agent_and_a_docs_change_holds_none(world: World) -> None:
    world.edit("docs/guides/widget.md", DOC)
    assert world.check() == 0 and world.holds == []
    world.edit("python/src/coney_tools/widget.py", PY)
    assert world.check() == 0
    assert world.holds == [("lane-a", "pytest")]


def test_a_pytest_run_that_gave_up_waiting_for_a_check_slot_stamps_nothing_and_exits_75(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", PY)
    world.hold_timeout = SlotTimeout("no check slot free after 180 minutes; held by bob (pytest)", 2, 3)
    assert world.check() == 75
    assert not world.log.stamps
    assert any("gave up waiting for a check slot (place 2 of 3)" in line for line in world.said)
    assert any("run task check again" in line for line in world.said)


def test_a_profile_with_pytest_needs_an_agent_id_for_the_slot(world: World, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(task_check, "_person", lambda: None)
    world.edit("python/src/coney_tools/widget.py", PY)
    env = world.env()
    env.who = lambda: {"kind": "tool", "tool": "coney-tools task check", "agent": {"id": "unknown"}, "source": "hook"}
    assert task_check.run_check(world.request(agent=None), env) == 2
    assert any("--agent" in line for line in world.said)


def test_each_check_logs_its_start_and_its_end_as_it_happens(world: World) -> None:
    # When mkdocs runs, the lines of docs-check (start and end) and mkdocs' own start were already said: the log grows
    # a stage at a time, not all at the end.
    seen: list[str] = []
    world.on_step["mkdocs"] = lambda: seen.extend(world.said)
    world.edit("docs/guides/widget.md", DOC)
    assert world.check() == 0
    stages = [line for line in seen if "docs-check" in line and line.startswith("  [")]
    assert len(stages) == 2 and "start docs-check" in stages[0] and "ok   docs-check" in stages[1]
    assert seen[-1].endswith("start mkdocs") and seen[-1].startswith("  [")  # a time of day leads each stage line
    assert any(line.endswith("start pre-commit") for line in world.said)


def test_a_build_stage_logs_its_start_before_it_runs(world: World) -> None:
    seen: list[str] = []
    original = world.build

    def build(root: Path, agent: str, preset: str, task: str) -> build_run.Result:
        seen.extend(world.said)
        return original(root, agent, preset, task)

    world.build = build  # type: ignore[method-assign]
    world.edit("src/ai/brain.cpp", "int main() { return 0; }\n")
    assert world.check() == 0
    assert any("start msvc-ci-build and ctest-full-disc (one build run)" in line for line in seen)


def test_the_checks_run_through_the_streaming_runner_with_logs_in_the_tasks_scratch_folder(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", PY)
    assert world.check() == 0
    logs = unit.Unit.of(world.root, TASK).dir / "check-logs"
    assert world.streamed_into == [logs]
    assert world.contexts and world.contexts[0].run is not local_gate.run_command


def test_the_real_environment_flushes_every_line_and_streams(monkeypatch: pytest.MonkeyPatch) -> None:
    flushed: list[bool] = []
    monkeypatch.setattr("builtins.print", lambda *args, **kwargs: flushed.append(bool(kwargs.get("flush"))))
    env = task_check.default_env(30.0)
    env.say("  start docs-check")
    assert flushed == [True]  # a run redirected to a file shows the line at once
    assert env.stream is not None and callable(env.hold)


# --- a fix task ------------------------------------------------------------------------------------------------------


def start_fix(root: Path, fix: str, parent: str) -> None:
    """The unit record `task start` writes for a fix task: the parent's branch and worktree, and the parent named."""
    unit_record.write_record(
        unit.Unit.of(root, fix).dir / load_conventions(root).tasks.start.unit_file,
        unit_record.UnitRecord(
            task=fix,
            kind="fix",
            issues=(757,),
            claimant="alice",
            branch="work",
            worktree=root,
            base_ref="main",
            base_commit="0" * 40,
            started=datetime(2026, 10, 23, 12, 0, tzinfo=UTC),
            parent=parent,
        ),
    )


def test_a_fix_task_named_by_its_own_id_is_stamped_in_its_parents_log(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    start_fix(world.root, "757-fix", TASK)
    assert world.check(task="757-fix") == 0
    # The parent's log holds the stamp (`UnitRecord.log_task`); the fix's own folder has none.
    assert world.stamp.step == "checked" and world.stamp.task == TASK
    assert not unit.Unit.of(world.root, "757-fix").stamps.exists()
    assert world.streamed_into == [unit.Unit.of(world.root, TASK).dir / "check-logs"]
    assert any("757-fix is a fix of 757-fix-widget" in line for line in world.said), world.said


def test_a_fix_task_with_no_unit_record_is_stamped_in_the_log_of_the_parent_its_task_file_names(world: World) -> None:
    world.edit("docs/guides/widget.md", "# Widget\n\nTwo.\n")
    folder = unit.Unit.of(world.root, "757-fix").dir
    folder.mkdir(parents=True)
    lines = ["task: 757-fix", "issues: [757]", "kind: fix", "claimant: alice", "claimed_at: '2026-10-23T09:00:00Z'"]
    (folder / load_conventions(world.root).tasks.file).write_text(
        "---\n" + "\n".join([*lines, f"parent: {TASK}"]) + "\n---\n# Task 757-fix\n", encoding="utf-8"
    )
    assert world.check(task="757-fix") == 0
    assert world.stamp.step == "checked" and world.stamp.task == TASK
    assert not unit.Unit.of(world.root, "757-fix").stamps.exists()
    assert any("757-fix is a fix of 757-fix-widget" in line for line in world.said), world.said

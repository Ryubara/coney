# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools ci disc-suite`: the whole suite with the disc on the default branch after merges, and the
P-high issue a failure opens. git, the build and gh are fakes; the build records are real files in a temporary
folder."""

import hashlib
import json
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO

from coney_tools import build_record, build_run, disc_suite
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions

CONVENTIONS = load_conventions(REPO)
TIP = "c0ffee" * 6 + "abcd"
PASSING = {"total": 3811, "passed": 3811, "failed": 0, "skipped": 0, "skipped_disc": 0}


class Machine:
    """The fakes `run_suite` is given, and what they were asked."""

    def __init__(self, tmp_path: Path) -> None:
        self.main = tmp_path / "repos" / "coney"
        self.main.mkdir(parents=True)
        self.records = tmp_path / "records"
        self.git_calls: list[tuple[Path, list[str]]] = []
        self.gh_calls: list[list[str]] = []
        self.said: list[str] = []
        self.builds: list[tuple[Path, str, str, str]] = []
        self.dirty = ""
        self.no_disc: str | None = None
        self.exit_code = 0
        self.tests: dict[str, int] = dict(PASSING)
        self.filters: list[str] = []
        self.issues: list[dict[str, Any]] = []

    def git(self, folder: Path, arguments: list[str]) -> str:
        self.git_calls.append((folder, arguments))
        if arguments[:2] == ["worktree", "add"]:
            Path(arguments[-2]).mkdir(parents=True)
        if arguments[0] == "status":
            return self.dirty
        return TIP + "\n" if arguments[0] == "rev-parse" else ""

    def build(self, root: Path, agent: str, preset: str, task: str) -> build_run.Result:
        self.builds.append((root, agent, preset, task))
        result = build_run.Result(
            ok=self.exit_code == 0, exit_code=self.exit_code, summary=["test: FAILED", "  failed: chase"]
        )
        if self.exit_code == 75:
            return result
        record = build_record.make(
            task=task,
            agent=agent,
            tip=TIP,
            dirty=False,
            preset=preset,
            filters=self.filters,
            command_line=["coney-tools", "build"],
            exit_code=self.exit_code,
            ran=("configure", "build", "test"),
            tests=self.tests,
            disc="the environment",
            log_sha256=hashlib.sha256(b"log").hexdigest(),
            started="2026-11-01T12:00:00Z",
            ended="2026-11-01T12:30:00Z",
        )
        result.tests = build_run.TestCounts(**self.tests)
        result.record = build_record.write(self.records, record)
        return result

    def gh(self, arguments: list[str]) -> str:
        self.gh_calls.append(arguments)
        if arguments[:2] == ["issue", "list"]:
            return json.dumps(self.issues)
        if arguments[:2] == ["issue", "create"]:
            return "https://example.invalid/issues/1400\n"
        return ""

    def env(self) -> disc_suite.SuiteEnv:
        return disc_suite.SuiteEnv(
            git=self.git, build=self.build, disc=lambda root: self.no_disc, gh=self.gh, say=self.said.append
        )

    def run(self, apply: bool = True) -> int:
        return disc_suite.run_suite(self.main, CONVENTIONS, "coord", self.env(), apply)

    @property
    def tree(self) -> Path:
        return disc_suite.worktree_of(self.main, CONVENTIONS)


@pytest.fixture
def machine(tmp_path: Path) -> Machine:
    return Machine(tmp_path)


def test_a_passing_suite_makes_its_worktree_at_the_default_branch_and_files_nothing(machine: Machine) -> None:
    assert machine.run() == 0
    assert machine.tree == (machine.main / "../../worktrees/coney/disc-suite").resolve()
    assert machine.git_calls[0] == (machine.main, ["fetch", "-q", "origin", "main"])
    assert machine.git_calls[1] == (
        machine.main,
        ["worktree", "add", "-q", "--detach", str(machine.tree), "origin/main"],
    )
    assert machine.builds == [(machine.tree, "coord", "ci", "disc-suite")]
    assert machine.gh_calls == [] and machine.said[-1] == "ci disc-suite: passed: 3811 of 3811 tests"


def test_an_existing_worktree_is_moved_to_the_default_branch(machine: Machine) -> None:
    machine.tree.mkdir(parents=True)
    assert machine.run() == 0
    assert (machine.tree, ["checkout", "-q", "--detach", "origin/main"]) in machine.git_calls


def test_a_dirty_worktree_or_no_disc_stops_before_the_build(machine: Machine) -> None:
    machine.tree.mkdir(parents=True)
    machine.dirty = " M src/a.cpp\n"
    with pytest.raises(ConfigError, match="uncommitted changes"):
        machine.run()
    machine.dirty = ""
    machine.no_disc = "no game_dir in coney.local.toml"
    with pytest.raises(ConfigError, match="needs the disc: no game_dir"):
        machine.run()
    assert machine.builds == []


def test_a_failure_opens_a_p_high_issue_in_the_task_form(machine: Machine) -> None:
    machine.exit_code = 8
    machine.tests = {**PASSING, "passed": 3810, "failed": 1}
    assert machine.run() == 1
    [create] = [call for call in machine.gh_calls if call[:2] == ["issue", "create"]]
    title, body = create[create.index("--title") + 1], create[create.index("--body") + 1]
    labels = [create[i + 1] for i, word in enumerate(create) if word == "--label"]
    assert title == "tests: Fix the disc suite failing on main"
    assert labels == ["task", "P-high", "A-tools", "S-ready-to-start"]
    assert body.startswith("<!-- coney-disc-suite -->")
    for heading in ("### Summary", "### Area", '### What "done" means', "### Fix list or steps", "### Screenshots"):
        assert heading in body
    assert "- 1 test failed" in body and "  failed: chase" in body and f"`{TIP[:7]}`" in body
    assert machine.said[-1] == "ci disc-suite: created issue #1400"


def test_a_failure_while_the_issue_is_open_comments_on_it(machine: Machine) -> None:
    machine.tests = {**PASSING, "passed": 3800, "skipped": 11, "skipped_disc": 11}
    machine.issues = [{"number": 1399, "body": "other"}, {"number": 1401, "body": "<!-- coney-disc-suite -->\n..."}]
    assert machine.run() == 1
    [comment] = [call for call in machine.gh_calls if call[:2] == ["issue", "comment"]]
    assert comment[2] == "1401" and "11 disc tests skipped" in comment[-1]
    assert not any(call[:2] == ["issue", "create"] for call in machine.gh_calls)


def test_without_apply_it_only_says_what_it_would_write(machine: Machine) -> None:
    machine.filters = ["-L", "disc"]
    assert machine.run(apply=False) == 1
    assert [call[:2] for call in machine.gh_calls] == [["issue", "list"]]
    assert any(line.startswith("ci disc-suite: would create (no --apply):") for line in machine.said)
    assert any("filtered run (-L disc); Check needs the full suite" in line for line in machine.said)


def test_a_build_that_gave_up_waiting_files_nothing_and_exits_75(machine: Machine) -> None:
    machine.exit_code = 75
    assert machine.run() == 75 and machine.gh_calls == []


def test_the_command_is_wired_into_the_ci_group(monkeypatch: pytest.MonkeyPatch) -> None:
    from coney_tools.cli import main

    seen: list[tuple[str, bool]] = []

    def run_suite(main_root: Path, conventions: object, agent: str, env: object, apply: bool) -> int:
        seen.append((agent, apply))
        return 0

    monkeypatch.chdir(REPO)
    monkeypatch.setattr(disc_suite, "run_suite", run_suite)
    assert main(["ci", "disc-suite", "--agent", "coord", "--apply"]) == 0
    assert seen == [("coord", True)]

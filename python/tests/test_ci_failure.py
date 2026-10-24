# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools ci report-failure`: a failed run on main opens (or updates) a P-critical issue.

The runs, logs, issues and git history are made up in the shape `gh` and `git` give them; nothing touches the network.
"""

import argparse
import json
from pathlib import Path
from typing import Any

import pytest
import yaml
from conventions_support import REPO, write_checkout

from coney_tools import ci_cli, issue_form
from coney_tools.ci_failure import excerpt_of, failed_jobs, files_in, report_failure, strip_log_prefix
from coney_tools.conventions import load_conventions
from coney_tools.issue_form import CheckOptions
from coney_tools.repo_checks import check_title

CONVENTIONS = load_conventions(REPO)
SETTINGS = CONVENTIONS.ci.failure
SHA = "d" * 40
FIX = "abc1234 core: Fix the brain (A Person, 2026-10-20)"
TASK = issue_form.load_form(REPO, CONVENTIONS, "task")

PREFIX = "clang-tidy\tRun clang-tidy\t2026-10-23T07:31:02.1234567Z "
WORK = "/work/coney/coney/"
LOG = "\n".join(
    PREFIX + text
    for text in (
        f"{WORK}src/ai/brain.cpp:12:5: error: use of a moved-from object [bugprone-use-after-move]",
        "   12 |     x.f();",
        f"{WORK}src/ai/brain.h:3:1: error: redundant [misc-redundant-expression]",
        f"{WORK}src/gone/removed.cpp:1:1: error: missing [bugprone-x]",
        "\x1b[31mError: Process completed with exit code 1.\x1b[0m",
    )
)


def run_info(**changes: Any) -> dict[str, Any]:
    """A failed nightly run of the build workflow on main, as `gh run view --json` gives it."""
    info: dict[str, Any] = {
        "databaseId": 900,
        "name": "build",
        "workflowName": "build",
        "event": "schedule",
        "headBranch": "main",
        "headSha": SHA,
        "conclusion": "failure",
        "url": "https://example.invalid/runs/900",
        "jobs": [
            {"name": "clang-tidy", "conclusion": "failure", "url": "https://example.invalid/jobs/1"},
            {"name": "build (linux-gcc-13)", "conclusion": "success", "url": "https://example.invalid/jobs/2"},
            {
                "name": "sanitizers (macos-appleclang)",
                "conclusion": "cancelled",
                "url": "https://example.invalid/jobs/3",
            },
            {"name": "format", "conclusion": "timed_out", "url": "https://example.invalid/jobs/4"},
        ],
    }
    info.update(changes)
    return info


class FakeGh:
    """A `gh` that answers the run, its failed log and the issue list, and records the writes."""

    def __init__(
        self,
        info: dict[str, Any] | None = None,
        log: str = LOG,
        issues: list[dict[str, Any]] | None = None,
        comments: list[str] | None = None,
    ) -> None:
        self.info = info if info is not None else run_info()
        self.log = log
        self.issues = issues or []
        self.comments = comments or []
        self.calls: list[list[str]] = []

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if arguments[:2] == ["run", "view"] and "--log-failed" in arguments:
            return self.log
        if arguments[:2] == ["run", "view"]:
            return json.dumps(self.info)
        if arguments[:2] == ["issue", "list"]:
            return json.dumps(self.issues)
        if arguments[:2] == ["issue", "view"]:
            return json.dumps({"comments": [{"body": text} for text in self.comments]})
        if arguments[:2] == ["issue", "create"]:
            return "https://example.invalid/issues/321\n"
        assert arguments[:2] == ["issue", "comment"], arguments
        return ""

    def writes(self, verb: str) -> list[list[str]]:
        """The `gh issue <verb>` calls made."""
        return [call for call in self.calls if call[:2] == ["issue", verb]]


def fake_git(known: dict[str, str] | None = None) -> Any:
    """A `git` that answers `log` for the files in `known` (file -> the log lines) and nothing for others."""
    known = (
        known
        if known is not None
        else {
            "src/ai/brain.cpp": FIX + "\nbcd2345 core: Add walking (B Person, 2026-10-18)",
            "src/ai/brain.h": FIX,
        }
    )

    def git(arguments: list[str]) -> str:
        assert arguments[0] == "log"
        return known.get(arguments[-1], "")

    return git


@pytest.fixture
def root(tmp_path: Path) -> Path:
    """A checkout holding two of the files the log names (the third was deleted since)."""
    for name in ("src/ai/brain.cpp", "src/ai/brain.h"):
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("x\n", encoding="utf-8")
    return tmp_path


# --- reading the run and its log ------------------------------------------------------------------------------------


def test_the_failed_jobs_are_the_failed_and_timed_out_ones_not_the_cancelled() -> None:
    assert [job["name"] for job in failed_jobs(run_info())] == ["clang-tidy", "format"]


def test_a_log_line_loses_the_job_step_timestamp_and_colour() -> None:
    line = LOG.splitlines()[4]
    assert strip_log_prefix(line) == "Error: Process completed with exit code 1."
    assert strip_log_prefix("plain line") == "plain line"


def test_the_files_come_from_the_log_in_order_without_the_runner_prefix(root: Path) -> None:
    lines = [strip_log_prefix(line) for line in LOG.splitlines()]
    assert files_in(lines, root, limit=8) == ["src/ai/brain.cpp", "src/ai/brain.h"]  # removed.cpp no longer exists
    assert files_in(lines, root, limit=1) == ["src/ai/brain.cpp"]


def test_a_python_and_a_ctest_failure_are_found_too(root: Path) -> None:
    (root / "python/tests").mkdir(parents=True)
    (root / "python/tests/test_x.py").write_text("x\n", encoding="utf-8")
    lines = [
        "FAILED python/tests/test_x.py::test_a - assert 1 == 2",
        "The following tests FAILED:",
        "  12 - coney.walk (Failed)",
    ]
    assert files_in(lines, root, 8) == ["python/tests/test_x.py"]
    shown = excerpt_of(lines, 10)
    assert shown == lines  # every one of them looks like a failure or sits under one


def test_the_excerpt_prefers_the_lines_that_name_an_error_and_is_cut_to_the_limit() -> None:
    lines = [f"noise {n}" for n in range(30)] + ["a.cpp:1:1: error: bad [x]"] + [f"more noise {n}" for n in range(30)]
    assert excerpt_of(lines, 5) == ["a.cpp:1:1: error: bad [x]"]
    quiet = [f"line {n}" for n in range(30)]
    assert excerpt_of(quiet, 5) == quiet[-5:]


# --- deciding whether to report -------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "changes",
    [
        {"conclusion": "success"},
        {"conclusion": "cancelled"},
        {"headBranch": "feature"},
        {"workflowName": "pr"},
        {"event": "pull_request"},
    ],
)
def test_only_a_failure_of_a_watched_workflow_on_main_is_reported(changes: dict[str, Any], root: Path) -> None:
    gh = FakeGh(run_info(**changes))
    outcome = report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=True)
    assert outcome.kind == "skipped" and not gh.writes("create") and not gh.writes("comment")


def test_the_events_and_workflows_are_the_conventions_not_the_code() -> None:
    assert set(SETTINGS.workflows) == {"build", "python", "docs"}
    assert {"schedule", "push"} <= set(SETTINGS.events)


# --- the issue ------------------------------------------------------------------------------------------------------


def test_a_failed_nightly_run_opens_a_p_critical_issue(root: Path) -> None:
    gh = FakeGh()
    outcome = report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=True)
    assert outcome.kind == "created" and outcome.number == 321
    call = gh.writes("create")[0]
    title = call[call.index("--title") + 1]
    body = call[call.index("--body") + 1]
    labels = [call[i + 1] for i, arg in enumerate(call) if arg == "--label"]
    assert title == "ci: Fix the failed build run on main" and check_title(title, CONVENTIONS.commits.title) == []
    assert labels == list(SETTINGS.labels) and "P-critical" in labels
    assert "<!-- coney-ci-failure:build -->" in body and "<!-- coney-ci-failure-run:900 -->" in body
    # what failed: the jobs (not the cancelled one), the findings and the files with their last commits
    assert "clang-tidy" in body and "format" in body and "sanitizers" not in body
    assert "src/ai/brain.cpp:12:5: error: use of a moved-from object [bugprone-use-after-move]" in body
    assert "`src/ai/brain.cpp`" in body and "abc1234 core: Fix the brain (A Person, 2026-10-20)" in body
    assert "https://example.invalid/runs/900" in body and SHA[:7] in body and "nightly" in body


def test_the_issue_body_is_a_valid_task_issue(root: Path) -> None:
    gh = FakeGh()
    report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=True)
    call = gh.writes("create")[0]
    title, body = call[call.index("--title") + 1], call[call.index("--body") + 1]
    result = issue_form.check_issue(title, body, TASK, CONVENTIONS, CheckOptions())
    assert [problem.message for problem in result.problems] == []
    assert result.area == SETTINGS.area


def test_without_apply_nothing_is_written_and_the_text_is_printed(root: Path) -> None:
    gh = FakeGh()
    outcome = report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=False)
    assert outcome.kind == "would-create" and outcome.number is None
    assert not gh.writes("create") and "### Summary" in outcome.text


def test_a_second_failure_comments_on_the_open_issue_instead_of_opening_another(root: Path) -> None:
    open_issue = {
        "number": 300,
        "body": "<!-- coney-ci-failure:build -->\n### Summary\nold\n",
        "title": "ci: Fix the failed build run on main",
    }
    other = {
        "number": 301,
        "body": "<!-- coney-ci-failure:python -->\n",
        "title": "ci: Fix the failed python run on main",
    }
    gh = FakeGh(issues=[other, open_issue])
    outcome = report_failure(901, CONVENTIONS, gh, fake_git(), root, apply=True)
    assert outcome.kind == "commented" and outcome.number == 300
    assert not gh.writes("create")
    call = gh.writes("comment")[0]
    assert call[2] == "300" and "<!-- coney-ci-failure-run:901 -->" in call[call.index("--body") + 1]


def test_a_run_that_is_already_on_the_issue_is_not_repeated(root: Path) -> None:
    open_issue = {
        "number": 300,
        "body": "<!-- coney-ci-failure:build -->\n<!-- coney-ci-failure-run:900 -->\n",
        "title": "t",
    }
    gh = FakeGh(issues=[open_issue])
    assert report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=True).kind == "known"
    done = FakeGh(
        issues=[{"number": 300, "body": "<!-- coney-ci-failure:build -->\n", "title": "t"}],
        comments=["<!-- coney-ci-failure-run:900 -->"],
    )
    assert report_failure(900, CONVENTIONS, done, fake_git(), root, apply=True).kind == "known"
    assert not done.writes("comment")


def test_a_log_gh_cannot_give_still_opens_the_issue_with_the_jobs(root: Path) -> None:
    class NoLog(FakeGh):
        def __call__(self, arguments: list[str]) -> str:
            if "--log-failed" in arguments:
                raise OSError("gh run view failed: log expired")
            return super().__call__(arguments)

    gh = NoLog()
    outcome = report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=True)
    assert (
        outcome.kind == "created"
        and "log expired" in gh.writes("create")[0][gh.writes("create")[0].index("--body") + 1]
    )


def test_a_failure_with_no_file_in_the_log_says_so(root: Path) -> None:
    gh = FakeGh(log="job\tstep\t2026-10-23T07:00:00.0000000Z Error: nothing useful\n")
    report_failure(900, CONVENTIONS, gh, fake_git({}), root, apply=True)
    body = gh.writes("create")[0][gh.writes("create")[0].index("--body") + 1]
    assert "no file" in body.lower()


def test_a_manual_run_on_main_is_a_failure_too(root: Path) -> None:
    gh = FakeGh(run_info(event="workflow_dispatch"))
    assert report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=True).kind == "created"
    body = gh.writes("create")[0][gh.writes("create")[0].index("--body") + 1]
    assert "manual" in body


def test_a_push_failure_names_the_commit(root: Path) -> None:
    gh = FakeGh(run_info(event="push"))
    report_failure(900, CONVENTIONS, gh, fake_git(), root, apply=True)
    assert "push" in gh.writes("create")[0][gh.writes("create")[0].index("--body") + 1]


# --- the command and the workflow -----------------------------------------------------------------------------------


def command(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, gh: FakeGh, *flags: str) -> int:
    """Run `ci <flags>` in a checkout of the real conventions."""
    write_checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    parser = argparse.ArgumentParser()
    ci_cli.add_ci_parser(parser.add_subparsers(dest="group"))
    return ci_cli.run_ci(parser.parse_args(["ci", *flags]), gh, git_runner=fake_git())


def test_the_command_prints_what_it_did(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh()
    assert command(tmp_path, monkeypatch, gh, "report-failure", "--run", "900", "--apply") == 0
    assert capsys.readouterr().out.splitlines()[0] == "ci report-failure: created issue #321 for run 900 on main"
    assert command(tmp_path, monkeypatch, FakeGh(run_info(conclusion="success")), "report-failure", "--run", "900") == 0
    assert "skipped" in capsys.readouterr().out


def test_the_dry_run_prints_the_title_and_the_body(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert command(tmp_path, monkeypatch, FakeGh(), "report-failure", "--run", "900") == 0
    out = capsys.readouterr().out
    assert "would create" in out and "ci: Fix the failed build run on main" in out and "### Summary" in out


def test_the_workflow_watches_the_workflows_of_the_conventions_and_only_main() -> None:
    workflow = yaml.safe_load((REPO / ".github" / "workflows" / "ci-failure.yml").read_text(encoding="utf-8"))
    trigger = workflow.get("on") or workflow[True]
    assert set(trigger["workflow_run"]["workflows"]) == set(SETTINGS.workflows)
    condition = workflow["jobs"]["report"]["if"]
    assert "head_branch" in condition and "pull_request" in condition and "head_repository" in condition
    assert workflow["jobs"]["report"]["permissions"]["issues"] == "write"

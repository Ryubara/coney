# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools repo pr-health --rerun`: made-up pull requests, runs and logs in the shape `gh` gives."""

import argparse
import json
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REMOVE, REPO, conventions_with, write_checkout
from test_pr_health import NOW, REQUIRED, FakeGh, pull, rules_for, run

from coney_tools import pr_health
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.pr_rerun import Rerun, plan_reruns

CONVENTIONS = load_conventions(REPO)
GOOD_TITLE = "tools: Add a thing"
GOOD_BODY = """## Summary

Adds a thing.

## Issues affected

None (nothing cites it)

## Checklist

Done:

- [x] the thing

Remaining:

## Testing

- 12 passed, 0 failed.

## Screenshots

Screenshots: none (no visible change)

## AI use

- **Commits written by:** an AI
- **Model:** Claude Sonnet 5.5
"""
SHA = "a" * 40


def failed(
    name: str, conclusion: str = "FAILURE", run_id: int = 500, workflow: str = "pr", job: int | None = None
) -> dict[str, Any]:
    """A failed check run of run `run_id` (job `job`, default run_id + 1), with the URL gh gives."""
    entry = run(name, conclusion)
    entry["workflowName"] = workflow
    entry["detailsUrl"] = f"https://github.com/o/r/actions/runs/{run_id}/job/{job or run_id + 1}"
    return entry


def with_failed(*checks: dict[str, Any], **changes: Any) -> dict[str, Any]:
    """A pull request whose rollup is green except for `checks`, with a head commit."""
    names = {check["name"] for check in checks}
    rollup = [*checks, *(run(n) for n in REQUIRED if n not in names)]
    return pull(statusCheckRollup=rollup, headRefOid=SHA, title=GOOD_TITLE, body=GOOD_BODY, **changes)


class RerunGh:
    """A `gh` that answers the run list and job logs of a pull request's head commit, and records every call."""

    def __init__(
        self,
        runs: list[dict[str, Any]] | None = None,
        logs: dict[int, str] | None = None,
        refuse: str = "",
    ) -> None:
        self.runs = runs if runs is not None else [{"databaseId": 500, "workflowName": "pr", "attempt": 1}]
        self.logs = logs or {}
        self.refuse = refuse
        self.calls: list[list[str]] = []

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if arguments[:2] == ["run", "list"]:
            assert arguments[arguments.index("--commit") + 1] == SHA
            return json.dumps(self.runs)
        if arguments[0] == "api":
            job = int(arguments[-1].split("/")[-2])
            if job not in self.logs:
                raise OSError("gh api failed: HTTP 404")
            return self.logs[job]
        assert arguments[:2] == ["run", "rerun"]
        if self.refuse:
            raise OSError(f"gh run rerun failed: {self.refuse}")
        return ""

    def reruns(self) -> list[list[str]]:
        """The `gh run rerun` calls made."""
        return [call for call in self.calls if call[:2] == ["run", "rerun"]]


def plan(pulls: list[dict[str, Any]], gh: RerunGh, apply: bool = False, conventions: Any = CONVENTIONS) -> list[Rerun]:
    """Plan (and with `apply` do) the reruns for `pulls`."""
    required = conventions.merge.required_checks
    return plan_reruns([pr_health.pull_checks(item, required) for item in pulls], conventions, gh, apply)


def summary(reruns: list[Rerun]) -> list[tuple[int, tuple[str, ...], str]]:
    """The (pull request, checks, status) of each rerun."""
    return [(item.number, item.checks, item.status) for item in reruns]


# --- a failure that is stale ---------------------------------------------------------------------------------------


def test_a_title_failure_is_stale_when_the_current_title_passes_and_needs_a_new_event_not_a_rerun() -> None:
    gh = RerunGh()
    result = plan([with_failed(failed("title"))], gh, apply=True)
    assert summary(result) == [(10, ("title",), "new-event")]
    assert result[0].reasons == ("the title now passes check-title",)
    assert result[0].detail.startswith("needs a new event: edit the title (a rerun reuses the event")
    assert gh.calls == []


def test_a_title_failure_is_real_when_the_current_title_still_fails() -> None:
    bad = with_failed(failed("title"))
    bad["title"] = "Added a thing."
    assert plan([bad], RerunGh()) == []


def test_a_body_failure_is_stale_when_the_current_description_passes_and_real_when_it_does_not() -> None:
    assert summary(plan([with_failed(failed("body"))], RerunGh())) == [(10, ("body",), "new-event")]
    bad = with_failed(failed("body"))
    bad["body"] = "## Summary\n\nNothing else.\n"
    assert plan([bad], RerunGh()) == []


def test_the_text_checks_are_named_in_the_conventions() -> None:
    renamed = conventions_with(("hygiene.pr_health.rerun.title_check", "format"))
    assert plan([with_failed(failed("title"))], RerunGh(), conventions=renamed) == []
    assert summary(plan([with_failed(failed("format"))], RerunGh(), conventions=renamed)) == [
        (10, ("format",), "new-event")
    ]


# --- a failure that is not the code's ------------------------------------------------------------------------------


@pytest.mark.parametrize("conclusion", ["CANCELLED", "STARTUP_FAILURE", "TIMED_OUT"])
def test_a_cancelled_or_never_started_or_timed_out_check_is_rerun_without_reading_its_log(conclusion: str) -> None:
    gh = RerunGh()
    result = plan([with_failed(failed("build-ok", conclusion))], gh)
    assert summary(result) == [(10, ("build-ok",), "would-rerun")]
    assert result[0].reasons == (f"concluded {conclusion.lower()}",)
    assert not [call for call in gh.calls if call[0] == "api"]


def test_the_conclusions_that_count_come_from_the_conventions() -> None:
    only_cancelled = conventions_with(("hygiene.pr_health.rerun.conclusions", ["CANCELLED"]))
    assert plan([with_failed(failed("build-ok", "TIMED_OUT"))], RerunGh(), conventions=only_cancelled) == []


def test_a_failure_whose_log_shows_the_runner_was_lost_is_rerun() -> None:
    log = "step 4\nError: The runner has received a shutdown signal. This can happen when the runner is preempted."
    gh = RerunGh(logs={501: log})
    result = plan([with_failed(failed("build-ok"))], gh)
    assert summary(result) == [(10, ("build-ok",), "would-rerun")]
    assert result[0].reasons == ("the log shows 'The runner has received a shutdown signal'",)
    assert gh.calls[-1][0] == "api" and gh.calls[-1][-1].endswith("/actions/jobs/501/logs")


def test_the_log_patterns_are_case_insensitive_and_come_from_the_conventions() -> None:
    custom = conventions_with(("hygiene.pr_health.rerun.log_patterns", ["disk is on fire"]))
    gh = RerunGh(logs={501: "the DISK IS ON FIRE"})
    assert summary(plan([with_failed(failed("build-ok"))], gh, conventions=custom)) == [
        (10, ("build-ok",), "would-rerun")
    ]
    gh = RerunGh(logs={501: "The runner has received a shutdown signal"})
    assert plan([with_failed(failed("build-ok"))], gh, conventions=custom) == []


def test_a_real_failure_is_never_rerun() -> None:
    gh = RerunGh(logs={501: "FAILED tests/test_x.py::test_y - assert 1 == 2"})
    assert plan([with_failed(failed("build-ok"), failed("python-ok", run_id=600, workflow="python"))], gh) == []
    assert gh.reruns() == []


def test_a_log_that_cannot_be_read_is_not_enough_to_rerun() -> None:
    assert plan([with_failed(failed("build-ok"))], RerunGh()) == []


def test_a_failed_check_that_is_not_required_is_left_alone() -> None:
    cancelled = failed("build (linux-gcc)", "CANCELLED")
    pr = pull(statusCheckRollup=[*(run(n) for n in REQUIRED), cancelled], headRefOid=SHA)
    assert plan([pr], RerunGh()) == []


def test_a_draft_and_a_passing_pull_request_are_left_alone() -> None:
    assert plan([with_failed(failed("build-ok", "CANCELLED"), isDraft=True)], RerunGh()) == []
    assert plan([pull(headRefOid=SHA)], RerunGh()) == []


def test_a_failure_that_a_newer_run_replaced_is_not_failed() -> None:
    rollup = [failed("build-ok", "CANCELLED"), *(run(n, hours_ago=0.2) for n in REQUIRED)]
    assert plan([pull(statusCheckRollup=rollup, headRefOid=SHA)], RerunGh()) == []


# --- the cap and the head commit -----------------------------------------------------------------------------------


def test_a_workflow_rerun_the_allowed_times_on_this_commit_is_not_rerun_again() -> None:
    gh = RerunGh(runs=[{"databaseId": 500, "workflowName": "pr", "attempt": 3}])
    result = plan([with_failed(failed("build-ok", "CANCELLED"))], gh, apply=True)
    assert summary(result) == [(10, ("build-ok",), "capped")]
    assert "2 reruns" in result[0].detail and "push a commit" in result[0].detail
    assert gh.reruns() == []


def test_reruns_of_the_workflow_count_across_its_runs_on_the_commit() -> None:
    runs = [
        {"databaseId": 500, "workflowName": "pr", "attempt": 2},
        {"databaseId": 510, "workflowName": "pr", "attempt": 2},
        {"databaseId": 520, "workflowName": "build", "attempt": 5},
    ]
    assert summary(plan([with_failed(failed("build-ok", "CANCELLED"))], RerunGh(runs=runs))) == [
        (10, ("build-ok",), "capped")
    ]
    one = runs[:1]
    assert summary(plan([with_failed(failed("build-ok", "CANCELLED"))], RerunGh(runs=one))) == [
        (10, ("build-ok",), "would-rerun")
    ]


def test_the_cap_comes_from_the_conventions() -> None:
    gh = RerunGh(runs=[{"databaseId": 500, "workflowName": "pr", "attempt": 2}])
    one = conventions_with(("hygiene.pr_health.rerun.max_per_sha", 1))
    assert summary(plan([with_failed(failed("build-ok", "CANCELLED"))], gh, conventions=one)) == [
        (10, ("build-ok",), "capped")
    ]


def test_a_capped_job_is_not_looked_at_in_its_log_again() -> None:
    gh = RerunGh(runs=[{"databaseId": 500, "workflowName": "pr", "attempt": 3}], logs={501: "shutdown signal"})
    assert plan([with_failed(failed("build-ok"))], gh) == []
    assert not [call for call in gh.calls if call[0] == "api"]


def test_a_run_that_is_not_on_the_head_commit_is_not_rerun() -> None:
    gh = RerunGh(runs=[{"databaseId": 999, "workflowName": "pr", "attempt": 1}])
    assert plan([with_failed(failed("build-ok", "CANCELLED"))], gh, apply=True) == []
    assert gh.reruns() == []


# --- doing it ------------------------------------------------------------------------------------------------------


def test_apply_reruns_the_failed_jobs_of_a_run_once_with_failed() -> None:
    gh = RerunGh()
    pr = with_failed(failed("build-ok", "CANCELLED", job=501), failed("docs", "CANCELLED", job=502))
    result = plan([pr], gh, apply=True)
    assert summary(result) == [(10, ("build-ok", "docs"), "reran")]
    assert gh.reruns() == [["run", "rerun", "500", "--failed"]]


def test_different_runs_are_rerun_separately_in_run_order() -> None:
    runs = [
        {"databaseId": 500, "workflowName": "pr", "attempt": 1},
        {"databaseId": 600, "workflowName": "build", "attempt": 1},
    ]
    gh = RerunGh(runs=runs)
    pr = with_failed(failed("build-ok", "CANCELLED", run_id=600, workflow="build"), failed("docs", "TIMED_OUT"))
    result = plan([pr], gh, apply=True)
    assert summary(result) == [(10, ("docs",), "reran"), (10, ("build-ok",), "reran")]
    assert gh.reruns() == [["run", "rerun", "500", "--failed"], ["run", "rerun", "600", "--failed"]]


def test_a_real_failure_in_the_same_run_is_not_dragged_along() -> None:
    gh = RerunGh()
    pr = with_failed(failed("docs", "CANCELLED"), failed("commits", job=502))
    result = plan([pr], gh, apply=True)
    assert summary(result) == [(10, ("docs",), "reran")]
    assert gh.reruns() == [["run", "rerun", "500", "--job", "501"]]


def test_a_stale_title_in_the_same_run_is_not_rerun_either_and_is_still_reported() -> None:
    gh = RerunGh()
    pr = with_failed(failed("docs", "CANCELLED"), failed("title", job=502))
    result = plan([pr], gh, apply=True)
    assert summary(result) == [(10, ("title",), "new-event"), (10, ("docs",), "reran")]
    assert gh.reruns() == [["run", "rerun", "500", "--job", "501"]]


def test_the_dry_run_calls_nothing_that_changes_github() -> None:
    gh = RerunGh()
    plan([with_failed(failed("build-ok", "CANCELLED"))], gh)
    assert gh.reruns() == []


def test_a_refused_rerun_reports_the_new_event_the_pull_request_needs() -> None:
    gh = RerunGh(refuse="run 500 cannot be retried")
    result = plan([with_failed(failed("build-ok", "CANCELLED"))], gh, apply=True)
    assert summary(result) == [(10, ("build-ok",), "refused")]
    assert "cannot be retried" in result[0].detail and result[0].detail.endswith("needs a new event: push a commit")


def test_the_new_event_for_stale_text_says_which_text_to_edit() -> None:
    result = plan([with_failed(failed("title"), failed("body"))], RerunGh())
    assert result[0].detail.startswith("needs a new event: edit the title or the description")


def test_lines_and_json_say_what_was_done() -> None:
    result = plan([with_failed(failed("build-ok", "TIMED_OUT"))], RerunGh())
    assert result[0].line() == "#10 branch-10 rerun build-ok (run 500): concluded timed_out -> would rerun"
    as_json = result[0].as_json()
    assert as_json["status"] == "would-rerun" and as_json["checks"] == ["build-ok"] and as_json["run"] == 500


# --- the conventions -----------------------------------------------------------------------------------------------


def test_the_real_rerun_rules_name_required_checks_and_valid_patterns() -> None:
    rules = CONVENTIONS.hygiene.pr_health.rerun
    assert {rules.title_check, rules.body_check} <= set(REQUIRED)
    assert rules.max_per_sha >= 1 and rules.log_patterns


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("max_per_sha", 0, "max_per_sha"),
        ("max_per_sha", REMOVE, "max_per_sha"),
        ("conclusions", ["SUCCESS"], "not a failing conclusion"),
        ("log_patterns", ["("], "not a regular expression"),
        ("title_check", "", "title_check"),
    ],
)
def test_the_rerun_rules_are_validated(path: str, value: object, message: str) -> None:
    with pytest.raises(Exception, match=message):
        conventions_with((f"hygiene.pr_health.rerun.{path}", value))


# --- the command ---------------------------------------------------------------------------------------------------


class Both(RerunGh):
    """The reads of the health report as well as the run list, logs and reruns."""

    def __init__(self, pulls: list[dict[str, Any]], **options: Any) -> None:
        super().__init__(**options)
        self.health = FakeGh(pulls, rules_for(REQUIRED))

    def __call__(self, arguments: list[str]) -> str:
        if arguments[:2] == ["pr", "list"] or arguments[:2] == ["api", "repos/{owner}/{repo}/rules/branches/main"]:
            self.calls.append(arguments)
            return self.health(arguments)
        return super().__call__(arguments)


def command(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, gh: Both, *flags: str) -> int:
    """Run `repo pr-health` with `flags` in a checkout of the real conventions against the fake gh."""
    write_checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    parser = argparse.ArgumentParser()
    pr_health.add_pr_health_parser(parser.add_subparsers(dest="command"))
    return pr_health.run_pr_health(parser.parse_args(["list-stuck", *flags]), gh, NOW)


def test_rerun_alone_is_a_dry_run_that_prints_what_it_would_rerun(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = Both([with_failed(failed("build-ok", "CANCELLED"))])
    assert command(tmp_path, monkeypatch, gh, "--rerun") == 1
    assert capsys.readouterr().out.splitlines() == [
        "#10 branch-10 checks-failed code: build-ok -> author",
        "#10 branch-10 rerun build-ok (run 500): concluded cancelled -> would rerun",
    ]
    assert gh.reruns() == []


def test_apply_reruns_and_the_report_says_so(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = Both([with_failed(failed("build-ok", "CANCELLED"))])
    command(tmp_path, monkeypatch, gh, "--rerun", "--apply")
    assert capsys.readouterr().out.splitlines()[-1].endswith("-> reran")
    assert gh.reruns() == [["run", "rerun", "500", "--failed"]]


def test_a_fixed_title_is_reported_as_needing_a_new_event(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = Both([with_failed(failed("title"))])
    command(tmp_path, monkeypatch, gh, "--rerun", "--apply")
    line = capsys.readouterr().out.splitlines()[-1]
    assert line.startswith("#10 branch-10 rerun title (run 500): the title now passes check-title -> new-event:")
    assert "needs a new event: edit" in line
    assert gh.reruns() == []


def test_without_rerun_nothing_extra_is_read(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    gh = Both([with_failed(failed("title"))])
    command(tmp_path, monkeypatch, gh)
    assert all(call[:2] in (["pr", "list"], ["api", "repos/{owner}/{repo}/rules/branches/main"]) for call in gh.calls)
    assert "headRefOid" not in gh.calls[0][-1]


def test_apply_needs_rerun(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    with pytest.raises(ConfigError, match="--apply goes with --labels or --rerun"):
        command(tmp_path, monkeypatch, Both([]), "--apply")


def test_json_lists_the_reruns(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = Both([with_failed(failed("build-ok", "CANCELLED")), pull(11)])
    command(tmp_path, monkeypatch, gh, "--rerun", "--apply", "--json")
    out = json.loads(capsys.readouterr().out)
    assert [(item["number"], item["checks"], item["status"]) for item in out["rerun"]] == [(10, ["build-ok"], "reran")]
    assert out["stuck"][0]["checks"] == ["build-ok"]


def test_json_has_no_rerun_key_without_the_option(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    command(tmp_path, monkeypatch, Both([pull()]), "--json")
    assert "rerun" not in json.loads(capsys.readouterr().out)

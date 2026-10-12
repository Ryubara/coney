# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools repo pr-health`: made-up pull requests in the shape `gh pr list --json` gives, no network."""

import argparse
import json
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REMOVE, REPO, changed, conventions_with, write_checkout

from coney_tools import pr_health
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.pr_health import judge, required_from_rules

NOW = datetime(2026, 10, 12, 12, 0, tzinfo=UTC)
CONVENTIONS = load_conventions(REPO)
REQUIRED = CONVENTIONS.merge.required_checks


def when(hours_ago: float) -> str:
    """A GitHub timestamp `hours_ago` hours before NOW."""
    return (NOW - timedelta(hours=hours_ago)).strftime("%Y-%m-%dT%H:%M:%SZ")


def run(name: str, conclusion: str = "SUCCESS", status: str = "COMPLETED", hours_ago: float = 1.0) -> dict[str, Any]:
    """A check run in the rollup."""
    return {
        "__typename": "CheckRun",
        "name": name,
        "status": status,
        "conclusion": conclusion if status == "COMPLETED" else "",
        "startedAt": when(hours_ago),
        "completedAt": when(hours_ago - 0.1) if status == "COMPLETED" else "0001-01-01T00:00:00Z",
    }


def green() -> list[dict[str, Any]]:
    """Every required check passed."""
    return [run(name) for name in REQUIRED]


def pull(number: int = 10, **changes: Any) -> dict[str, Any]:
    """A healthy open pull request into main: green, mergeable, auto-merge on."""
    result: dict[str, Any] = {
        "number": number,
        "title": "tools: Add a thing",
        "headRefName": f"branch-{number}",
        "baseRefName": "main",
        "isDraft": False,
        "mergeable": "MERGEABLE",
        "autoMergeRequest": {"mergeMethod": "SQUASH"},
        "statusCheckRollup": green(),
        "url": f"https://example.invalid/pull/{number}",
    }
    result.update(changes)
    return result


def causes(health: pr_health.Health) -> list[tuple[int, str, str]]:
    """The (number, cause, detail) of each finding."""
    return [(item.number, item.cause, item.detail) for item in health.stuck]


def test_a_healthy_pull_request_is_not_stuck() -> None:
    health = judge([pull()], REQUIRED, CONVENTIONS, NOW)
    assert health.stuck == [] and health.checked == 1 and health.notes == []


def test_a_failed_lint_check_is_named_with_its_class_and_goes_to_the_author() -> None:
    rollup = [run("format", "FAILURE"), run("title", "FAILURE")] + [
        run(n) for n in REQUIRED if n not in ("format", "title")
    ]
    health = judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW)
    assert causes(health) == [(10, "checks-failed", "lint: format, title")]
    assert health.stuck[0].who == "author" and health.stuck[0].kind == "lint"
    assert health.stuck[0].line() == "#10 branch-10 checks-failed lint: format, title -> author"


def test_a_failed_build_or_python_check_is_code() -> None:
    rollup = [run("build-ok", "FAILURE")] + [run(n) for n in REQUIRED if n != "build-ok"]
    health = judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW)
    assert causes(health) == [(10, "checks-failed", "code: build-ok")]


def test_lint_and_code_failures_together_are_code_and_listed_by_class() -> None:
    rollup = [run("build-ok", "TIMED_OUT"), run("docs", "FAILURE")] + [
        run(n) for n in REQUIRED if n not in ("build-ok", "docs")
    ]
    health = judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW)
    assert causes(health) == [(10, "checks-failed", "code: build-ok; lint: docs")]


def test_a_failing_check_that_is_not_required_is_ignored() -> None:
    rollup = [*green(), run("build (linux-gcc)", "FAILURE"), run("codecov/patch", "FAILURE")]
    assert judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW).stuck == []


def test_a_commit_status_that_failed_counts_like_a_check_run() -> None:
    status = {"__typename": "StatusContext", "context": "title", "state": "FAILURE", "startedAt": when(2)}
    rollup = [status] + [run(n) for n in REQUIRED if n != "title"]
    assert causes(judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW)) == [
        (10, "checks-failed", "lint: title")
    ]


def test_a_rerun_that_passed_replaces_the_failed_run() -> None:
    rollup = [run("build-ok", "FAILURE", hours_ago=5), run("build-ok", "SUCCESS", hours_ago=1)]
    rollup += [run(n) for n in REQUIRED if n != "build-ok"]
    assert judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW).stuck == []


def test_skipped_and_neutral_required_checks_pass() -> None:
    rollup = [run("docs", "SKIPPED"), run("body", "NEUTRAL")] + [run(n) for n in REQUIRED if n not in ("docs", "body")]
    assert judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW).stuck == []


def test_a_conflict_with_main_is_reported() -> None:
    health = judge([pull(mergeable="CONFLICTING")], REQUIRED, CONVENTIONS, NOW)
    assert causes(health) == [(10, "conflict", "with main")]
    assert health.stuck[0].who == "author"


def test_unknown_mergeability_is_a_note_not_a_finding() -> None:
    health = judge([pull(mergeable="UNKNOWN")], REQUIRED, CONVENTIONS, NOW)
    assert health.stuck == []
    assert [(n.number, n.what) for n in health.notes] == [(10, "mergeable-unknown")]
    assert list(pr_health.format_text(health, verbose=False)) == []
    assert "#10 branch-10 mergeable-unknown" in next(iter(pr_health.format_text(health, verbose=True)))


def test_a_missing_mergeable_field_is_unknown_too() -> None:
    health = judge([pull(mergeable=None)], REQUIRED, CONVENTIONS, NOW)
    assert health.stuck == [] and health.notes[0].what == "mergeable-unknown"


def test_a_required_check_pending_too_long_is_stuck_in_ci_for_the_coordinator() -> None:
    rollup = [run("build-ok", status="IN_PROGRESS", hours_ago=4.2)] + [run(n) for n in REQUIRED if n != "build-ok"]
    health = judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW)
    assert causes(health) == [(10, "ci-stuck", "build-ok 4.2 h (limit 3.0 h)")]
    assert health.stuck[0].who == "coordinator" and health.stuck[0].checks == ("build-ok",)


def test_a_check_pending_less_than_the_limit_is_fine() -> None:
    rollup = [run("build-ok", status="QUEUED", hours_ago=2.9)] + [run(n) for n in REQUIRED if n != "build-ok"]
    assert judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW).stuck == []


def test_the_pending_limit_comes_from_the_conventions_and_an_option_overrides_it() -> None:
    rollup = [run("build-ok", status="IN_PROGRESS", hours_ago=2.0)] + [run(n) for n in REQUIRED if n != "build-ok"]
    one_hour = conventions_with(("hygiene.thresholds.pr_pending_hours", 1))
    assert causes(judge([pull(statusCheckRollup=rollup)], REQUIRED, one_hour, NOW)) == [
        (10, "ci-stuck", "build-ok 2.0 h (limit 1.0 h)")
    ]
    assert judge([pull(statusCheckRollup=rollup)], REQUIRED, one_hour, NOW, pending_hours=6).stuck == []


def test_a_pending_check_that_is_not_required_is_ignored() -> None:
    rollup = [*green(), run("build (macos)", status="IN_PROGRESS", hours_ago=9)]
    assert judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW).stuck == []


def test_a_pending_check_without_a_start_time_is_not_judged() -> None:
    waiting = {"__typename": "CheckRun", "name": "build-ok", "status": "QUEUED", "conclusion": "", "startedAt": ""}
    rollup = [waiting] + [run(n) for n in REQUIRED if n != "build-ok"]
    assert judge([pull(statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW).stuck == []


def test_auto_merge_off_on_a_ready_pull_request_into_main() -> None:
    health = judge([pull(autoMergeRequest=None)], REQUIRED, CONVENTIONS, NOW)
    assert causes(health) == [(10, "auto-merge-off", "into main")]
    assert health.stuck[0].who == "coordinator"


def test_a_stacked_pull_request_is_a_note_and_never_lacks_auto_merge() -> None:
    health = judge([pull(baseRefName="feature-a", autoMergeRequest=None)], REQUIRED, CONVENTIONS, NOW)
    assert health.stuck == []
    assert [(n.what, n.detail) for n in health.notes] == [("stacked", "on feature-a")]


def test_a_stacked_pull_request_with_a_failed_required_check_is_still_stuck() -> None:
    rollup = [run("format", "FAILURE")] + [run(n) for n in REQUIRED if n != "format"]
    health = judge([pull(baseRefName="feature-a", statusCheckRollup=rollup)], REQUIRED, CONVENTIONS, NOW)
    assert causes(health) == [(10, "checks-failed", "lint: format")]


def test_a_draft_is_never_stuck() -> None:
    rollup = [run("build-ok", "FAILURE")]
    draft = pull(isDraft=True, autoMergeRequest=None, mergeable="CONFLICTING", statusCheckRollup=rollup)
    health = judge([draft], REQUIRED, CONVENTIONS, NOW)
    assert health.stuck == [] and [n.what for n in health.notes] == ["draft"]


def test_findings_are_sorted_by_number_then_cause() -> None:
    late = pull(30, mergeable="CONFLICTING", autoMergeRequest=None)
    early = pull(5, statusCheckRollup=[run("format", "FAILURE")], mergeable="CONFLICTING")
    health = judge([late, early], REQUIRED, CONVENTIONS, NOW)
    assert [(item.number, item.cause) for item in health.stuck] == [
        (5, "checks-failed"),
        (5, "conflict"),
        (30, "conflict"),
        (30, "auto-merge-off"),
    ]


def test_the_required_checks_come_from_the_rules_not_from_a_constant() -> None:
    rollup = [run("only-this", "FAILURE"), run("format", "FAILURE")]
    health = judge([pull(statusCheckRollup=rollup)], ["only-this"], CONVENTIONS, NOW)
    assert causes(health) == [(10, "checks-failed", "code: only-this")]


def test_required_checks_are_read_from_the_branch_rules() -> None:
    rules = [
        {"type": "deletion"},
        {
            "type": "required_status_checks",
            "parameters": {"required_status_checks": [{"context": "title"}, {"context": "build-ok"}]},
        },
        {"type": "required_status_checks", "parameters": {"required_status_checks": [{"context": "build-ok"}]}},
        {"type": "pull_request", "parameters": {}},
    ]
    assert required_from_rules(rules) == ("title", "build-ok")
    assert required_from_rules([]) == () and required_from_rules({"message": "Not Found"}) == ()


# --- the real conventions -----------------------------------------------------------------------------------------


def test_the_lint_checks_are_required_checks() -> None:
    lint = set(CONVENTIONS.hygiene.pr_health.lint_checks)
    assert lint <= set(REQUIRED) and not lint & {"build-ok", "python-ok"}
    assert CONVENTIONS.hygiene.thresholds.pr_pending_hours == 3


def test_the_pending_limit_must_be_a_whole_number_of_hours() -> None:
    with pytest.raises(Exception, match=r"hygiene\.thresholds\.pr_pending_hours"):
        conventions_with(("hygiene.thresholds.pr_pending_hours", 0))
    with pytest.raises(Exception, match=r"pr_pending_hours"):
        conventions_with(("hygiene.thresholds.pr_pending_hours", REMOVE))


# --- the command --------------------------------------------------------------------------------------------------


class FakeGh:
    """A `gh` that answers the two reads the command makes, and records them."""

    def __init__(self, pulls: object, rules: object, fail: str = "") -> None:
        self.pulls, self.rules, self.fail = pulls, rules, fail
        self.calls: list[list[str]] = []

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if self.fail:
            raise OSError(self.fail)
        if arguments[:2] == ["pr", "list"]:
            return json.dumps(self.pulls)
        assert arguments == ["api", "repos/{owner}/{repo}/rules/branches/main"]
        return json.dumps(self.rules)


def rules_for(names: tuple[str, ...]) -> list[dict[str, Any]]:
    """The ruleset answer requiring `names`."""
    return [
        {"type": "required_status_checks", "parameters": {"required_status_checks": [{"context": n} for n in names]}}
    ]


def command(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, gh: FakeGh, *flags: str) -> int:
    """Run the command in a checkout of the real conventions with the fake gh."""
    write_checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    parser = argparse.ArgumentParser()
    pr_health.add_pr_health_parser(parser.add_subparsers(dest="command"))
    return pr_health.run_pr_health(parser.parse_args(["pr-health", *flags]), gh, NOW)


def test_the_command_exits_0_and_prints_nothing_when_nothing_is_stuck(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh([pull()], rules_for(REQUIRED))
    assert command(tmp_path, monkeypatch, gh) == 0
    assert capsys.readouterr().out == ""
    assert len(gh.calls) == 2 and gh.calls[0][:2] == ["pr", "list"] and "statusCheckRollup" in gh.calls[0][-1]


def test_the_command_prints_one_line_per_cause_and_exits_1(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    pulls = [pull(12, mergeable="CONFLICTING"), pull(7, statusCheckRollup=[run("body", "FAILURE")])]
    assert command(tmp_path, monkeypatch, FakeGh(pulls, rules_for(REQUIRED))) == 1
    assert capsys.readouterr().out.splitlines() == [
        "#7 branch-7 checks-failed lint: body -> author",
        "#12 branch-12 conflict with main -> author",
    ]


def test_verbose_shows_what_was_passed_over(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    pulls = [pull(1, mergeable="UNKNOWN"), pull(2, isDraft=True), pull(3, baseRefName="x")]
    assert command(tmp_path, monkeypatch, FakeGh(pulls, rules_for(REQUIRED)), "--verbose") == 0
    assert capsys.readouterr().out.splitlines() == [
        "#1 branch-1 mergeable-unknown GitHub has not computed it yet (not stuck)",
        "#2 branch-2 draft (not stuck)",
        "#3 branch-3 stacked on x (not stuck)",
    ]


def test_json_lists_the_findings_the_other_pull_requests_and_the_notices(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    pulls = [pull(4, autoMergeRequest=None), pull(5, isDraft=True)]
    assert command(tmp_path, monkeypatch, FakeGh(pulls, []), "--json") == 1
    out = json.loads(capsys.readouterr().out)
    assert out["checked"] == 2
    assert [(s["number"], s["cause"], s["who"]) for s in out["stuck"]] == [(4, "auto-merge-off", "coordinator")]
    assert out["other"] == [{"number": 5, "branch": "branch-5", "what": "draft", "detail": ""}]
    assert out["notices"] == ["the main ruleset requires no status checks"]


def test_the_pending_option_is_in_hours(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    rollup = [run("build-ok", status="IN_PROGRESS", hours_ago=2.0)] + [run(n) for n in REQUIRED if n != "build-ok"]
    gh = FakeGh([pull(statusCheckRollup=rollup)], rules_for(REQUIRED))
    assert command(tmp_path, monkeypatch, gh) == 0
    assert command(tmp_path, monkeypatch, gh, "--pending-hours", "1.5") == 1
    assert "ci-stuck build-ok 2.0 h (limit 1.5 h) -> coordinator" in capsys.readouterr().out


def test_a_gh_failure_is_a_config_error_so_the_exit_code_is_2(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    with pytest.raises(ConfigError, match="cannot read GitHub with gh"):
        command(tmp_path, monkeypatch, FakeGh([], [], fail="gh could not run"))


def test_unreadable_json_is_a_config_error(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    with pytest.raises(ConfigError):
        command(tmp_path, monkeypatch, FakeGh({"not": "a list"}, []))


def test_the_default_branch_comes_from_the_conventions(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    write_checkout(tmp_path, changed(("repo.default_branch", "trunk")))
    monkeypatch.chdir(tmp_path)
    seen: list[list[str]] = []

    def gh(arguments: list[str]) -> str:
        seen.append(arguments)
        return "[]"

    parser = argparse.ArgumentParser()
    pr_health.add_pr_health_parser(parser.add_subparsers(dest="command"))
    pr_health.run_pr_health(parser.parse_args(["pr-health"]), gh, NOW)
    assert seen[1] == ["api", "repos/{owner}/{repo}/rules/branches/trunk"]

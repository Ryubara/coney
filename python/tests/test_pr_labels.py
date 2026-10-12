# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools repo pr-health --labels`: made-up pull requests and a fake `gh`, no network."""

import argparse
import json
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REMOVE, REPO, conventions_with, write_checkout

from coney_tools import pr_health
from coney_tools.config import ConfigError
from coney_tools.conventions import PrHealth, load_conventions
from coney_tools.pr_health import judge
from coney_tools.pr_labels import LabelChange, plan, write

NOW = datetime(2026, 10, 12, 12, 0, tzinfo=UTC)
CONVENTIONS = load_conventions(REPO)
REQUIRED = CONVENTIONS.merge.required_checks
RULES = CONVENTIONS.hygiene.pr_health
CONFLICT, FAILING = RULES.labels["conflict"], RULES.labels["checks-failed"]


def labelled(*names: str) -> list[dict[str, str]]:
    """A pull request's `labels` in gh's shape."""
    return [{"name": name} for name in names]


def checks(failing: tuple[str, ...] = ()) -> list[dict[str, Any]]:
    """A rollup in which the required checks pass, except the `failing` ones."""
    return [
        {
            "name": name,
            "status": "COMPLETED",
            "conclusion": "FAILURE" if name in failing else "SUCCESS",
            "startedAt": "2026-10-12T10:00:00Z",
            "completedAt": "2026-10-12T10:05:00Z",
        }
        for name in REQUIRED
    ]


def pull(number: int, mergeable: str = "MERGEABLE", failing: tuple[str, ...] = (), **changes: Any) -> dict[str, Any]:
    """An open pull request into main with auto-merge on."""
    result: dict[str, Any] = {
        "number": number,
        "title": "tools: Add a thing",
        "headRefName": f"branch-{number}",
        "baseRefName": "main",
        "isDraft": False,
        "mergeable": mergeable,
        "autoMergeRequest": {"mergeMethod": "SQUASH"},
        "statusCheckRollup": checks(failing),
        "labels": [],
        "url": f"https://example.invalid/pull/{number}",
    }
    result.update(changes)
    return result


def changes_for(pulls: list[dict[str, Any]]) -> list[tuple[int, str, bool]]:
    """The (number, label, add) of each planned change."""
    health = judge(pulls, REQUIRED, CONVENTIONS, NOW)
    return [(c.number, c.label, c.add) for c in plan(pulls, health, RULES)]


# --- the plan -----------------------------------------------------------------------------------------------------


def test_a_healthy_pull_request_gets_no_label() -> None:
    assert changes_for([pull(1)]) == []


def test_a_conflicting_pull_request_gets_the_conflict_label() -> None:
    assert changes_for([pull(1, "CONFLICTING")]) == [(1, CONFLICT, True)]


def test_a_pull_request_failing_a_required_check_gets_the_failing_label() -> None:
    assert changes_for([pull(1, failing=("build-ok",))]) == [(1, FAILING, True)]


def test_a_pull_request_with_both_problems_gets_both_labels() -> None:
    assert changes_for([pull(1, "CONFLICTING", failing=("body",))]) == [(1, CONFLICT, True), (1, FAILING, True)]


def test_a_failing_check_that_is_not_required_is_ignored() -> None:
    rollup = [*checks(), {"name": "optional", "status": "COMPLETED", "conclusion": "FAILURE"}]
    assert changes_for([pull(1, statusCheckRollup=rollup)]) == []


def test_a_label_already_there_is_not_added_again() -> None:
    assert changes_for([pull(1, "CONFLICTING", labels=labelled(CONFLICT))]) == []


def test_a_label_whose_cause_is_gone_is_removed() -> None:
    both = labelled(CONFLICT, FAILING)
    assert changes_for([pull(1, labels=both)]) == [(1, CONFLICT, False), (1, FAILING, False)]
    assert changes_for([pull(2, "CONFLICTING", labels=both)]) == [(2, FAILING, False)]


def test_a_draft_is_never_stuck_so_it_loses_the_labels() -> None:
    assert changes_for([pull(1, "CONFLICTING", isDraft=True, labels=labelled(CONFLICT))]) == [(1, CONFLICT, False)]


def test_other_labels_are_never_touched() -> None:
    other = labelled("bug", "S-pending-review", "A-tools")
    assert changes_for([pull(1, labels=other), pull(2, "CONFLICTING", labels=other)]) == [(2, CONFLICT, True)]


def test_an_unknown_mergeability_keeps_the_conflict_label_as_it_is() -> None:
    pulls = [pull(1, "UNKNOWN", labels=labelled(CONFLICT)), pull(2, "UNKNOWN")]
    assert changes_for(pulls) == []


def test_an_unknown_mergeability_still_decides_the_failing_label() -> None:
    assert changes_for([pull(1, "UNKNOWN", failing=("body",))]) == [(1, FAILING, True)]
    assert changes_for([pull(2, "UNKNOWN", labels=labelled(FAILING))]) == [(2, FAILING, False)]


def test_changes_are_ordered_by_pull_request_number() -> None:
    pulls = [pull(9, "CONFLICTING"), pull(3, "CONFLICTING")]
    assert [c.number for c in plan(pulls, judge(pulls, REQUIRED, CONVENTIONS, NOW), RULES)] == [3, 9]


def test_the_line_says_what_would_and_what_does_happen() -> None:
    add, drop = LabelChange(12, CONFLICT, True, "conflict with main"), LabelChange(7, FAILING, False, "cleared")
    assert add.line(False) == f"would add {CONFLICT} to #12 (conflict with main)"
    assert drop.line(True) == f"remove {FAILING} from #7 (cleared)"


# --- writing ------------------------------------------------------------------------------------------------------


def test_write_uses_the_rest_labels_endpoints() -> None:
    calls: list[list[str]] = []

    def gh(arguments: list[str]) -> str:
        calls.append(arguments)
        return ""

    write([LabelChange(12, CONFLICT, True, "x"), LabelChange(7, "a label", False, "cleared")], gh)
    assert calls == [
        ["api", "--method", "POST", "repos/{owner}/{repo}/issues/12/labels", "-f", f"labels[]={CONFLICT}", "--silent"],
        ["api", "--method", "DELETE", "repos/{owner}/{repo}/issues/7/labels/a%20label", "--silent"],
    ]


def test_a_failed_write_is_a_config_error_naming_the_pull_request() -> None:
    def gh(arguments: list[str]) -> str:
        raise OSError("HTTP 403")

    with pytest.raises(ConfigError, match=r"#12.*HTTP 403"):
        write([LabelChange(12, CONFLICT, True, "x")], gh)


# --- the command --------------------------------------------------------------------------------------------------


class FakeGh:
    """A `gh` that answers the reads with successive pull request lists (the last repeats) and records every call."""

    def __init__(self, *reads: list[dict[str, Any]]) -> None:
        self.reads = list(reads)
        self.calls: list[list[str]] = []
        self.rules = [
            {
                "type": "required_status_checks",
                "parameters": {"required_status_checks": [{"context": name} for name in REQUIRED]},
            }
        ]

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if arguments[:2] == ["pr", "list"]:
            return json.dumps(self.reads.pop(0) if len(self.reads) > 1 else self.reads[0])
        if arguments[0] == "api" and "--method" not in arguments:
            return json.dumps(self.rules)
        return ""

    def writes(self) -> list[list[str]]:
        """The calls that changed something."""
        return [call for call in self.calls if "--method" in call]


def command(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, gh: FakeGh, *flags: str) -> tuple[int, list[float]]:
    """Run the command in a checkout of the real conventions; returns its exit code and the sleeps it asked for."""
    write_checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    parser = argparse.ArgumentParser()
    pr_health.add_pr_health_parser(parser.add_subparsers(dest="command"))
    slept: list[float] = []
    code = pr_health.run_pr_health(parser.parse_args(["pr-health", *flags]), gh, NOW, slept.append)
    return code, slept


def test_labels_without_apply_only_prints_the_plan(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh([pull(12, "CONFLICTING"), pull(7, labels=labelled(FAILING))])
    code, _ = command(tmp_path, monkeypatch, gh, "--labels")
    assert code == 1  # a conflict is still a finding
    assert capsys.readouterr().out.splitlines() == [
        "#12 branch-12 conflict with main -> author",
        f"would remove {FAILING} from #7 (cleared)",
        f"would add {CONFLICT} to #12 (conflict with main)",
    ]
    assert gh.writes() == []


def test_apply_writes_the_changes_and_says_so(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh([pull(12, "CONFLICTING"), pull(7, labels=labelled(FAILING))])
    command(tmp_path, monkeypatch, gh, "--labels", "--apply")
    out = capsys.readouterr().out.splitlines()
    assert out[1:] == [f"remove {FAILING} from #7 (cleared)", f"add {CONFLICT} to #12 (conflict with main)"]
    assert [call[2:4] for call in gh.writes()] == [
        ["DELETE", f"repos/{{owner}}/{{repo}}/issues/7/labels/{FAILING}"],
        ["POST", "repos/{owner}/{repo}/issues/12/labels"],
    ]


def test_without_labels_the_command_reads_only() -> None:
    parser = argparse.ArgumentParser()
    pr_health.add_pr_health_parser(parser.add_subparsers(dest="command"))
    args = parser.parse_args(["pr-health"])
    assert args.labels is False and args.apply is False


def test_apply_without_labels_is_refused(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    gh = FakeGh([])
    with pytest.raises(ConfigError, match="--apply goes with --labels"):
        command(tmp_path, monkeypatch, gh, "--apply")
    assert gh.calls == []


def test_an_unknown_mergeability_is_reread_until_github_answers(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh([pull(5, "UNKNOWN")], [pull(5, "UNKNOWN")], [pull(5, "CONFLICTING")])
    _, slept = command(tmp_path, monkeypatch, gh, "--labels", "--apply")
    assert slept == [RULES.unknown_wait_seconds] * 2
    assert f"add {CONFLICT} to #5" in capsys.readouterr().out
    assert len(gh.writes()) == 1


def test_rereading_stops_at_the_limit_and_leaves_the_conflict_label_alone(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh([pull(5, "UNKNOWN", labels=labelled(CONFLICT))])
    code, slept = command(tmp_path, monkeypatch, gh, "--labels", "--apply")
    assert code == 0 and len(slept) == RULES.unknown_retries
    assert capsys.readouterr().out == "" and gh.writes() == []


def test_the_json_report_lists_the_label_changes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh([pull(12, "CONFLICTING")])
    command(tmp_path, monkeypatch, gh, "--labels", "--json")
    out = json.loads(capsys.readouterr().out)
    assert out["labels"] == [{"number": 12, "label": CONFLICT, "action": "add", "why": "conflict with main"}]


def test_the_plain_command_makes_no_label_calls(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    gh = FakeGh([pull(12, "CONFLICTING")])
    command(tmp_path, monkeypatch, gh)
    assert len(gh.calls) == 2 and "labels" in gh.calls[0][-1]


# --- the conventions ----------------------------------------------------------------------------------------------


def test_the_real_conventions_map_conflict_and_failing_to_labels_of_the_list() -> None:
    assert isinstance(RULES, PrHealth)
    assert dict(RULES.labels) == {"conflict": "S-conflict", "checks-failed": "S-failing"}
    assert {"S-conflict", "S-failing"} <= set(CONVENTIONS.labels.names("status"))
    assert RULES.unknown_retries >= 1 and RULES.unknown_wait_seconds >= 1


@pytest.mark.parametrize(
    ("change", "message"),
    [
        (("hygiene.pr_health.labels", {"conflict": "S-nowhere"}), "S-nowhere"),
        (("hygiene.pr_health.labels", {"stale": "S-conflict"}), "stale"),
        (("hygiene.pr_health.labels", {"conflict": "S-failing", "checks-failed": "S-failing"}), "two causes"),
        (("hygiene.pr_health.labels", REMOVE), "labels"),
        (("hygiene.pr_health.unknown_retries", -1), "unknown_retries"),
        (("hygiene.pr_health.unknown_wait_seconds", 0), "unknown_wait_seconds"),
    ],
)
def test_a_bad_label_setting_names_its_key(change: tuple[str, object], message: str) -> None:
    with pytest.raises(Exception, match=message):
        conventions_with(change)

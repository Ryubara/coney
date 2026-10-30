# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the GitHub reads of the slot-fill check (slot_fill_github): what is asked, what is read from the answers,
the cache and the budget. `gh` is a stand-in that returns JSON; nothing touches the network
(docs/guides/slot-fill.md).
"""

import json
import subprocess
from datetime import timedelta
from pathlib import Path
from typing import Any

import pytest
from audit_support import CONVENTIONS
from claim_support import START, Clock

from coney_tools import slot_fill_github
from coney_tools.config import ConfigError
from coney_tools.issue_claim_model import Card, render_card
from coney_tools.issue_form import section_heading
from coney_tools.slot_fill import counted_claims
from coney_tools.slot_fill_github import CACHE_FILE, GhSources

RULES = CONVENTIONS.claims
HEADING = section_heading(CONVENTIONS, "blocked-by") or ""


def raw_issue(number: int, *labels: str, assignee: str | None = None, body: str = "") -> dict[str, Any]:
    """An issue as `gh issue list --json` prints it."""
    return {
        "number": number,
        "title": f"title {number}",
        "labels": [{"name": name} for name in labels],
        "assignees": [{"login": assignee}] if assignee else [],
        "body": body,
    }


class FakeGh:
    """A `gh` that answers by the start of the command; every call is recorded."""

    def __init__(self) -> None:
        self.answers: dict[str, object] = {}
        self.calls: list[list[str]] = []
        self.fail: str | None = None

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        key = " ".join(arguments[:2])
        if self.fail is not None and key.startswith(self.fail):
            raise OSError("gh failed: offline")
        for start, answer in self.answers.items():
            if " ".join(arguments).startswith(start):
                return answer if isinstance(answer, str) else json.dumps(answer)
        raise OSError(f"no answer for {arguments}")

    def count(self, start: str) -> int:
        return sum(1 for call in self.calls if " ".join(call).startswith(start))


@pytest.fixture
def gh() -> FakeGh:
    return FakeGh()


@pytest.fixture
def clock() -> Clock:
    return Clock(START)


class Seconds:
    """The monotonic clock of the budget, moved by hand."""

    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now


def sources(
    tmp_path: Path, gh: FakeGh, clock: Clock, seconds: Seconds | None = None, changed: float = 0.0
) -> GhSources:
    return GhSources(CONVENTIONS, tmp_path, gh, clock.now, seconds or Seconds(), changed=lambda: changed)


def test_the_ready_issues_are_the_open_ones_with_the_claimable_status(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = [raw_issue(5, "task", RULES.claimable_status, "workflow"), raw_issue(6, "task")]
    found = sources(tmp_path, gh, clock).ready()
    first = found[0]
    assert (first.number, first.labels, first.assignees) == (5, ("task", RULES.claimable_status, "workflow"), ())
    (call,) = gh.calls
    assert call[:7] == ["issue", "list", "--state", "open", "--label", RULES.claimable_status, "--limit"]


def card_lines(number: int, *cards: Card) -> str:
    """The comments of an issue as `gh api --jq` prints them: the bot's claim cards, one JSON object per line."""
    return chr(10).join(
        json.dumps(
            {
                "id": 100 * number + index,
                "user": {"login": RULES.bot_login, "type": "Bot"},
                "author_association": "NONE",
                "created_at": "2026-10-22T12:00:00Z",
                "body": render_card(card, RULES),
            }
        )
        for index, card in enumerate(cards)
    )


def held(number: int, **changes: Any) -> Card:
    """A held claim card of the account."""
    return Card(issue=number, claimant="Ryubara", claimed_at=START, **changes)


def test_the_claims_are_the_held_cards_of_the_issues_assigned_to_me(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = [{"number": 1}, {"number": 2}, {"number": 3}, {"number": 4}]
    gh.answers["api repos/{owner}/{repo}/issues/1/"] = card_lines(1, held(1, task="one"))
    gh.answers["api repos/{owner}/{repo}/issues/2/"] = card_lines(2, held(2, status="pr-open", exempt=True))
    gh.answers["api repos/{owner}/{repo}/issues/3/"] = ""  # assigned by hand: no card, no claim
    gh.answers["api repos/{owner}/{repo}/issues/4/"] = card_lines(4, held(4, status="released"))
    found = sources(tmp_path, gh, clock).claimed()  # issue 4 was released
    assert [(c.issue, c.task, c.status, c.exempt) for c in found] == [
        (1, "one", "claimed", False),
        (2, None, "pr-open", True),
    ]
    assert "--assignee" in gh.calls[0] and "@me" in gh.calls[0]


def test_a_claim_paused_by_a_label_is_read_whatever_labels_the_issue_has(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    gh.answers["issue list"] = [{"number": 7}]
    gh.answers["api repos/{owner}/{repo}/issues/7/"] = card_lines(7, held(7, waiting="S-on-hold"))
    (card,) = sources(tmp_path, gh, clock).claimed()
    assert (card.issue, card.waiting) == (7, "S-on-hold")
    assert counted_claims([card]) == ["issue-7"]


def test_the_failed_pulls_are_mine_open_not_draft_with_a_required_check_failed(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    def pull(number: int, *conclusions: tuple[str, str], draft: bool = False) -> dict[str, Any]:
        return {
            "number": number,
            "headRefName": f"branch-{number}",
            "headRefOid": "abcdef1234",
            "isDraft": draft,
            "closingIssuesReferences": [{"number": 40 + number}],
            "statusCheckRollup": [
                {"__typename": "CheckRun", "name": name, "status": "COMPLETED", "conclusion": result}
                for name, result in conclusions
            ],
        }

    gh.answers["pr list"] = [
        pull(1, ("build-ok", "FAILURE"), ("docs", "SUCCESS")),
        pull(2, ("build-ok", "SUCCESS")),
        pull(3, ("build-ok", "FAILURE"), draft=True),
        pull(4, ("optional-check", "FAILURE")),
        pull(5, ("python-ok", "FAILURE"), ("build-ok", "FAILURE")),
    ]
    gh.answers["api repos"] = [
        {
            "type": "required_status_checks",
            "parameters": {"required_status_checks": [{"context": "build-ok"}, {"context": "python-ok"}]},
        }
    ]
    found = sources(tmp_path, gh, clock).failed_pulls()
    assert [(p.number, p.checks, p.closes, p.head) for p in found] == [
        (1, ("build-ok",), (41,), "abcdef1234"),
        (5, ("build-ok", "python-ok"), (45,), "abcdef1234"),
    ]
    assert "--author" in gh.calls[0] and "@me" in gh.calls[0]


def test_a_blocker_that_is_not_finished_is_reported(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["api repos/{owner}/{repo}/issues/4"] = {"number": 4, "title": "t", "state": "open", "labels": []}
    gh.answers["api repos/{owner}/{repo}/issues/3"] = {
        "number": 3,
        "title": "t",
        "state": "closed",
        "state_reason": "completed",
    }
    gh.answers["api graphql"] = {
        "data": {
            "repository": {
                "issue": {
                    "closedByPullRequestsReferences": {"nodes": [{"number": 9, "state": "MERGED", "merged": True}]}
                }
            }
        }
    }
    body = f"### {HEADING}\n\n#4 and #3\n\n### Next\n\nx"
    from slot_fill_support import issue

    found = sources(tmp_path, gh, clock).blockers(issue(5, body=body))
    assert found == [4]


def test_an_issue_without_the_section_has_no_blockers(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    from slot_fill_support import issue

    assert sources(tmp_path, gh, clock).blockers(issue(5, body="### Summary\n\n#4")) == []
    assert gh.calls == []


def test_the_answer_is_kept_for_the_cache_time(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = [raw_issue(5, RULES.claimable_status)]
    reader = sources(tmp_path, gh, clock)
    reader.ready()
    clock.advance(seconds=CONVENTIONS.tasks.start.fill.cache_seconds - 1)
    reader.ready()
    assert gh.count("issue list") == 1
    clock.advance(seconds=2)
    reader.ready()
    assert gh.count("issue list") == 2
    assert (tmp_path / CACHE_FILE).is_file()


def test_the_cache_is_shared_between_runs_and_keyed_by_what_is_read(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list --state open --label"] = [raw_issue(5, RULES.claimable_status)]
    gh.answers["issue list --state open --assignee"] = [{"number": 1}]
    gh.answers["api repos/{owner}/{repo}/issues/1/"] = card_lines(1, held(1))
    sources(tmp_path, gh, clock).ready()
    assert [c.issue for c in sources(tmp_path, gh, clock).claimed()] == [1]
    assert [c.issue for c in sources(tmp_path, gh, clock).claimed()] == [1]  # the cards come from the cache
    assert [i.number for i in sources(tmp_path, gh, clock).ready()] == [5]
    assert gh.count("issue list") == 2 and gh.count("api") == 1


def test_a_task_file_newer_than_the_answer_drops_it(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = [raw_issue(5, RULES.claimable_status)]
    sources(tmp_path, gh, clock).ready()
    newer = (START + timedelta(seconds=5)).timestamp()
    sources(tmp_path, gh, clock, changed=newer).ready()
    assert gh.count("issue list") == 2
    older = (START - timedelta(seconds=5)).timestamp()
    sources(tmp_path, gh, clock, changed=older).ready()
    assert gh.count("issue list") == 2


def test_a_damaged_cache_is_read_again(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = [raw_issue(5, RULES.claimable_status)]
    (tmp_path / CACHE_FILE).write_text("[not json", encoding="utf-8")
    assert [i.number for i in sources(tmp_path, gh, clock).ready()] == [5]


def test_the_real_newest_change_is_the_latest_task_file(tmp_path: Path) -> None:
    import os

    assert slot_fill_github.newest_change(tmp_path / "missing", CONVENTIONS) == 0.0
    (tmp_path / "a").mkdir()
    (tmp_path / "a" / "TASK.md").write_text("x", encoding="utf-8")
    os.utime(tmp_path / "a" / "TASK.md", (1000, 1000))
    (tmp_path / "b").mkdir()
    (tmp_path / "b" / "unit.json").write_text("{}", encoding="utf-8")
    os.utime(tmp_path / "b" / "unit.json", (2000, 2000))
    (tmp_path / "c").mkdir()
    (tmp_path / "c" / "other.txt").write_text("x", encoding="utf-8")
    assert slot_fill_github.newest_change(tmp_path, CONVENTIONS) == 2000.0


def test_a_failed_read_is_a_config_error_and_ends_all_reading(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.fail = "issue list"
    reader = sources(tmp_path, gh, clock)
    with pytest.raises(ConfigError, match="cannot read GitHub"):
        reader.ready()
    gh.fail = None
    gh.answers["issue list"] = []
    with pytest.raises(ConfigError, match="an earlier read failed"):
        reader.claimed()
    assert gh.count("issue list") == 1


def test_no_read_starts_after_the_budget(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list --state open --label"] = []
    gh.answers["issue list --state open --assignee"] = []
    seconds = Seconds()
    reader = sources(tmp_path, gh, clock, seconds)
    reader.ready()
    seconds.now = CONVENTIONS.tasks.start.fill.budget_seconds + 1
    with pytest.raises(ConfigError, match="GitHub took more than"):
        reader.claimed()
    assert gh.count("issue list") == 1


def test_answers_that_are_not_what_gh_prints_are_config_errors(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = {"oops": 1}
    with pytest.raises(ConfigError, match="did not return a list"):
        sources(tmp_path, gh, clock).ready()
    gh.answers["issue list"] = [{"title": "no number"}]
    with pytest.raises(ConfigError, match="cannot be read"):
        sources(tmp_path / "other", gh, clock).ready()
    gh.answers["issue list"] = "not json at all"
    with pytest.raises(ConfigError, match="cannot read GitHub"):
        sources(tmp_path / "third", gh, clock).ready()


def test_the_bounded_runner_kills_a_slow_call_and_reports_a_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    seen: dict[str, Any] = {}

    def fake_run(command: list[str], **options: Any) -> subprocess.CompletedProcess[str]:
        seen.update(options)
        seen["command"] = command
        raise subprocess.TimeoutExpired(command, options["timeout"])

    monkeypatch.setattr(subprocess, "run", fake_run)
    with pytest.raises(OSError, match="gh could not run"):
        slot_fill_github.bounded_runner(7)(["issue", "list"])
    assert seen["timeout"] == 7 and seen["command"] == ["gh", "issue", "list"]

    def failing(command: list[str], **options: Any) -> subprocess.CompletedProcess[str]:
        return subprocess.CompletedProcess(command, 1, "", "not logged in")

    monkeypatch.setattr(subprocess, "run", failing)
    with pytest.raises(OSError, match="not logged in"):
        slot_fill_github.bounded_runner(7)(["issue", "list"])


def linked(*states: str) -> dict[str, Any]:
    """A `gh api graphql` answer for an issue that pull requests with the given states close."""
    nodes = [
        {
            "number": 900 + index,
            "state": "MERGED" if state == "merged" else state.upper(),
            "merged": state == "merged",
            "isDraft": False,
            "author": {"login": "Ryubara"},
            "baseRepository": {"nameWithOwner": "acme/widgets"},
            "commits": {"nodes": []},
        }
        for index, state in enumerate(states)
    ]
    issue = {"closedByPullRequestsReferences": {"nodes": nodes}}
    return {"data": {"repository": {"nameWithOwner": "acme/widgets", "issue": issue}}}


def test_a_fix_is_over_when_the_pull_requests_closing_its_issue_are_merged_or_closed(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    gh.answers["api graphql"] = linked("merged", "closed")
    assert sources(tmp_path, gh, clock).pulls_over((7,)) is True


def test_a_fix_is_not_over_while_a_pull_request_is_open_or_none_exists(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    gh.answers["api graphql"] = linked("closed", "open")
    assert sources(tmp_path, gh, clock).pulls_over((7,)) is False
    gh.answers["api graphql"] = linked()
    assert sources(tmp_path, gh, clock).pulls_over((7,)) is False


def test_a_fix_is_not_over_when_github_cannot_be_read(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.fail = "api graphql"
    assert sources(tmp_path, gh, clock).pulls_over((7,)) is False

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the GitHub reads of the slot-fill check (slot_fill_github): what is asked, what is read from the answers,
the cache and the budget. `gh` is a stand-in that returns JSON; nothing touches the network
(docs/guides/slot-fill.md).
"""

import json
import re
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
from coney_tools.slot_fill_github import BLOCKERS_PER_QUERY, CACHE_FILE, GhFailed, GhSources

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
    """A `gh` that answers by the start of the command; every call is recorded.

    The GraphQL queries are answered from the tables the test fills: `cards` (assigned issue -> its comments, as the
    query's nodes), `states` (blocker number -> the node GitHub has for it, None for a number it does not know)."""

    def __init__(self) -> None:
        self.answers: dict[str, object] = {}
        self.calls: list[list[str]] = []
        self.fail: str | None = None
        self.cards: dict[int, list[dict[str, Any]]] = {}
        #: Issues whose comments do not fit one query (the answer says there is a next page).
        self.long: set[int] = set()
        #: The assigned issues do not fit one query.
        self.next_page = False
        self.states: dict[int, dict[str, Any] | None] = {}

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        key = " ".join(arguments[:2])
        if self.fail is not None and key.startswith(self.fail):
            raise OSError("gh failed: offline")
        query = self._query(arguments)
        if arguments[:2] == ["api", "graphql"] and ("search(" in query or "issueOrPullRequest" in query):
            return self._graphql(query)
        for start, answer in self.answers.items():
            if " ".join(arguments).startswith(start):
                return answer if isinstance(answer, str) else json.dumps(answer)
        raise OSError(f"no answer for {arguments}")

    @staticmethod
    def _query(arguments: list[str]) -> str:
        """The text of a GraphQL query among the arguments of a call."""
        return next((item for item in arguments if item.startswith("query=")), "")

    def _graphql(self, query: str) -> str:
        """The answer to a GraphQL query: the claim cards, or the states of the blockers it names. The question of
        whether a fix is over (the pull requests that close one issue) is not asked here: it falls to `answers`."""
        if "search(" in query:
            nodes = [
                {
                    "number": number,
                    "comments": {"pageInfo": {"hasNextPage": number in self.long}, "nodes": comments},
                }
                for number, comments in self.cards.items()
            ]
            return json.dumps({"data": {"search": {"pageInfo": {"hasNextPage": self.next_page}, "nodes": nodes}}})
        repository: dict[str, Any] = {"nameWithOwner": "owner/repo"}
        missing: list[dict[str, Any]] = []
        for number in (int(found) for found in re.findall(r"b(\d+):issueOrPullRequest", query)):
            repository[f"b{number}"] = self.states[number]
            if self.states[number] is None:
                missing.append({"type": "NOT_FOUND", "path": ["repository", f"b{number}"], "message": "not found"})
        answer = json.dumps({"data": {"repository": repository}, **({"errors": missing} if missing else {})})
        if missing:  # gh prints the partial answer and exits with an error
            raise GhFailed("gh api graphql failed: Could not resolve to an issue", answer)
        return answer

    def count(self, start: str) -> int:
        return sum(1 for call in self.calls if " ".join(call).startswith(start))

    def queries(self, kind: str) -> int:
        """The GraphQL calls whose query mentions `kind`."""
        return sum(1 for call in self.calls if call[:2] == ["api", "graphql"] and kind in " ".join(call))


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


def card_comment(number: int, card: Card, *, login: str = "github-actions", kind: str = "Bot") -> dict[str, Any]:
    """A comment as the claim-card query returns it: a card the bot wrote (GraphQL prints its login without `[bot]`)."""
    return {
        "databaseId": 100 * number + 1,
        "author": {"login": login, "__typename": kind},
        "authorAssociation": "NONE",
        "createdAt": "2026-10-22T12:00:00Z",
        "body": render_card(card, RULES),
    }


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
    gh.cards = {
        1: [card_comment(1, held(1, task="one"))],
        2: [card_comment(2, held(2, status="pr-open", exempt=True))],
        3: [],  # assigned by hand: no card, no claim
        4: [card_comment(4, held(4, status="released"))],
        5: [card_comment(5, held(5), login="someone", kind="User")],  # a card typed by a person is not the bot's
    }
    found = sources(tmp_path, gh, clock).claimed()  # issue 4 was released
    assert [(c.issue, c.task, c.status, c.exempt) for c in found] == [
        (1, "one", "claimed", False),
        (2, None, "pr-open", True),
    ]
    (call,) = gh.calls
    assert call[:2] == ["api", "graphql"] and any("assignee:@me" in item for item in call)


def test_a_claim_paused_by_a_label_is_read_whatever_labels_the_issue_has(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    gh.cards = {7: [card_comment(7, held(7, waiting="S-on-hold"))]}
    (card,) = sources(tmp_path, gh, clock).claimed()
    assert (card.issue, card.waiting) == (7, "S-on-hold")
    assert counted_claims([card]) == ["issue-7"]


def test_an_issue_with_more_comments_than_one_query_holds_is_read_on_its_own(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    gh.cards = {1: [], 2: [card_comment(2, held(2))]}
    gh.long = {1}
    gh.answers["api repos/{owner}/{repo}/issues/1/comments"] = card_lines(1, held(1))
    assert sorted(c.issue for c in sources(tmp_path, gh, clock).claimed()) == [1, 2]
    assert gh.queries("search(") == 1 and gh.count("api repos/{owner}/{repo}/issues/1/comments") == 1
    assert gh.count("api repos/{owner}/{repo}/issues/2/") == 0


def test_more_assigned_issues_than_one_query_holds_is_an_error_not_a_cut_list(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    gh.next_page = True
    with pytest.raises(ConfigError, match="more than 100"):
        sources(tmp_path, gh, clock).claimed()


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


def issue_node(state: str, reason: str | None = None, *closing: tuple[bool, str]) -> dict[str, Any]:
    """An issue as the blocker query returns it: its state and the pull requests that close it (merged, repository)."""
    return {
        "__typename": "Issue",
        "state": state,
        "stateReason": reason,
        "closedByPullRequestsReferences": {
            "nodes": [{"merged": merged, "baseRepository": {"nameWithOwner": repo}} for merged, repo in closing]
        },
    }


def with_blockers(body_numbers: str) -> str:
    """An issue body whose `Blocked by` section names `body_numbers`."""
    return f"### {HEADING}\n\n{body_numbers}\n\n### Next\n\nx"


def test_a_blocker_is_finished_only_when_a_merged_pull_request_closed_it(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    from slot_fill_support import issue

    gh.states = {
        1: issue_node("OPEN"),
        2: issue_node("CLOSED", "COMPLETED", (True, "owner/repo")),
        3: issue_node("CLOSED", "NOT_PLANNED"),
        4: issue_node("CLOSED", "COMPLETED"),  # closed by hand
        5: issue_node("CLOSED", "COMPLETED", (False, "owner/repo")),  # its pull request was closed unmerged
        6: issue_node("CLOSED", "COMPLETED", (True, "other/repo")),  # a pull request of another repository
        7: {"__typename": "PullRequest", "merged": True},
        8: {"__typename": "PullRequest", "merged": False},
    }
    reader = sources(tmp_path, gh, clock)
    assert reader.blockers(issue(20, body=with_blockers("#1 #2 #3 #4 #5 #6 #7 #8"))) == [1, 3, 4, 5, 6, 8]
    assert reader.blockers(issue(21, body=with_blockers("#2 and #7"))) == []
    assert gh.queries("issueOrPullRequest") == 1  # the second issue's blockers were in the first query


def test_an_issue_without_the_section_has_no_blockers(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    from slot_fill_support import issue

    assert sources(tmp_path, gh, clock).blockers(issue(5, body="### Summary\n\n#4")) == []
    assert gh.calls == []


def test_the_blockers_of_every_ready_issue_are_read_in_one_query(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = [
        raw_issue(10, RULES.claimable_status, body=with_blockers("#1 #2")),
        raw_issue(11, RULES.claimable_status, body=with_blockers("#2 #3")),
        raw_issue(12, RULES.claimable_status, body=with_blockers("#4")),
    ]
    gh.states = {number: issue_node("OPEN") for number in (1, 2, 3, 4)}
    reader = sources(tmp_path, gh, clock)
    assert [reader.blockers(item) for item in reader.ready()] == [[1, 2], [2, 3], [4]]
    assert gh.queries("issueOrPullRequest") == 1


def test_a_number_github_does_not_know_spoils_only_the_issues_that_name_it(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    from slot_fill_support import issue

    gh.states = {1: issue_node("OPEN"), 99: None}
    reader = sources(tmp_path, gh, clock)
    assert reader.blockers(issue(5, body=with_blockers("#1"))) == [1]
    with pytest.raises(ConfigError, match="#99 was not found"):
        reader.blockers(issue(6, body=with_blockers("#1 #99")))
    assert reader.blockers(issue(7, body=with_blockers("#1"))) == [1]  # reading goes on
    assert gh.queries("issueOrPullRequest") == 2  # #99 was the second query's; the third issue needed none


def test_many_blockers_are_read_in_a_few_queries_not_one_each(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    from slot_fill_support import issue

    count = BLOCKERS_PER_QUERY * 2 + 1
    gh.states = {number: issue_node("OPEN") for number in range(1, count + 1)}
    numbers = " ".join(f"#{number}" for number in range(1, count + 1))
    assert len(sources(tmp_path, gh, clock).blockers(issue(500, body=with_blockers(numbers)))) == count
    assert gh.queries("issueOrPullRequest") == 3


def test_the_blocker_states_are_kept_with_the_other_answers(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    from slot_fill_support import issue

    gh.states = {1: issue_node("OPEN")}
    body = with_blockers("#1")
    sources(tmp_path, gh, clock).blockers(issue(5, body=body))
    assert sources(tmp_path, gh, clock).blockers(issue(5, body=body)) == [1]
    assert gh.queries("issueOrPullRequest") == 1
    gh.states[2] = issue_node("OPEN")  # a number the kept answer lacks is read again
    assert sources(tmp_path, gh, clock).blockers(issue(6, body=with_blockers("#1 #2"))) == [1, 2]
    assert gh.queries("issueOrPullRequest") == 2


def test_a_realistic_run_makes_a_handful_of_gh_calls(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    """20 assigned issues and 30 ready ones with two blockers each are read in five calls, whatever their number."""
    gh.answers["issue list"] = [
        raw_issue(number, RULES.claimable_status, body=with_blockers(f"#{number - 100} #{number + 100}"))
        for number in range(200, 230)
    ]
    gh.cards = {number: [card_comment(number, held(number))] for number in range(1, 21)}
    gh.states = {number: issue_node("OPEN") for number in range(100, 330) if number not in range(200, 230)}
    gh.answers["pr list"] = []
    gh.answers["api repos/{owner}/{repo}/rules"] = []
    reader = sources(tmp_path, gh, clock)
    assert len(reader.claimed()) == 20
    assert all(reader.blockers(item) for item in reader.ready())
    assert reader.failed_pulls() == []
    assert gh.queries("search(") == 1 and gh.queries("issueOrPullRequest") == 1
    assert len(gh.calls) == 5  # the ready list, the cards, the blockers, the pull requests and the rules
    again = len(gh.calls)
    assert len(sources(tmp_path, gh, clock).claimed()) == 20  # the next run, in the cache time, reads nothing
    assert len(gh.calls) == again


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
    gh.cards = {1: [card_comment(1, held(1))]}
    sources(tmp_path, gh, clock).ready()
    assert [c.issue for c in sources(tmp_path, gh, clock).claimed()] == [1]
    assert [c.issue for c in sources(tmp_path, gh, clock).claimed()] == [1]  # the cards come from the cache
    assert [i.number for i in sources(tmp_path, gh, clock).ready()] == [5]
    assert gh.count("issue list") == 1 and gh.count("api") == 1  # each read once


def test_a_task_file_newer_than_the_answer_drops_it(tmp_path: Path, gh: FakeGh, clock: Clock) -> None:
    gh.answers["issue list"] = [raw_issue(5, RULES.claimable_status)]
    sources(tmp_path, gh, clock).ready()
    newer = (START + timedelta(seconds=5)).timestamp()
    sources(tmp_path, gh, clock, changed=newer).ready()
    assert gh.count("issue list") == 2
    older = (START - timedelta(seconds=5)).timestamp()
    sources(tmp_path, gh, clock, changed=older).ready()
    assert gh.count("issue list") == 2


def test_a_task_file_newer_than_the_answers_drops_every_kind_of_answer(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    """The claim cards, the blockers' states and the pull requests are dropped as the ready issues are."""
    from slot_fill_support import issue

    gh.cards = {1: [card_comment(1, held(1))]}
    gh.states = {4: issue_node("OPEN")}
    gh.answers["pr list"] = []
    gh.answers["api repos/{owner}/{repo}/rules"] = []

    def read(changed: float) -> None:
        reader = sources(tmp_path, gh, clock, changed=changed)
        reader.claimed()
        reader.blockers(issue(5, body=with_blockers("#4")))
        reader.failed_pulls()

    read(0.0)
    calls = len(gh.calls)
    read((START - timedelta(seconds=5)).timestamp())  # nothing changed since the answers: all kept
    assert len(gh.calls) == calls
    read((START + timedelta(seconds=5)).timestamp())  # a claim made since: all read again
    assert len(gh.calls) == 2 * calls


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
    seconds = Seconds()
    reader = sources(tmp_path, gh, clock, seconds)
    reader.ready()
    seconds.now = CONVENTIONS.tasks.start.fill.budget_seconds + 1
    with pytest.raises(ConfigError) as spent:
        reader.claimed()
    assert gh.count("issue list") == 1 and len(gh.calls) == 1
    # It says the reads together took too long, and how many were made; it does not blame GitHub or gh.
    message = str(spent.value)
    assert "1 gh call made" in message and f"{CONVENTIONS.tasks.start.fill.budget_seconds} s" in message
    assert "together" in message and "GitHub took" not in message and "cannot read" not in message


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


def test_the_issues_a_waiver_names_are_closed_in_any_way_and_read_in_one_query(
    tmp_path: Path, gh: FakeGh, clock: Clock
) -> None:
    gh.states = {
        1: issue_node("OPEN"),
        2: issue_node("CLOSED", "COMPLETED"),
        3: issue_node("CLOSED", "NOT_PLANNED"),
        4: {"__typename": "PullRequest", "state": "MERGED"},
        5: {"__typename": "PullRequest", "state": "OPEN"},
        99: None,  # GitHub has no such number: not closed, so the waiver runs its hours
    }
    assert sources(tmp_path, gh, clock).closed([1, 2, 3, 4, 5, 99]) == {2, 3, 4}
    assert gh.queries("issueOrPullRequest") == 1

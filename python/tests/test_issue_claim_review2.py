# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the second code review of claiming: an open pull request suspends the release, runs that touch one issue
at the same time never lose a card write or post a second card, and the smaller notes."""

from collections.abc import Callable
from datetime import timedelta

import pytest
from audit_support import CONVENTIONS
from claim_support import RULES, Clock, FakeGitHub, make_engine, plant_claim

from coney_tools import conventions_render
from coney_tools import issue_claim_model as model
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_pass import Activity


@pytest.fixture
def clock() -> Clock:
    return Clock()


@pytest.fixture
def github(clock: Clock) -> FakeGitHub:
    fake = FakeGitHub(clock)
    for number in range(10, 18):
        fake.add_issue(number)
    return fake


@pytest.fixture
def engine(github: FakeGitHub, clock: Clock) -> Engine:
    return make_engine(github, clock)


def card_of(github: FakeGitHub, number: int) -> model.Card:
    """The newest card of an issue, parsed."""
    card = model.parse_card(github.cards(number)[-1].body, RULES)
    assert card is not None
    return card


def once_before(github: FakeGitHub, method: str, action: Callable[[], object]) -> None:
    """Run `action` the first time the engine calls `method` on the fake: another run's work landing mid-pass."""
    original = getattr(github, method)
    fired = {"done": False}

    def wrapped(*args: object) -> object:
        if not fired["done"]:
            fired["done"] = True
            action()
        return original(*args)

    setattr(github, method, wrapped)


# --- an open pull request of the claimant suspends the release -------------------------------------------------------


def test_an_open_pull_request_of_the_claimant_suspends_the_release(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    github.say(10, "alice", "/claim")
    engine.reconcile(10)
    github.add_pr(60)
    github.link_pr(10, 60, "open", head_date=clock.now(), author="alice")
    engine.reconcile(10)
    clock.advance(hours=24 * 4)  # a long review
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",) and github.others(10) == []
    assert "S-pending-review" in github.view(10).labels and "S-ready-to-start" not in github.view(10).labels
    card = card_of(github, 10)
    assert (card.status, card.waiting) == ("pr-open", "PR #60 open")
    assert "waits while PR #60 open" in github.cards(10)[-1].body
    # The pull request closes unmerged: the clock starts again from that moment.
    github.link_pr(10, 60, "closed", head_date=None, author="alice")
    engine.reconcile(10)
    assert card_of(github, 10).waiting is None and card_of(github, 10).activity == clock.now()
    clock.advance(hours=49)
    engine.reconcile(10)  # the warning
    clock.advance(hours=22)
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)
    clock.advance(hours=2)
    engine.reconcile(10)
    assert github.view(10).assignees == ()


def test_a_warning_given_before_the_pull_request_is_taken_back(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    github.say(10, "alice", "/claim")
    engine.reconcile(10)
    clock.advance(hours=50)
    engine.reconcile(10)
    assert len(github.others(10)) == 1
    github.add_pr(60)
    github.link_pr(10, 60, "open", head_date=clock.now() - timedelta(hours=60), author="alice")
    engine.reconcile(10)
    assert github.others(10) == [] and card_of(github, 10).warned_at is None


def test_a_paused_claim_writes_its_card_once_not_every_pass(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.say(10, "alice", "/claim")
    engine.reconcile(10)
    github.add_label(10, "S-blocked")
    engine.reconcile(10)
    before = list(github.writes)
    for _ in range(3):
        clock.advance(hours=1)
        engine.reconcile(10)
    assert github.writes == before
    assert card_of(github, 10).last_activity_why != "paused: S-blocked"


# --- runs that touch one issue at the same time -----------------------------------------------------------------------


def test_a_pull_request_run_does_not_lose_a_progress_written_meanwhile(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    github.say(10, "alice", "/claim")
    engine.reconcile(10)
    github.add_pr(60)
    github.link_pr(10, 60, "open", head_date=clock.now(), author="alice")
    clock.advance(minutes=1)
    comment_run = make_engine(github, clock)
    posted: list[int] = []

    def progress_lands() -> None:
        # After the pull request run read the issue: `/progress` is posted and its own run handles it.
        posted.append(github.say(10, "alice", "/progress studied"))
        comment_run.reconcile(10)

    once_before(github, "linked_prs", progress_lands)
    engine.reconcile(10, Activity(clock.now(), "alice"))

    card = card_of(github, 10)
    assert len(github.cards(10)) == 1
    assert (card.status, card.stage) == ("pr-open", "studied") and posted[0] in card.handled


def test_a_sweep_and_a_comment_run_on_one_claim_post_one_card(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    command = github.say(10, "alice", "/claim")
    comment_run = make_engine(github, clock)

    # The sweep has read the issue and checked the claim; the comment run finishes the claim before the sweep saves.
    once_before(github, "assign", lambda: comment_run.reconcile(10))
    engine.reconcile(10)

    assert len(github.cards(10)) == 1
    card = card_of(github, 10)
    assert (card.claimant, card.status) == ("alice", "claimed") and command in card.handled
    assert github.view(10).assignees == ("alice",)


def test_two_cards_of_one_claimant_are_merged_not_lost_to_themselves(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    plant_claim(github, 10, "alice")
    first = github.cards(10)[0].id
    clock.advance(seconds=5)
    plant_claim(github, 10, "alice")
    log = engine.reconcile(10)
    assert [comment.id for comment in github.cards(10)] == [first]
    assert card_of(github, 10).status == "claimed" and github.view(10).assignees == ("alice",)
    assert not any("lost" in line for line in log)


# --- the smaller notes ------------------------------------------------------------------------------------------------


def test_the_limit_notice_is_posted_only_after_a_real_release(
    engine: Engine, github: FakeGitHub, clock: Clock, monkeypatch: pytest.MonkeyPatch
) -> None:
    for number in (10, 11, 12):
        plant_claim(github, number, "alice")
        clock.advance(minutes=1)
    monkeypatch.setattr(engine, "_unchanged", lambda work, stored: False)
    engine._enforce_limits()
    assert github.replies(12) == [] and github.view(12).assignees == ("alice",)


def test_the_limit_notice_follows_the_release(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    for number in (10, 11, 12):
        plant_claim(github, number, "alice")
        clock.advance(minutes=1)
    engine._enforce_limits()
    assert github.view(12).assignees == () and "Claim released" in github.replies(12)[-1].body


@pytest.mark.parametrize(
    ("labels", "state", "words"),
    [
        (("task", "S-needs-triage"), "open", "S-ready-to-start"),
        (("task", "S-in-progress"), "closed", "is closed"),
    ],
)
def test_finishing_a_half_claim_still_runs_the_checks(
    github: FakeGitHub, clock: Clock, labels: tuple[str, ...], state: str, words: str
) -> None:
    engine = make_engine(github, clock, CONVENTIONS)
    github.add_issue(30, labels=labels, state=state, assignees=("alice",))
    github.say(30, "alice", "/claim")
    engine.reconcile(30)
    assert github.cards(30) == [] and words in github.replies(30)[-1].body
    assert github.view(30).labels == labels


def test_maintainer_permissions_read_as_either() -> None:
    text = "\n".join(conventions_render._claim_rules(CONVENTIONS))
    assert "`admin` or `write` permission" in text

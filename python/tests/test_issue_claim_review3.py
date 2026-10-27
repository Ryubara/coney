# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the third code review of claiming: a stale run does not undo a newer claim or a newer `/progress` (the
backstop behind the one-at-a-time workflow), twin cards merge everything, a run catches up the commands of runs GitHub
cancelled, and the smaller notes."""

import json
import re
from collections.abc import Callable
from datetime import timedelta

import pytest
import yaml
from audit_support import CONVENTIONS
from claim_support import RULES, Clock, FakeGitHub, make_engine, plant_claim
from conventions_support import REPO

from coney_tools import issue_claim_cli as cli
from coney_tools import issue_claim_model as model
from coney_tools.config import ConfigError
from coney_tools.issue_claim_engine import Engine


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


def comment_event(number: int, body: str = "/claim") -> dict[str, object]:
    """The payload of a comment on an issue."""
    return {"issue": {"number": number}, "comment": {"body": body}}


# --- a stale run does not undo newer work ----------------------------------------------------------------------------


def test_a_stale_unclaim_does_not_release_the_claim_made_after_it(github: FakeGitHub, clock: Clock) -> None:
    github.say(10, "alice", "/claim")
    make_engine(github, clock).reconcile(10)
    clock.advance(minutes=1)
    github.say(10, "alice", "/unclaim")
    stale = make_engine(github, clock)

    def meanwhile() -> None:
        # After the stale run read the issue: the comment run handles /unclaim and alice claims again.
        make_engine(github, clock).reconcile(10)
        clock.advance(seconds=10)
        github.say(10, "alice", "/claim")
        make_engine(github, clock).reconcile(10)

    once_before(github, "all_cards", meanwhile)
    stale.reconcile(10)
    assert github.view(10).assignees == ("alice",) and card_of(github, 10).active


def test_a_progress_after_the_checks_but_before_the_unassign_keeps_the_claim(github: FakeGitHub, clock: Clock) -> None:
    github.say(10, "alice", "/claim")
    make_engine(github, clock).reconcile(10)
    clock.advance(hours=73)
    stale = make_engine(github, clock)

    def meanwhile() -> None:
        github.say(10, "alice", "/progress studied")
        make_engine(github, clock).reconcile(10)

    # The idle release has passed every check; the /progress lands just before it unassigns.
    once_before(github, "unassign", meanwhile)
    stale.reconcile(10)
    card = card_of(github, 10)
    assert github.view(10).assignees == ("alice",) and "S-in-progress" in github.view(10).labels
    assert (card.status, card.stage, card.last_activity_why) == ("claimed", "studied", "progress")


def test_a_twin_card_that_got_a_progress_keeps_it_when_merged(github: FakeGitHub, clock: Clock) -> None:
    plant_claim(github, 10, "alice")
    clock.advance(seconds=5)
    plant_claim(github, 10, "alice")
    clock.advance(minutes=1)
    github.say(10, "alice", "/progress studied")
    make_engine(github, clock).reconcile(10)
    assert len(github.cards(10)) == 1
    card = card_of(github, 10)
    assert (card.stage, card.last_activity_why, card.activity) == ("studied", "progress", clock.now())


def test_a_twin_merge_keeps_the_scope_the_other_card_holds(github: FakeGitHub, clock: Clock) -> None:
    plant_claim(github, 10, "alice")
    clock.advance(seconds=5)
    github.say(10, "alice", "/claim scope: src/ai/")
    twin = model.Card(issue=10, claimant="alice", claimed_at=clock.now(), scope=("src/ai/",), stage="studied")
    github.create_comment(10, model.render_card(twin, RULES))
    make_engine(github, clock).reconcile(10)
    card = card_of(github, 10)
    assert len(github.cards(10)) == 1 and (card.scope, card.stage) == (("src/ai/",), "studied")


def test_retries_that_run_out_report_what_each_attempt_did(
    engine: Engine, github: FakeGitHub, clock: Clock, monkeypatch: pytest.MonkeyPatch
) -> None:
    github.say(10, "alice", "/claim")
    from coney_tools import issue_claim_engine

    def always_stale(work: object) -> None:
        raise issue_claim_engine._Stale("#10: the claim changed meanwhile")

    monkeypatch.setattr(engine, "_still_current", always_stale)
    lines, errors = cli.run_event(engine, "issue_comment", comment_event(10), CONVENTIONS, clock.now)
    assert len(errors) == 1 and "kept changing" in errors[0]
    assert sum("starting over" in line for line in lines) == issue_claim_engine.ATTEMPTS
    with pytest.raises(ConfigError):
        engine.reconcile(10)


# --- whose pull request makes the clock wait --------------------------------------------------------------------------


@pytest.mark.parametrize(("author", "draft"), [("alice", True), ("Ryubara", False)])
def test_only_the_claimants_pull_request_in_review_makes_the_clock_wait(
    engine: Engine, github: FakeGitHub, clock: Clock, author: str, draft: bool
) -> None:
    github.say(10, "alice", "/claim")
    engine.reconcile(10)
    github.add_pr(60)
    github.link_pr(10, 60, "open", head_date=clock.now(), author=author, draft=draft)
    clock.advance(hours=50)
    engine.reconcile(10)
    assert card_of(github, 10).waiting is None and len(github.others(10)) == 1  # warned: the clock runs


# --- who a comment mentions -------------------------------------------------------------------------------------------


def test_a_reply_mentions_the_commands_author_and_names_others_without_an_at_sign(
    engine: Engine, github: FakeGitHub
) -> None:
    github.say(10, "alice", "/claim")
    engine.reconcile(10)
    github.say(10, "bob", "/claim")
    engine.reconcile(10)
    text = github.replies(10)[-1].body
    assert "@bob" in text and "@alice" not in text and "alice" in text


# --- a run catches up what a cancelled run left -----------------------------------------------------------------------


def test_a_run_also_handles_a_recent_command_of_another_issue(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.say(12, "bob", "/claim")  # its own run was cancelled while pending
    clock.advance(minutes=2)
    github.say(11, "alice", "/claim")
    _, errors = cli.run_event(engine, "issue_comment", comment_event(11), CONVENTIONS, clock.now)
    assert errors == []
    assert card_of(github, 11).claimant == "alice" and card_of(github, 12).claimant == "bob"


def test_an_older_command_is_left_to_the_sweep(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.say(12, "bob", "/claim")
    clock.advance(minutes=RULES.catch_up_minutes + 1)
    github.say(11, "alice", "/claim")
    cli.run_event(engine, "issue_comment", comment_event(11), CONVENTIONS, clock.now)
    assert github.cards(12) == []
    engine.sweep()
    assert card_of(github, 12).claimant == "bob"


# --- the workflow: one run at a time ----------------------------------------------------------------------------------


def workflow() -> dict[object, object]:
    data = yaml.safe_load((REPO / ".github/workflows/claim.yml").read_text(encoding="utf-8"))
    assert isinstance(data, dict)
    return data


def test_every_claim_run_queues_in_one_repository_wide_group() -> None:
    jobs = workflow()["jobs"]
    assert isinstance(jobs, dict)
    concurrency = jobs["claim"]["concurrency"]
    assert concurrency["cancel-in-progress"] is False
    group = concurrency["group"]
    assert "'claim'" in group and "issue.number" not in group and "pull_request.number" not in group
    assert "github.run_id" in group  # a skipped event never takes the pending place


def test_only_a_paused_label_starts_a_run_and_the_list_is_the_conventions() -> None:
    jobs = workflow()["jobs"]
    assert isinstance(jobs, dict)
    job = jobs["claim"]
    for text in (job["if"], job["concurrency"]["group"]):
        found = re.search(r"fromJSON\('(\[.*?\])'\)", text)
        assert found is not None and tuple(json.loads(found.group(1))) == (*RULES.paused_labels, RULES.exempt_label)
        assert "github.event.label.name" in text


def test_the_catch_up_window_is_shorter_than_the_sweep_lookback() -> None:
    assert 0 < RULES.catch_up_minutes < RULES.command_lookback_hours * 60
    assert timedelta(minutes=RULES.catch_up_minutes) >= timedelta(minutes=10)


def test_the_rules_block_states_the_queue_and_the_catch_up_window() -> None:
    from coney_tools import conventions_render

    text = " ".join(conventions_render._claim_rules(CONVENTIONS))
    assert "one at a time" in text and f"last {RULES.catch_up_minutes} minutes" in text


# --- review 4: a second release of the same claim, catch-up failures, dispatch refs, one read of the pull requests ----


@pytest.mark.parametrize("adopt", [False, True])
def test_two_releases_of_one_claim_do_not_bring_it_back(github: FakeGitHub, clock: Clock, adopt: bool) -> None:
    from dataclasses import replace

    def engine_with_adopt() -> Engine:
        made = make_engine(github, clock)
        made.rules = replace(made.rules, adopt=adopt)
        return made

    github.say(10, "alice", "/claim")
    engine_with_adopt().reconcile(10)
    clock.advance(hours=73)
    stale = engine_with_adopt()

    # A run outside the queue (`claim run` by hand) releases the idle claim just before this one unassigns.
    once_before(github, "unassign", lambda: engine_with_adopt().reconcile(10))
    stale.reconcile(10)
    view = github.view(10)
    assert view.assignees == () and "S-in-progress" not in view.labels and "S-ready-to-start" in view.labels
    assert all(not card.active for card in (model.parse_card(c.body, RULES) for c in github.cards(10)) if card)


def test_a_release_in_flight_does_not_strand_a_reclaim_made_at_the_unassign(github: FakeGitHub, clock: Clock) -> None:
    github.say(10, "alice", "/claim")
    make_engine(github, clock).reconcile(10)
    clock.advance(hours=73)
    stale = make_engine(github, clock)

    def meanwhile() -> None:
        # A run outside the queue releases the idle claim and alice claims again (a new card, a new comment id), all
        # after the stale pass passed its checks and before it unassigns.
        make_engine(github, clock).reconcile(10)
        clock.advance(seconds=10)
        github.say(10, "alice", "/claim")
        make_engine(github, clock).reconcile(10)

    once_before(github, "unassign", meanwhile)
    stale.reconcile(10)
    view = github.view(10)
    assert view.assignees == ("alice",) and "S-in-progress" in view.labels and "S-ready-to-start" not in view.labels
    cards = [model.parse_card(c.body, RULES) for c in github.cards(10)]
    held = [card for card in cards if card and card.active]
    assert [card.claimant for card in held] == ["alice"]
    assert not any(card and card.released_why == "unassigned by hand" for card in cards)


def test_a_catch_up_that_cannot_read_github_is_an_error_not_a_crash(
    engine: Engine, github: FakeGitHub, clock: Clock, monkeypatch: pytest.MonkeyPatch
) -> None:
    github.say(11, "alice", "/claim")

    def broken(since: object) -> list[object]:
        raise ConfigError("cannot read from GitHub with gh (rate limited)")

    monkeypatch.setattr(github, "recent_commands", broken)
    lines, errors = cli.run_event(engine, "issue_comment", comment_event(11), CONVENTIONS, clock.now)
    assert card_of(github, 11).claimant == "alice" and any("claimed it" in line for line in lines)
    assert len(errors) == 1 and "catch-up" in errors[0] and "rate limited" in errors[0]


def test_a_dispatch_runs_only_from_the_default_branch() -> None:
    jobs = workflow()["jobs"]
    assert isinstance(jobs, dict)
    condition = jobs["claim"]["if"]
    assert "github.event_name != 'workflow_dispatch'" in condition
    assert "github.ref == format('refs/heads/{0}', github.event.repository.default_branch)" in condition
    group = jobs["claim"]["concurrency"]["group"]
    assert "github.ref != format('refs/heads/{0}', github.event.repository.default_branch)" in group


def test_a_pass_reads_the_linked_pull_requests_once(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.say(10, "alice", "/claim")
    engine.reconcile(10)
    github.add_pr(60)
    github.link_pr(10, 60, "open", head_date=clock.now(), author="alice")
    calls: list[int] = []
    original = github.linked_prs

    def counted(number: int):  # type: ignore[no-untyped-def]
        calls.append(number)
        return original(number)

    github.linked_prs = counted  # type: ignore[method-assign]
    engine.reconcile(10)
    assert calls == [10]

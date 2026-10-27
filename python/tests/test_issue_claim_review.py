# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the code review's fixes to claiming: the task cap, races between runs, pull request activity, a claim that
stops half way, the adopt and release switches, a release that must not undo a newer change, and the smaller notes."""

from dataclasses import replace
from datetime import timedelta

import pytest
from audit_support import CONVENTIONS
from claim_support import RULES, START, Clock, FakeGitHub, idle_past_deadline, make_engine, plant_claim

from coney_tools import issue_claim_cli as cli
from coney_tools import issue_claim_model as model
from coney_tools.config import ConfigError
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_model import Command, Rejected
from coney_tools.issue_claim_pass import Activity
from coney_tools.issue_claim_rules import check_claim, is_maintainer, overrun


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


def replies(github: FakeGitHub, number: int) -> str:
    """The text of every refusal reply of an issue."""
    return "\n".join(reply.body for reply in github.replies(number))


def say(github: FakeGitHub, number: int, login: str = "alice", text: str = "/claim") -> int:
    """Someone comments a command."""
    return github.say(number, login, text)


# --- the task id is no way round the limit ----------------------------------------------------------------------------


def test_a_task_holds_at_most_three_issues(engine: Engine, github: FakeGitHub) -> None:
    for number in (10, 11, 12, 13):
        say(github, number, "Ryubara", "/claim task: big")
        engine.reconcile(number)
    assert [n for n in (10, 11, 12, 13) if github.view(n).assignees] == [10, 11, 12]
    assert "task `big` already holds 3 of your issues (3 at most)" in replies(github, 13)


def test_many_issues_without_a_task_are_still_held_to_the_limit(engine: Engine, github: FakeGitHub) -> None:
    for number in (10, 11, 12, 13):
        say(github, number, "Ryubara", f"/claim task: t{number}")
        engine.reconcile(number)
    assert [n for n in (10, 11, 12, 13) if github.view(n).assignees] == [10, 11, 12]
    assert "3 open claim(s)" in replies(github, 13)


def test_the_task_of_a_claim_cannot_change(engine: Engine, github: FakeGitHub) -> None:
    say(github, 10, "alice", "/claim task: one")
    say(github, 11, "alice")
    engine.reconcile(10)
    engine.reconcile(11)
    say(github, 10, "alice", "/claim task: two")
    say(github, 11, "alice", "/claim task: two")
    engine.reconcile(10)
    engine.reconcile(11)
    assert "cannot change (it is `one`)" in replies(github, 10)
    assert "cannot change (it has none)" in replies(github, 11)
    assert card_of(github, 10).task == "one" and card_of(github, 11).task is None
    say(github, 10, "alice", "/claim scope: src/ai/ task: one")
    engine.reconcile(10)
    assert card_of(github, 10).scope == ("src/ai/",)


def test_the_cap_is_a_rule_check_too(github: FakeGitHub) -> None:
    held = [model.Card(issue=n, claimant="alice", claimed_at=START, task="t") for n in (1, 2, 3)]
    refusal = check_claim(Command("claim", task="t"), github.view(10), "alice", True, None, held, github, RULES, None)
    assert refusal is not None and "3 at most" in refusal.reason
    other = check_claim(Command("claim", task="t"), github.view(10), "bob", True, None, held, github, RULES, None)
    assert other is None


# --- parallel runs: recount after assigning ---------------------------------------------------------------------------


def after_assign(github: FakeGitHub, action: object) -> None:
    """Run `action` right after the next `assign`: another run landing at the same time."""
    original = github.assign

    def assign(number: int, login: str) -> tuple[str, ...]:
        result = original(number, login)
        github.assign = original  # type: ignore[method-assign]
        action()  # type: ignore[operator]
        return result

    github.assign = assign  # type: ignore[method-assign]


def test_a_claim_over_the_limit_after_a_parallel_run_is_released_with_a_reply(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    plant_claim(github, 10, "alice", START - timedelta(hours=2))
    plant_claim(github, 11, "alice", START - timedelta(hours=1))
    # The check passed on stale data: a third claim is on its way in while this run assigns.
    github.items[10].view = replace(github.view(10), assignees=(), labels=("task", "S-ready-to-start"))
    github.comment_list[10] = [c for c in github.comment_list[10] if c.login != RULES.bot_login]
    after_assign(github, lambda: plant_claim(github, 10, "alice", START - timedelta(hours=2)))
    say(github, 12, "alice")
    clock.advance(minutes=1)
    engine.reconcile(12)
    assert github.view(12).assignees == () and "S-ready-to-start" in github.view(12).labels
    card = card_of(github, 12)
    assert card.status == "released" and "more than 2 open claims" in (card.released_why or "")
    assert "your claim was released again" in replies(github, 12)
    assert github.view(10).assignees == ("alice",) and github.view(11).assignees == ("alice",)


def test_the_earlier_claim_of_a_parallel_pair_is_kept(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    plant_claim(github, 10, "alice", START - timedelta(hours=2))
    after_assign(github, lambda: plant_claim(github, 11, "alice", START + timedelta(hours=1)))
    say(github, 12, "alice")
    engine.reconcile(12)
    assert github.view(12).assignees == ("alice",) and card_of(github, 12).status == "claimed"


def test_a_p_critical_issue_that_appeared_meanwhile_releases_the_claim(engine: Engine, github: FakeGitHub) -> None:
    def appears() -> None:
        github.add_issue(20, labels=("bug", "P-critical", "S-ready-to-start"))

    after_assign(github, appears)
    say(github, 10, "alice")
    engine.reconcile(10)
    assert github.view(10).assignees == () and "`P-critical` comes first: #20" in replies(github, 10)


def test_the_sweep_releases_the_claims_that_are_over_the_limit(engine: Engine, github: FakeGitHub) -> None:
    for offset, number in enumerate((10, 11, 12)):
        plant_claim(github, number, "alice", START - timedelta(hours=3 - offset))
    result = engine.sweep()
    assert [n for n in (10, 11, 12) if github.view(n).assignees] == [10, 11]
    assert card_of(github, 12).status == "released" and "released" in " ".join(result.lines)
    assert "Claim released" in replies(github, 12)
    # A second sweep finds nothing more to do.
    assert not [line for line in engine.sweep().lines if "released" in line]


def test_the_sweep_leaves_an_adopted_claim_alone(engine: Engine, github: FakeGitHub) -> None:
    for number in (10, 11):
        plant_claim(github, number, "alice", START - timedelta(hours=4))
    github.assign(12, "alice")
    engine.sweep()
    assert github.view(12).assignees == ("alice",) and card_of(github, 12).adopted


def test_overrun_orders_by_the_first_claim_of_each_task() -> None:
    cards = [
        model.Card(issue=1, claimant="a", claimed_at=START, task="x"),
        model.Card(issue=2, claimant="a", claimed_at=START + timedelta(hours=1)),
        model.Card(issue=3, claimant="a", claimed_at=START + timedelta(hours=2)),
    ]
    assert overrun(cards[0], cards, False, RULES) is None
    assert overrun(cards[1], cards, False, RULES) is None
    assert overrun(cards[2], cards, False, RULES) is not None
    assert overrun(cards[2], cards, True, RULES) is None


# --- pull requests: activity and whose they are -----------------------------------------------------------------------


@pytest.mark.parametrize(
    ("action", "counts"),
    [("opened", True), ("reopened", True), ("ready_for_review", True), ("labeled", False), ("edited", False)],
)
def test_a_pull_request_event_is_activity_only_when_it_shows_work(
    engine: Engine, github: FakeGitHub, clock: Clock, action: str, counts: bool
) -> None:
    say(github, 10)
    engine.reconcile(10)
    clock.advance(hours=50)
    github.add_pr(60)
    github.link_pr(10, 60, "open", author="alice")
    payload = {"action": action, "pull_request": {"body": "Fixes #10", "user": {"login": "alice"}}}
    cli.run_event(engine, "pull_request_target", payload, CONVENTIONS, clock.now)
    assert (card_of(github, 10).last_activity_why == "pull request") is counts
    # Either way the open pull request of the claimant suspends the clock: no warning.
    assert github.others(10) == [] and card_of(github, 10).waiting == "PR #60 open"


def test_someone_elses_pull_request_is_not_the_claims(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    say(github, 10)
    engine.reconcile(10)
    clock.advance(hours=50)
    github.add_pr(60)
    github.link_pr(10, 60, "open", head_date=clock.now(), author="mallory")
    engine.reconcile(10, Activity(clock.now(), "mallory"))
    card = card_of(github, 10)
    assert (card.status, card.pr) == ("claimed", None)
    assert "S-in-progress" in github.view(10).labels and "S-pending-review" not in github.view(10).labels
    assert len(github.others(10)) == 1  # still idle: warned


def test_a_maintainers_pull_request_counts(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    say(github, 10)
    engine.reconcile(10)
    github.add_pr(60)
    github.link_pr(10, 60, "open", author="Ryubara")
    engine.reconcile(10)
    assert card_of(github, 10).status == "pr-open"


def test_at_most_ten_closing_lines_are_acted_on() -> None:
    body = "\n".join(f"Fixes #{n}" for n in range(1, 13))
    numbers, ignored = cli.closing_numbers(body, CONVENTIONS)
    assert numbers == list(range(1, 11)) and ignored == 2
    clock = Clock()
    github = FakeGitHub(clock)
    for n in range(1, 13):
        github.add_issue(n)
    lines, errors = cli.run_event(
        make_engine(github, clock), "pull_request_target", {"pull_request": {"body": body}}, CONVENTIONS, clock.now
    )
    assert errors == [] and any("ignored 2 more" in line for line in lines)


# --- a claim that stops half way --------------------------------------------------------------------------------------


def test_a_claim_that_failed_after_assigning_is_finished_by_the_next_pass(engine: Engine, github: FakeGitHub) -> None:
    command = say(github, 10, "alice", "/claim scope: src/ai/ task: half")
    github.fail_add_label = 1
    with pytest.raises(ConfigError):
        engine.reconcile(10)
    assert github.view(10).assignees == ("alice",) and not github.cards(10)
    log = engine.reconcile(10)
    card = card_of(github, 10)
    assert (card.claimant, card.scope, card.task) == ("alice", ("src/ai/",), "half") and command in card.handled
    assert "S-in-progress" in github.view(10).labels and replies(github, 10) == ""
    assert any("stopped half way" in line for line in log)


# --- adopt and release are off until the cutover ----------------------------------------------------------------------


def test_nothing_is_adopted_or_released_before_the_cutover(github: FakeGitHub, clock: Clock) -> None:
    engine = make_engine(github, clock, CONVENTIONS)
    github.assign(10, "carol")
    say(github, 11)
    engine.reconcile(10)
    engine.reconcile(11)
    assert not github.cards(10) and github.view(10).assignees == ("carol",)
    clock.advance(hours=200)
    engine.reconcile(11)
    assert github.view(11).assignees == ("alice",) and github.others(11) == []
    assert "not enforced yet" in github.cards(11)[-1].body and "relative-time" not in github.cards(11)[-1].body


def test_an_in_progress_issue_without_an_assignee_is_reported_not_stuck(github: FakeGitHub, clock: Clock) -> None:
    engine = make_engine(github, clock, CONVENTIONS)
    github.add_issue(82, labels=("task", "A-ai", "S-in-progress"))
    result = engine.sweep()
    assert engine.orphans == {82}
    assert any(line.startswith("orphan: #82") for line in result.lines)
    assert github.view(82).labels == ("task", "A-ai", "S-in-progress")  # reported, left alone


def test_an_adopted_issue_keeps_its_blocked_label(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(30, labels=("task", "S-blocked"), assignees=("carol",))
    engine.reconcile(30)
    assert "S-blocked" in github.view(30).labels and "S-in-progress" not in github.view(30).labels
    assert card_of(github, 30).adopted


# --- a paused claim ---------------------------------------------------------------------------------------------------


def test_a_blocked_claim_keeps_its_label_and_its_clock_waits(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    say(github, 10)
    engine.reconcile(10)
    github.add_label(10, "S-blocked")
    clock.advance(hours=100)
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",) and github.others(10) == []
    assert "S-blocked" in github.view(10).labels and "S-in-progress" in github.view(10).labels
    assert "waits while S-blocked" in github.cards(10)[-1].body
    github.remove_label(10, "S-blocked")
    engine.reconcile(10)  # the `unlabeled` event: the clock starts again now
    assert card_of(github, 10).last_activity_why == "S-blocked ended"
    clock.advance(hours=49)
    engine.reconcile(10)  # the warning
    clock.advance(hours=22)
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)
    clock.advance(hours=2)
    engine.reconcile(10)
    assert github.view(10).assignees == ()


def test_a_held_claim_keeps_s_on_hold_through_sweeps_and_is_not_released(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    say(github, 10)
    engine.reconcile(10)
    github.add_label(10, "S-on-hold")
    # Past the 72 h deadline, with a sweep every hour as the schedule runs it: the hold stays, the claim keeps, and
    # the hold replaces S-in-progress (a hold label, unlike S-blocked), so the issue has one status label.
    for _ in range(100):
        clock.advance(hours=1)
        engine.sweep()
    assert github.view(10).assignees == ("alice",) and github.others(10) == []
    assert [label for label in github.view(10).labels if label.startswith("S-")] == ["S-on-hold"]
    assert "waits while S-on-hold" in github.cards(10)[-1].body
    # Taking the hold off starts the clock again from that moment, like S-blocked, and the claim's label returns.
    github.remove_label(10, "S-on-hold")
    engine.reconcile(10)
    assert card_of(github, 10).last_activity_why == "S-on-hold ended"
    assert [label for label in github.view(10).labels if label.startswith("S-")] == ["S-in-progress"]


def labels_of(github: FakeGitHub, number: int = 10) -> list[str]:
    """The status labels of an issue, sorted."""
    return sorted(label for label in github.view(number).labels if label.startswith("S-"))


def test_a_held_claim_with_an_open_pull_request_loses_pending_review_and_gets_it_back(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    say(github, 10)
    engine.reconcile(10)
    github.add_pr(60)
    github.link_pr(10, 60, "open", head_date=clock.now(), author="alice")
    engine.reconcile(10)
    assert labels_of(github) == ["S-pending-review"] and card_of(github, 10).status == "pr-open"
    # The hold takes the claim's own label off, even the one the pull request set; the claim and the pull request stay.
    github.add_label(10, "S-on-hold")
    clock.advance(hours=100)
    engine.reconcile(10)
    engine.sweep()
    assert labels_of(github) == ["S-on-hold"]
    assert github.view(10).assignees == ("alice",) and card_of(github, 10).status == "pr-open"
    # Lifting the hold while the pull request is open brings back the label for the pull request, not S-in-progress.
    github.remove_label(10, "S-on-hold")
    engine.reconcile(10)
    assert labels_of(github) == ["S-pending-review"]


def test_a_hold_and_a_block_on_one_claim_both_stay_and_the_claim_label_waits_for_both_to_go(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    say(github, 10)
    engine.reconcile(10)
    github.add_label(10, "S-blocked")
    github.add_label(10, "S-on-hold")
    clock.advance(hours=100)
    for _ in range(3):
        engine.reconcile(10)
    # Both paused labels stay (a maintainer put each there), the hold takes S-in-progress off and the claim keeps.
    assert labels_of(github) == ["S-blocked", "S-on-hold"]
    assert github.view(10).assignees == ("alice",) and github.others(10) == []
    assert "waits while" in github.cards(10)[-1].body
    # Lifting only the hold leaves the block: the claim's label comes back when the last paused label is gone.
    github.remove_label(10, "S-on-hold")
    engine.reconcile(10)
    assert labels_of(github) == ["S-blocked"] and github.view(10).assignees == ("alice",)
    github.remove_label(10, "S-blocked")
    engine.reconcile(10)
    assert labels_of(github) == ["S-in-progress"]


def count_comment_reads(github: FakeGitHub, number: int) -> list[int | str]:
    """Make `github.comments` record each call (the list it returns grows as the engine reads comments)."""
    original = github.comments
    calls: list[int | str] = []

    def comments(n: int):  # type: ignore[no-untyped-def]
        calls.append(n)
        return original(n)

    github.comments = comments  # type: ignore[method-assign,assignment]
    return calls


@pytest.mark.parametrize("with_pull_request", [False, True])
def test_a_held_claim_costs_no_comments_request_beyond_the_one_that_starts_the_pass(
    engine: Engine, github: FakeGitHub, clock: Clock, with_pull_request: bool
) -> None:
    say(github, 10)
    engine.reconcile(10)
    if with_pull_request:
        github.add_pr(60)
        github.link_pr(10, 60, "open", head_date=clock.now(), author="alice")
    github.add_label(10, "S-on-hold")
    engine.reconcile(10)  # the pass that takes the claim's label off and writes the card's waiting note
    reads = count_comment_reads(github, 10)
    clock.advance(hours=1)
    engine.reconcile(10)
    # Once the hold has taken the claim's label off, the wanted label stays missing but nothing is written for it, so
    # there is no write to guard: the pass reads the comments once, to see the cards, and not again for a skipped label.
    assert reads == [10]
    reads.clear()
    engine.sweep()
    assert reads == [10]


def test_a_held_claim_that_still_has_its_label_is_guarded_before_the_label_is_taken_off(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    # The pass after the hold writes (it removes S-in-progress), so the stale-read guard runs before that write. A card
    # edited just before the second comments read (the guard's) stops that pass before it writes anything; the label
    # goes only in the pass that starts over, after a third read sees the edit. Unguarded, it went right after read 1.
    say(github, 10)
    engine.reconcile(10)
    github.add_label(10, "S-on-hold")
    newer = card_of(github, 10).changed(last_activity=clock.now(), last_activity_why="progress")
    comment_id = github.cards(10)[-1].id
    inject_on_read(github, 10, 2, lambda: github.edit_comment(comment_id, model.render_card(newer, RULES)))
    events = count_comment_reads(github, 10)
    remove_label = github.remove_label

    # Each label removal goes into the same list as the comment reads, so the test sees their order.
    def record_removal(number: int, label: str) -> None:
        events.append(f"remove {label}")
        remove_label(number, label)

    github.remove_label = record_removal  # type: ignore[method-assign]
    engine.reconcile(10)
    removed = events.index("remove S-in-progress")
    assert events[:removed].count(10) >= 3
    assert labels_of(github) == ["S-on-hold"] and card_of(github, 10).last_activity_why == "progress"


def test_a_hold_lifted_after_unclaim_leaves_the_status_to_the_coordinator(engine: Engine, github: FakeGitHub) -> None:
    say(github, 10)
    engine.reconcile(10)
    github.add_label(10, "S-on-hold")
    engine.reconcile(10)
    say(github, 10, "alice", "/unclaim")
    engine.reconcile(10)
    assert github.view(10).assignees == ()
    assert [label for label in github.view(10).labels if label.startswith("S-")] == ["S-on-hold"]
    # No claim stands, so lifting the hold puts no S-in-progress back: the coordinator sets the next status.
    github.remove_label(10, "S-on-hold")
    engine.reconcile(10)
    assert [label for label in github.view(10).labels if label.startswith("S-")] == []


def test_otherwise_a_claimed_issue_has_exactly_one_status_label(engine: Engine, github: FakeGitHub) -> None:
    say(github, 10)
    engine.reconcile(10)
    github.add_label(10, "S-needs-triage")
    engine.reconcile(10)
    assert [label for label in github.view(10).labels if label.startswith("S-")] == ["S-in-progress"]


# --- a release does not undo a newer change ---------------------------------------------------------------------------


def inject_on_read(github: FakeGitHub, number: int, nth: int, action: object) -> None:
    """Run `action` just before the `nth` read of an issue's comments: a change that arrives mid-pass."""
    original = github.comments
    count = {"n": 0}

    def comments(n: int):  # type: ignore[no-untyped-def]
        if n == number:
            count["n"] += 1
            if count["n"] == nth:
                action()  # type: ignore[operator]
        return original(n)

    github.comments = comments  # type: ignore[method-assign,assignment]


def test_a_renew_that_arrives_while_releasing_wins(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    say(github, 10)
    engine.reconcile(10)
    idle_past_deadline(github, clock, 10)
    inject_on_read(github, 10, 2, lambda: say(github, 10, "alice", "/claim renew"))
    log = engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)
    assert any("changed while it was being released" in line for line in log)
    engine.reconcile(10)
    assert card_of(github, 10).last_activity_why == "renew" and github.view(10).assignees == ("alice",)


def test_a_card_edited_while_releasing_is_left_alone(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    say(github, 10)
    engine.reconcile(10)
    idle_past_deadline(github, clock, 10)
    card = card_of(github, 10)
    newer = card.changed(last_activity=clock.now(), last_activity_why="progress")
    comment_id = github.cards(10)[-1].id
    inject_on_read(github, 10, 2, lambda: github.edit_comment(comment_id, model.render_card(newer, RULES)))
    log = engine.reconcile(10)
    assert github.view(10).assignees == ("alice",) and any("left alone" in line for line in log)


# --- commands: what is deleted and what is said -----------------------------------------------------------------------


def test_a_malformed_command_is_kept_with_its_reply_and_a_refused_real_one_is_deleted(
    engine: Engine, github: FakeGitHub
) -> None:
    github.add_issue(40, labels=("task", "S-blocked"))
    bad = say(github, 10, "alice", "/claim please")
    refused = say(github, 40, "alice")
    engine.reconcile(10)
    engine.reconcile(40)
    assert bad in {c.id for c in github.comments(10)} and "not a command" in replies(github, 10)
    assert refused not in {c.id for c in github.comments(40)} and replies(github, 40)
    engine.reconcile(10)
    assert len(github.replies(10)) == 1


def test_a_refusal_names_another_person_without_an_at_sign(engine: Engine, github: FakeGitHub) -> None:
    say(github, 10, "alice")
    engine.reconcile(10)
    say(github, 10, "bob")
    say(github, 10, "bob", "/unclaim")
    engine.reconcile(10)
    text = replies(github, 10)
    assert "claimed by alice" in text and "@alice" not in text


def test_a_scope_that_is_too_long_has_its_own_refusal() -> None:
    long = "/claim scope: " + ", ".join(f"src/dir{n}/" for n in range(80))
    result = model.parse_command(long, RULES)
    assert isinstance(result, Rejected) and "characters; 400 at most" in result.reason
    fits = ", ".join(["a" * 100] * 3)
    assert model.parse_command("/claim scope: " + fits, RULES) == Command("claim", scope=("a" * 100,))


# --- who is a maintainer ----------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("permission", "association", "expected"),
    [
        ("admin", "NONE", True),
        ("write", "COLLABORATOR", True),
        ("read", "COLLABORATOR", False),
        ("none", "OWNER", False),
        (None, "OWNER", True),
        (None, "COLLABORATOR", False),
        (None, "MEMBER", False),
    ],
)
def test_a_maintainer_is_decided_by_the_repository_permission(
    permission: str | None, association: str, expected: bool
) -> None:
    assert is_maintainer(permission, association, RULES) is expected


def test_the_limit_follows_the_permission_not_the_association(engine: Engine, github: FakeGitHub) -> None:
    github.permissions["bob"] = "read"
    for number in (10, 11, 12):
        github.say(number, "bob", "/claim", "COLLABORATOR")
        engine.reconcile(number)
    assert [n for n in (10, 11, 12) if github.view(n).assignees] == [10, 11]
    github.permissions["carol"] = "write"
    for number in (13, 14, 15):
        github.say(number, "carol", "/claim", "NONE")
        engine.reconcile(number)
    assert all(github.view(n).assignees == ("carol",) for n in (13, 14, 15))


def test_only_a_claimable_p_critical_issue_comes_first(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(20, labels=("bug", "P-critical", "S-blocked"))
    github.add_issue(21, labels=("bug", "P-critical", "S-ready-to-start"), assignees=("carol",))
    say(github, 10)
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)
    github.add_issue(22, labels=("bug", "P-critical", "S-ready-to-start"))
    say(github, 11)
    engine.reconcile(11)
    assert github.view(11).assignees == () and "#22" in replies(github, 11) and "#20" not in replies(github, 11)

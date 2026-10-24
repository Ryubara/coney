# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.issue_claim_engine and issue_claim_rules over an in-memory GitHub (claim_support)."""

from datetime import timedelta

import pytest
from claim_support import BOT, RULES, START, Clock, FakeGitHub, make_engine

from coney_tools import issue_claim_model as model
from coney_tools.issue_claim_engine import Activity, Engine
from coney_tools.issue_claim_remote import Blocker


@pytest.fixture
def clock() -> Clock:
    return Clock()


@pytest.fixture
def github(clock: Clock) -> FakeGitHub:
    fake = FakeGitHub(clock)
    fake.add_issue(10)
    return fake


@pytest.fixture
def engine(github: FakeGitHub, clock: Clock) -> Engine:
    return make_engine(github, clock)


def card_of(github: FakeGitHub, number: int) -> model.Card:
    """The newest card of an issue, parsed."""
    card = model.parse_card(github.cards(number)[-1].body, RULES)
    assert card is not None
    return card


def reasons(github: FakeGitHub, number: int) -> str:
    """The text of every refusal reply of an issue."""
    return "\n".join(reply.body for reply in github.replies(number))


def claim(
    github: FakeGitHub, number: int, login: str = "alice", text: str = "/claim", association: str = "NONE"
) -> int:
    """Someone comments a command."""
    return github.say(number, login, text, association)


# --- a claim ---------------------------------------------------------------------------------------------------------


def test_a_claim_assigns_labels_posts_the_card_and_deletes_the_command(engine: Engine, github: FakeGitHub) -> None:
    command = claim(github, 10, text="/claim scope: src/ai/, 0x00322b78 task: 10-fight")
    log = engine.reconcile(10)
    view = github.view(10)
    assert view.assignees == ("alice",)
    assert "S-in-progress" in view.labels and "S-ready-to-start" not in view.labels
    card = card_of(github, 10)
    assert (card.claimant, card.status, card.scope, card.task) == (
        "alice",
        "claimed",
        ("src/ai/", "0x00322b78"),
        "10-fight",
    )
    assert card.claimed_at == START and command in card.handled
    assert command not in {c.id for c in github.comments(10)}
    assert any("claimed it" in line for line in log)


def test_a_second_pass_changes_nothing(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10)
    engine.reconcile(10)
    github.writes.clear()
    assert engine.reconcile(10) == []
    assert github.writes == []


@pytest.mark.parametrize(
    ("labels", "words"),
    [
        (("task", "S-needs-triage"), "`S-ready-to-start` can be claimed"),
        (("task", "S-blocked"), "`S-ready-to-start` can be claimed"),
        (("task", "S-needs-info"), "`S-ready-to-start` can be claimed"),
        (("task", "S-on-hold"), "`S-ready-to-start` can be claimed"),
        (("task",), "no status label"),
    ],
)
def test_only_a_ready_issue_can_be_claimed_by_anyone(
    engine: Engine, github: FakeGitHub, labels: tuple[str, ...], words: str
) -> None:
    github.add_issue(11, labels=labels)
    claim(github, 11, "Ryubara", association="OWNER")
    engine.reconcile(11)
    assert words in reasons(github, 11) and github.view(11).assignees == () and not github.cards(11)
    assert [c for c in github.comments(11) if c.login == "Ryubara"] == []


def test_a_refusal_is_a_reply_naming_the_command_and_the_command_is_deleted(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(11, labels=("task", "S-blocked"))
    command = claim(github, 11)
    engine.reconcile(11)
    (reply,) = github.replies(11)
    assert reply.body.startswith(f"<!-- coney-claim-reply:{command} -->") and "alice" in reply.body
    assert command not in {c.id for c in github.comments(11)}


def test_an_issue_held_by_someone_else_is_refused(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10, "alice")
    engine.reconcile(10)
    claim(github, 10, "bob")
    engine.reconcile(10)
    assert "already claimed by alice" in reasons(github, 10)
    assert github.view(10).assignees == ("alice",)


def test_the_earliest_claim_wins_even_when_only_one_run_happens(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    """GitHub keeps one pending run per group: three claims, one run, the first claim wins."""
    first = claim(github, 10, "alice")
    clock.advance(seconds=3)
    claim(github, 10, "bob")
    clock.advance(seconds=3)
    claim(github, 10, "carol")
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)
    assert card_of(github, 10).claimant == "alice" and card_of(github, 10).handled == (first,)
    assert reasons(github, 10).count("already claimed by alice") == 2
    assert [c for c in github.comments(10) if c.user_type != "Bot"] == []


def test_a_command_that_cannot_be_deleted_is_not_acted_on_twice(engine: Engine, github: FakeGitHub) -> None:
    github.refuse_delete = True
    command = claim(github, 10)
    engine.reconcile(10)
    github.writes.clear()
    assert engine.reconcile(10) == []
    assert command in {c.id for c in github.comments(10)} and command in card_of(github, 10).handled
    assert github.writes == []
    # A refusal that cannot be deleted is not answered twice either.
    github.add_issue(11, labels=("task", "S-blocked"))
    claim(github, 11, "bob")
    engine.reconcile(11)
    engine.reconcile(11)
    assert len(github.replies(11)) == 1


def test_a_person_github_cannot_assign_is_told_and_nothing_else_changes(engine: Engine, github: FakeGitHub) -> None:
    github.unassignable.add("alice")
    claim(github, 10)
    engine.reconcile(10)
    assert "did not assign @alice" in reasons(github, 10) and not github.cards(10)
    assert "S-ready-to-start" in github.view(10).labels


def test_a_command_in_a_sentence_is_not_a_command(engine: Engine, github: FakeGitHub) -> None:
    github.say(10, "alice", "I might /claim this later")
    assert engine.reconcile(10) == [] and github.view(10).assignees == ()


def test_a_command_that_is_close_gets_the_grammar(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10, text="/claim please")
    engine.reconcile(10)
    assert "/progress <stage>" in reasons(github, 10) and not github.cards(10)


# --- the checks ------------------------------------------------------------------------------------------------------


def test_p_critical_comes_first_for_everyone(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(20, labels=("bug", "P-critical", "S-ready-to-start"))
    claim(github, 10, "Ryubara", association="OWNER")
    engine.reconcile(10)
    assert "`P-critical` comes first: #20" in reasons(github, 10) and github.view(10).assignees == ()
    claim(github, 20, "bob")
    engine.reconcile(20)
    assert github.view(20).assignees == ("bob",)
    claim(github, 10, "alice")
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)


def test_a_blocker_must_be_finished_by_a_merged_pull_request(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(12, body="### Summary\n\nx\n\n### Blocked by\n\n#30 and #31\n\n### Screenshots\n\nnone")
    github.add_issue(30, state="closed")
    github.add_issue(31, state="open")
    github.blockers[30] = Blocker(30, False, "closed without a merged pull request")
    claim(github, 12)
    engine.reconcile(12)
    text = reasons(github, 12)
    assert "blocked by #30 (closed without a merged pull request), #31 (open)" in text
    github.blockers[30] = Blocker(30, True, "closed by a merged pull request")
    github.blockers[31] = Blocker(31, True, "merged")
    claim(github, 12)
    engine.reconcile(12)
    assert github.view(12).assignees == ("alice",)


def test_a_contributor_holds_two_claims_and_a_maintainer_three(engine: Engine, github: FakeGitHub) -> None:
    for number in (11, 12, 13, 14):
        github.add_issue(number)
    for number in (10, 11, 12):
        claim(github, number, "alice")
        engine.reconcile(number)
    assert github.view(10).assignees == ("alice",) and github.view(11).assignees == ("alice",)
    assert github.view(12).assignees == () and "2 open claim(s) (2 at most for a contributor)" in reasons(github, 12)
    for number in (13, 14, 15):
        github.add_issue(number)
    for number in (13, 14, 15):
        claim(github, number, "Ryubara", association="OWNER")
        engine.reconcile(number)
    assert [n for n in (13, 14, 15) if github.view(n).assignees == ("Ryubara",)] == [13, 14, 15]
    github.add_issue(16)
    claim(github, 16, "Ryubara", association="OWNER")
    engine.reconcile(16)
    assert "3 open claim(s) (3 at most for a maintainer)" in reasons(github, 16)


def test_issues_of_one_task_count_as_one_claim(engine: Engine, github: FakeGitHub) -> None:
    for number in (11, 12):
        github.add_issue(number)
    for number in (10, 11, 12):
        claim(github, number, "alice", text="/claim task: big-task")
        engine.reconcile(number)
    assert all(github.view(n).assignees == ("alice",) for n in (10, 11, 12))
    github.add_issue(13)
    github.add_issue(14)
    claim(github, 13, "alice")
    engine.reconcile(13)
    claim(github, 14, "alice")
    engine.reconcile(14)
    assert github.view(13).assignees == ("alice",) and github.view(14).assignees == ()


def test_a_pull_request_cannot_be_claimed(engine: Engine, github: FakeGitHub) -> None:
    github.add_pr(40)
    github.comment_list[40] = []
    claim(github, 40)
    assert engine.reconcile(40) == []


# --- the other commands ---------------------------------------------------------------------------------------------


def test_progress_renew_and_scope_belong_to_the_claimant(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    claim(github, 10)
    engine.reconcile(10)
    clock.advance(hours=5)
    claim(github, 10, "alice", "/progress studied")
    claim(github, 10, "mallory", "/progress checked")
    claim(github, 10, "mallory", "/claim renew")
    engine.reconcile(10)
    card = card_of(github, 10)
    assert (
        card.stage == "studied" and card.last_activity_why == "progress" and card.activity == START + timedelta(hours=5)
    )
    assert reasons(github, 10).count("only the claimant") == 2
    clock.advance(hours=1)
    claim(github, 10, "alice", "/claim scope: src/ai/")
    engine.reconcile(10)
    card = card_of(github, 10)
    assert (card.scope, card.task, card.last_activity_why) == (("src/ai/",), None, "scope")
    claim(github, 10, "alice", "/claim")
    engine.reconcile(10)
    assert "already yours" in reasons(github, 10)
    clock.advance(hours=1)
    claim(github, 10, "alice", "/claim renew")
    engine.reconcile(10)
    assert card_of(github, 10).last_activity_why == "renew"


def test_unclaim_by_the_claimant_or_a_maintainer_releases(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10)
    engine.reconcile(10)
    claim(github, 10, "mallory", "/unclaim")
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",) and "only alice or a maintainer" in reasons(github, 10)
    claim(github, 10, "Ryubara", "/unclaim", "OWNER")
    engine.reconcile(10)
    view = github.view(10)
    assert view.assignees == () and "S-ready-to-start" in view.labels and "S-in-progress" not in view.labels
    card = card_of(github, 10)
    assert card.status == "released" and "unclaimed by Ryubara" in (card.released_why or "")
    claim(github, 10, "bob")
    engine.reconcile(10)
    assert github.view(10).assignees == ("bob",) and len(github.cards(10)) == 2


def test_unclaim_without_a_claim_is_answered(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10, text="/unclaim")
    engine.reconcile(10)
    assert "no claim to give up" in reasons(github, 10)


# --- the pull request, the deadlines, drift -------------------------------------------------------------------------


def test_an_open_pull_request_changes_the_label_and_the_status_and_is_activity(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    claim(github, 10)
    engine.reconcile(10)
    clock.advance(hours=60)
    github.add_pr(50)
    github.link_pr(10, 50, "open", head_date=START + timedelta(hours=59))
    engine.reconcile(10)
    view = github.view(10)
    assert "S-pending-review" in view.labels and "S-in-progress" not in view.labels
    card = card_of(github, 10)
    assert (card.status, card.pr, card.last_activity_why) == ("pr-open", 50, "pull request")
    assert "PR #50 open" in github.cards(10)[-1].body and github.others(10) == []


def test_merging_ends_the_claim_as_merged(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10)
    engine.reconcile(10)
    github.add_pr(50)
    github.link_pr(10, 50, "merged")
    github.items[10].view = github.view(10).__class__(**{**github.view(10).__dict__, "state": "closed", "labels": ()})
    engine.reconcile(10)
    card = card_of(github, 10)
    assert (card.status, card.pr) == ("merged", 50)
    assert github.view(10).assignees == ("alice",)  # a closed issue keeps its history


def test_closing_without_a_merge_releases(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10)
    engine.reconcile(10)
    github.items[10].view = github.view(10).__class__(**{**github.view(10).__dict__, "state": "closed"})
    engine.reconcile(10)
    assert card_of(github, 10).status == "released" and "closed" in (card_of(github, 10).released_why or "")


def test_the_claimant_is_warned_at_48_hours_and_released_at_72(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    claim(github, 10)
    engine.reconcile(10)
    clock.advance(hours=47)
    engine.reconcile(10)
    assert github.others(10) == []
    clock.advance(hours=1)
    engine.reconcile(10)
    (warning,) = github.others(10)
    assert warning.body.startswith("@alice") and "24 h" in warning.body
    engine.reconcile(10)
    assert len(github.others(10)) == 1  # warned once
    assert card_of(github, 10).warning_comment == warning.id
    clock.advance(hours=24)
    engine.reconcile(10)
    view = github.view(10)
    assert view.assignees == () and "S-ready-to-start" in view.labels
    card = card_of(github, 10)
    assert card.status == "released" and card.released_why == "idle 72 h" and github.others(10) == []


@pytest.mark.parametrize("how", ["/progress studied", "/claim renew"])
def test_activity_takes_the_warning_back_and_restarts_the_clock(
    engine: Engine, github: FakeGitHub, clock: Clock, how: str
) -> None:
    claim(github, 10)
    engine.reconcile(10)
    clock.advance(hours=50)
    engine.reconcile(10)
    assert len(github.others(10)) == 1
    claim(github, 10, "alice", how)
    engine.reconcile(10)
    assert github.others(10) == [] and card_of(github, 10).warned_at is None
    clock.advance(hours=47)
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",) and github.others(10) == []


def test_a_push_reported_by_the_event_is_activity(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    claim(github, 10)
    engine.reconcile(10)
    clock.advance(hours=50)
    github.add_pr(50)
    github.link_pr(10, 50, "open")
    engine.reconcile(10, Activity(clock.now(), "alice"))
    assert github.others(10) == [] and card_of(github, 10).last_activity_why == "pull request"


def test_a_hand_unassignment_ends_the_claim(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10)
    engine.reconcile(10)
    github.unassign(10, ("alice",))
    engine.reconcile(10)
    assert card_of(github, 10).released_why == "unassigned by hand"
    assert "S-ready-to-start" in github.view(10).labels


def test_a_hand_assignment_is_adopted_and_gets_the_deadline(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.add_issue(11, labels=("task", "S-needs-triage"), assignees=("carol",))
    engine.reconcile(11)
    card = card_of(github, 11)
    assert (card.claimant, card.adopted, card.status) == ("carol", True, "claimed")
    assert "S-in-progress" in github.view(11).labels and "S-needs-triage" not in github.view(11).labels
    assert "assigned by hand" in github.cards(11)[-1].body
    clock.advance(hours=73)
    engine.reconcile(11)
    assert github.view(11).assignees == () and card_of(github, 11).status == "released"


def test_a_second_assignee_by_hand_is_removed(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10)
    engine.reconcile(10)
    github.assign(10, "bob")
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)


def test_two_held_cards_on_one_issue_keep_the_earliest(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    """The race the per-issue group should prevent: two runs each posted a card."""
    claim(github, 10, "alice")
    engine.reconcile(10)
    later = model.Card(issue=10, claimant="bob", claimed_at=START + timedelta(seconds=2), handled=(1,))
    github.create_comment(10, model.render_card(later, RULES))
    github.assign(10, "bob")
    engine.reconcile(10)
    assert github.view(10).assignees == ("alice",)
    loser = model.parse_card(github.cards(10)[1].body, RULES)
    assert loser is not None and (loser.status, loser.released_why) == ("lost", "lost to alice")
    assert card_of(github, 10).claimant == "bob"  # the newest card is the loser's, now ended
    engine2 = make_engine(github, clock)
    assert engine2.reconcile(10) == []


def test_a_bots_lookalike_from_a_person_is_not_a_card(engine: Engine, github: FakeGitHub) -> None:
    fake_card = model.Card(issue=10, claimant="mallory", claimed_at=START)
    github.say(10, "mallory", model.render_card(fake_card, RULES))
    engine.reconcile(10)
    assert github.view(10).assignees == () and engine.held_cards() == {}


# --- the sweep ------------------------------------------------------------------------------------------------------


def test_the_sweep_answers_a_command_a_dropped_run_left(engine: Engine, github: FakeGitHub) -> None:
    claim(github, 10, "alice")
    result = engine.sweep()
    assert github.view(10).assignees == ("alice",) and result.errors == []
    assert any("claimed it" in line for line in result.lines)


def test_the_sweep_warns_and_releases_every_claim_in_its_turn(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    for number in (11, 12):
        github.add_issue(number)
    claim(github, 10, "alice")
    engine.reconcile(10)
    clock.advance(hours=30)
    claim(github, 11, "bob")
    engine.reconcile(11)
    clock.advance(hours=20)  # alice 50 h idle, bob 20 h
    engine.sweep()
    assert len(github.others(10)) == 1 and github.others(11) == []
    clock.advance(hours=23)  # alice 73 h, bob 43 h
    engine.sweep()
    assert github.view(10).assignees == () and github.view(11).assignees == ("bob",)


def test_the_sweep_removes_old_replies_once_their_command_is_gone(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    github.add_issue(11, labels=("task", "S-blocked"))
    claim(github, 11)
    engine.reconcile(11)
    clock.advance(hours=23)
    engine.sweep()
    assert len(github.replies(11)) == 1
    clock.advance(hours=2)
    engine.sweep()
    assert github.replies(11) == []


def test_a_reply_stays_while_its_command_cannot_be_deleted(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.refuse_delete = True
    github.add_issue(11, labels=("task", "S-blocked"))
    claim(github, 11)
    engine.reconcile(11)
    clock.advance(hours=30)
    engine.sweep()
    assert len(github.replies(11)) == 1


def test_one_failing_issue_does_not_stop_the_sweep(engine: Engine, github: FakeGitHub) -> None:
    from coney_tools.config import ConfigError

    github.add_issue(11)
    claim(github, 10, "alice")
    claim(github, 11, "bob")
    original = github.issue

    def flaky(number: int):  # type: ignore[no-untyped-def]
        if number == 10:
            raise ConfigError("boom")
        return original(number)

    github.issue = flaky  # type: ignore[method-assign]
    result = engine.sweep()
    assert result.errors == ["#10: boom"] and github.view(11).assignees == ("bob",)


def test_the_cards_list_the_claimants_open_claims(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(11)
    claim(github, 10, "Ryubara", association="OWNER")
    engine.reconcile(10)
    claim(github, 11, "Ryubara", association="OWNER")
    engine.reconcile(11)
    assert card_of(github, 11).open_claims == (10, 11)
    engine.reconcile(10)
    assert card_of(github, 10).open_claims == (10, 11)
    assert RULES.bot_login == BOT

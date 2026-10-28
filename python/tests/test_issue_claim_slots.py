# SPDX-License-Identifier: GPL-3.0-or-later
"""The open-claims limit follows the engineer slots (`claims.limit_ends_at`), and a comment cannot escape it.

A unit's slot frees when its review passes, so its claim stops counting. The Action sees only GitHub, where a comment
is free to write, so it trusts a stage only from a maintainer (whose tools post it after the review) and otherwise waits
for a pull request that is open and not a draft, and one such pull request frees one task's slot, however many issues it
names. The count comes back when that pull request closes unmerged, and the newest claims past the limit are then warned
and released after their notice.
"""

from datetime import timedelta

import pytest
from audit_support import CONVENTIONS
from claim_support import LIVE, START, Clock, FakeGitHub

from coney_tools import issue_claim_model as model
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_engine import Engine

OFF = CONVENTIONS
RULES = OFF.claims
READY = ("task", "A-tools", "P-low", "S-ready-to-start")


def card_of(github: FakeGitHub, number: int) -> model.Card:
    """The newest card of an issue."""
    card = model.parse_card(github.cards(number)[-1].body, RULES)
    assert card is not None
    return card


class Scene:
    """Issues 1 to 8 on an in-memory GitHub, and a way to act as a person and let the Action look."""

    def __init__(self, issues: int = 8, conventions: Conventions = OFF) -> None:
        self.conventions = conventions
        self.clock = Clock()
        self.github = FakeGitHub(self.clock)
        for number in range(1, issues + 1):
            self.github.add_issue(number, labels=READY)

    def engine(self) -> Engine:
        """An engine on the scene's rules: the repository's as committed (adoption and release off), or `LIVE`."""
        return Engine(self.github, self.conventions, self.clock.now)

    def say(self, number: int, login: str, text: str) -> None:
        """`login` comments `text` on the issue and the Action handles it."""
        self.github.say(number, login, text, "OWNER" if login == "Ryubara" else "NONE")
        self.engine().reconcile(number)

    def held(self, number: int) -> bool:
        """Whether the issue is claimed by someone."""
        return bool(self.github.view(number).assignees)

    def pull_request(self, issue: int, number: int, author: str, state: str = "open", draft: bool = False) -> None:
        """A pull request that closes the issue appears or changes, and the Action looks at the issue."""
        self.link(number, author, (issue,), state, draft)

    def link(self, number: int, author: str, issues: tuple[int, ...], state: str = "open", draft: bool = False) -> None:
        """A pull request that closes several issues appears or changes, and the Action looks at each of them."""
        if number not in self.github.items:
            self.github.add_pr(number)
        for issue in issues:
            self.github.link_pr(issue, number, state, author=author, draft=draft)
        for issue in issues:
            self.engine().reconcile(issue)


def test_a_maintainers_review_stops_counting_but_a_stage_is_still_a_stage() -> None:
    scene = Scene()
    github = scene.github
    for number in (1, 2, 3):
        scene.say(number, "Ryubara", "/claim")
    scene.say(4, "Ryubara", "/claim")
    assert not scene.held(4) and "3 open claim(s)" in github.replies(4)[-1].body
    # The tools post `/progress reviewed` as the maintainer's account: the claim stays held but no longer counts.
    scene.say(1, "Ryubara", "/progress reviewed")
    card = card_of(github, 1)
    assert card.active and card.freed and not card.counted and "no longer counted" in github.cards(1)[-1].body
    scene.say(4, "Ryubara", "/claim")
    assert scene.held(4)
    # A later stage (more work after a review) does not take the slot back, as a slot is not taken back either.
    scene.say(1, "Ryubara", "/progress checked")
    assert card_of(github, 1).freed and not card_of(github, 1).counted
    scene.say(2, "Ryubara", "/progress finished")
    assert not card_of(github, 2).counted
    assert scene.engine().sweep().errors == []


def test_a_contributor_cannot_end_the_count_by_commenting_a_stage() -> None:
    """The escape the review found: a comment is free, so `/progress reviewed` by hand must not free a slot."""
    scene = Scene()
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    for number, stage in zip((1, 2), ("reviewed", "pr-opened"), strict=True):
        scene.say(number, "alice", f"/progress {stage}")
        card = card_of(github, number)
        assert card.stage == stage and card.active and card.counted and not card.freed
    # Every stage that would end the count for a maintainer leaves a contributor's claim counted.
    for stage in RULES.limit_ends_at:
        scene.say(1, "alice", f"/progress {stage}")
        assert card_of(github, 1).counted
    scene.say(3, "alice", "/claim")
    assert not scene.held(3) and "2 open claim(s) (2 at most for a contributor)" in github.replies(3)[-1].body
    # The Action says so in its log: the stage was recorded, the slot was not freed.
    github.say(1, "alice", "/progress fix")
    assert any("frees a slot only for a maintainer" in line for line in scene.engine().reconcile(1))
    assert scene.engine().sweep().errors == []
    assert scene.held(1) and scene.held(2) and not scene.held(3)


def test_a_maintainer_who_is_not_the_claimant_cannot_free_the_slot() -> None:
    scene = Scene()
    scene.say(1, "alice", "/claim")
    scene.say(1, "Ryubara", "/progress reviewed")
    assert scene.github.replies(1) and card_of(scene.github, 1).counted


def test_a_ready_pull_request_frees_the_slot_for_anyone_and_a_draft_does_not() -> None:
    scene = Scene()
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice", draft=True)
    card = card_of(github, 1)
    assert card.status == "pr-open" and card.counted and not card.regained
    scene.say(3, "alice", "/claim")
    assert not scene.held(3)
    # Someone else's pull request that names the issue is not the claim's: nothing changes.
    scene.pull_request(1, 61, "mallory")
    assert card_of(github, 1).counted
    # The draft is made ready for review: the slot is free, and the next claim goes through.
    scene.pull_request(1, 60, "alice")
    assert not card_of(github, 1).counted and not card_of(github, 1).freed
    scene.say(3, "alice", "/claim")
    assert scene.held(3)


def test_the_count_comes_back_when_the_pull_request_closes_unmerged_and_the_newest_are_warned_then_released() -> None:
    scene = Scene(conventions=LIVE)
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(3, "alice", "/claim")
    assert scene.held(3) and not card_of(github, 1).counted
    # The pull request is closed without merging: the claim counts again, so alice now holds three.
    scene.pull_request(1, 60, "alice", state="closed")
    card = card_of(github, 1)
    assert card.counted and card.regained and card.status == "claimed"
    # Over the limit: the sweep warns for the newest claim (#3) and releases nothing yet.
    result = scene.engine().sweep()
    assert result.errors == [] and not any("ended" in line for line in result.lines)
    assert all(scene.held(n) for n in (1, 2, 3))
    assert len(github.others(3)) == 1 and "@alice you hold more open claims than the limit" in github.others(3)[0].body
    assert github.others(1) == [] and github.others(2) == []
    warned = card_of(github, 3)
    assert warned.over_warned_at == START and "Over the limit" in github.cards(3)[-1].body
    # A sweep inside the notice does nothing more (no second warning), and she takes no new claim meanwhile.
    scene.clock.advance(hours=RULES.deadline_hours - RULES.warn_hours - 1)
    scene.engine().sweep()
    assert scene.held(3) and len(github.others(3)) == 1
    scene.say(4, "alice", "/claim")
    assert not scene.held(4) and "3 open claim(s)" in github.replies(4)[-1].body
    # The notice runs out: the newest claim is released, with its warning removed, and the other two stay.
    scene.clock.advance(hours=2)
    result = scene.engine().sweep()
    assert not scene.held(3) and scene.held(1) and scene.held(2)
    assert card_of(github, 3).status == "released" and "more than 2 open claims" in (
        card_of(github, 3).released_why or ""
    )
    assert "Claim released" in github.replies(3)[-1].body
    assert not any("@alice you hold more open claims" in c.body for c in github.comment_list[3])
    assert any("ended" in line for line in result.lines)


def test_giving_a_claim_up_clears_the_over_limit_warning() -> None:
    scene = Scene(conventions=LIVE)
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(3, "alice", "/claim")
    scene.pull_request(1, 60, "alice", state="closed")
    scene.engine().sweep()
    assert card_of(github, 3).over_warned_at is not None
    # She finishes or gives up #2 within the notice: the claim in danger is no longer over, and its warning goes.
    scene.say(2, "alice", "/unclaim")
    scene.engine().sweep()
    kept = card_of(github, 3)
    assert kept.active and kept.over_warned_at is None and kept.over_warning_comment is None
    assert not any("you hold more open claims" in c.body for c in github.comment_list[3])
    scene.clock.advance(hours=RULES.deadline_hours - RULES.warn_hours + 1)
    scene.engine().sweep()
    assert scene.held(1) and scene.held(3)


def test_a_draft_again_or_a_reopened_pull_request_follows_the_pull_request() -> None:
    scene = Scene()
    scene.say(1, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    assert not card_of(scene.github, 1).counted
    scene.pull_request(1, 60, "alice", draft=True)
    assert card_of(scene.github, 1).counted
    scene.pull_request(1, 60, "alice")
    assert not card_of(scene.github, 1).counted


def test_a_maintainers_freed_slot_stays_free_when_the_pull_request_closes() -> None:
    scene = Scene()
    scene.say(1, "Ryubara", "/claim")
    scene.say(1, "Ryubara", "/progress reviewed")
    scene.pull_request(1, 60, "Ryubara")
    scene.pull_request(1, 60, "Ryubara", state="closed")
    card = card_of(scene.github, 1)
    assert card.freed and not card.counted and not card.regained


def test_a_stage_that_ends_counting_is_a_stage_and_old_cards_still_read() -> None:
    assert set(RULES.limit_ends_at) <= set(RULES.stages)
    assert "reviewed" in RULES.limit_ends_at and "pr-opened" in RULES.limit_ends_at
    card = model.Card(issue=1, claimant="a", claimed_at=START)
    text = model.render_card(card, RULES)
    for gone in (',"counted":true', ',"freed":false', ',"regained":false'):
        text = text.replace(gone, "")
    parsed = model.parse_card(text, RULES)
    assert parsed is not None and parsed.counted and not parsed.freed and not parsed.regained
    both = model.render_card(card.changed(freed=True, counted=False, regained=True), RULES)
    again = model.parse_card(both, RULES)
    assert again is not None and again.freed and not again.counted and again.regained
    warned = card.changed(over_warned_at=START, over_warning_comment=77)
    text = model.render_card(warned, RULES)
    assert "Over the limit" in text and "2026-10-25" in text
    read = model.parse_card(text, RULES)
    assert read is not None and read.over_warned_at == START and read.over_warning_comment == 77


def test_two_cards_of_one_claim_keep_a_freed_slot() -> None:
    """A merged twin card must not give back a slot one of them freed."""
    scene = Scene()
    scene.say(1, "Ryubara", "/claim")
    github = scene.github
    keeper = card_of(github, 1)
    twin = keeper.changed(freed=True, counted=False, regained=True)
    github.create_comment(1, model.render_card(twin, RULES))
    scene.engine().reconcile(1)
    merged = card_of(github, 1)
    assert merged.freed and merged.regained and not merged.counted


def test_one_ready_pull_request_frees_one_claim_however_many_issues_it_names_or_grows_to_name() -> None:
    """The bound the second review asked for: a pull request is cheap to open and to edit, so one of them frees one
    task's slot (the oldest of the claimant's claims it closes), not every claim it names."""
    scene = Scene()
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.link(60, "alice", (1, 2))
    assert not card_of(github, 1).counted and card_of(github, 2).counted
    scene.say(3, "alice", "/claim")
    assert scene.held(3)
    # The same pull request grows to name the new claim too: still only the oldest is free.
    scene.link(60, "alice", (1, 2, 3))
    assert [card_of(github, n).counted for n in (1, 2, 3)] == [False, True, True]
    scene.say(4, "alice", "/claim")
    assert not scene.held(4) and "2 open claim(s) (2 at most for a contributor)" in github.replies(4)[-1].body
    # The sweep agrees: nothing over the limit, nothing warned.
    result = scene.engine().sweep()
    assert result.errors == [] and all(card_of(github, n).over_warned_at is None for n in (1, 2, 3))
    # A second ready pull request frees one more claim: the oldest it names (each pull request frees one).
    scene.link(61, "alice", (2, 3))
    assert [card_of(github, n).counted for n in (1, 2, 3)] == [False, False, True]


def test_one_ready_pull_request_frees_the_claims_of_one_task_and_no_other() -> None:
    scene = Scene()
    github = scene.github
    for number in (1, 2, 3):
        scene.say(number, "alice", "/claim task: big")
    scene.say(4, "alice", "/claim")
    scene.link(60, "alice", (1, 2, 3, 4))
    # The task of #1 (all three of its issues, which count as one claim) is free; #4 keeps counting.
    assert [card_of(github, n).counted for n in (1, 2, 3, 4)] == [False, False, False, True]
    scene.say(5, "alice", "/claim")
    assert scene.held(5)
    scene.say(6, "alice", "/claim")
    assert not scene.held(6)


def test_a_maintainers_own_ready_pull_request_frees_every_claim_it_closes() -> None:
    scene = Scene()
    for number in (1, 2, 3):
        scene.say(number, "Ryubara", "/claim")
    scene.link(60, "Ryubara", (1, 2, 3))
    assert [card_of(scene.github, n).counted for n in (1, 2, 3)] == [False, False, False]


def test_a_maintainers_ready_pull_request_does_not_free_a_contributors_slot() -> None:
    """The slot is the claimant's unit: a maintainer taking the issue over is not the claimant's pull request."""
    scene = Scene()
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.pull_request(1, 60, "Ryubara")
    card = card_of(github, 1)
    assert card.status == "pr-open" and card.counted and not card.regained
    scene.pull_request(1, 60, "alice")
    assert not card_of(github, 1).counted


def test_a_claim_that_came_back_counts_when_parallel_claims_are_ordered() -> None:
    """A claimant with a returned claim who posts two `/claim` at once keeps one, not both."""

    from coney_tools.issue_claim_rules import overrun

    old = model.Card(issue=1, claimant="alice", claimed_at=START, regained=True)
    first = model.Card(issue=2, claimant="alice", claimed_at=START.replace(minute=1))
    second = model.Card(issue=3, claimant="alice", claimed_at=START.replace(minute=2))
    held = [old, first, second]
    assert [overrun(c, held, False, RULES) is None for c in held] == [True, True, False]


def test_a_regained_claim_is_not_exempt_from_the_order_either() -> None:
    from coney_tools.issue_claim_rules import overrun

    cards = [
        model.Card(issue=n, claimant="alice", claimed_at=START.replace(minute=n), regained=n == 1) for n in (1, 2, 3)
    ]
    assert [overrun(c, cards, False, RULES) is None for c in cards] == [True, True, False]


def test_a_toggled_pull_request_gets_a_fresh_notice_but_never_past_twice_the_notice() -> None:
    """The warning is keyed to the claimant's count: made ready (under the limit) at +23 h and a draft again (over) at
    +24.5 h, with no sweep between, the claim is not released at once; the notice starts again, and a second toggle
    cannot put the release off beyond twice the notice after the first warning."""
    scene = Scene(conventions=LIVE)
    github = scene.github
    notice = RULES.deadline_hours - RULES.warn_hours
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(3, "alice", "/claim")
    scene.pull_request(1, 60, "alice", state="closed")
    scene.engine().sweep()
    first = card_of(github, 3)
    assert first.over_warned_at == START and first.over_first_at == START
    scene.clock.advance(hours=23)
    scene.pull_request(1, 60, "alice")  # ready again: she is under the limit, and no sweep sees it
    scene.clock.advance(hours=1.5)
    scene.pull_request(1, 60, "alice", draft=True)  # a draft again: over the limit again, 24.5 h after the warning
    result = scene.engine().sweep()
    assert scene.held(3) and not any("ended" in line for line in result.lines)
    again = card_of(github, 3)
    assert again.over_warned_at == START + timedelta(hours=24.5) and again.over_first_at == START
    assert again.over_deadline(RULES) == START + timedelta(hours=2 * notice)  # the cap, not 24.5 h + notice
    warnings = [c for c in github.comment_list[3] if "you hold more open claims" in c.body]
    assert len(warnings) == 1 and "2026-10-24 12:00 UTC" in warnings[0].body
    # A second toggle cannot push it beyond the cap.
    scene.clock.advance(hours=5)
    scene.pull_request(1, 60, "alice")
    scene.clock.advance(hours=1)
    scene.pull_request(1, 60, "alice", draft=True)
    scene.engine().sweep()
    assert scene.held(3) and card_of(github, 3).over_deadline(RULES) == START + timedelta(hours=2 * notice)
    scene.clock.advance(hours=2 * notice - 30.5 - 0.5)
    scene.engine().sweep()
    assert scene.held(3)
    scene.clock.advance(hours=1)
    scene.engine().sweep()
    assert not scene.held(3) and card_of(github, 3).status == "released"


def test_a_warning_given_before_a_dip_is_not_released_on_the_old_notice() -> None:
    """Without the toggle in the sweep gap the notice is the plain one: released 24 h after the warning."""
    scene = Scene(conventions=LIVE)
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(3, "alice", "/claim")
    scene.pull_request(1, 60, "alice", state="closed")
    scene.engine().sweep()
    scene.clock.advance(hours=RULES.deadline_hours - RULES.warn_hours + 0.5)
    scene.engine().sweep()
    assert not scene.held(3)


def test_the_over_limit_release_is_off_until_the_cutover_and_says_what_it_would_do() -> None:
    scene = Scene()
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(3, "alice", "/claim")
    scene.pull_request(1, 60, "alice", state="closed")
    result = scene.engine().sweep()
    assert any("#3: would warn alice" in line and "release is off until the cutover" in line for line in result.lines)
    assert github.others(3) == [] and card_of(github, 3).over_warned_at is None
    scene.clock.advance(hours=200)
    scene.engine().sweep()
    assert scene.held(1) and scene.held(2) and scene.held(3)
    # `/claim` still refuses a new claim meanwhile.
    scene.say(4, "alice", "/claim")
    assert not scene.held(4)


def test_the_trial_label_runs_the_over_limit_release_on_one_issue_before_the_cutover() -> None:
    scene = Scene()
    github = scene.github
    github.add_issue(9, labels=(*READY, RULES.trial.label))
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(9, "alice", "/claim")
    scene.pull_request(1, 60, "alice", state="closed")
    scene.engine().sweep()
    assert "@alice you hold more open claims" in github.others(9)[0].body
    # The trial's notice is its idle hours (2 h less 1 h), not a day.
    scene.clock.advance(hours=RULES.trial.deadline_hours - RULES.trial.warn_hours + 0.2)
    scene.engine().sweep()
    assert not scene.held(9) and scene.held(1) and scene.held(2)


def test_a_maintainers_claim_is_never_released_for_being_over_the_limit() -> None:
    scene = Scene(conventions=LIVE)
    github = scene.github
    for number in (1, 2, 3):
        scene.say(number, "Ryubara", "/claim")
    scene.pull_request(1, 60, "Ryubara")
    scene.say(4, "Ryubara", "/claim")
    assert scene.held(4)
    scene.pull_request(1, 60, "Ryubara", state="closed")
    result = scene.engine().sweep()
    assert any("a maintainer's claim is never released for it" in line for line in result.lines)
    scene.clock.advance(hours=RULES.deadline_hours - RULES.warn_hours + 2)
    scene.engine().sweep()
    assert all(scene.held(n) for n in (1, 2, 3, 4)) and github.others(4) == []


def _regained_scene() -> Scene:
    """Alice holds #1, #2 and #3 after a ready pull request on #1 closed unmerged; the sweep has not run yet."""
    scene = Scene(conventions=LIVE)
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(3, "alice", "/claim")
    scene.pull_request(1, 60, "alice", state="closed")
    return scene


def test_a_permission_github_will_not_say_is_treated_as_a_maintainers_and_nothing_is_released(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    scene = _regained_scene()
    github = scene.github
    # A transient API error: the permission lookup answers nothing at all (not "none", which is a 404).
    monkeypatch.setattr(github, "permission", lambda login: None)
    result = scene.engine().sweep()
    assert any("could not read alice's permission" in line for line in result.lines)
    assert github.others(3) == [] and card_of(github, 3).over_warned_at is None
    scene.clock.advance(hours=RULES.deadline_hours + 1)
    scene.engine().sweep()
    assert all(scene.held(n) for n in (1, 2, 3))
    # Once GitHub says she is a contributor, the same claim is warned (first) as it would have been.
    monkeypatch.undo()
    scene.engine().sweep()
    assert card_of(github, 3).over_warned_at is not None and scene.held(3)


def test_a_claimant_under_the_limit_again_loses_the_regained_mark_so_a_later_overrun_is_not_warned_for_ever() -> None:
    scene = _regained_scene()
    github = scene.github
    assert card_of(github, 1).regained
    # She gives #2 up before the sweep: nothing is over the limit any more, and the mark goes from every claim.
    scene.say(2, "alice", "/unclaim")
    result = scene.engine().sweep()
    assert result.errors == []
    assert not card_of(github, 1).regained and not card_of(github, 3).regained
    assert github.others(3) == []
    # Still over the limit through a warning, the mark stays: the claimant is not forgiven for a toggled count.
    again = _regained_scene()
    again.engine().sweep()
    assert card_of(again.github, 1).regained and card_of(again.github, 3).over_warned_at is not None

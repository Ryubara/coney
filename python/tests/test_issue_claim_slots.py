# SPDX-License-Identifier: GPL-3.0-or-later
"""The open-claims limit follows the engineer slots (`claims.limit_ends_at`), and a comment cannot escape it.

A unit's slot frees when its review passes, so its claim stops counting. The Action sees only GitHub, where a comment
is free to write, so it trusts a stage only from a maintainer (whose tools post it after the review) and otherwise waits
for a pull request that is open and not a draft, and one such pull request frees one task's slot, however many issues it
names. The count comes back when that pull request closes unmerged, and the newest claims past the limit are then warned
and released after their notice.
"""

from audit_support import CONVENTIONS
from claim_support import START, Clock, FakeGitHub

from coney_tools import issue_claim_model as model
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

    def __init__(self, issues: int = 8) -> None:
        self.clock = Clock()
        self.github = FakeGitHub(self.clock)
        for number in range(1, issues + 1):
            self.github.add_issue(number, labels=READY)

    def engine(self) -> Engine:
        """An engine on the repository's rules as committed (adoption and release off)."""
        return Engine(self.github, OFF, self.clock.now)

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
    assert Engine(github, OFF, scene.clock.now).sweep().errors == []


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
    assert Engine(github, OFF, scene.clock.now).sweep().errors == []
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
    scene = Scene()
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
    result = Engine(github, OFF, scene.clock.now).sweep()
    assert result.errors == [] and not any("ended" in line for line in result.lines)
    assert all(scene.held(n) for n in (1, 2, 3))
    assert len(github.others(3)) == 1 and "@alice you hold more open claims than the limit" in github.others(3)[0].body
    assert github.others(1) == [] and github.others(2) == []
    warned = card_of(github, 3)
    assert warned.over_warned_at == START and "Over the limit" in github.cards(3)[-1].body
    # A sweep inside the notice does nothing more (no second warning), and she takes no new claim meanwhile.
    scene.clock.advance(hours=RULES.deadline_hours - RULES.warn_hours - 1)
    Engine(github, OFF, scene.clock.now).sweep()
    assert scene.held(3) and len(github.others(3)) == 1
    scene.say(4, "alice", "/claim")
    assert not scene.held(4) and "3 open claim(s)" in github.replies(4)[-1].body
    # The notice runs out: the newest claim is released, with its warning removed, and the other two stay.
    scene.clock.advance(hours=2)
    result = Engine(github, OFF, scene.clock.now).sweep()
    assert not scene.held(3) and scene.held(1) and scene.held(2)
    assert card_of(github, 3).status == "released" and "more than 2 open claims" in (
        card_of(github, 3).released_why or ""
    )
    assert "Claim released" in github.replies(3)[-1].body
    assert not any("@alice you hold more open claims" in c.body for c in github.comment_list[3])
    assert any("ended" in line for line in result.lines)


def test_giving_a_claim_up_clears_the_over_limit_warning() -> None:
    scene = Scene()
    github = scene.github
    scene.say(1, "alice", "/claim")
    scene.say(2, "alice", "/claim")
    scene.pull_request(1, 60, "alice")
    scene.say(3, "alice", "/claim")
    scene.pull_request(1, 60, "alice", state="closed")
    Engine(github, OFF, scene.clock.now).sweep()
    assert card_of(github, 3).over_warned_at is not None
    # She finishes or gives up #2 within the notice: the claim in danger is no longer over, and its warning goes.
    scene.say(2, "alice", "/unclaim")
    Engine(github, OFF, scene.clock.now).sweep()
    kept = card_of(github, 3)
    assert kept.active and kept.over_warned_at is None and kept.over_warning_comment is None
    assert not any("you hold more open claims" in c.body for c in github.comment_list[3])
    scene.clock.advance(hours=RULES.deadline_hours)
    Engine(github, OFF, scene.clock.now).sweep()
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
    result = Engine(github, OFF, scene.clock.now).sweep()
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

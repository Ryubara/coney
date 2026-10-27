# SPDX-License-Identifier: GPL-3.0-or-later
"""The open-claims limit follows the engineer slots (`claims.limit_ends_at`), and a comment cannot escape it.

A unit's slot frees when its review passes, so its claim stops counting. The Action sees only GitHub, where a comment
is free to write, so it trusts a stage only from a maintainer (whose tools post it after the review) and otherwise waits
for a pull request that is open and not a draft; the count comes back when that pull request closes unmerged.
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
        if number not in self.github.items:
            self.github.add_pr(number)
        self.github.link_pr(issue, number, state, author=author, draft=draft)
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


def test_the_count_comes_back_when_the_pull_request_closes_unmerged_and_nothing_is_released_for_it() -> None:
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
    # Over the limit, but her work in progress is kept: the sweep releases nothing...
    result = Engine(github, OFF, scene.clock.now).sweep()
    assert result.errors == [] and not any("released" in line for line in result.lines)
    assert all(scene.held(n) for n in (1, 2, 3))
    # ...and she takes no new claim until she is under the limit.
    scene.say(4, "alice", "/claim")
    assert not scene.held(4) and "3 open claim(s)" in github.replies(4)[-1].body
    scene.say(3, "alice", "/unclaim")
    scene.say(4, "alice", "/claim")
    assert not scene.held(4)
    scene.say(2, "alice", "/unclaim")
    scene.say(4, "alice", "/claim")
    assert scene.held(4)


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

# SPDX-License-Identifier: GPL-3.0-or-later
"""The exempt label (`claims.exempt_label`): a claim of a workflow unit is outside the open-claims limit.

The maintainer's decision of 2026-10-27: the units that build the new workflow claim under the maintainer's account
like every task, but neither count toward the limit nor are refused by it (docs/guides/issues.md#claiming).
"""

from dataclasses import replace

import pytest
from claim_support import RULES, START, Clock, FakeGitHub, make_engine

from coney_tools import issue_claim_model as model
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_rules import open_task_keys, overrun

READY = ("task", "A-tools", "P-high", "S-ready-to-start")
WORKFLOW = (*READY, RULES.exempt_label)


@pytest.fixture
def clock() -> Clock:
    return Clock()


@pytest.fixture
def github(clock: Clock) -> FakeGitHub:
    return FakeGitHub(clock)


@pytest.fixture
def engine(github: FakeGitHub, clock: Clock) -> Engine:
    return make_engine(github, clock)


def card_of(github: FakeGitHub, number: int) -> model.Card:
    """The newest card of an issue, parsed."""
    card = model.parse_card(github.cards(number)[-1].body, RULES)
    assert card is not None
    return card


def take(engine: Engine, github: FakeGitHub, number: int, login: str = "Ryubara") -> None:
    """`login` comments `/claim` on `number` and the Action handles it."""
    github.say(number, login, "/claim", "OWNER" if login == "Ryubara" else "NONE")
    engine.reconcile(number)


def held_by(github: FakeGitHub, numbers: range | tuple[int, ...], login: str = "Ryubara") -> list[int]:
    """The issues of `numbers` assigned to `login`."""
    return [n for n in numbers if github.view(n).assignees == (login,)]


def test_the_label_is_in_the_conventions_and_the_label_list() -> None:
    from audit_support import CONVENTIONS

    assert RULES.exempt_label == "workflow"
    assert RULES.exempt_label in CONVENTIONS.labels.names()


def test_a_maintainer_at_the_limit_still_claims_a_workflow_unit(engine: Engine, github: FakeGitHub) -> None:
    for number in (1, 2, 3, 4):
        github.add_issue(number, labels=READY)
    github.add_issue(5, labels=WORKFLOW)
    for number in (1, 2, 3):
        take(engine, github, number)
    take(engine, github, 4)
    assert held_by(github, (1, 2, 3, 4)) == [1, 2, 3]
    take(engine, github, 5)
    assert github.view(5).assignees == ("Ryubara",) and github.replies(5) == []
    card = card_of(github, 5)
    assert card.exempt and "not counted toward the open-claims limit (`workflow`)" in github.cards(5)[-1].body
    assert not card_of(github, 1).exempt


def test_workflow_claims_do_not_use_up_the_limit(engine: Engine, github: FakeGitHub) -> None:
    for number in range(1, 8):
        github.add_issue(number, labels=WORKFLOW)
    for number in range(10, 14):
        github.add_issue(number, labels=READY)
    for number in range(1, 8):
        take(engine, github, number)
    assert held_by(github, range(1, 8)) == list(range(1, 8))
    for number in range(10, 14):
        take(engine, github, number)
    assert held_by(github, range(10, 14)) == [10, 11, 12]
    assert "3 open claim(s) (3 at most for a maintainer)" in github.replies(13)[-1].body


def test_a_contributor_is_exempt_too_on_a_labelled_issue(engine: Engine, github: FakeGitHub) -> None:
    for number in (1, 2, 3):
        github.add_issue(number, labels=READY)
    github.add_issue(4, labels=WORKFLOW)
    for number in (1, 2, 3):
        take(engine, github, number, "alice")
    assert held_by(github, (1, 2, 3), "alice") == [1, 2]
    take(engine, github, 4, "alice")
    assert github.view(4).assignees == ("alice",)


def test_the_sweep_never_releases_a_workflow_claim_for_the_limit(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    for number in range(1, 6):
        github.add_issue(number, labels=WORKFLOW)
    for number in range(1, 6):
        take(engine, github, number)
    clock.advance(hours=1)
    result = make_engine(github, clock).sweep()
    assert result.errors == [] and held_by(github, range(1, 6)) == list(range(1, 6))
    assert not any("released" in line for line in result.lines)


def test_the_flag_follows_the_label_and_a_claim_that_loses_it_counts_again(
    engine: Engine, github: FakeGitHub, clock: Clock
) -> None:
    for number in (1, 2, 3):
        github.add_issue(number, labels=READY)
    github.add_issue(4, labels=WORKFLOW)
    take(engine, github, 4)
    for number in (1, 2, 3):
        clock.advance(minutes=1)
        take(engine, github, number)
    assert held_by(github, (1, 2, 3, 4)) == [1, 2, 3, 4]
    # A maintainer takes the label off: the next pass makes the claim count, and it is the oldest of four.
    github.items[4].view = replace(github.view(4), labels=tuple(x for x in github.view(4).labels if x != "workflow"))
    clock.advance(hours=1)
    make_engine(github, clock).sweep()
    assert not card_of(github, 4).exempt
    # Ordered by the first claim, #3 is the late one and goes back.
    assert held_by(github, (1, 2, 3, 4)) == [1, 2, 4]
    # The label goes back on: the flag follows on the next pass.
    github.items[4].view = replace(github.view(4), labels=(*github.view(4).labels, "workflow"))
    make_engine(github, clock).reconcile(4)
    assert card_of(github, 4).exempt


def test_the_rules_leave_exempt_cards_out_of_the_count() -> None:
    def card(issue: int, exempt: bool) -> model.Card:
        return model.Card(issue=issue, claimant="Ryubara", claimed_at=START, exempt=exempt)

    held = [card(1, True), card(2, True), card(3, False)]
    assert open_task_keys(held, "Ryubara", 0) == {"issue-3"}
    late = model.Card(issue=9, claimant="Ryubara", claimed_at=START.replace(hour=13), exempt=True)
    many = [card(n, False) for n in range(1, 4)]
    assert overrun(late, [*many, late], True, RULES) is None
    assert overrun(replace(late, exempt=False), [*many, replace(late, exempt=False)], True, RULES) is not None


def test_an_old_card_without_the_field_reads_as_counted() -> None:
    text = model.render_card(model.Card(issue=1, claimant="a", claimed_at=START), RULES)
    old = text.replace(',"exempt":false', "")
    assert '"exempt"' not in old
    parsed = model.parse_card(old, RULES)
    assert parsed is not None and not parsed.exempt

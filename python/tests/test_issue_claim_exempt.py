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


def test_the_cap_of_a_task_holds_for_an_exempt_claim_in_the_recount() -> None:
    def card(issue: int, minute: int) -> model.Card:
        return model.Card(
            issue=issue, claimant="Ryubara", claimed_at=START.replace(minute=minute), task="big", exempt=True
        )

    held = [card(n, n) for n in range(1, 5)]
    assert [overrun(c, held, True, RULES) is None for c in held] == [True, True, True, False]


def test_claim_list_json_says_whether_a_claim_is_exempt(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    import json

    from conventions_support import REPO

    from coney_tools import issue_claim_cli as cli

    monkeypatch.chdir(REPO)
    item = {
        "id": 9,
        "issue_url": "https://api.invalid/repos/o/r/issues/7",
        "user": {"login": RULES.bot_login, "type": "Bot"},
        "body": model.render_card(model.Card(issue=7, claimant="a", claimed_at=START, exempt=True), RULES),
        "created_at": "2026-10-22T12:00:00Z",
    }
    import argparse

    parser = argparse.ArgumentParser()
    cli.add_claim_parser(parser.add_subparsers(dest="group"))
    args = parser.parse_args(["claim", "list", "--json"])
    assert cli.run_claim(args, lambda arguments: json.dumps(item)) == 0
    assert json.loads(capsys.readouterr().out)[0]["exempt"] is True


# --- the Studio label (`claims.studio_label`, maintainer decision 2026-10-28) -----------------------------------------

assert RULES.studio_label is not None
STUDIO_LABEL: str = RULES.studio_label
STUDIO = (*READY, STUDIO_LABEL)


def test_the_studio_label_is_in_the_conventions_and_the_label_list() -> None:
    from audit_support import CONVENTIONS

    assert RULES.studio_label == "studio" and RULES.studio_label in CONVENTIONS.labels.names()


def test_a_studio_claim_is_outside_the_limit_and_the_card_says_studio(engine: Engine, github: FakeGitHub) -> None:
    for number in (1, 2, 3):
        github.add_issue(number, labels=READY)
    for number in (5, 6, 7, 8):
        github.add_issue(number, labels=STUDIO)
    for number in (1, 2, 3, 5, 6, 7, 8):
        take(engine, github, number)
    assert held_by(github, (1, 2, 3, 5, 6, 7, 8)) == [1, 2, 3, 5, 6, 7, 8]
    card = card_of(github, 5)
    assert card.exempt and card.studio
    assert "not counted toward the open-claims limit (`studio`)" in github.cards(5)[-1].body
    workflow = card_of(github, 1)
    assert not workflow.studio and not workflow.exempt


def test_a_workflow_claim_is_not_studio(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(4, labels=WORKFLOW)
    take(engine, github, 4)
    card = card_of(github, 4)
    assert card.exempt and card.workflow and card.in_workflow and not card.studio
    assert "(`workflow`)" in github.cards(4)[-1].body


def test_a_claim_of_an_issue_with_both_labels_says_workflow_and_studio(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(4, labels=(*WORKFLOW, STUDIO_LABEL))
    take(engine, github, 4)
    card = card_of(github, 4)
    assert card.exempt and card.workflow and card.studio and card.in_workflow


def test_an_old_card_without_the_workflow_field_reads_from_exempt() -> None:
    text = model.render_card(model.Card(issue=1, claimant="a", claimed_at=START, exempt=True, workflow=True), RULES)
    old = text.replace(',"workflow":true', "")
    assert '"workflow"' not in old
    parsed = model.parse_card(old, RULES)
    assert parsed is not None and not parsed.workflow and parsed.in_workflow
    studio = model.Card(issue=2, claimant="a", claimed_at=START, exempt=True, studio=True)
    assert not studio.in_workflow


def test_the_studio_flag_follows_the_label(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.add_issue(4, labels=READY)
    take(engine, github, 4)
    assert not card_of(github, 4).studio
    github.items[4].view = replace(github.view(4), labels=(*github.view(4).labels, STUDIO_LABEL))
    clock.advance(minutes=5)
    make_engine(github, clock).reconcile(4)
    assert card_of(github, 4).studio and card_of(github, 4).exempt
    github.items[4].view = replace(github.view(4), labels=tuple(x for x in github.view(4).labels if x != "studio"))
    clock.advance(minutes=5)
    make_engine(github, clock).reconcile(4)
    assert not card_of(github, 4).studio and not card_of(github, 4).exempt


def test_an_old_card_without_the_studio_field_reads_as_not_studio() -> None:
    text = model.render_card(model.Card(issue=1, claimant="a", claimed_at=START), RULES)
    old = text.replace(',"studio":false', "")
    assert '"studio"' not in old
    parsed = model.parse_card(old, RULES)
    assert parsed is not None and not parsed.studio


def test_the_rules_name_both_labels_outside_the_limit() -> None:
    assert RULES.outside_limit(("task", "workflow")) and RULES.outside_limit(("studio",))
    assert not RULES.outside_limit(("task", "P-high"))
    assert RULES.is_studio(("studio",)) and not RULES.is_studio(("workflow",))
    assert RULES.is_workflow(("workflow", "studio")) and not RULES.is_workflow(("studio",))


def test_a_file_without_the_studio_label_has_no_studio_work() -> None:
    # A default branch from before the Studio track has no `claims.studio_label`; the release commands read its claims
    # (`load_release_rules_at`), so the key is optional and its absence means no issue is Studio work.
    from conventions_support import REMOVE, conventions_with

    claims = conventions_with(("claims.studio_label", REMOVE)).claims
    assert claims.studio_label is None
    assert not claims.is_studio(("studio",)) and not claims.outside_limit(("studio",))
    assert claims.outside_limit(("workflow",))


# --- the networking label (`claims.networking_label`, maintainer decision 2026-10-29) ---------------------------------

assert RULES.networking_label is not None
NETWORKING_LABEL: str = RULES.networking_label
NETWORKING = (*READY, NETWORKING_LABEL)


def test_the_networking_label_is_in_the_conventions_and_the_label_list() -> None:
    from audit_support import CONVENTIONS

    assert RULES.networking_label == "networking" and RULES.networking_label in CONVENTIONS.labels.names()


def test_a_networking_claim_is_accepted_beside_three_counted_ones(engine: Engine, github: FakeGitHub) -> None:
    # The issue's test: the maintainer's limit of 3 is used up, and a claim of a networking issue still goes through.
    for number in (1, 2, 3):
        github.add_issue(number, labels=READY)
    for number in (5, 6):
        github.add_issue(number, labels=NETWORKING)
    for number in (1, 2, 3, 5, 6):
        take(engine, github, number)
    assert held_by(github, (1, 2, 3, 5, 6)) == [1, 2, 3, 5, 6]
    card = card_of(github, 5)
    assert card.exempt and card.networking and not card.studio and not card.in_workflow
    assert "not counted toward the open-claims limit (`networking`)" in github.cards(5)[-1].body
    counted = card_of(github, 1)
    assert not counted.networking and not counted.exempt


def test_a_fourth_counted_claim_is_still_refused_beside_a_networking_one(engine: Engine, github: FakeGitHub) -> None:
    github.add_issue(5, labels=NETWORKING)
    for number in (1, 2, 3, 4):
        github.add_issue(number, labels=READY)
    for number in (5, 1, 2, 3, 4):
        take(engine, github, number)
    assert held_by(github, (1, 2, 3, 4, 5)) == [1, 2, 3, 5]


def test_the_networking_flag_follows_the_label(engine: Engine, github: FakeGitHub, clock: Clock) -> None:
    github.add_issue(4, labels=READY)
    take(engine, github, 4)
    assert not card_of(github, 4).networking
    github.items[4].view = replace(github.view(4), labels=(*github.view(4).labels, NETWORKING_LABEL))
    clock.advance(minutes=5)
    make_engine(github, clock).reconcile(4)
    assert card_of(github, 4).networking and card_of(github, 4).exempt
    github.items[4].view = replace(github.view(4), labels=tuple(x for x in github.view(4).labels if x != "networking"))
    clock.advance(minutes=5)
    make_engine(github, clock).reconcile(4)
    assert not card_of(github, 4).networking and not card_of(github, 4).exempt


def test_a_card_that_is_exempt_for_networking_alone_is_not_a_workflow_card() -> None:
    card = model.Card(issue=2, claimant="a", claimed_at=START, exempt=True, networking=True)
    assert not card.in_workflow
    assert card.limit_label(RULES) == "networking"


def test_an_old_card_without_the_networking_field_reads_as_not_networking() -> None:
    text = model.render_card(model.Card(issue=1, claimant="a", claimed_at=START), RULES)
    old = text.replace(',"networking":false', "")
    assert '"networking"' not in old
    parsed = model.parse_card(old, RULES)
    assert parsed is not None and not parsed.networking


def test_the_rules_name_the_networking_label_outside_the_limit() -> None:
    assert RULES.outside_limit(("networking",)) and RULES.is_networking(("task", "networking"))
    assert not RULES.is_networking(("studio", "workflow"))


def test_a_file_without_the_networking_label_has_no_networking_work() -> None:
    # A default branch from before the networking track has no `claims.networking_label` (the release commands read its
    # claims): the key is optional and its absence means no issue is networking work.
    from conventions_support import REMOVE, conventions_with

    claims = conventions_with(("claims.networking_label", REMOVE)).claims
    assert claims.networking_label is None
    assert not claims.is_networking(("networking",)) and not claims.outside_limit(("networking",))
    assert claims.outside_limit(("studio",))

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `claim unclaim` and `claim renew` (coney_tools.issue_claim_post): they post the command for the signed-in
account, the way `task create` posts `/claim`, and wait for the claim Action's answer.

The Action is the real engine over the in-memory GitHub of claim_support; the fake `sleep` runs it, as the workflow
would run while the command waits, so a test sees the whole round trip without a network or a real wait.
"""

import argparse
import json
from datetime import timedelta
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from claim_support import RULES, Clock, FakeGitHub, make_engine
from conventions_support import REPO

from coney_tools import issue_claim_cli as cli
from coney_tools import issue_claim_model as model
from coney_tools.issue_claim_post import PostEnv, post_claim_command


class World:
    """The fake GitHub, the clock and a claim Action that runs whenever the command sleeps (unless `stalled`)."""

    def __init__(self) -> None:
        self.clock = Clock()
        self.github = FakeGitHub(self.clock)
        self.lines: list[str] = []
        self.stalled = False

    def sleep(self, seconds: float) -> None:
        """Time passes, and the Action handles what was posted (it runs for every comment)."""
        self.clock.sleep(seconds)
        if not self.stalled:
            for number in list(self.github.items):
                make_engine(self.github, self.clock).reconcile(number)

    def env(self) -> PostEnv:
        return PostEnv(self.github, CONVENTIONS, self.clock.now, self.sleep, self.lines.append)

    def claim(self, number: int, login: str) -> None:
        """`login` claims `number` through the Action."""
        self.github.add_issue(number)
        self.github.say(number, login, "/claim", "OWNER" if login == "Ryubara" else "NONE")
        make_engine(self.github, self.clock).reconcile(number)
        assert self.github.view(number).assignees == (login,)


def card_of(github: FakeGitHub, number: int) -> model.Card:
    card = model.parse_card(github.cards(number)[-1].body, RULES)
    assert card is not None
    return card


def test_unclaim_posts_the_command_and_waits_for_the_release() -> None:
    world = World()
    world.claim(10, "Ryubara")
    assert post_claim_command("unclaim", [10], world.env()) == 0
    assert world.lines == ["#10: /unclaim done"]
    view = world.github.view(10)
    assert view.assignees == () and RULES.released_label in view.labels
    assert card_of(world.github, 10).status == "released"
    # The command comment was the signed-in account's, and the Action deleted it after acting.
    assert not any(c.body == "/unclaim" for c in world.github.comments(10))


def test_a_maintainer_gives_back_someone_elses_claim() -> None:
    world = World()
    world.claim(10, "alice")
    assert post_claim_command("unclaim", [10], world.env()) == 0
    assert card_of(world.github, 10).released_why == "unclaimed by Ryubara"


def test_a_contributor_cannot_give_back_someone_elses_claim_and_nothing_is_posted() -> None:
    world = World()
    world.claim(10, "alice")
    world.github.actor, world.github.association_of_actor = "bob", "NONE"
    assert post_claim_command("unclaim", [10], world.env()) == 1
    assert world.lines == [
        "refused: #10 is claimed by alice: only alice or a maintainer can give this claim up; nothing posted"
    ]
    assert not any(c.login == "bob" for c in world.github.comments(10))


def test_an_issue_without_a_claim_gets_nothing_posted() -> None:
    world = World()
    world.github.add_issue(10)
    assert post_claim_command("unclaim", [10], world.env()) == 1
    assert world.lines == ["refused: #10 has no claim to give up; nothing posted"]
    assert world.github.comments(10) == []


def test_renew_restarts_the_clock_and_only_the_claimant_may() -> None:
    world = World()
    world.claim(10, "Ryubara")
    world.clock.advance(hours=30)
    assert post_claim_command("renew", [10], world.env()) == 0
    card = card_of(world.github, 10)
    assert card.last_activity_why == "renew" and card.activity - card.claimed_at >= timedelta(hours=30)
    assert world.github.view(10).assignees == ("Ryubara",)
    world.claim(11, "alice")
    assert post_claim_command("renew", [11], world.env()) == 1
    assert "only the claimant can renew a claim" in world.lines[-1]


def test_several_issues_are_handled_in_turn_and_one_failure_is_exit_one() -> None:
    world = World()
    world.claim(10, "Ryubara")
    world.github.add_issue(11)
    world.claim(12, "Ryubara")
    assert post_claim_command("unclaim", [10, 11, 12], world.env()) == 1
    assert world.lines[0] == "#10: /unclaim done" and world.lines[2] == "#12: /unclaim done"
    assert world.github.view(10).assignees == () and world.github.view(12).assignees == ()


def test_a_wait_that_runs_out_stops_and_names_the_issues_not_tried() -> None:
    world = World()
    for number in (10, 11, 12):
        world.claim(number, "Ryubara")
    world.stalled = True
    assert post_claim_command("unclaim", [10, 11, 12], world.env()) == 1
    assert world.lines[-1].startswith("not tried (the Action is slow): #11, #12")
    assert not any(c.body == "/unclaim" for n in (11, 12) for c in world.github.comments(n))


def test_no_answer_in_time_says_so_and_the_command_stays_for_the_sweep() -> None:
    world = World()
    world.claim(10, "Ryubara")
    world.stalled = True
    assert post_claim_command("unclaim", [10], world.env()) == 1
    assert f"no answer came within {RULES.create_wait_seconds} s" in world.lines[-1]
    assert any(c.body == "/unclaim" for c in world.github.comments(10))
    # The next run acts on it.
    make_engine(world.github, world.clock).reconcile(10)
    assert world.github.view(10).assignees == ()


def test_a_refusal_reply_is_reported() -> None:
    world = World()
    world.claim(10, "Ryubara")
    # Someone else gives it back between the check and the Action's run: the Action then refuses ours.
    original = world.github.post_command

    def racing(number: int, body: str) -> int:
        world.github.say(number, "Ryubara", "/unclaim", "OWNER")
        make_engine(world.github, world.clock).reconcile(number)
        return original(number, body)

    world.github.post_command = racing  # type: ignore[method-assign]
    assert post_claim_command("unclaim", [10], world.env()) == 1
    assert (
        world.lines[-1] == "#10: refused by the claim Action: Not done for @Ryubara: this issue has no claim to give up"
    )


def test_no_wait_posts_and_returns() -> None:
    world = World()
    world.claim(10, "Ryubara")
    world.stalled = True
    assert post_claim_command("unclaim", [10], world.env(), wait=False) == 0
    assert world.lines[-1].startswith("#10: posted /unclaim (comment ")


# --- the command line -----------------------------------------------------------------------------------------------


def parse(*words: str) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    cli.add_claim_parser(parser.add_subparsers(dest="group"))
    return parser.parse_args(["claim", *words])


def test_the_commands_post_through_gh_issue_comment(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(REPO)
    calls: list[list[str]] = []

    def gh(arguments: list[str]) -> str:
        calls.append(arguments)
        if arguments[:2] == ["api", "user"]:
            return "Ryubara\n"
        if arguments[:2] == ["issue", "comment"]:
            return "https://github.invalid/o/r/issues/10#issuecomment-555\n"
        if arguments[1].endswith("/comments"):
            card = model.Card(issue=10, claimant="Ryubara", claimed_at=model.parse_time("2026-10-27T10:00:00Z"))
            item = {
                "id": 9,
                "user": {"login": RULES.bot_login, "type": "Bot"},
                "author_association": "NONE",
                "body": model.render_card(card, RULES),
                "created_at": "2026-10-27T10:00:00Z",
            }
            return json.dumps(item)
        if arguments[1].endswith("/issues/10"):
            return json.dumps({"state": "open", "labels": [], "assignees": [{"login": "Ryubara"}], "body": ""})
        return ""

    assert cli.run_claim(parse("unclaim", "10", "--no-wait"), gh) == 0
    assert ["issue", "comment", "10", "--body", "/unclaim"] in calls
    calls.clear()
    assert cli.run_claim(parse("renew", "10", "--no-wait"), gh) == 0
    assert ["issue", "comment", "10", "--body", "/claim renew"] in calls


def test_the_posted_texts_are_commands_of_the_grammar() -> None:
    from coney_tools.issue_claim_post import COMMANDS

    kinds = {text: model.parse_command(text, RULES) for text in COMMANDS.values()}
    assert {text: getattr(found, "kind", None) for text, found in kinds.items()} == {
        "/unclaim": "unclaim",
        "/claim renew": "renew",
    }


def test_only_the_coordinator_may_run_them() -> None:
    text = (Path(REPO) / ".claude/hooks/hooks.toml").read_text(encoding="utf-8")
    import tomllib

    roles = tomllib.loads(text)["roles"]
    for role in ("engineer", "tooling", "reviewer"):
        assert {"claim unclaim", "claim renew"} <= set(roles[role]["deny_coney_tools"])
    assert "claim unclaim" not in roles["coordinator"].get("deny_coney_tools", [])

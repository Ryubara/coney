# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `task create` (task_create, task_cli): an in-memory GitHub, a fake Action and a clock nobody waits on.

The fake Action is the real engine, run from the fake sleep once `ACTION_DELAY` seconds have passed since a command was
posted, which is how the real one behaves: the claim card appears a little after the comment.
"""

from datetime import timedelta
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from claim_support import BOT, START, Clock, FakeGitHub, make_engine
from conventions_support import REPO

from coney_tools import issue_claim_model as model
from coney_tools import task_cli
from coney_tools.config import ConfigError
from coney_tools.task_create import CreateEnv, CreateRequest, create_task, default_kind
from coney_tools.task_file import parse_task_file
from coney_tools.task_leads import LeadsResult, Match
from coney_tools.task_leads_index import Entry

RULES = CONVENTIONS.claims
ACTION_DELAY = 20.0
BODY = (
    '### Summary\n\nThe fight goal never re-targets.\n\n### Area\n\nA-ai\n\n### What "done" means\n\n'
    "A test fails before and passes after.\n\n### Blocked by\n\n#7\n\n### Screenshots\n\nScreenshots: none (AI)\n"
)


class World:
    """GitHub, a clock, a fake Action and the scratch folder of one test."""

    def __init__(self, scratch: Path) -> None:
        self.clock = Clock()
        self.github = FakeGitHub(self.clock)
        self.github.add_issue(10, body=BODY, title="gameplay: Fix the fight goal")
        self.github.add_issue(7, state="closed")
        self.scratch = scratch
        self.engine = make_engine(self.github, self.clock)
        self.action_runs = True
        self.sleeps: list[float] = []
        self.hooks: list[object] = []
        self.output: list[str] = []
        self.leads_result = LeadsResult()
        self.leads_calls: list[tuple[list[str], str]] = []
        self.posted: list[tuple[int, str]] = []
        original = self.github.post_command

        def record(number: int, body: str) -> int:
            self.posted.append((number, body))
            return original(number, body)

        self.github.post_command = record  # type: ignore[method-assign]

    def sleep(self, seconds: float) -> None:
        """Move the clock; run the Action when a command has waited long enough; run test hooks."""
        self.sleeps.append(seconds)
        self.clock.sleep(seconds)
        if self.action_runs:
            for number in self.github.items:
                waiting = [
                    c
                    for c in self.github.comment_list.get(number, [])
                    if c.user_type != "Bot"
                    and model.command_shaped(c.body)
                    and (self.clock.now() - c.created_at).total_seconds() >= ACTION_DELAY
                ]
                if waiting:
                    self.engine.reconcile(number)
        for hook in list(self.hooks):
            hook()  # type: ignore[operator]

    def leads(self, terms: list[str], text: str) -> LeadsResult:
        """The leads search, recorded."""
        self.leads_calls.append((terms, text))
        return self.leads_result

    def env(self) -> CreateEnv:
        """The environment of `create_task`."""
        return CreateEnv(
            self.github, CONVENTIONS, self.scratch, self.clock.now, self.sleep, self.leads, self.output.append
        )

    def run(self, **request: object) -> int:
        """Run `create_task` with a request."""
        request.setdefault("issues", (10,))
        return create_task(CreateRequest(**request), self.env())  # type: ignore[arg-type]

    def said(self) -> str:
        """Everything printed."""
        return "\n".join(self.output)

    def actor_comments(self, number: int) -> list[str]:
        """What the signed-in person posted on an issue."""
        return [c.body for c in self.github.comment_list[number] if c.login == self.github.actor]

    def task_path(self, name: str) -> Path:
        """Where the task file of `name` goes."""
        return self.scratch / name / CONVENTIONS.tasks.file


@pytest.fixture
def world(tmp_path: Path) -> World:
    return World(tmp_path / "scratch")


# --- the claim, then the file ---------------------------------------------------------------------------------------


def test_the_claim_comes_first_then_the_settle_then_the_file(world: World) -> None:
    assert world.run(scope=("src/ai/", "0x00322b78"), terms=("fight",)) == 0
    # The one /claim comment carries the scope and the task id in the grammar's exact shape.
    number, body = world.posted[0]
    assert (number, body) == (10, "/claim scope: src/ai/, 0x00322b78 task: 10-fix-the-fight-goal")
    assert isinstance(model.parse_command(body, RULES), model.Command)
    assert world.posted[-1] == (10, "/progress assigned")
    assert world.github.view(10).assignees == ("Ryubara",)
    # It waited for the card, then settled a full minute before writing.
    assert sum(world.sleeps) >= ACTION_DELAY + RULES.settle_seconds
    path = world.task_path("10-fix-the-fight-goal")
    parsed = parse_task_file(path.read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert (parsed.issues, parsed.kind, parsed.claimant, parsed.scope) == (
        (10,),
        "feature",
        "Ryubara",
        ("src/ai/", "0x00322b78"),
    )
    assert parsed.claimed_at == START
    assert parsed.sections["Goal"] == "> The fight goal never re-targets."
    assert parsed.sections["Done"] == "> A test fails before and passes after."
    assert "`src/ai/`" in parsed.sections["Scope"] and "`0x00322b78`" in parsed.sections["Scope"]
    assert "Blocked by: #7" in parsed.sections["Issues"]
    assert world.leads_calls[0][0] == ["fight"] and "never re-targets" in world.leads_calls[0][1]


def test_the_file_is_written_only_after_the_settle(world: World) -> None:
    seen: list[bool] = []

    def watch() -> None:
        seen.append(world.task_path("10-fix-the-fight-goal").exists())

    world.hooks.append(watch)
    assert world.run() == 0
    assert seen and not any(seen)  # never present while it was still waiting or settling


def test_leads_and_unmatched_terms_go_into_the_file(world: World) -> None:
    entry = Entry("orig", "FightGoal_Retarget", "src/ai/fight.cpp:40", ("0x00322b78",), (), "@orig 0x00322b78")
    world.leads_result = LeadsResult(
        ranked=[Match(entry, "rare", 9.0, 'term "retarget" matches the name part "retarget" (rare)')],
        unmatched=["zebra"],
    )
    assert world.run(terms=("retarget", "zebra")) == 0
    parsed = parse_task_file(world.task_path("10-fix-the-fight-goal").read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert "| orig | `FightGoal_Retarget` at src/ai/fight.cpp:40" in parsed.sections["Where to look"]
    assert 'matches the name part "retarget" (rare)' in parsed.sections["Where to look"]
    assert parsed.sections["Searched, not related"] == "No match for: `zebra`."
    assert parsed.terms == ("retarget", "zebra")


def test_a_docs_task_has_no_failing_test(world: World) -> None:
    world.github.items[10].view = world.github.view(10).__class__(
        **{**world.github.view(10).__dict__, "labels": ("docs", "S-ready-to-start")}
    )
    assert world.run() == 0
    parsed = parse_task_file(world.task_path("10-fix-the-fight-goal").read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert parsed.kind == "docs" and parsed.sections["Expected tests"].startswith("no fails-first test")


def test_the_default_kind_follows_the_labels(world: World) -> None:
    view = world.github.view(10)
    assert default_kind(view) == "feature"
    assert default_kind(view.__class__(**{**view.__dict__, "labels": ("A-tools",)})) == "tooling"


def test_the_task_file_says_workflow_when_the_issue_has_the_exempt_label(world: World) -> None:
    world.github.add_issue(10, labels=("task", "A-tools", "S-ready-to-start", CONVENTIONS.claims.exempt_label))
    assert world.run() == 0
    path = next(world.scratch.glob("*/TASK.md"))
    assert parse_task_file(path.read_text(encoding="utf-8"), CONVENTIONS.tasks).workflow is True


def test_the_task_file_of_an_ordinary_issue_is_not_a_workflow_task(world: World) -> None:
    assert world.run() == 0
    path = next(world.scratch.glob("*/TASK.md"))
    text = path.read_text(encoding="utf-8")
    assert parse_task_file(text, CONVENTIONS.tasks).workflow is False and "workflow:" not in text


# --- several issues, one task -----------------------------------------------------------------------------------------


def test_issues_of_one_task_share_its_id_and_count_once(world: World) -> None:
    world.github.add_issue(11)
    world.github.add_issue(12)
    world.github.add_issue(13)
    assert world.run(issues=(10, 11, 12), name="big-task") == 0
    assert all(world.github.view(n).assignees == ("Ryubara",) for n in (10, 11, 12))
    assert {model.parse_card(c.body, RULES).task for n in (10, 11, 12) for c in world.github.cards(n)} == {"big-task"}  # type: ignore[union-attr]
    parsed = parse_task_file(world.task_path("big-task").read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert parsed.issues == (10, 11, 12)
    # Three issues of one task are one claim, so a second task is still within the limit of three.
    world.github.add_issue(14)
    world.github.add_issue(15)
    assert world.run(issues=(13,), name="second") == 0
    assert world.run(issues=(14,), name="third") == 0
    assert world.run(issues=(15,), name="fourth") == 1
    assert "3 open claim(s) (3 at most for a maintainer)" in world.said()


# --- refusals: nothing is written -------------------------------------------------------------------------------------


def test_a_claim_that_cannot_succeed_is_refused_before_anything_is_posted(world: World) -> None:
    world.github.add_issue(11, labels=("task", "S-needs-triage"))
    assert world.run(issues=(11,)) == 1
    assert "only an issue labelled `S-ready-to-start` can be claimed" in world.said()
    assert world.github.comment_list[11] == [] and not (world.scratch).exists()


def test_a_p_critical_issue_comes_first_here_too(world: World) -> None:
    world.github.add_issue(20, labels=("bug", "P-critical", "S-ready-to-start"))
    assert world.run() == 1 and "`P-critical` comes first: #20" in world.said()


def test_a_blocker_stops_the_task(world: World) -> None:
    world.github.add_issue(7, state="open")
    assert world.run() == 1 and "blocked by #7 (open)" in world.said()


def test_a_closed_issue_or_a_pull_request_is_refused(world: World) -> None:
    world.github.add_pr(40)
    assert world.run(issues=(40,)) == 1 and world.run(issues=(7,)) == 1


def test_a_race_lost_to_someone_else_writes_nothing(world: World) -> None:
    """Someone else's /claim lands between the check and the post: theirs is first, ours is refused."""
    original = world.github.post_command

    def intruder(number: int, body: str) -> int:
        world.github.say(number, "alice", "/claim")
        return original(number, body)

    world.github.post_command = intruder  # type: ignore[method-assign]
    assert world.run() == 1
    assert "already claimed by alice" in world.said() and not world.scratch.exists()
    assert world.github.view(10).assignees == ("alice",)


def test_a_claim_that_never_gets_a_card_times_out_and_writes_nothing(world: World) -> None:
    world.action_runs = False
    assert world.run() == 1
    assert "no card for @Ryubara on #10 after 300 s" in world.said() and "--resume" in world.said()
    assert not world.scratch.exists()
    assert RULES.create_wait_seconds <= sum(world.sleeps) <= RULES.create_wait_seconds + RULES.poll_interval_seconds


def test_a_change_during_the_settle_stops_it_with_nothing_written(world: World) -> None:
    def intrude() -> None:
        if world.github.cards(10) and "bob" not in world.github.view(10).assignees:
            world.github.assign(10, "bob")

    world.action_runs = False
    # The card appears at the first sleep; the intruder arrives at the next.
    world.hooks.append(lambda: world.engine.reconcile(10) and None)
    world.hooks.append(intrude)
    assert world.run() == 1
    assert "the assignees are now Ryubara, bob" in world.said() and "nothing was written" in world.said()
    assert not world.scratch.exists()


def test_losing_the_claim_during_the_settle_stops_it(world: World) -> None:
    def take_away() -> None:
        if (
            world.github.cards(10)
            and world.github.view(10).assignees
            and world.clock.now() >= START + timedelta(seconds=80)
        ):
            world.github.unassign(10, ("Ryubara",))
            world.engine.reconcile(10)

    world.hooks.append(take_away)
    assert world.run() == 1
    assert "the card no longer names @Ryubara" in world.said() and not world.scratch.exists()


def test_an_earlier_claim_still_waiting_stops_the_settle(world: World) -> None:
    def late() -> None:
        card = world.github.cards(10)
        if card and not any(c.login == "carol" for c in world.github.comment_list[10]):
            world.github.comment_list[10].append(
                type(card[0])(9999, 10, "carol", "User", "NONE", "/claim", START - timedelta(seconds=5))
            )

    world.hooks.append(late)
    assert world.run() == 1 and "an earlier /claim by @carol is still waiting" in world.said()


# --- resume, fix, dry run, names --------------------------------------------------------------------------------------


def test_resume_continues_a_claim_that_landed_late_without_posting_again(world: World) -> None:
    world.action_runs = False
    assert world.run() == 1
    world.action_runs = True
    world.engine.reconcile(10)  # the claim lands after all
    world.output.clear()
    assert world.run() == 1 and "you hold #10 already" in world.said()
    posted = len(world.posted)
    assert world.run(resume=True) == 0
    assert [body for _, body in world.posted[posted:]] == ["/progress assigned"]
    assert world.task_path("10-fix-the-fight-goal").is_file()


def test_a_fix_task_reuses_the_claim_and_posts_progress_fix(world: World) -> None:
    assert world.run() == 0
    world.output.clear()
    before = len(world.github.comment_list[10])
    assert world.run(issues=(), fix_of=10, terms=("re-target",)) == 0
    assert world.actor_comments(10)[-1] == "/progress fix"
    assert [c.body for c in world.github.comment_list[10][before:] if c.login == "Ryubara"] == ["/progress fix"]
    parsed = parse_task_file(world.task_path("10-fix").read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert parsed.kind == "fix" and parsed.issues == (10,)
    # The fix names the task of the claim it reuses, so `task start` opens that task's unit (stamps.md#api).
    assert parsed.parent == "10-fix-the-fight-goal"
    assert world.run(issues=(), fix_of=10) == 0 and world.task_path("10-fix-2").is_file()


def test_a_fix_of_a_claim_made_without_a_task_has_no_unit_to_fix(world: World) -> None:
    world.github.say(10, "Ryubara", "/claim", "OWNER")
    world.engine.reconcile(10)
    assert world.run(issues=(), fix_of=10) == 1
    assert "was made without a task id" in world.said() and not world.task_path("10-fix").exists()


def test_a_fix_task_needs_your_own_claim(world: World) -> None:
    assert world.run(issues=(), fix_of=10) == 1 and "needs your own held claim on #10" in world.said()
    world.output.clear()
    assert world.run(kind="fix") == 1 and "--fix-of" in world.said()
    world.output.clear()
    assert world.run(fix_of=10, kind="feature") == 1


def test_a_dry_run_posts_and_writes_nothing(world: World) -> None:
    assert world.run(dry_run=True, scope=("src/ai/",)) == 0
    assert world.github.comment_list[10] == [] and not world.scratch.exists()
    assert "dry run: would post on #10: /claim scope: src/ai/ task: 10-fix-the-fight-goal" in world.said()
    assert "## Where to look" in world.said()


def test_a_name_must_be_a_task_id_and_a_task_file_is_never_overwritten(world: World) -> None:
    assert world.run(name="Bad Name") == 1 and "is not a task id" in world.said()
    assert world.run(name="mine") == 0
    world.output.clear()
    world.github.add_issue(11)
    assert world.run(issues=(11,), name="mine") == 1 and "exists already" in world.said()


def test_an_unknown_kind_is_refused(world: World) -> None:
    assert world.run(kind="epic") == 1 and "not one of" in world.said()


def test_scopes_of_other_claims_that_overlap_are_a_warning_not_a_refusal(world: World) -> None:
    world.github.add_issue(11)
    world.github.say(11, "alice", "/claim scope: src/ai/goals.cpp, 0x00111111")
    world.engine.reconcile(11)
    assert world.run(scope=("src/ai/", "0x00222222")) == 0
    assert "warning: the scope overlaps #11 (@alice) shares src/ai/" in world.said()
    parsed = parse_task_file(world.task_path("10-fix-the-fight-goal").read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert "Overlap: #11 (@alice) shares src/ai/" in parsed.sections["Scope"]


def test_a_contributor_is_held_to_two_claims(world: World) -> None:
    world.github.permissions["Ryubara"] = "read"
    for number in (11, 12, 13):
        world.github.add_issue(number)
    assert world.run(issues=(10,), name="a") == 0
    assert world.run(issues=(11,), name="b") == 0
    assert world.run(issues=(12,), name="c") == 1 and "2 at most for a contributor" in world.said()
    assert BOT


# --- the command line -------------------------------------------------------------------------------------------------


def parse(*words: str):  # type: ignore[no-untyped-def]
    """Parse a `task` command line."""
    import argparse

    group = argparse.ArgumentParser().add_subparsers(dest="group")
    task_cli.add_task_parser(group)
    return group.choices["task"].parse_args(words)


def test_create_takes_issues_then_options() -> None:
    args = parse("create", "12", "13", "--terms", "fight", "goal", "--scope", "src/ai/", "--name", "t", "--dry-run")
    assert (args.issues, args.terms, args.scope, args.name, args.dry_run) == (
        [12, 13],
        ["fight", "goal"],
        "src/ai/",
        "t",
        True,
    )
    assert parse("create", "--fix-of", "5").fix_of == 5


def test_a_bad_scope_or_no_issue_is_a_config_error(monkeypatch: pytest.MonkeyPatch, world: World) -> None:
    monkeypatch.chdir(REPO)
    with pytest.raises(ConfigError, match="--scope"):
        task_cli.run_task(parse("create", "10", "--scope", "../x"), remote=world.github)
    with pytest.raises(ConfigError, match="at least one issue"):
        task_cli.run_task(parse("create"), remote=world.github)


def test_create_runs_through_the_command(monkeypatch: pytest.MonkeyPatch, world: World) -> None:
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(task_cli, "scratch_folder", lambda root: world.scratch)
    monkeypatch.setattr(task_cli, "_index", lambda *args, **kwargs: type("I", (), {"entries": (), "notices": ()})())
    status = task_cli.run_task(
        parse("create", "10", "--terms", "fight"), now=world.clock.now, sleep=world.sleep, remote=world.github
    )
    assert status == 0 and world.task_path("10-fix-the-fight-goal").is_file()


def test_leads_and_index_commands(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(task_cli, "scratch_folder", lambda root: tmp_path)
    assert task_cli.run_task(parse("index", "--no-gh")) == 0
    assert "entries at" in capsys.readouterr().out
    assert task_cli.run_task(parse("leads", "--terms", "zzzqqqxxx")) == 0
    out = capsys.readouterr().out
    assert "no match for: zzzqqqxxx" in out


# --- the review's fixes -----------------------------------------------------------------------------------------------


def test_a_task_of_more_than_three_issues_is_refused_before_anything_is_posted(world: World) -> None:
    for number in (11, 12, 13):
        world.github.add_issue(number)
    assert world.run(issues=(10, 11, 12, 13)) == 1
    assert "a task holds at most 3 issues, not 4" in world.said() and world.posted == []


def test_a_resumed_claim_must_match_the_task_it_was_made_under(world: World) -> None:
    world.action_runs = False
    assert world.run(name="first-name") == 1
    world.action_runs = True
    world.engine.reconcile(10)
    world.output.clear()
    assert world.run(resume=True, name="other-name") == 1
    assert "you hold #10 under task `first-name`, not `other-name`" in world.said()
    # Without --name the task id comes from the card.
    assert world.run(resume=True) == 0 and world.task_path("first-name").is_file()


def test_when_the_cards_vanish_after_the_settle_it_stops_and_says_how_to_unclaim(
    world: World, monkeypatch: pytest.MonkeyPatch
) -> None:
    import coney_tools.task_create as module

    def settle_then_lose(env: object, numbers: list[int], user: str) -> None:
        world.github.unassign(10, ("Ryubara",))
        world.engine.reconcile(10)

    monkeypatch.setattr(module, "_settle", settle_then_lose)
    assert world.run() == 1
    assert "the claim on #10 is gone; nothing was written" in world.said()
    assert not world.scratch.exists()


def test_a_stopped_settle_says_how_to_give_the_claims_back(world: World) -> None:
    world.github.add_issue(11)

    def intrude() -> None:
        if world.github.cards(11) and "bob" not in world.github.view(11).assignees:
            world.github.assign(11, "bob")

    world.hooks.append(intrude)
    assert world.run(issues=(10, 11), name="pair") == 1
    assert "`coney-tools claim unclaim 10" in world.said() and "#10" in world.said()


def test_issue_text_cannot_break_the_sections_of_the_file(world: World) -> None:
    # A heading inside a code fence is text of the section, so it reaches the file: it must stay inside its quote.
    fenced = "The fight goal never re-targets.\n\n```\n## Done\n\nforged\n## Scope\nforged\n```"
    body = BODY.replace("The fight goal never re-targets.", fenced)
    world.github.items[10].view = world.github.view(10).__class__(**{**world.github.view(10).__dict__, "body": body})
    world.leads_result = LeadsResult(unmatched=["bad`term\nwith lines"])
    assert world.run(terms=("bad`term\nwith lines",)) == 0
    parsed = parse_task_file(world.task_path("10-fix-the-fight-goal").read_text(encoding="utf-8"), CONVENTIONS.tasks)
    assert "> ## Done" in parsed.sections["Goal"] and parsed.sections["Done"].startswith("> A test fails")
    assert parsed.sections["Searched, not related"] == "No match for: `bad'term with lines`."

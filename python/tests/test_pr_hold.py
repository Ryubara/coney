# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools pr hold`: the records that say somebody owns a pull request's problems (pr.md#hold).

A hold is one small file per pull request in the scratch directory. The store is tested on a made-up folder with a fixed
clock; nothing reads GitHub."""

import argparse
import json
from collections.abc import Callable
from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import cli, pr_hold
from coney_tools.conventions import ConclaveWatch, load_conventions
from coney_tools.pr_hold import WAIVED, Hold

NOW = datetime(2026, 10, 25, 12, 0, tzinfo=UTC)
RULES = load_conventions(REPO).conclave.watch


def hold(pr: int = 812, hours: float = 4, **changes: object) -> Hold:
    """A hold made `at` NOW that lasts `hours`."""
    base: dict[str, object] = {
        "pr": pr,
        "who": "engineer-2",
        "cause": None,
        "until": NOW + timedelta(hours=hours),
        "note": "fixing the lint",
        "at": NOW,
    }
    base.update(changes)
    return Hold(**base)  # type: ignore[arg-type]


def args(*words: str) -> argparse.Namespace:
    """The namespace `coney-tools pr hold <words>` parses to."""
    return cli._build_parser().parse_args(["pr", "hold", *words])


class Out:
    """Collects what a command prints on each stream."""

    def __init__(self) -> None:
        self.out: list[str] = []
        self.err: list[str] = []

    def run(
        self,
        scratch: Path,
        *words: str,
        rules: ConclaveWatch = RULES,
        now: datetime = NOW,
        head: Callable[[int], str] = lambda number: "a1b2c3d4e5f6" + "0" * 28,
        conflicting: Callable[[int], bool] = lambda number: False,
        fork: Callable[[int], str | None] = lambda number: None,
    ) -> int:
        return pr_hold.run_hold(
            args(*words),
            scratch,
            rules,
            now,
            self.out.append,
            self.err.append,
            head,
            conflicting=conflicting,
            fork_of=fork,
        )


# --- the store ------------------------------------------------------------------------------------------------------


def test_a_hold_is_written_and_read_back(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold())
    found = pr_hold.read_active(tmp_path, NOW)
    assert found.problems == [] and found.holds == [hold()]


def test_one_file_per_pull_request_and_cause_and_a_later_hold_replaces_the_earlier(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(812))
    pr_hold.write(tmp_path, hold(813))
    pr_hold.write(tmp_path, hold(812, who="engineer-9"))
    assert sorted(path.name for path in tmp_path.iterdir()) == ["812.all.json", "813.all.json"]
    assert [(h.pr, h.who) for h in pr_hold.read_active(tmp_path, NOW).holds] == [
        (812, "engineer-9"),
        (813, "engineer-2"),
    ]


def test_an_expired_hold_is_not_active_and_a_hold_that_ends_now_is_expired(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(812, hours=-1))
    pr_hold.write(tmp_path, hold(813, hours=0))
    pr_hold.write(tmp_path, hold(814, hours=1))
    assert [h.pr for h in pr_hold.read_active(tmp_path, NOW).holds] == [814]


def test_a_missing_folder_holds_nothing(tmp_path: Path) -> None:
    assert pr_hold.read_active(tmp_path / "nope", NOW).holds == []


def test_a_file_that_cannot_be_read_is_reported_and_the_others_stand(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(812))
    (tmp_path / "813.json").write_text("not json", encoding="utf-8")
    (tmp_path / "814.json").write_text(json.dumps({"pr": 814, "who": "x"}), encoding="utf-8")
    (tmp_path / "notes.txt").write_text("ignored", encoding="utf-8")
    found = pr_hold.read_active(tmp_path, NOW)
    assert [h.pr for h in found.holds] == [812]
    assert len(found.problems) == 2 and "813.json" in found.problems[0] and "814.json" in found.problems[1]


def test_a_hold_covers_every_cause_or_only_the_one_it_names() -> None:
    whole = hold()
    one = hold(cause="checks-failed")
    assert whole.covers("conflict", NOW) and whole.covers("checks-failed", NOW)
    assert one.covers("checks-failed", NOW) and not one.covers("conflict", NOW)
    assert not hold(hours=-1).covers("conflict", NOW)


def test_a_hold_covers_only_the_head_it_was_made_on() -> None:
    # a new push is a new problem (pr.md#hold): the hold does not follow it
    on_head = hold(head="a1b2c3d")
    assert on_head.covers("conflict", NOW, "a1b2c3d") and not on_head.covers("conflict", NOW, "e5f6a7b")
    assert not on_head.covers("conflict", NOW, "unknown") and not on_head.covers("conflict", NOW, None)
    # a hold written before heads were recorded keeps covering whatever head the pull request has
    assert hold().covers("conflict", NOW, "e5f6a7b") and hold().covers("conflict", NOW)


def test_the_head_is_written_and_read_back(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(head="a1b2c3d"))
    assert pr_hold.read_active(tmp_path, NOW).holds[0].head == "a1b2c3d"
    assert json.loads((tmp_path / "812.all.json").read_text(encoding="utf-8"))["head"] == "a1b2c3d"


def test_a_waiver_is_a_hold_by_nobody(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(who=WAIVED, note="waiting for the runner pool"))
    assert pr_hold.read_active(tmp_path, NOW).holds[0].waived


def test_release_removes_the_file_and_says_whether_there_was_one(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold())
    assert pr_hold.release(tmp_path, 812) is True
    assert pr_hold.release(tmp_path, 812) is False
    assert list(tmp_path.iterdir()) == []


def test_writing_a_hold_removes_the_expired_ones(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(700, hours=-5))
    pr_hold.write(tmp_path, hold(812), now=NOW)
    assert sorted(path.name for path in tmp_path.iterdir()) == ["812.all.json"]


def test_a_hold_for_one_cause_keeps_the_hold_for_every_cause_and_the_other_causes(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(who=WAIVED, note="waits for the maintainer"))
    pr_hold.write(tmp_path, hold(cause="conflict", note="rebasing"))
    pr_hold.write(tmp_path, hold(cause="auto-merge-off", note="fork"))
    found = pr_hold.read_active(tmp_path, NOW).holds
    assert sorted(held.cause or "" for held in found) == ["", "auto-merge-off", "conflict"]
    # a later hold for the same cause replaces that one only
    pr_hold.write(tmp_path, hold(cause="conflict", who="engineer-9"))
    found = pr_hold.read_active(tmp_path, NOW).holds
    assert len(found) == 3 and {held.who for held in found if held.cause == "conflict"} == {"engineer-9"}
    assert {held.who for held in found if held.cause is None} == {WAIVED}


def test_a_hold_in_the_old_one_file_form_is_read_and_replaced_by_the_same_cause(tmp_path: Path) -> None:
    # before the store kept one hold per cause, a pull request's one hold was `<pr>.json`, whatever its cause
    old = hold(cause="conflict").as_json()
    (tmp_path / "812.json").write_text(json.dumps(old), encoding="utf-8")
    assert [held.cause for held in pr_hold.read_active(tmp_path, NOW).holds] == ["conflict"]
    pr_hold.write(tmp_path, hold())
    assert sorted(held.cause or "" for held in pr_hold.read_active(tmp_path, NOW).holds) == ["", "conflict"]
    pr_hold.write(tmp_path, hold(cause="conflict", who="engineer-9"))
    found = pr_hold.read_active(tmp_path, NOW).holds
    assert sorted(held.cause or "" for held in found) == ["", "conflict"]
    assert not (tmp_path / "812.json").exists()


# --- the command ----------------------------------------------------------------------------------------------------


def test_an_assignment_names_who_and_lasts_the_default_hours(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "812", "--who", "engineer-2") == 0
    [held] = pr_hold.read_active(tmp_path, NOW).holds
    assert held.who == "engineer-2" and held.until == NOW + timedelta(hours=RULES.hold_default_hours)
    assert "#812" in run.out[0] and "engineer-2" in run.out[0] and "2026-10-25T16:00:00Z" in run.out[0]


def test_a_hold_records_the_head_commit_it_was_made_on(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "812", "--who", "engineer-2") == 0
    [held] = pr_hold.read_active(tmp_path, NOW).holds
    assert held.head == "a1b2c3d" and "a1b2c3d" in run.out[0]


def test_a_hold_by_an_agent_on_a_forks_pull_request_says_its_branch_is_no_units(tmp_path: Path) -> None:
    # #1522: Conclave briefed task 285's fixes against a contributor's #1415, whose fork branch had the same name.
    run = Out()
    assert run.run(tmp_path, "1415", "--who", "engineer-2", fork=lambda number: "contributor:285-add-x") == 0
    assert run.err == [
        "pr hold: note: #1415 comes from a fork (contributor:285-add-x), so it is no unit's pull request here, even "
        "when a unit's branch has the same name; engineer-2 fixes the fork's branch, not a unit's"
    ]
    asked: list[int] = []

    def fork(number: int) -> str:
        asked.append(number)
        return "contributor:285-add-x"

    waiver = Out()
    assert waiver.run(tmp_path, "1415", "--note", "the contributor fixes it", fork=fork) == 0
    assert asked == [] and waiver.err == []
    ours = Out()
    assert ours.run(tmp_path, "812", "--who", "engineer-2") == 0 and ours.err == []


def test_a_hold_whose_fork_cannot_be_told_is_made_all_the_same(tmp_path: Path) -> None:
    def broken(number: int) -> str | None:
        raise OSError("gh pr view failed")

    run = Out()
    assert run.run(tmp_path, "812", "--who", "engineer-2", fork=broken) == 0
    assert run.err == []


def test_the_fork_of_a_pull_request_is_read_through_the_one_head_match(monkeypatch: pytest.MonkeyPatch) -> None:
    answers = {
        1415: {"headRefName": "b", "headRepositoryOwner": {"login": "contributor"}, "isCrossRepository": True},
        812: {"headRefName": "b", "headRepositoryOwner": {"login": "acme"}, "isCrossRepository": False},
    }
    asked: list[list[str]] = []

    def gh(arguments: list[str], *rest: object, **more: object) -> str:
        asked.append(arguments)
        return json.dumps(answers[int(arguments[2])])

    monkeypatch.setattr(pr_hold, "run_gh", gh)
    assert pr_hold.fork_of(1415) == "contributor:b"
    assert pr_hold.fork_of(812) is None
    assert asked[0][:4] == ["pr", "view", "1415", "--json"] and "isCrossRepository" in asked[0][4]


def test_a_hold_whose_head_cannot_be_read_is_refused_and_writes_nothing(tmp_path: Path) -> None:
    def broken(number: int) -> str:
        raise OSError("gh pr view failed: not signed in")

    run = Out()
    assert run.run(tmp_path, "812", "--who", "engineer-2", head=broken) == 2
    assert run.err == ["pr hold: cannot read the head commit of #812: gh pr view failed: not signed in"]
    assert list(tmp_path.iterdir()) == []


def test_a_hold_can_name_one_cause_a_time_and_a_note(tmp_path: Path) -> None:
    assert Out().run(tmp_path, "812", "--who", "e", "--hours", "2", "--cause", "conflict", "--note", "rebasing") == 0
    [held] = pr_hold.read_active(tmp_path, NOW).holds
    assert (held.cause, held.note, held.until) == ("conflict", "rebasing", NOW + timedelta(hours=2))


def test_a_hold_for_one_cause_does_not_replace_the_hold_for_every_cause(tmp_path: Path) -> None:
    # the coordinator's case: a conflict waits on the maintainer while auto-merge is off on a fork
    run = Out()
    assert run.run(tmp_path, "812", "--note", "auto-merge is off on a fork, the maintainer merges it") == 0
    assert run.run(tmp_path, "812", "--who", "engineer-2", "--cause", "conflict") == 0
    found = pr_hold.read_active(tmp_path, NOW).holds
    assert [(held.cause, held.who) for held in sorted(found, key=lambda held: held.cause or "")] == [
        (None, WAIVED),
        ("conflict", "engineer-2"),
    ]
    assert all(any(held.covers(cause, NOW, "a1b2c3d") for held in found) for cause in ("auto-merge-off", "conflict"))


def test_a_second_hold_for_the_same_cause_replaces_the_first(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "812", "--who", "engineer-2", "--cause", "conflict") == 0
    assert run.run(tmp_path, "812", "--who", "engineer-3", "--cause", "conflict") == 0
    assert run.run(tmp_path, "812", "--who", "engineer-4") == 0
    assert run.run(tmp_path, "812", "--who", "engineer-5") == 0
    assert sorted((held.cause or "", held.who) for held in pr_hold.read_active(tmp_path, NOW).holds) == [
        ("", "engineer-5"),
        ("conflict", "engineer-3"),
    ]


def test_a_waiver_needs_a_reason(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "812") == 2
    assert "--who" in run.err[0] and "--note" in run.err[0]
    assert Out().run(tmp_path, "812", "--note", "runner pool is down") == 0
    assert pr_hold.read_active(tmp_path, NOW).holds[0].waived


def test_a_hold_longer_than_the_most_is_refused(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "812", "--who", "e", "--hours", str(RULES.hold_max_hours + 1)) == 2
    assert str(RULES.hold_max_hours) in run.err[0] and not list(tmp_path.iterdir())
    assert Out().run(tmp_path, "812", "--who", "e", "--hours", "0") == 2


def test_an_unknown_cause_is_refused_by_the_parser() -> None:
    with pytest.raises(SystemExit):
        args("812", "--who", "e", "--cause", "weather")


def test_list_prints_the_active_holds_and_says_when_there_are_none(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "--list") == 0 and run.out == ["no holds"]
    pr_hold.write(tmp_path, hold(812, cause="checks-failed"))
    pr_hold.write(tmp_path, hold(813, who=WAIVED, note="runner pool"))
    run = Out()
    assert run.run(tmp_path, "--list") == 0
    assert run.out[0].startswith("#812") and "checks-failed" in run.out[0] and "engineer-2" in run.out[0]
    assert run.out[1].startswith("#813") and "waived" in run.out[1]


def test_list_as_json_is_the_holds_with_their_times(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(812))
    run = Out()
    assert run.run(tmp_path, "--list", "--json") == 0
    [row] = json.loads(run.out[0])["holds"]
    assert row["pr"] == 812 and row["until"] == "2026-10-25T16:00:00Z" and row["cause"] is None


def test_release_removes_the_hold_and_a_second_release_says_there_was_none(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold())
    run = Out()
    assert run.run(tmp_path, "812", "--release") == 0 and "released" in run.out[0]
    run = Out()
    assert run.run(tmp_path, "812", "--release") == 1 and "no hold" in run.err[0]


def test_list_prints_each_cause_and_says_whether_the_hold_may_cover_a_conflict(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(who=WAIVED, note="the runner pool is down"))
    pr_hold.write(tmp_path, hold(cause="conflict", who=WAIVED, note="waits for the maintainer's answer on #1130"))
    pr_hold.write(tmp_path, hold(cause="checks-failed"))
    run = Out()
    assert run.run(tmp_path, "--list") == 0 and len(run.out) == 3
    run = Out()
    assert run.run(tmp_path, "--list", "--json") == 0
    rows = {row["cause"]: row for row in json.loads(run.out[0])["holds"]}
    assert set(rows) == {None, "conflict", "checks-failed"}
    # the Stop hook of the Conclave mod trusts this flag instead of knowing the conventions' decision pattern
    assert [rows[cause]["covers_conflict"] for cause in (None, "conflict", "checks-failed")] == [False, True, True]


def test_release_ends_every_hold_of_the_pull_request_or_the_one_cause(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold())
    pr_hold.write(tmp_path, hold(cause="conflict"))
    pr_hold.write(tmp_path, hold(813))
    run = Out()
    assert run.run(tmp_path, "812", "--release", "--cause", "conflict") == 0 and "conflict" in run.out[0]
    assert [(held.pr, held.cause) for held in pr_hold.read_active(tmp_path, NOW).holds] == [(812, None), (813, None)]
    assert run.run(tmp_path, "812", "--release", "--cause", "conflict") == 1
    assert Out().run(tmp_path, "812", "--release") == 0
    assert [held.pr for held in pr_hold.read_active(tmp_path, NOW).holds] == [813]


def test_the_command_needs_a_pull_request_unless_it_lists(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "--who", "e") == 2 and "pull request" in run.err[0]
    assert Out().run(tmp_path, "812", "--list") == 2
    assert Out().run(tmp_path, "812", "--release", "--who", "e") == 2


def test_the_holds_folder_is_in_the_scratch_directory_under_the_conventions_name(tmp_path: Path) -> None:
    assert pr_hold.holds_folder(tmp_path, RULES) == tmp_path / "pr-holds"


# --- a waiver on a conflict -----------------------------------------------------------------------------------------

DECISION = "waits for the maintainer's answer on #1130"


def test_a_waiver_on_a_conflict_needs_a_note_that_names_a_maintainer_decision(tmp_path: Path) -> None:
    out = Out()
    assert out.run(tmp_path, "812", "--cause", "conflict", "--note", "will do it later") == 2
    assert "maintainer decision" in out.err[0] and "--who" in out.err[0]
    assert pr_hold.read_active(tmp_path, NOW).holds == []
    assert out.run(tmp_path, "812", "--cause", "conflict", "--note", DECISION) == 0
    [held] = pr_hold.read_active(tmp_path, NOW).holds
    assert held.waived and held.cause == "conflict" and held.note == DECISION


def test_a_hold_with_who_on_a_conflict_needs_no_decision(tmp_path: Path) -> None:
    assert Out().run(tmp_path, "812", "--cause", "conflict", "--who", "engineer-2", "--note", "rebasing") == 0


def test_a_waiver_for_every_cause_of_a_pull_request_in_conflict_needs_the_decision_too(tmp_path: Path) -> None:
    out = Out()
    assert out.run(tmp_path, "812", "--note", "runner pool down", conflicting=lambda number: number == 812) == 2
    assert "812" in out.err[0] and "conflict" in out.err[0]
    assert out.run(tmp_path, "812", "--note", DECISION, conflicting=lambda number: True) == 0


def test_a_waiver_for_every_cause_of_a_pull_request_without_a_conflict_is_as_it_was(tmp_path: Path) -> None:
    assert Out().run(tmp_path, "812", "--note", "the runner pool is down") == 0


def test_a_waiver_for_a_cause_other_than_a_conflict_needs_no_decision(tmp_path: Path) -> None:
    out = Out()
    assert out.run(tmp_path, "812", "--cause", "ci-stuck", "--note", "pool down", conflicting=lambda n: True) == 0


def test_a_waiver_for_every_cause_is_refused_when_the_conflict_cannot_be_read(tmp_path: Path) -> None:
    def broken(number: int) -> bool:
        raise OSError("gh is down")

    out = Out()
    assert out.run(tmp_path, "812", "--note", "runner pool down", conflicting=broken) == 2
    assert "gh is down" in out.err[0] and pr_hold.read_active(tmp_path, NOW).holds == []


@pytest.mark.parametrize(("answer", "conflicts"), [("CONFLICTING\n", True), ("MERGEABLE\n", False)])
def test_github_says_a_pull_request_conflicts_or_does_not(
    monkeypatch: pytest.MonkeyPatch, answer: str, conflicts: bool
) -> None:
    monkeypatch.setattr(pr_hold, "run_gh", lambda args: answer)
    assert pr_hold.conflicting_now(812) is conflicts


@pytest.mark.parametrize("answer", ["UNKNOWN\n", "\n"])
def test_a_waiver_for_every_cause_is_refused_while_github_has_not_worked_out_mergeability(
    monkeypatch: pytest.MonkeyPatch, answer: str, tmp_path: Path
) -> None:
    # GitHub computes mergeability lazily: UNKNOWN is not "no conflict", so the waiver would slip through
    monkeypatch.setattr(pr_hold, "run_gh", lambda args: answer)
    out = Out()
    assert out.run(tmp_path, "812", "--note", "runner pool down", conflicting=pr_hold.conflicting_now) == 2
    assert "--cause" in out.err[0] and pr_hold.read_active(tmp_path, NOW).holds == []
    # a waiver for a cause other than the conflict never asks
    assert (
        out.run(tmp_path, "812", "--cause", "ci-stuck", "--note", "pool down", conflicting=pr_hold.conflicting_now) == 0
    )


def test_the_decision_rule_is_the_conventions_pattern() -> None:
    assert pr_hold.names_decision("waits for the maintainer on #1130", RULES)
    assert pr_hold.names_decision("#1130 asks the maintainer whether to drop it", RULES)
    assert not pr_hold.names_decision("waits for the maintainer", RULES)
    assert not pr_hold.names_decision("see #1130", RULES)
    assert not pr_hold.names_decision("", RULES)

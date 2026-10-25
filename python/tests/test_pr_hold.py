# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools pr hold`: the records that say somebody owns a pull request's problems (pr.md#hold).

A hold is one small file per pull request in the scratch directory. The store is tested on a made-up folder with a fixed
clock; nothing reads GitHub."""

import argparse
import json
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

    def run(self, scratch: Path, *words: str, rules: ConclaveWatch = RULES, now: datetime = NOW) -> int:
        return pr_hold.run_hold(args(*words), scratch, rules, now, self.out.append, self.err.append)


# --- the store ------------------------------------------------------------------------------------------------------


def test_a_hold_is_written_and_read_back(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold())
    found = pr_hold.read_active(tmp_path, NOW)
    assert found.problems == [] and found.holds == [hold()]


def test_one_file_per_pull_request_and_a_later_hold_replaces_the_earlier(tmp_path: Path) -> None:
    pr_hold.write(tmp_path, hold(812))
    pr_hold.write(tmp_path, hold(813))
    pr_hold.write(tmp_path, hold(812, who="engineer-9"))
    assert sorted(path.name for path in tmp_path.iterdir()) == ["812.json", "813.json"]
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
    assert sorted(path.name for path in tmp_path.iterdir()) == ["812.json"]


# --- the command ----------------------------------------------------------------------------------------------------


def test_an_assignment_names_who_and_lasts_the_default_hours(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "812", "--who", "engineer-2") == 0
    [held] = pr_hold.read_active(tmp_path, NOW).holds
    assert held.who == "engineer-2" and held.until == NOW + timedelta(hours=RULES.hold_default_hours)
    assert "#812" in run.out[0] and "engineer-2" in run.out[0] and "2026-10-25T16:00:00Z" in run.out[0]


def test_a_hold_can_name_one_cause_a_time_and_a_note(tmp_path: Path) -> None:
    assert Out().run(tmp_path, "812", "--who", "e", "--hours", "2", "--cause", "conflict", "--note", "rebasing") == 0
    [held] = pr_hold.read_active(tmp_path, NOW).holds
    assert (held.cause, held.note, held.until) == ("conflict", "rebasing", NOW + timedelta(hours=2))


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


def test_the_command_needs_a_pull_request_unless_it_lists(tmp_path: Path) -> None:
    run = Out()
    assert run.run(tmp_path, "--who", "e") == 2 and "pull request" in run.err[0]
    assert Out().run(tmp_path, "812", "--list") == 2
    assert Out().run(tmp_path, "812", "--release", "--who", "e") == 2


def test_the_holds_folder_is_in_the_scratch_directory_under_the_conventions_name(tmp_path: Path) -> None:
    assert pr_hold.holds_folder(tmp_path, RULES) == tmp_path / "pr-holds"

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the check slots (check_slots.py, lane `check` of build_claims.py): at most `check_slots` full suites hold
a slot at once across worktrees, the rest wait in a queue and say their place, a crashed holder's slot frees, and the
slots show in `build status`. No real suites and no real time: the clock, liveness and sleep are fakes."""

import json
import os
from pathlib import Path

import pytest
from test_build_claims import NOW, World, take, ticket

from coney_tools import build_claims, check_slots
from coney_tools.build_claims import CHECK_LANE, Slots, SlotTimeout, Waiting
from coney_tools.cli import main
from coney_tools.config import ConfigError, load_config


@pytest.fixture
def world() -> World:
    return World()


def check_lane(tmp_path: Path, world: World, slots: int = 2) -> Slots:
    """The check slots beside the build slots of one claims folder, on the world's fake clock."""
    return Slots(
        tmp_path / "claims",
        slots,
        clock=world.clock,
        alive=world.is_alive,
        sleep=world.sleep,
        lane=CHECK_LANE,
        what="check slot",
    )


def test_check_slots_have_their_own_claims_and_queue_apart_from_the_build_slots(tmp_path: Path, world: World) -> None:
    checks = check_lane(tmp_path, world, 1)
    builds = Slots(tmp_path / "claims", 1, clock=world.clock, alive=world.is_alive, sleep=world.sleep)
    mine = checks.join("alice", "wt-a", "pytest", False, 20_001)
    assert checks.try_claim(mine) is not None
    assert (tmp_path / "claims" / "check-1.claim" / "owner.json").is_file()
    assert (tmp_path / "claims" / "queue-check").is_dir()
    assert all(row.claim is None for row in builds.status().slots)  # a build still finds its slot free


def test_with_check_slots_n_at_most_n_suites_run_at_once_and_the_rest_wait(tmp_path: Path, world: World) -> None:
    # Three worktrees ask for a slot of two: the first two hold one, the third is the head of the queue and waits.
    checks = check_lane(tmp_path, world, 2)
    first, second = take(checks, world, "a", 20_000), take(checks, world, "b", 20_001)
    assert first is not None and second is not None
    third = ticket(checks, world, "c", 20_002)
    assert checks.try_claim(third) is None  # no slot is free: the third waits at the head of the queue
    held = [row.claim.agent for row in checks.status().slots if row.claim]
    assert sorted(held) == ["a", "b"]
    checks.release_claim(first)
    assert checks.try_claim(third) is not None  # the wait is over when a suite ends


def test_a_suite_that_waits_says_its_place_and_who_holds_the_slots(tmp_path: Path, world: World) -> None:
    checks = check_lane(tmp_path, world, 1)
    held = take(checks, world, "alice", 20_001)
    assert held is not None
    said: list[str] = []
    freed = False

    def free_after(sleeps: int) -> None:
        nonlocal freed
        if sleeps == 3 and not freed:
            freed = True
            checks.release_claim(held)

    world.on_sleep.append(free_after)
    with check_slots.hold(
        checks, "bob", "wt-b", "pytest", said.append, pid=30_000, max_wait=60, poll=2, report_every=2
    ):
        pass
    waiting = [line for line in said if line.startswith("waiting for a check slot")]
    assert waiting, said
    assert "place 1 of 1" in waiting[0] and "held by alice (" in waiting[0]
    assert "1 full suite may run at once (check_slots in coney.local.toml)" in waiting[0]
    assert any(line.startswith("got check slot 1 after") for line in said)


def test_a_crashed_holders_slot_frees_and_the_next_suite_takes_it_over(tmp_path: Path, world: World) -> None:
    checks = check_lane(tmp_path, world, 1)
    world.alive.add(777)
    dead = checks.join("crashed", "wt-c", "pytest", False, 777)
    assert checks.try_claim(dead) is not None
    world.alive.discard(777)  # the process died; its claim is still on disk
    said: list[str] = []
    with check_slots.hold(checks, "bob", "wt-b", "pytest", said.append, pid=30_000, max_wait=1):
        owners = [row.claim.agent for row in checks.status().slots if row.claim]
        assert owners == ["bob"]
    assert "took over a stale check slot of crashed" in said
    assert all(row.claim is None for row in checks.status().slots)  # released on the way out


def test_a_suite_gives_up_after_the_wait_and_leaves_the_queue(tmp_path: Path, world: World) -> None:
    checks = check_lane(tmp_path, world, 1)
    assert take(checks, world, "alice", 20_001) is not None
    with (
        pytest.raises(SlotTimeout) as stopped,
        check_slots.hold(checks, "bob", "wt-b", "pytest", lambda line: None, pid=30_000, max_wait=1),
    ):
        raise AssertionError("no slot was free")
    assert (stopped.value.position, stopped.value.total) == (1, 1)
    assert [row for row in checks.status().queue if not row.stale] == []
    assert "gave up waiting for a check slot (place 1 of 1)" in check_slots.queued_message(stopped.value, "pytest")


def test_the_slot_is_released_when_the_suite_raises(tmp_path: Path, world: World) -> None:
    checks = check_lane(tmp_path, world, 1)
    with pytest.raises(RuntimeError), check_slots.hold(checks, "bob", "wt-b", "pytest", lambda line: None, pid=30_000):
        raise RuntimeError("pytest crashed")
    assert all(row.claim is None for row in checks.status().slots)


def test_the_claim_records_what_runs_and_the_wait_estimate_uses_a_suites_length(tmp_path: Path, world: World) -> None:
    checks = check_lane(tmp_path, world, 1)
    with check_slots.hold(checks, "bob", "wt-b", "pytest", lambda line: None, pid=30_000):
        claim = next(row.claim for row in checks.status().slots if row.claim)
        assert (claim.preset, claim.step, claim.lane) == ("pytest", "pytest", CHECK_LANE)
        # A suite that began a minute ago: about a suite's length is left for the next in line.
        world.now += 60
        minutes = build_claims.estimate_wait(1, [claim], 1, world.clock(), CHECK_LANE)
        assert 30 < minutes <= build_claims.CHECK_MINUTES


def test_the_waiting_line_names_the_holders_and_the_count(tmp_path: Path, world: World) -> None:
    checks = check_lane(tmp_path, world, 2)
    claim = build_claims.BuildClaim(1, "alice", "wt-a", "ctest", "ctest", 1, "2027-01-15T08:00:00+00:00", CHECK_LANE)
    line = check_slots.waiting_line(Waiting(2, 3, [claim], 12.0), checks, "pytest", NOW)
    assert "place 2 of 3" in line and "about 12 min" in line and "2 full suites may run at once" in line


def test_the_default_count_is_a_quarter_of_the_cores_and_the_setting_wins() -> None:
    assert build_claims.check_slot_count(0, 8) == 2
    assert build_claims.check_slot_count(0, 16) == 4
    assert build_claims.check_slot_count(0, 2) == 1 and build_claims.check_slot_count(0, None) == 1
    assert build_claims.check_slot_count(3, 8) == 3


def test_check_slots_in_the_local_config_sets_the_count(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text("check_slots = 3\n", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    slots = check_slots.default_check_slots()
    assert (slots.slots, slots.lane, slots.what) == (3, CHECK_LANE, "check slot")
    assert slots.claims_dir == build_claims.default_slots().claims_dir  # one folder for every lane, every worktree


def test_without_a_setting_the_count_comes_from_the_cores(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(os, "cpu_count", lambda: 12)
    assert check_slots.default_check_slots().slots == 3


def test_a_negative_check_slots_setting_is_refused(tmp_path: Path) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text("check_slots = -1\n", encoding="utf-8")
    with pytest.raises(ConfigError, match="check_slots"):
        load_config(tmp_path)


def test_build_status_lists_the_check_slots_and_a_worktree_with_one_is_live(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text(
        f"check_slots = 2\nbuild_claims_dir = '{tmp_path / 'claims'}'\n", encoding="utf-8"
    )
    monkeypatch.chdir(tmp_path)
    checks = check_slots.default_check_slots()
    ticket = checks.join("alice", "wt-suite", "pytest", False, os.getpid())
    assert checks.try_claim(ticket) is not None
    assert main(["build", "status"]) == 0
    out = capsys.readouterr().out
    assert "check lane (2 slots):" in out and "slot 1: alice, wt-suite, pytest, starting" in out
    assert main(["build", "status", "--json"]) == 0
    assert json.loads(capsys.readouterr().out)["check"]["slots"][0]["claim"]["agent"] == "alice"
    assert "wt-suite" in build_claims.live_worktrees(checks.claims_dir)  # `workspace clean` leaves its build alone


def test_the_guide_has_the_section_every_page_links_to() -> None:
    # mkdocs does not fail on a missing anchor, so the pages that link to #check-slots are held to it here.
    repo = Path(__file__).resolve().parents[2]
    page = (repo / "docs" / "guides" / "coney-tools-build.md").read_text(encoding="utf-8")
    assert "{#check-slots}" in page
    for name in ("agents.md", "testing.md", "workspace.md", "coney-tools-repo.md"):
        assert "coney-tools-build.md#check-slots" in (repo / "docs" / "guides" / name).read_text(encoding="utf-8")

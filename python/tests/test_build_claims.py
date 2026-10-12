# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.build_claims: the build slots and the queue in front of them. Atomic claims, at most
`slots` holders, FIFO order with priority tickets ahead, stale takeover, release on every exit, and the status the
`build status` command prints. No real processes and no real time: liveness and the clock are fakes."""

import json
import os
import threading
from pathlib import Path

import pytest

from coney_tools.build_claims import (
    FAST_LANE,
    BuildClaim,
    BuildClaimError,
    Slots,
    SlotTimeout,
    Ticket,
    Waiting,
    estimate_wait,
)

NOW = 1_800_000_000.0


class World:
    """The machine: a clock the tests move, and the set of pids that are alive."""

    def __init__(self) -> None:
        self.now = NOW
        self.alive: set[int] = set()
        self.slept: list[float] = []
        #: Called on every sleep with the number of sleeps so far; lets a test free a slot mid-wait.
        self.on_sleep: list[object] = []

    def clock(self) -> float:
        return self.now

    def is_alive(self, pid: int) -> bool:
        return pid in self.alive or pid >= 10_000  # builds the tests start with a pid of 10000 or more are alive

    def sleep(self, seconds: float) -> None:
        self.slept.append(seconds)
        self.now += seconds
        for action in list(self.on_sleep):
            assert callable(action)
            action(len(self.slept))


@pytest.fixture
def world() -> World:
    return World()


def make(tmp_path: Path, world: World, slots: int = 2, max_age: float = 4 * 3600.0) -> Slots:
    return Slots(tmp_path / "claims", slots, max_age, clock=world.clock, alive=world.is_alive, sleep=world.sleep)


def ticket(slots: Slots, world: World, agent: str, pid: int, priority: bool = False) -> Ticket:
    """Join the queue as `agent` (alive pid `pid`); the clock moves on so the order is the call order."""
    world.alive.add(pid)
    joined = slots.join(agent, f"wt-{agent}", "dev", priority, pid)
    world.now += 1
    return joined


def take(slots: Slots, world: World, agent: str, pid: int) -> BuildClaim | None:
    """Join the queue, claim and leave the queue, as a build that gets a slot does."""
    mine = ticket(slots, world, agent, pid)
    claim = slots.try_claim(mine)
    if claim is not None:
        slots.leave(mine)
    return claim


def test_a_claim_records_its_owner_in_one_folder(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world)
    mine = ticket(slots, world, "alice", 101)
    claim = slots.try_claim(mine)
    assert claim is not None
    owner = json.loads((tmp_path / "claims" / "slot-1.claim" / "owner.json").read_text(encoding="utf-8"))
    assert (owner["agent"], owner["worktree"], owner["preset"], owner["pid"]) == ("alice", "wt-alice", "dev", 101)
    assert owner["step"] == "starting" and owner["started"].startswith("2027-")
    assert claim.slot == 1


def test_at_most_the_configured_number_of_builds_hold_slots(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=2)
    tickets = [ticket(slots, world, name, 100 + n) for n, name in enumerate(["a", "b", "c"])]
    first = slots.try_claim(tickets[0])
    assert first is not None
    slots.leave(tickets[0])
    second = slots.try_claim(tickets[1])
    assert second is not None
    slots.leave(tickets[1])
    assert {first.slot, second.slot} == {1, 2}
    assert slots.try_claim(tickets[2]) is None
    assert slots.release_claim(first)
    third = slots.try_claim(tickets[2])
    assert third is not None and third.slot == first.slot


def test_one_slot_is_won_by_exactly_one_racer(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    tickets = [ticket(slots, world, f"agent{n}", 200 + n) for n in range(8)]
    barrier = threading.Barrier(len(tickets))
    wins: list[int] = []

    def attempt(mine: Ticket) -> None:
        barrier.wait()
        if slots._take(1, mine) is not None:  # every racer goes straight at the slot, queue order aside
            wins.append(mine.pid)

    threads = [threading.Thread(target=attempt, args=(t,)) for t in tickets]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert len(wins) == 1
    assert len(list((tmp_path / "claims").glob("slot-*.claim"))) == 1
    assert not list((tmp_path / "claims").glob(".*new-*"))


def test_only_the_head_of_the_queue_may_claim_even_with_a_free_slot(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=2)
    first = ticket(slots, world, "first", 301)
    second = ticket(slots, world, "second", 302)
    assert slots.try_claim(second) is None
    assert slots.try_claim(first) is not None
    slots.leave(first)
    assert slots.try_claim(second) is not None


def test_the_queue_is_first_come_first_served_and_priority_goes_ahead(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world)
    ticket(slots, world, "early", 401)
    ticket(slots, world, "later", 402)
    ticket(slots, world, "urgent", 403, priority=True)
    ticket(slots, world, "urgent2", 404, priority=True)
    queue = slots.status().queue
    assert [(row.position, row.ticket.agent) for row in queue] == [
        (1, "urgent"),
        (2, "urgent2"),
        (3, "early"),
        (4, "later"),
    ]
    assert [row.ticket.priority for row in queue] == [True, True, False, False]


def test_a_dead_process_ticket_is_dropped_and_does_not_block_the_queue(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world)
    ghost = ticket(slots, world, "ghost", 501)
    mine = ticket(slots, world, "mine", 502)
    world.alive.discard(501)
    assert [row.position for row in slots.status().queue if row.stale] == [0]
    assert slots.try_claim(mine) is not None  # the live head, with the ghost ahead of it in time
    assert slots.drop_stale_tickets() == [ghost]
    assert [row.ticket.agent for row in slots.status().queue] == ["mine"]


def test_a_claim_of_a_dead_process_is_taken_over(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    old = take(slots, world, "crashed", 601)
    assert old is not None
    world.alive.discard(601)
    row = slots.status().slots[0]
    assert row.stale and row.claim == old
    new = take(slots, world, "next", 602)
    assert new is not None and new.agent == "next"
    assert slots.last_takeover == old


def test_a_claim_of_a_live_process_is_never_taken_over(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    assert take(slots, world, "busy", 701) is not None
    world.now += 3 * 3600
    assert take(slots, world, "next", 702) is None


def test_a_live_claim_older_than_the_maximum_age_is_stale(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1, max_age=3600.0)
    assert take(slots, world, "hung", 801) is not None
    world.now += 3601
    assert slots.status().slots[0].stale
    assert take(slots, world, "next", 802) is not None


def test_a_claim_folder_without_an_owner_is_stale_only_after_a_few_seconds(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    folder = tmp_path / "claims" / "slot-1.claim"
    folder.mkdir(parents=True)
    os.utime(folder, (NOW - 1, NOW - 1))  # made one (fake) second ago
    held = slots.status().slots[0]
    assert held.claim is not None and held.claim.agent == "?" and not held.stale
    world.now += 60
    assert slots.status().slots[0].stale


def test_a_stale_takeover_is_won_by_exactly_one_racer(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    assert take(slots, world, "crashed", 901) is not None
    world.alive.discard(901)
    tickets = [ticket(slots, world, f"racer{n}", 910 + n) for n in range(6)]
    barrier = threading.Barrier(len(tickets))
    wins: list[int] = []

    def attempt(mine: Ticket) -> None:
        barrier.wait()
        if slots._take(1, mine) is not None:
            wins.append(mine.pid)

    threads = [threading.Thread(target=attempt, args=(t,)) for t in tickets]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert len(wins) == 1
    held = slots.status().slots[0].claim
    assert held is not None and held.pid == wins[0]


def test_hold_releases_the_slot_when_the_build_raises(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    with pytest.raises(RuntimeError), slots.hold("alice", "wt", "dev", False, 11001, poll=1, report=None):
        assert slots.status().slots[0].claim is not None
        raise RuntimeError("compiler on fire")
    assert slots.status().slots[0].claim is None
    assert slots.status().queue == []


def test_hold_releases_the_slot_on_interrupt(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    with pytest.raises(KeyboardInterrupt), slots.hold("alice", "wt", "dev", False, 11002, poll=1, report=None):
        raise KeyboardInterrupt
    assert slots.status().slots[0].claim is None


def test_hold_releases_the_slot_on_success_and_leaves_no_ticket(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world)
    with slots.hold("alice", "wt", "dev", False, 11003, poll=1, report=None) as claim:
        assert claim.agent == "alice"
        assert slots.status().queue == []
    assert all(row.claim is None for row in slots.status().slots)


def test_releasing_after_a_takeover_leaves_the_new_holder(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    old = take(slots, world, "slow", 1101)
    assert old is not None
    world.alive.discard(1101)
    new = take(slots, world, "new", 1102)
    assert new is not None
    assert not slots.release_claim(old)
    held = slots.status().slots[0].claim
    assert held is not None and held.agent == "new"


def test_set_step_updates_the_owner_file_only_while_the_claim_is_mine(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    mine = take(slots, world, "alice", 1201)
    assert mine is not None
    updated = slots.set_step(mine, "build")
    assert updated is not None and updated.step == "build"
    held = slots.status().slots[0].claim
    assert held is not None and held.step == "build"
    world.alive.discard(1201)
    assert take(slots, world, "bob", 1202) is not None
    assert slots.set_step(updated, "test") is None
    taken = slots.status().slots[0].claim
    assert taken is not None and taken.agent == "bob" and taken.step == "starting"


def test_acquire_waits_in_the_queue_and_reports_its_place(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    holder = take(slots, world, "holder", 1301)
    assert holder is not None
    reports: list[Waiting] = []
    world.on_sleep.append(lambda n: slots.release_claim(holder) if n == 3 else None)
    claim = slots.acquire(
        "waiter",
        "wt",
        "dev",
        False,
        11302,
        poll=10,
        report=reports.append,
        report_every=15,
    )
    assert claim.agent == "waiter"
    assert reports and (reports[0].place, reports[0].total, [h.agent for h in reports[0].holders]) == (1, 1, ["holder"])
    assert reports[0].minutes > 0
    assert len(reports) == 2  # at once, then 20 seconds later (every 15 or more); not at every poll
    assert slots.status().queue == []


def test_acquire_times_out_and_leaves_the_queue(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    assert take(slots, world, "holder", 1401) is not None
    with pytest.raises(SlotTimeout, match="holder") as timeout:
        slots.acquire("waiter", "wt", "dev", False, 11402, poll=10, report=None, timeout=60)
    assert (timeout.value.position, timeout.value.total) == (1, 1)
    assert slots.status().queue == []


def test_acquire_rejoins_with_its_place_when_its_ticket_is_removed(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    holder = take(slots, world, "holder", 1501)
    assert holder is not None
    world.alive.add(1502)

    def lose_ticket_then_free(n: int) -> None:
        if n == 1:
            for row in slots.status().queue:
                slots.leave(row.ticket)
        if n == 2:
            slots.release_claim(holder)

    world.on_sleep.append(lose_ticket_then_free)
    assert slots.acquire("waiter", "wt", "dev", False, 1502, poll=10, report=None).agent == "waiter"


def test_status_lists_holders_waiters_and_stale_entries(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=2)
    held = take(slots, world, "alice", 1601)
    assert held is not None
    world.now += 120
    crashed = take(slots, world, "bob", 1602)
    assert crashed is not None
    world.alive.discard(1602)
    ticket(slots, world, "carol", 1603)
    ticket(slots, world, "dead", 1604)
    world.alive.discard(1604)
    snapshot = slots.status()
    assert [(row.number, row.claim.agent if row.claim else None, row.stale) for row in snapshot.slots] == [
        (1, "alice", False),
        (2, "bob", True),
    ]
    assert [(row.position, row.ticket.agent, row.stale) for row in snapshot.queue] == [
        (1, "carol", False),
        (0, "dead", True),
    ]


def test_release_by_agent_frees_its_stale_claim_and_tickets(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=2)
    assert take(slots, world, "alice", 1701) is not None
    ticket(slots, world, "alice", 1702)
    world.alive.clear()
    claims, tickets = slots.release("alice")
    assert [c.agent for c in claims] == ["alice"] and len(tickets) == 1
    assert slots.status().queue == []
    assert slots.status().slots[0].claim is None
    with pytest.raises(BuildClaimError, match="nothing to release"):
        slots.release("alice")


def test_release_of_a_running_build_or_of_another_agent_needs_force(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    assert take(slots, world, "alice", 1801) is not None
    with pytest.raises(BuildClaimError, match="still runs"):
        slots.release("alice")
    with pytest.raises(BuildClaimError, match="held by alice"):
        slots.release("bob", slot=1)
    claims, _ = slots.release("bob", slot=1, force=True)
    assert [c.agent for c in claims] == ["alice"]


def test_agent_names_are_checked(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world)
    with pytest.raises(BuildClaimError, match="agent id"):
        slots.join("bad name", "wt", "dev", False, 1)


def test_the_default_slots_come_from_the_local_config(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    from coney_tools import build_claims

    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text(
        f"build_slots = 3\nbuild_claims_dir = '{tmp_path / 'elsewhere'}'\n", encoding="utf-8"
    )
    monkeypatch.chdir(tmp_path)
    slots = build_claims.default_slots()
    assert slots.slots == 3 and slots.claims_dir == (tmp_path / "elsewhere").resolve()


def test_without_config_the_claims_sit_beside_the_pcsx2_claims(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    from coney_tools import build_claims

    root = tmp_path / "work" / "repos" / "coney"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(root)
    slots = build_claims.default_slots()
    assert slots.slots == 2
    assert slots.claims_dir == (tmp_path / "work" / "scratch" / "build-claims").resolve()


def fast_lane(tmp_path: Path, world: World) -> Slots:
    """The fast lane beside the main slots of `make`: the same folder, its own claim and queue, one slot."""
    return Slots(tmp_path / "claims", 1, clock=world.clock, alive=world.is_alive, sleep=world.sleep, lane=FAST_LANE)


def test_the_fast_lane_has_its_own_claim_and_queue_beside_full_main_slots(tmp_path: Path, world: World) -> None:
    main = make(tmp_path, world, slots=1)
    fast = fast_lane(tmp_path, world)
    big = take(main, world, "big", 2001)
    assert big is not None
    waiting_big = ticket(main, world, "waiting", 2002)
    small = take(fast, world, "small", 2003)
    assert small is not None and small.lane == FAST_LANE and small.slot == 1
    assert (tmp_path / "claims" / "fast-1.claim" / "owner.json").is_file()
    assert main.try_claim(waiting_big) is None  # the main slot is still full
    assert [row.ticket.agent for row in main.status().queue] == ["waiting"]
    assert fast.status().queue == []


def test_the_fast_lane_runs_one_job_at_a_time_and_queues_the_next(tmp_path: Path, world: World) -> None:
    fast = fast_lane(tmp_path, world)
    first = take(fast, world, "one", 2101)
    assert first is not None
    second = ticket(fast, world, "two", 2102)
    assert fast.try_claim(second) is None
    assert fast.release_claim(first)
    assert fast.try_claim(second) is not None
    assert not list((tmp_path / "claims").glob("slot-*.claim"))


def test_a_stale_fast_claim_is_taken_over_like_a_main_one(tmp_path: Path, world: World) -> None:
    fast = fast_lane(tmp_path, world)
    assert take(fast, world, "crashed", 2201) is not None
    world.alive.discard(2201)
    assert take(fast, world, "next", 2202) is not None


def holder(world: World, agent: str, step: str, minutes_ago: float, pid: int) -> object:
    """A claim record for the estimate: `agent` at `step` since `minutes_ago`."""
    from coney_tools.build_claims import BuildClaim, _iso

    return BuildClaim(1, agent, "wt", "dev", step, pid, _iso(world.now - minutes_ago * 60))


def test_the_wait_estimate_follows_the_holders_step_and_elapsed_time(world: World) -> None:
    from coney_tools.build_claims import BuildClaim

    early = holder(world, "a", "build", 5, 1)
    late = holder(world, "b", "build", 14, 2)
    assert isinstance(early, BuildClaim) and isinstance(late, BuildClaim)
    # The build step is about 15 minutes: the second holder is nearly done (1 minute), the first has about 10 left.
    assert estimate_wait(1, [early, late], 2, world.now) == pytest.approx(1.0)
    assert estimate_wait(2, [early, late], 2, world.now) == pytest.approx(10.0)
    # The third in line waits for a whole build after the first slot frees.
    assert estimate_wait(3, [early, late], 2, world.now) == pytest.approx(1.0 + 15.0)
    # A free slot is no wait; a holder past its typical length still counts at least a minute.
    assert estimate_wait(1, [early], 2, world.now) == 0.0
    overdue = holder(world, "c", "test", 30, 3)
    assert isinstance(overdue, BuildClaim)
    assert estimate_wait(1, [overdue], 1, world.now) == pytest.approx(1.0)


def test_the_fast_lane_estimate_is_short(world: World) -> None:
    from coney_tools.build_claims import BuildClaim

    running = holder(world, "a", "build", 1, 1)
    assert isinstance(running, BuildClaim)
    assert estimate_wait(1, [running], 1, world.now, FAST_LANE) == pytest.approx(2.0)


def test_the_default_fast_lane_is_one_slot_in_the_same_folder(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    from coney_tools import build_claims

    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text("build_slots = 3\n", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    main, fast = build_claims.default_slots(), build_claims.default_slots(FAST_LANE)
    assert (main.slots, fast.slots) == (3, 1) and main.claims_dir == fast.claims_dir


def test_a_build_folder_has_one_lock_of_its_own_beside_the_slots(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    from coney_tools import build_claims

    root = tmp_path / "work" / "worktrees" / "coney" / "wt"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(root)
    folder = root / "build" / "ci"
    lock = build_claims.folder_lock(folder, root)
    again = build_claims.folder_lock(root / "build" / "ci" / ".." / "ci", root)
    other = build_claims.folder_lock(root / "build" / "dev", root)
    assert lock.slots == 1 and lock.claims_dir == again.claims_dir  # the same folder spelled twice: the same lock
    assert lock.claims_dir != other.claims_dir  # another folder, another lock
    slots = build_claims.default_slots(root=root)
    assert lock.claims_dir.parent == slots.claims_dir / "folders"  # shared by every worktree, apart from the slots
    assert "wt" in lock.claims_dir.name and "ci" in lock.claims_dir.name
    # The slots' status looks only at its own folder, so a lock never shows up as a slot.
    ticket_ = lock.join("alice", "wt", "ci", False, os.getpid())
    assert lock.try_claim(ticket_) is not None
    assert all(row.claim is None for row in slots.status().slots)


def test_the_lock_message_names_what_is_busy(tmp_path: Path, world: World) -> None:
    lock = Slots(tmp_path / "locks", 1, clock=world.clock, alive=world.is_alive, sleep=world.sleep, what="build folder")
    assert take(lock, world, "holder", 1601) is not None
    with pytest.raises(SlotTimeout, match=r"build folder .*holder"):
        lock.acquire("waiter", "wt", "dev", False, 11602, poll=10, report=None, timeout=30)


def test_a_waiting_build_can_be_called_off_by_its_caller(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    assert take(slots, world, "holder", 1701) is not None
    asked: list[float] = []

    def abandon(waited: float) -> str | None:
        asked.append(waited)
        return "would not fit" if waited >= 30 else None

    with pytest.raises(SlotTimeout, match=r"would not fit; held by holder") as timeout:
        slots.acquire("waiter", "wt", "dev", False, 11702, poll=10, report=None, abandon=abandon)
    assert (timeout.value.position, timeout.value.total) == (1, 1)
    assert asked == [0.0, 10.0, 20.0, 30.0]  # asked at every look, with the seconds waited so far
    assert slots.status().queue == []


def test_a_build_that_gets_its_slot_is_not_called_off(tmp_path: Path, world: World) -> None:
    slots = make(tmp_path, world, slots=1)
    claim = slots.acquire("waiter", "wt", "dev", False, 11703, poll=10, report=None, abandon=lambda waited: "never")
    assert claim.agent == "waiter"

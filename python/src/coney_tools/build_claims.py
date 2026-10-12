# SPDX-License-Identifier: GPL-3.0-or-later
"""Build slots: at most `slots` builds (configure, compile, test) run on the machine at once, and the rest wait in a
fair queue (docs/guides/coney-tools.md#build).

The model is the PCSX2 claims (pcsx2_claims.py), with the same folder mechanics (claim_files.py). The claims dir is
one shared place every worktree sees:

* `slot-N.claim/owner.json`: who holds slot N (agent, worktree, preset, the step the build is at, pid, start time).
  The folder is built complete in a private folder and renamed into place, so one caller wins and none sees a
  half-made claim.
* `queue/<millis>-<pid>-<id>.json`: a ticket per waiting build. The order is priority tickets first, then by time;
  **only the head of the queue may claim**, so waiters cannot overtake one another.

A third kind of claim, the **folder lock** (`folder_lock`), is the same machinery with one slot and a folder of its own
per build folder: a run holds it from configure to its last test, so two runs never share one build folder.

A second, separate **fast lane** (`fast-1.claim/`, queue `queue-fast/`, one slot) is for small jobs: an incremental
rebuild of a few steps, a test run that selects a few tests. It has its own claim and its own short queue, so these
never wait behind a full build (the caller decides which lane a job belongs to; build_run.py).

Everything reported is rebuilt from those files and the live processes each time; no cached state file exists to go
stale. A claim is stale, and may be taken over, when its holder's process is gone, or when it is older than the
maximum age (a hung build; a pid can also be reused). A ticket is stale when its process is gone, and is dropped.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import time
import uuid
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from dataclasses import asdict, dataclass, replace
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import claim_files, pcsx2_proc
from coney_tools.claim_files import OWNER_FILE, TAKEOVER_ATTEMPTS
from coney_tools.config import ConfigError, find_repo_root, load_config, local_config_file, main_checkout
from coney_tools.pcsx2_claims import AGENT_NAME, INCOMPLETE_SECONDS

#: How many builds may run at once when `build_slots` is not set.
DEFAULT_SLOTS = 2
#: A claim whose process is still alive is stale after this many seconds (4 hours): a hung build, or a reused pid.
DEFAULT_MAX_AGE = 4 * 3600.0
#: A ticket that old is dropped even when its pid lives (a reused pid after a crash).
TICKET_MAX_AGE = 12 * 3600.0
#: Seconds between looks at the queue while waiting.
POLL_SECONDS = 2.0
#: Seconds between "you are Nth in the queue" lines while waiting.
REPORT_SECONDS = 60.0
#: The lanes: the main slots (full builds, full test runs) and the single fast-lane slot (small jobs).
MAIN_LANE = "main"
FAST_LANE = "fast"
#: What each lane calls its claim folders and its queue folder.
_PREFIX = {MAIN_LANE: "slot", FAST_LANE: "fast"}
_QUEUE_DIR = {MAIN_LANE: "queue", FAST_LANE: "queue-fast"}
#: Typical minutes of each step on a main slot, for the wait estimate (a rough guide, not a promise).
TYPICAL_MINUTES = {"starting": 5.0, "configure": 5.0, "build": 15.0, "test": 10.0}
#: A fast-lane job is short by definition.
FAST_MINUTES = 3.0


class BuildClaimError(ConfigError):
    """A slot cannot be claimed, found or released; the message says who holds what."""


class SlotTimeout(BuildClaimError):
    """No slot came free within the time the caller was willing to wait; `position` of `total` is its place."""

    def __init__(self, message: str, position: int = 0, total: int = 0) -> None:
        super().__init__(message)
        self.position = position
        self.total = total


@dataclass(frozen=True)
class BuildClaim:
    """The owner file of a slot: who builds, what, which step it is at and the process that keeps the claim alive."""

    slot: int
    agent: str
    worktree: str
    preset: str
    step: str
    pid: int
    started: str
    lane: str = MAIN_LANE

    def age(self, now: float) -> float:
        """Seconds since the claim was made."""
        return now - datetime.fromisoformat(self.started).timestamp()


@dataclass(frozen=True)
class Ticket:
    """A build waiting for a slot. `name` is its file in the queue folder; `time` (epoch seconds) fixes its place."""

    name: str
    agent: str
    worktree: str
    preset: str
    pid: int
    priority: bool
    time: float

    def sort_key(self) -> tuple[bool, float, str]:
        """Priority tickets first, then by arrival, then by name so equal times still order the same for everyone."""
        return (not self.priority, self.time, self.name)


@dataclass(frozen=True)
class Waiting:
    """What a waiting build reports: its place in the queue of `total`, who holds the slots and a rough wait."""

    place: int
    total: int
    holders: list[BuildClaim]
    minutes: float


@dataclass(frozen=True)
class SlotRow:
    """One slot of `status`: its number, its claim (if any) and whether that claim is stale."""

    number: int
    claim: BuildClaim | None
    stale: bool


@dataclass(frozen=True)
class QueueRow:
    """One waiting build: its place (1 is the head; 0 for a stale ticket, which has none) and whether it is stale."""

    position: int
    ticket: Ticket
    stale: bool


@dataclass(frozen=True)
class Snapshot:
    """The slots and the queue as they are now."""

    slots: list[SlotRow]
    queue: list[QueueRow]


def _iso(epoch: float) -> str:
    """An epoch time as the ISO text owner files hold."""
    return datetime.fromtimestamp(epoch, UTC).isoformat(timespec="seconds")


def default_slots(lane: str = MAIN_LANE, root: Path | None = None) -> Slots:
    """The slots (or, with `lane="fast"`, the single fast-lane slot) of this machine:
    `build_slots` and `build_claims_dir` from coney.local.toml (this checkout's, else the main checkout's), else two
    slots in `build-claims` in the shared scratch (`../../scratch` beside the main checkout, like `pcsx2-claims`).
    The default ignores `scratch_dir`: a worktree may point that at its own folder,
    and its claims would then be invisible to every other agent. `root` is the checkout to start from (default: the
    current folder)."""
    root = find_repo_root(root or Path.cwd())
    main = main_checkout(root)
    config = load_config(root) if local_config_file(root) else None
    claims = config.paths.get("build_claims_dir") if config else None
    count = config.numbers["build_slots"] if config else DEFAULT_SLOTS
    directory = claims or (main / ".." / ".." / "scratch" / "build-claims").resolve()
    return Slots(directory, 1 if lane == FAST_LANE else count, lane=lane)


def _typical(step: str, lane: str) -> float:
    """Typical minutes of a step in a lane."""
    return FAST_MINUTES if lane == FAST_LANE else TYPICAL_MINUTES.get(step, TYPICAL_MINUTES["build"])


def estimate_wait(place: int, holders: list[BuildClaim], slots: int, now: float, lane: str = MAIN_LANE) -> float:
    """A rough wait in minutes for the build at `place` (1 is the head): the p-th slot to free up, judged from each
    holder's elapsed time against the typical length of the step it records, plus a full typical build for each
    round of builds ahead of it beyond the first. A slot with no holder counts as free now."""
    busy = sorted(max(1.0, _typical(h.step, lane) - h.age(now) / 60) for h in holders)
    remaining = [0.0] * max(0, slots - len(busy)) + busy
    rounds, index = divmod(max(place, 1) - 1, max(slots, 1))
    return remaining[index] + rounds * _typical("build", lane)


def folder_lock(build_dir: Path, root: Path | None = None) -> Slots:
    """The lock of one build folder: a single slot, with its own queue, in `folders/` beside the build claims (so every
    worktree sees it, and the slots' status never lists it). The folder is named for the checkout, the folder and a
    digest of the folder's resolved path, so the same folder spelled two ways is one lock."""
    root = find_repo_root(root or Path.cwd())
    resolved = Path(os.path.realpath(build_dir))
    digest = hashlib.sha1(os.path.normcase(str(resolved)).encode("utf-8"), usedforsecurity=False).hexdigest()[:10]
    safe = re.compile(r"[^A-Za-z0-9._-]+")
    name = f"{safe.sub('_', root.name)}-{safe.sub('_', resolved.name)}-{digest}"
    directory = default_slots(MAIN_LANE, root).claims_dir / "folders" / name
    return Slots(directory, 1, what="build folder")


class Slots:
    """The build slots and their queue under `claims_dir`."""

    def __init__(
        self,
        claims_dir: Path,
        slots: int = DEFAULT_SLOTS,
        max_age: float = DEFAULT_MAX_AGE,
        clock: Callable[[], float] | None = None,
        alive: Callable[[int], bool] | None = None,
        sleep: Callable[[float], None] | None = None,
        lane: str = MAIN_LANE,
        what: str = "build slot",
    ) -> None:
        """`clock` returns epoch seconds, `alive` says whether a pid lives and `sleep` waits (default: the real ones;
        tests pass fakes).
        `lane` picks the claim folders and the queue this object works on; `what` names what is claimed, in messages."""
        self.claims_dir = claims_dir
        self.slots = slots
        self.max_age = max_age
        # The real ones are looked up at each call, so a test that replaces time.time or pid_alive is obeyed.
        self.clock = clock or (lambda: time.time())
        self.alive = alive or (lambda pid: pcsx2_proc.pid_alive(pid))
        self.sleep = sleep or (lambda seconds: time.sleep(seconds))
        self.lane = lane
        self.what = what
        #: The stale claim the last successful claim took over, for the caller to report.
        self.last_takeover: BuildClaim | None = None

    # -- files

    def _folder(self, number: int) -> Path:
        """The claim folder of a slot."""
        return self.claims_dir / f"{_PREFIX[self.lane]}-{number}.claim"

    def _queue(self) -> Path:
        """The folder of the tickets."""
        return self.claims_dir / _QUEUE_DIR[self.lane]

    def _read(self, number: int) -> BuildClaim | None:
        """The claim on a slot, None when it is free. A claim folder with no readable owner file (one being made, or
        damaged) reads as held by agent "?", dated by the folder."""
        folder = self._folder(number)
        if not folder.is_dir():
            return None
        try:
            return BuildClaim(**json.loads((folder / OWNER_FILE).read_text(encoding="utf-8")))
        except (OSError, ValueError, TypeError):
            try:
                when = _iso(folder.stat().st_mtime)
            except OSError:
                return None  # it was removed meanwhile: free
            return BuildClaim(number, "?", "", "", "?", 0, when, self.lane)

    def _numbers(self) -> list[int]:
        """The slot numbers to look at: 1 to `slots`, and any higher folder left from a larger setting."""
        pattern = re.compile(rf"^{_PREFIX[self.lane]}-(\d+)\.claim$")
        found = [int(m.group(1)) for p in self.claims_dir.glob("*.claim") if (m := pattern.match(p.name))]
        return sorted({*range(1, self.slots + 1), *found})

    # -- liveness

    def is_stale(self, claim: BuildClaim) -> bool:
        """Whether a claim's holder is gone or hung (see the module comment)."""
        age = claim.age(self.clock())
        if claim.agent == "?":
            return age > INCOMPLETE_SECONDS
        return not self.alive(claim.pid) or age > self.max_age

    def _ticket_stale(self, ticket: Ticket) -> bool:
        """Whether a ticket's process is gone (or the ticket is implausibly old)."""
        return not self.alive(ticket.pid) or self.clock() - ticket.time > TICKET_MAX_AGE

    def _tickets(self) -> list[Ticket]:
        """Every readable ticket, in queue order (stale ones included)."""
        found = []
        for path in self._queue().glob("*.json") if self._queue().is_dir() else []:
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                found.append(Ticket(name=path.name, **data))
            except (OSError, ValueError, TypeError):
                continue  # being written or removed: it is not in the queue yet / any more
        return sorted(found, key=Ticket.sort_key)

    def status(self) -> Snapshot:
        """Every slot with its claim and every ticket with its place, from the files and the processes now."""
        slots = []
        for number in self._numbers():
            claim = self._read(number)
            slots.append(SlotRow(number, claim, claim is not None and self.is_stale(claim)))
        live = [t for t in self._tickets() if not self._ticket_stale(t)]
        dead = [t for t in self._tickets() if self._ticket_stale(t)]
        queue = [QueueRow(n, t, False) for n, t in enumerate(live, start=1)] + [QueueRow(0, t, True) for t in dead]
        return Snapshot(slots, queue)

    # -- the queue

    def join(self, agent: str, worktree: str, preset: str, priority: bool, pid: int) -> Ticket:
        """Take a place in the queue. The ticket file appears complete (written privately, then renamed)."""
        if not AGENT_NAME.match(agent):
            raise BuildClaimError(f"agent id {agent!r}: use letters, digits, '.', '_' and '-' (at most 64)")
        now = self.clock()
        name = f"{int(now * 1000):015d}-{pid}-{uuid.uuid4().hex[:6]}.json"
        ticket = Ticket(name, agent, worktree, preset, pid, priority, now)
        self._write_ticket(ticket)
        return ticket

    def _write_ticket(self, ticket: Ticket) -> None:
        """Write a ticket file whole: a temporary file, then an atomic rename (so a reader never sees half of one)."""
        queue = self._queue()
        queue.mkdir(parents=True, exist_ok=True)
        data = asdict(ticket)
        del data["name"]
        temporary = queue / f".{ticket.name}.{os.getpid()}.tmp"
        temporary.write_text(json.dumps(data, indent=1), encoding="utf-8")
        temporary.replace(queue / ticket.name)

    def leave(self, ticket: Ticket) -> None:
        """Leave the queue (a ticket already gone is fine)."""
        (self._queue() / ticket.name).unlink(missing_ok=True)

    def drop_stale_tickets(self) -> list[Ticket]:
        """Remove the tickets whose process is gone, so they never hold the head; returns what was removed."""
        dropped = [t for t in self._tickets() if self._ticket_stale(t)]
        for ticket in dropped:
            self.leave(ticket)
        return dropped

    # -- claiming and releasing

    def try_claim(self, ticket: Ticket) -> BuildClaim | None:
        """Claim a free slot for the ticket's build, or None. Only the head of the queue (the first live ticket) may
        claim, so a build never overtakes one that has waited longer; and a slot is made by an atomic rename, so
        however many callers race, no more than `slots` hold one."""
        head = next((t for t in self._tickets() if not self._ticket_stale(t)), None)
        if head is None or head.name != ticket.name:
            return None
        for number in self._numbers():
            if number > self.slots:
                continue
            claim = self._take(number, ticket)
            if claim is not None:
                return claim
        return None

    def _take(self, number: int, ticket: Ticket) -> BuildClaim | None:
        """Make slot `number`'s claim folder; over a stale claim, remove that one first. None when the slot is held
        or someone else won it."""
        self.claims_dir.mkdir(parents=True, exist_ok=True)
        folder = self._folder(number)
        claim = BuildClaim(
            number,
            ticket.agent,
            ticket.worktree,
            ticket.preset,
            "starting",
            ticket.pid,
            _iso(self.clock()),
            self.lane,
        )
        took_over: BuildClaim | None = None
        for _ in range(TAKEOVER_ATTEMPTS):
            if claim_files.publish(folder, json.dumps(asdict(claim), indent=1)):
                self.last_takeover = took_over
                return claim
            seen = self._read(number)
            if seen is None:
                continue
            if not self.is_stale(seen):
                return None

            def unchanged(held: BuildClaim = seen) -> bool:
                return self._read(number) == held

            outcome = claim_files.remove_under_lock(folder, unchanged)
            if outcome == "changed":
                return None
            if outcome == "removed":
                took_over = seen
        return None

    def set_step(self, claim: BuildClaim, step: str) -> BuildClaim | None:
        """Note the step the build is at in the owner file; the updated claim, or None when the claim is no longer
        this build's (it was released by hand or taken over)."""
        current = self._read(claim.slot)
        if current is None or (current.agent, current.pid, current.started) != (claim.agent, claim.pid, claim.started):
            return None
        updated = replace(current, step=step)
        temporary = self._folder(claim.slot) / f"{OWNER_FILE}.{os.getpid()}.{uuid.uuid4().hex[:6]}"
        try:
            temporary.write_text(json.dumps(asdict(updated), indent=1), encoding="utf-8")
            temporary.replace(self._folder(claim.slot) / OWNER_FILE)
        except OSError:
            return None
        return updated

    def release_claim(self, claim: BuildClaim) -> bool:
        """Free the slot if it is still this build's claim (never a successor's, after a takeover or a manual
        release). True when it was removed."""
        folder = self._folder(claim.slot)

        def mine() -> bool:
            current = self._read(claim.slot)
            return current is not None and (current.agent, current.pid, current.started) == (
                claim.agent,
                claim.pid,
                claim.started,
            )

        for _ in range(TAKEOVER_ATTEMPTS):
            outcome = claim_files.remove_under_lock(folder, mine)
            if outcome != "busy":
                return outcome == "removed"
        return False

    def acquire(
        self,
        agent: str,
        worktree: str,
        preset: str,
        priority: bool,
        pid: int,
        *,
        poll: float = POLL_SECONDS,
        report: Callable[[Waiting], None] | None = None,
        report_every: float = REPORT_SECONDS,
        timeout: float | None = None,
    ) -> BuildClaim:
        """Wait in the queue until this build is at its head and a slot is free, then claim it. `report` is called
        with a Waiting (place, queue length, holders, a rough wait) when waiting starts and every `report_every`
        seconds after. Raises SlotTimeout, carrying the place, after `timeout` seconds. The ticket is gone however
        this returns or raises."""
        ticket = self.join(agent, worktree, preset, priority, pid)
        started = self.clock()
        last_report: float | None = None
        try:
            while True:
                self.drop_stale_tickets()
                if not (self._queue() / ticket.name).is_file():
                    self._write_ticket(ticket)  # removed by hand: take the same place again
                claim = self.try_claim(ticket)
                if claim is not None:
                    return claim
                now = self.clock()
                snapshot = self.status()
                holders = [row.claim for row in snapshot.slots if row.claim is not None and not row.stale]
                live = [row for row in snapshot.queue if not row.stale]
                place = next((row.position for row in live if row.ticket.name == ticket.name), len(live))
                if timeout is not None and now - started >= timeout:
                    held = ", ".join(f"{h.agent} ({h.preset}, {h.step})" for h in holders) or "nobody"
                    raise SlotTimeout(
                        f"no {self.what} free after {timeout / 60:.0f} minutes; held by {held}", place, len(live)
                    )
                if report is not None and (last_report is None or now - last_report >= report_every):
                    minutes = estimate_wait(place, holders, self.slots, now, self.lane)
                    report(Waiting(place, len(live), holders, minutes))
                    last_report = now
                self.sleep(poll)
        finally:
            self.leave(ticket)

    @contextmanager
    def hold(
        self,
        agent: str,
        worktree: str,
        preset: str,
        priority: bool,
        pid: int,
        *,
        poll: float = POLL_SECONDS,
        report: Callable[[Waiting], None] | None = None,
        report_every: float = REPORT_SECONDS,
        timeout: float | None = None,
    ) -> Iterator[BuildClaim]:
        """Hold a slot for a `with` block: acquired as `acquire` does, released however the block ends (success,
        failure or an interrupt)."""
        claim = self.acquire(
            agent, worktree, preset, priority, pid, poll=poll, report=report, report_every=report_every, timeout=timeout
        )
        try:
            yield claim
        finally:
            self.release_claim(claim)

    def release(
        self, agent: str, slot: int | None = None, force: bool = False
    ) -> tuple[list[BuildClaim], list[Ticket]]:
        """Manual cleanup: free `slot`, or every slot `agent` holds, and drop the agent's tickets (not with `slot`).
        A claim whose build still runs, or another agent's, needs `force`; a stale one does not. Returns what was
        removed. Nothing here stops a process: a forced release of a live build only frees its slot."""
        rows = [row for row in self.status().slots if row.claim is not None]
        chosen = [row for row in rows if (row.number == slot if slot is not None else row.claim.agent == agent)]  # type: ignore[union-attr]
        tickets = [] if slot is not None else [r.ticket for r in self.status().queue if r.ticket.agent == agent]
        if not chosen and not tickets:
            raise BuildClaimError(f"nothing to release: {agent} holds no slot and has no place in the queue")
        for row in chosen:
            assert row.claim is not None
            if row.stale or force:
                continue
            if row.claim.agent != agent:
                raise BuildClaimError(f"slot {row.number} is held by {row.claim.agent}, not {agent}; --force overrides")
            raise BuildClaimError(
                f"slot {row.number}: the build of {agent} (pid {row.claim.pid}) still runs; "
                "--force frees the slot without stopping it"
            )
        released = []
        for row in chosen:
            assert row.claim is not None
            if self.release_claim(row.claim):
                released.append(row.claim)
        for ticket in tickets:
            self.leave(ticket)
        return released, tickets

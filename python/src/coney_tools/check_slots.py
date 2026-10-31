# SPDX-License-Identifier: GPL-3.0-or-later
"""Check slots: at most `check_slots` full suites run on the machine at once
(docs/guides/coney-tools-build.md#check-slots).

Eight to sixteen full pytest and ctest runs on an eight-core machine made a half-hour suite take two or three hours,
which then outlived its caller's time limit and was run again. So every command that runs a whole suite holds a check
slot for the run, the way `coney-tools build` holds a build slot: the same claim folders, queue and stale-lock rules
(build_claims.py, lane `check`), its own `check_slots` count. A run that has to wait says its place in the queue and
who holds the slots, every minute. A targeted run of a few test files takes no slot.

This module is the one place a caller asks for a slot: `hold` is a `with` block, released however it ends. The holders
are the pytest step of `task check` (task_check.py), the python step of `ci run` (check_cli.py) and the full ctest
run of `build --test` (build_run.py, which asks the lane itself).
"""

from __future__ import annotations

import os
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from pathlib import Path

from coney_tools import build_claims
from coney_tools.build_claims import CHECK_LANE, Slots, SlotTimeout, Waiting
from coney_tools.check_progress import Progress

#: Minutes a suite waits for a slot before giving up (exit 75, "run it again"); the suites themselves are 30 to 40.
DEFAULT_MAX_WAIT = 180.0


def default_check_slots(root: Path | None = None) -> Slots:
    """The check slots of this machine (`check_slots` of coney.local.toml, else one per four cores), in the claims
    folder every worktree shares."""
    return build_claims.default_slots(CHECK_LANE, root)


def waiting_line(waiting: Waiting, slots: Slots, what: str, now: float) -> str:
    """The line a suite waiting for a check slot says: its place in the queue, a rough wait, who holds the slots and
    how many suites may run at once."""
    held = ", ".join(f"{h.agent} ({h.preset}, {h.age(now) / 60:.0f} min)" for h in waiting.holders) or "nobody"
    many = "suite" if slots.slots == 1 else "suites"
    return (
        f"waiting for a check slot ({what}): place {waiting.place} of {waiting.total}, "
        f"about {waiting.minutes:.0f} min; "
        f"held by {held}; {slots.slots} full {many} may run at once (check_slots in coney.local.toml)"
    )


@contextmanager
def hold(
    slots: Slots,
    agent: str,
    worktree: str,
    what: str,
    say: Callable[[str], None],
    *,
    max_wait: float = DEFAULT_MAX_WAIT,
    priority: bool = False,
    pid: int | None = None,
    poll: float = build_claims.POLL_SECONDS,
    report_every: float = build_claims.REPORT_SECONDS,
    abandon: Callable[[float, float], str | None] | None = None,
    progress: Progress | None = None,
) -> Iterator[None]:
    """Hold a check slot for a `with` block that runs the full suite `what` (`pytest`, `ctest`) as process `pid`
    (default: this one). Waits in the queue, saying the place with `say` when it starts to wait and every
    `report_every` seconds; raises `SlotTimeout` after `max_wait` minutes (or when `abandon` answers). Says so when it
    took over a stale claim (a suite whose process died) and when it waited. The slot is released however the block
    ends. With a `progress` (check_progress.py) each report of the wait also gives it the place and the estimate, the
    same values the line says, and it runs once the slot is held."""
    started = slots.clock()

    def report(waiting: Waiting) -> None:
        say(waiting_line(waiting, slots, what, slots.clock()))
        if progress is not None:
            holders = [holder.agent for holder in waiting.holders]
            progress.waiting(CHECK_LANE, waiting.place, waiting.total, waiting.minutes, holders)

    with slots.hold(
        agent,
        worktree,
        what,
        priority,
        os.getpid() if pid is None else pid,
        poll=poll,
        report=report,
        report_every=report_every,
        timeout=max_wait * 60,
        abandon=abandon,
    ) as claim:
        if slots.last_takeover is not None:
            say(f"took over a stale check slot of {slots.last_takeover.agent}")
            if slots.last_reaped:
                trees = "process tree" if slots.last_reaped == 1 else "process trees"
                say(f"stopped {slots.last_reaped} {trees} {slots.last_takeover.agent}'s run left running")
        waited = slots.clock() - started
        if waited >= 1:
            say(f"got check slot {claim.slot} after {waited / 60:.0f} min; running {what}")
        slots.set_step(claim, what)
        if progress is not None:
            progress.running()
        yield


def queued_message(timeout: SlotTimeout, what: str) -> str:
    """What a command says when it gave up waiting for a check slot: the place it had and that nothing was written."""
    return (
        f"the {what} run gave up waiting for a check slot (place {timeout.position} of {timeout.total}); "
        f"nothing was run: {timeout}"
    )

# SPDX-License-Identifier: GPL-3.0-or-later
"""Records a scenario on the running original: one CSV row per character update, with the scripted pad played.

The game runs two 60 Hz ticks a frame and counts them at `0x005104f4`; `Humans_Update` steps the characters on the
tick that makes the count even, so the count halved numbers the character updates (docs/research/tasks.md#tick).
The recorder polls that count together with every field in one PINE message and keeps a sample only when an update
has run since the last one; the step is the number of updates since the first sample (step 0, before any input).
After the sample of step N it writes the pad for frame N of the script, which the update of step N + 1 reads: the same
numbering as Coney's `--trace`, where frame N of an input script is step N + 1. A step the poll did not see is a
missed update (a frame that caught up runs two); its input is applied late, and the step is reported
(docs/guides/research-workflow.md#recording-a-trace).

The pad goes through the patched pad read (the `scripted-pad` patch): the button bytes at `0x005de3aa` / `0x005de3ab`
and the sticks at `0x005de3ac`-`0x005de3af` (right x, right y, left x, left y).

With hooks installed (coney_tools.hooks), each poll also reads the ring's count and, when it has moved, the new
entries, so a hook's log is read as the game runs. The ring's count is read between two reads of the tick count, and
the new entries are labelled from that count with entry_step(): the step of the update they were made in, the step
whose sample shows what they did. A poll often lands while the characters' tick is still running (the count even,
the update's sample not yet due), so the label comes from the count, not from the last sample's step. An entry of a
missed update goes to the step after it; a hook that logs `[0x005104f4]` pins its tick exactly.
"""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass, field

from coney_tools.game_memory import (
    HUMAN_COUNT,
    HUMAN_SIZE,
    HUMAN_TABLE,
    MATH,
    TYPES,
    ExpressionError,
    GameMemory,
    Job,
    Number,
    Value,
    convert,
    evaluate_all,
    to_raw,
)
from coney_tools.hooks import CALL_BASE, RING_BASE, Hook, HookLog, RingReader
from coney_tools.input_script import ScriptedPad
from coney_tools.pine import Read, Write
from coney_tools.scenario import Field, Scenario

#: The spare bytes after the scePadRead buffer the patched pad read takes the buttons and sticks from.
PAD_BYTES = 0x005DE3AA
#: The count of 60 Hz ticks; a character update runs on each tick that makes it even.
TICK_COUNTER = 0x005104F4


def entry_step(ticks: int, first_update: int) -> int:
    """The step of the update a hook entry made while the tick count is `ticks` belongs to. The pair of ticks that
    turns the count to 2Q and then 2Q + 1 is the update a sample labels Q + 1 (a sample sees it once the count is
    odd), and what runs after the pair, until the count turns even again, belongs to the same update: so both counts
    give Q + 1, the count halved, rounded down, plus one. `first_update` is the update of step 0."""
    return ticks // 2 + 1 - first_update


class RecordError(Exception):
    """A recording that cannot go on: the game stopped, or a name did not resolve."""


@dataclass
class Recording:
    """What a recording gives: the CSV columns and rows, and how well the poll kept up."""

    header: list[str]
    rows: list[list[Value]] = field(default_factory=list)
    missed: list[int] = field(default_factory=list)
    hooks: HookLog | None = None
    polls: int = 0
    seconds: float = 0.0
    words: int = 0
    #: The updates sampled (a row each, or a row per human with rows = "humans").
    updates: int = 0
    #: With rows = "humans": the rows as CSV lines (kept as text, a match being hundreds of thousands of rows).
    lines: list[str] = field(default_factory=list)
    #: With rows = "humans": the steps whose sample saw the next update start before it was read to the end.
    torn: list[int] = field(default_factory=list)

    def quality(self) -> str:
        """Every missed and torn step, a line each (`missed: 3 4`, `torn: 7`): where a long recording's samples
        cannot be trusted, which the summary line only starts to list."""
        return f"missed: {' '.join(map(str, self.missed))}\ntorn: {' '.join(map(str, self.torn))}\n"

    def csv(self) -> str:
        """The trace as CSV text, header first."""
        lines = [",".join(self.header)]
        lines += [",".join(str(value) for value in row) for row in self.rows]
        lines += self.lines
        return "\n".join(lines) + "\n"


def _format(value: Number, item: Field) -> Value:
    """A field's value as it goes into the CSV: a float with the field's decimals, an integer as it is."""
    if isinstance(value, float):
        return f"{value:.{item.digits}f}"
    return value


def _row(prefix: list[Value], fields: tuple[Field, ...], raw: list[Number]) -> list[Value]:
    """One CSV row: `prefix` (the step, and the human in a row per human), then each visible field (formulas worked
    out from the fields before them); `raw` holds the other fields' values in order."""
    values: dict[str, Number] = {}
    out: list[Value] = list(prefix)
    read = iter(raw)
    for item in fields:
        if item.formula is not None:

            def lookup(name: str) -> object:
                """A field before this one, else a math function."""
                return values[name] if name in values else MATH[name]

            try:
                values[item.name] = item.formula.evaluate(lookup)
            except ExpressionError:
                values[item.name] = float("nan")
        else:
            values[item.name] = next(read)
        if not item.hidden:
            out.append(_format(values[item.name], item))
    return out


def _value_job(item: Field, bound: dict[str, Value]) -> Job:
    """The job that works out a `value` field with the names `bound`."""
    assert item.value is not None
    value = item.value
    return lambda game: value.evaluate(game.names(bound))


def _follow_job(item: Field, bound: dict[str, Value]) -> Job:
    """The job that reads a `follow` field, its address worked out with the names `bound`."""
    assert item.address is not None
    address, kind = item.address, item.type
    return lambda game: game.read(int(address.evaluate(game.names(bound))), kind)


class Recorder:
    """Plays a scenario's input on a running original and samples its fields once per update."""

    def __init__(
        self,
        game: GameMemory,
        scenario: Scenario,
        clock: Callable[[], float] = time.monotonic,
        hooks: list[Hook] | None = None,
        started: Callable[[], None] | None = None,
    ) -> None:
        """Record `scenario` over `game`; `clock` (seconds) bounds how long it waits for the game. `hooks` are the
        hooks installed in the state, in the order build() gave them their ids; their ring is read every poll.
        `started` runs once, right after the sample of step 0: a run that holds the game paused at its start (an
        input recording's replay) resumes it there, so step 0 is the start itself."""
        self.game = game
        self.scenario = scenario
        self.clock = clock
        self.hooks = hooks or []
        self.started = started
        if not game.goal_types:
            game.goal_types = dict(scenario.goal_types)
        # With rows = "humans": each human's fixed reads, and the handle index they were worked out for.
        self._fixed: dict[int, list[Read | None]] = {}
        self._fixed_index: dict[int, int] = {}

    def resolve(self) -> tuple[dict[str, Value], list[tuple[Field, Read | None]]]:
        """Resolve the scenario's names and every field's address, once; a `follow` field's address is worked out
        again at each sample instead (None here)."""
        bound: dict[str, Value] = {}
        for name, expression in self.scenario.let:
            bound[name] = expression.evaluate(self.game.names(bound))
        reads: list[tuple[Field, Read | None]] = []
        if self.scenario.rows == "humans":
            # Each human's addresses are worked out now, before the first sample, and for a human that comes later
            # when it is first seen (_human_rows).
            for human in self.game.humans():
                self._fixed_reads(human, bound)
            return bound, reads
        names = self.game.names(bound)
        for item in self.scenario.fields:
            if item.value is not None:
                item.value.evaluate(names)
                reads.append((item, None))
            elif item.address is not None:
                if item.follow:
                    item.address.evaluate(names)
                    reads.append((item, None))
                    continue
                address = int(item.address.evaluate(names))
                reads.append((item, Read(address, TYPES[item.type][0])))
        return bound, reads

    def _sample(
        self, reads: list[tuple[Field, Read | None]], values: list[int], bound: dict[str, Value]
    ) -> list[Number]:
        """The address fields' values of one sample: the batch's `values` for fixed addresses, and a fresh read for
        each `follow` field, whose address (a chain of pointers) is worked out now."""
        names = self.game.names(bound)
        batch = iter(values)
        raw: list[Number] = []
        for item, read in reads:
            if read is not None:
                raw.append(convert(next(batch), item.type))
                continue
            try:
                if item.value is not None:
                    raw.append(item.value.evaluate(names))
                    continue
                assert item.address is not None
                raw.append(self.game.read(int(item.address.evaluate(names)), item.type))
            except ExpressionError:
                raw.append(float("nan"))
        return raw

    def _fixed_reads(self, human: int, bound: dict[str, Value]) -> list[Read | None]:
        """A human's read for each field with an address worked out once (None for the others), cached."""
        reads = self._fixed.get(human)
        if reads is None:
            self._fixed_index[human] = self.game.index(human)
            names = self.game.names({**bound, "h": human})
            reads = []
            for item in self.scenario.fields:
                if item.address is not None and not item.follow:
                    reads.append(Read(int(item.address.evaluate(names)), TYPES[item.type][0]))
                else:
                    reads.append(None)
            self._fixed[human] = reads
        return reads

    def _human_rows(self, step: int, bound: dict[str, Value]) -> list[list[Value]]:
        """The rows of one sample with rows = "humans": one per human in use, in table order. One message reads
        which humans are in use and every fixed address of the humans already seen; the `value` and `follow`
        fields are then worked out together in a few more (game_memory.evaluate_all)."""
        memory = self.game.memory
        fields = self.scenario.fields
        in_use = [Read(HUMAN_TABLE + i * HUMAN_SIZE + 0xD4, 4) for i in range(HUMAN_COUNT)]
        wanted = list(in_use)
        for human, reads in self._fixed.items():
            wanted += [read for read in reads if read is not None]
            wanted.append(Read(human + 0x92, 2))
        known = dict(zip(((read.address, read.size) for read in wanted), memory.batch(wanted), strict=True))
        humans = [HUMAN_TABLE + i * HUMAN_SIZE for i, read in enumerate(in_use) if known[(read.address, 4)]]
        for human in humans:
            # A slot reused by another human (another handle index) has other tables: work its addresses out again.
            if human in self._fixed and convert(known[(human + 0x92, 2)], "s16") != self._fixed_index[human]:
                del self._fixed[human]
        try:
            new = [human for human in humans if human not in self._fixed]
            more = [Read(human + 0x92, 2) for human in new]
            more += [read for human in new for read in self._fixed_reads(human, bound) if read is not None]
        except ExpressionError as error:
            raise RecordError(str(error)) from error
        if more:
            known.update(zip(((read.address, read.size) for read in more), memory.batch(more), strict=True))
        jobs: list[Job] = []
        for human in humans:
            for item in fields:
                if item.value is not None:
                    jobs.append(_value_job(item, {**bound, "h": human}))
                elif item.address is not None and item.follow:
                    jobs.append(_follow_job(item, {**bound, "h": human}))
        results = iter(evaluate_all(memory, known, jobs, self.scenario.goal_types))
        rows = []
        for human in humans:
            raw: list[Number] = []
            for item, read in zip(fields, self._fixed[human], strict=True):
                if read is not None:
                    raw.append(convert(known[(read.address, read.size)], item.type))
                elif item.formula is None:
                    raw.append(next(results))
            index = convert(known[(human + 0x92, 2)], "s16")
            rows.append(_row([step, index], fields, raw))
        return rows

    def _setup_writes(self, step: int, bound: dict[str, Value]) -> list[Write]:
        """The setup writes due after the sample of `step`, their values worked out now."""
        writes = []
        names = self.game.names(bound)
        for item in self.scenario.setup:
            if item.frame <= step <= item.until:
                address = int(item.address.evaluate(names))
                value = to_raw(item.value.evaluate(names), item.type)
                writes.append(Write(address, TYPES[item.type][0], value))
        return writes

    def _call_writes(self, step: int, bound: dict[str, Value]) -> list[Write]:
        """The call due after the sample of `step`: its arguments, then its function's address, which a call hook
        takes as the sign to make the call. Raises RecordError when the call before it has not been made."""
        due = [call for call in self.scenario.calls if call.frame == step]
        if not due:
            return []
        if not any(hook.call for hook in self.hooks):
            raise RecordError(f"{self.scenario.path}: has calls but no call hook among its patches")
        if self.game.read(CALL_BASE, "u32"):
            raise RecordError(f"the call before frame {step} was not made; does the call hook's function run?")
        names = self.game.names(bound)
        call = due[0]
        writes = [
            Write(CALL_BASE + 4 + 4 * k, 4, to_raw(arg.evaluate(names), "u32")) for k, arg in enumerate(call.args)
        ]
        return [*writes, Write(CALL_BASE, 4, to_raw(call.function.evaluate(names), "u32"))]

    def record(self, timeout: float | None = None) -> Recording:
        """Run the scenario for its updates. Raises RecordError when the game time stops for `timeout` seconds
        (default: a few seconds, more for a long run) or a name does not resolve."""
        try:
            bound, reads = self.resolve()
        except ExpressionError as error:
            raise RecordError(str(error)) from error
        fields = self.scenario.fields
        prefix = ["step", "human"] if self.scenario.rows == "humans" else ["step"]
        recording = Recording([*prefix, *(item.name for item in fields if not item.hidden)])
        batch_reads = [Read(TICK_COUNTER, 4), *(read for _, read in reads if read is not None)]
        ring = RingReader(self.game.memory, self.hooks) if self.hooks else None
        if ring is not None:
            # The ring's count, then the tick count again, go last, so the field values keep their places.
            batch_reads += [Read(RING_BASE, 4), Read(TICK_COUNTER, 4)]
            ring.done = self.game.memory.batch([Read(RING_BASE, 4)])[0]
        recording.words = len(batch_reads)
        pad = ScriptedPad(self.scenario.events)
        stall = timeout if timeout is not None else 5.0
        start = self.clock()
        last_progress = start
        first_update: int | None = None
        last_ticks: int | None = None
        step = -1
        written: bytes | None = None
        writes: list[Write] = []
        while step < self.scenario.updates:
            values = self.game.memory.batch(batch_reads, writes)
            writes = []
            recording.polls += 1
            now = self.clock()
            ticks = values[0]
            if first_update is None:
                first_update = (ticks + 1) // 2
            if ring is not None:
                ring_count, ticks_after = values[-2], values[-1]
                values = values[:-2]
                # Entries are drained only when the count did not move during the message, so none of them can be
                # newer than the tick count they are labelled from; the next poll takes them otherwise.
                if ticks_after == ticks:
                    ring.drain(ring_count, entry_step(ticks, first_update))
            if ticks == last_ticks:
                if now - last_progress > stall:
                    raise RecordError(f"the game stood still for {stall:.0f} s at step {step}; is PCSX2 paused?")
                continue
            last_ticks, last_progress = ticks, now
            # The update count moves on when the tick after an update's tick starts (the count turns odd), so a
            # sample never sees an update half done; a frame that catches up runs two updates, and a poll may miss one.
            new_step = (ticks + 1) // 2 - first_update
            if new_step <= step:
                continue
            recording.missed.extend(range(step + 1, new_step))
            step = new_step
            recording.updates += 1
            if self.scenario.rows == "humans":
                lines = self._human_rows(step, bound)
                recording.lines += [",".join(str(value) for value in row) for row in lines]
                if self.game.memory.batch([Read(TICK_COUNTER, 4)])[0] != ticks:
                    # Another update ran while the humans were read: this sample mixes two updates.
                    recording.torn.append(step)
            else:
                recording.rows.append(_row([step], fields, self._sample(reads, values[1:], bound)))
            if self.started is not None:
                started, self.started = self.started, None
                started()
                last_progress = self.clock()
            # The pad for frame `step`, read by the next update; then the setup writes due now.
            state = pad.advance(step).raw()
            if state != written:
                writes += [Write(PAD_BYTES + k, 1, byte) for k, byte in enumerate(state)]
                written = state
            writes += self._setup_writes(step, bound)
            writes += self._call_writes(step, bound)
        # Let go of everything, so the game is left standing.
        rest = bytes([0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80])
        self.game.memory.batch([], [Write(PAD_BYTES + k, 1, byte) for k, byte in enumerate(rest)])
        if ring is not None:
            # The entries made since the last poll, labelled by the tick count as in the loop.
            ticks, count = self.game.memory.batch([Read(TICK_COUNTER, 4), Read(RING_BASE, 4)])
            ring.drain(count, entry_step(ticks, first_update if first_update is not None else (ticks + 1) // 2))
            recording.hooks = ring.log
        recording.seconds = self.clock() - start
        return recording

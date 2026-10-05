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
entries, so a hook's log is read as the game runs and each entry is tagged with the step it was read at.
"""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass, field

from coney_tools.game_memory import MATH, TYPES, ExpressionError, GameMemory, Number, Value, convert, to_raw
from coney_tools.hooks import CALL_BASE, RING_BASE, Hook, HookLog, RingReader
from coney_tools.input_script import ScriptedPad
from coney_tools.pine import Read, Write
from coney_tools.scenario import Field, Scenario

#: The spare bytes after the scePadRead buffer the patched pad read takes the buttons and sticks from.
PAD_BYTES = 0x005DE3AA
#: The count of 60 Hz ticks; a character update runs on each tick that makes it even.
TICK_COUNTER = 0x005104F4


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

    def csv(self) -> str:
        """The trace as CSV text, header first."""
        lines = [",".join(self.header)]
        lines += [",".join(str(value) for value in row) for row in self.rows]
        return "\n".join(lines) + "\n"


def _format(value: Number, item: Field) -> Value:
    """A field's value as it goes into the CSV: a float with the field's decimals, an integer as it is."""
    if isinstance(value, float):
        return f"{value:.{item.digits}f}"
    return value


def _row(step: int, fields: tuple[Field, ...], raw: list[Number]) -> list[Value]:
    """One CSV row: the step, then each visible field (formulas worked out from the fields before them)."""
    values: dict[str, Number] = {}
    out: list[Value] = [step]
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


class Recorder:
    """Plays a scenario's input on a running original and samples its fields once per update."""

    def __init__(
        self,
        game: GameMemory,
        scenario: Scenario,
        clock: Callable[[], float] = time.monotonic,
        hooks: list[Hook] | None = None,
    ) -> None:
        """Record `scenario` over `game`; `clock` (seconds) bounds how long it waits for the game. `hooks` are the
        hooks installed in the state, in the order build() gave them their ids; their ring is read every poll."""
        self.game = game
        self.scenario = scenario
        self.clock = clock
        self.hooks = hooks or []

    def resolve(self) -> tuple[dict[str, Value], list[tuple[Field, Read | None]]]:
        """Resolve the scenario's names and every field's address, once; a `follow` field's address is worked out
        again at each sample instead (None here)."""
        bound: dict[str, Value] = {}
        for name, expression in self.scenario.let:
            bound[name] = expression.evaluate(self.game.names(bound))
        names = self.game.names(bound)
        reads: list[tuple[Field, Read | None]] = []
        for item in self.scenario.fields:
            if item.address is not None:
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
            assert item.address is not None
            try:
                raw.append(self.game.read(int(item.address.evaluate(names)), item.type))
            except ExpressionError:
                raw.append(float("nan"))
        return raw

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
        recording = Recording(["step", *(item.name for item in fields if not item.hidden)])
        batch_reads = [Read(TICK_COUNTER, 4), *(read for _, read in reads if read is not None)]
        ring = RingReader(self.game.memory, self.hooks) if self.hooks else None
        if ring is not None:
            # The ring's count goes last, so the field values keep their places.
            batch_reads.append(Read(RING_BASE, 4))
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
            if ring is not None:
                ring.drain(values[-1], step)
                values = values[:-1]
            if ticks == last_ticks:
                if now - last_progress > stall:
                    raise RecordError(f"the game stood still for {stall:.0f} s at step {step}; is PCSX2 paused?")
                continue
            last_ticks, last_progress = ticks, now
            # The update count moves on when the tick after an update's tick starts (the count turns odd), so a
            # sample never sees an update half done; a frame that catches up runs two updates, and a poll may miss one.
            update = (ticks + 1) // 2
            if first_update is None:
                first_update = update
            new_step = update - first_update
            if new_step <= step:
                continue
            recording.missed.extend(range(step + 1, new_step))
            step = new_step
            recording.rows.append(_row(step, fields, self._sample(reads, values[1:], bound)))
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
            ring.drain(self.game.memory.batch([Read(RING_BASE, 4)])[0], step)
            recording.hooks = ring.log
        recording.seconds = self.clock() - start
        return recording

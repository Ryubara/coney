# SPDX-License-Identifier: GPL-3.0-or-later
"""Records a scenario on the running original: one CSV row per character update, with the scripted pad played.

The game updates its characters every 1/30 s of game time, `*(0x0050b734) + 0x48` in milliseconds. The recorder
polls that time together with every field in one PINE message and keeps a sample only when the time has moved on by
an update; the step is the number of updates since the first sample (step 0, before any input). After the sample of
step N it writes the pad for frame N of the script, which the update of step N + 1 reads: the same numbering as
Coney's `--trace`, where frame N of an input script is step N + 1. A step the poll did not see is a missed update; its
input is applied late, and the step is reported (docs/guides/research-workflow.md#recording-a-trace).

The pad goes through the patched pad read (the `scripted-pad` patch): the button bytes at `0x005de3aa` / `0x005de3ab`
and the sticks at `0x005de3ac`-`0x005de3af` (right x, right y, left x, left y).
"""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass, field

from coney_tools.game_memory import MATH, TYPES, ExpressionError, GameMemory, Number, Value, convert, to_raw
from coney_tools.input_script import ScriptedPad
from coney_tools.pine import Read, Write
from coney_tools.scenario import Field, Scenario

#: The spare bytes after the scePadRead buffer the patched pad read takes the buttons and sticks from.
PAD_BYTES = 0x005DE3AA
#: One character update of game time, in milliseconds.
UPDATE_MS = 1000.0 / 30.0


class RecordError(Exception):
    """A recording that cannot go on: the game stopped, or a name did not resolve."""


@dataclass
class Recording:
    """What a recording gives: the CSV columns and rows, and how well the poll kept up."""

    header: list[str]
    rows: list[list[Value]] = field(default_factory=list)
    missed: list[int] = field(default_factory=list)
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

    def __init__(self, game: GameMemory, scenario: Scenario, clock: Callable[[], float] = time.monotonic) -> None:
        """Record `scenario` over `game`; `clock` (seconds) bounds how long it waits for the game."""
        self.game = game
        self.scenario = scenario
        self.clock = clock

    def resolve(self) -> tuple[dict[str, Value], list[tuple[Field, Read]], int]:
        """Resolve the scenario's names, every field's address and the game time's address, once."""
        bound: dict[str, Value] = {}
        for name, expression in self.scenario.let:
            bound[name] = expression.evaluate(self.game.names(bound))
        names = self.game.names(bound)
        reads = []
        for item in self.scenario.fields:
            if item.address is not None:
                address = int(item.address.evaluate(names))
                reads.append((item, Read(address, TYPES[item.type][0])))
        time_address = int(names("game_time"))
        return bound, reads, time_address

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

    def record(self, timeout: float | None = None) -> Recording:
        """Run the scenario for its updates. Raises RecordError when the game time stops for `timeout` seconds
        (default: a few seconds, more for a long run) or a name does not resolve."""
        try:
            bound, reads, time_address = self.resolve()
        except ExpressionError as error:
            raise RecordError(str(error)) from error
        fields = self.scenario.fields
        recording = Recording(["step", *(item.name for item in fields if not item.hidden)])
        batch_reads = [Read(time_address, 4), *(read for _, read in reads)]
        recording.words = len(batch_reads)
        pad = ScriptedPad(self.scenario.events)
        stall = timeout if timeout is not None else 5.0
        start = self.clock()
        last_progress = start
        first_time: int | None = None
        last_time: int | None = None
        step = -1
        written: bytes | None = None
        writes: list[Write] = []
        while step < self.scenario.updates:
            values = self.game.memory.batch(batch_reads, writes)
            writes = []
            recording.polls += 1
            now = self.clock()
            game_time = values[0]
            if game_time == last_time:
                if now - last_progress > stall:
                    raise RecordError(f"the game time stood still for {stall:.0f} s at step {step}; is PCSX2 paused?")
                continue
            last_time, last_progress = game_time, now
            if first_time is None:
                first_time = game_time
            # Count updates by game time: the game may run two frames per update, and a poll may miss one.
            new_step = round((game_time - first_time) / UPDATE_MS)
            if new_step <= step:
                continue
            recording.missed.extend(range(step + 1, new_step))
            step = new_step
            raw = [convert(value, item.type) for (item, _), value in zip(reads, values[1:], strict=True)]
            recording.rows.append(_row(step, fields, raw))
            # The pad for frame `step`, read by the next update; then the setup writes due now.
            state = pad.advance(step).raw()
            if state != written:
                writes += [Write(PAD_BYTES + k, 1, byte) for k, byte in enumerate(state)]
                written = state
            writes += self._setup_writes(step, bound)
        # Let go of everything, so the game is left standing.
        rest = bytes([0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80])
        self.game.memory.batch([], [Write(PAD_BYTES + k, 1, byte) for k, byte in enumerate(rest)])
        recording.seconds = self.clock() - start
        return recording

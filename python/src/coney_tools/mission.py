# SPDX-License-Identifier: GPL-3.0-or-later
"""Mission playthroughs on either game, driven by one course, each writing an event log
(docs/guides/research-workflow.md#differential-playthroughs).

A **mission scenario** (`research/traces/missions/*.toml`) names the course that plays it (coney_tools.course), the
event that ends it, how the original starts (a save state and its patches, the event hooks among them) and how Coney
starts (the level and checkpoint the Story path loads):

```toml
description = "level99 checkpoint 1: the combat tutorial, to checkpoint 2"
research = "docs/research/scripting.md#level99-lessons"
course = "level99-tutorial"
updates = 40000                      # at most
until = "checkpoint SetCheckPoint:2"  # a normalized event (kind name) that ends the run
after = 60                           # updates played after it

[original]
state = "states/l99-cp1.p2s"         # under scratch_dir, never committed
patches = ["scripted-pad", "right-stick", "event-call", "event-callback", "event-hint", "event-sound"]
friends = ["Ash", "Rembrandt"]        # humans never fought, by name
enemy_brain = 4                      # the brain type (+0x04) of the humans to fight

[coney]
level = "level99"
args = ["--checkpoint", "1"]
```

Two games give the course the same Observation and the same events each update:

- ConeyGame runs Coney headless with `--pad-pipe` (its observation and events on standard output, the pad on
  standard input, in lock step) and `--event-log`.
- OriginalGame plays a PCSX2 started on the patched state over PINE: the pad through the `scripted-pad` patch, the
  events from the event hooks' ring (events.original_events()) and a watch of the human table, the observation read
  from the game's tables once an update.
"""

from __future__ import annotations

import contextlib
import csv
import io
import json
import math
import struct
import subprocess
import time
import tomllib
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import IO, Protocol

from coney_tools.course import COURSES, Course, Observation, Seen
from coney_tools.events import Event, HookEvents, Rules, normalize, original_events
from coney_tools.game_memory import (
    BRAIN_SIZE,
    BRAIN_TABLE,
    CAMERA_POINTER,
    HUMAN_COUNT,
    HUMAN_SIZE,
    HUMAN_TABLE,
    TRANSFORM_SIZE,
    TRANSFORM_TABLE,
)
from coney_tools.hooks import RING_BASE, Hook, RingReader, entry_row
from coney_tools.input_script import PadState
from coney_tools.pine import Memory, Read, Write
from coney_tools.recorder import PAD_BYTES, TICK_COUNTER, entry_step

#: A human's state flags that mean down, dying, dead or knocked out (`Human_IsDownOrDead`, 0x00227e60: the 64-bit
#: word at record +0x00).
DOWN_FLAGS = 0x180050000
#: The world object holding the object task manager (+0x840), whose spawn records are at +0x14 (count +0x18).
WORLD_POINTER = 0x00512C7C
#: The pointer to the object database: 0x90-byte type records whose name is at +0x28, indexed by a spawn record's
#: type (`ObjTypeList_Get`, 0x00391330; docs/research/objects.md#object-types).
OBJECT_DB_POINTER = 0x00512C04
#: The live objects' handle table: 8 bytes an entry, the object pointer first; an object's position is at +0x10.
HANDLE_TABLE = 0x006EBD38
#: How often play() reports its progress, in updates.
PROGRESS_EVERY = 1000


class MissionError(Exception):
    """A mission scenario that cannot be read, or a game that stopped."""


@dataclass
class MissionScenario:
    """A mission scenario file (the module docstring)."""

    path: Path
    description: str
    research: str
    course: str
    updates: int
    until: tuple[str, str] | None
    after: int
    state: str | None
    patches: list[str]
    friends: list[str]
    enemy_brain: int | None
    level: str
    args: list[str]


def load_mission(path: Path) -> MissionScenario:
    """Read a mission scenario. Raises MissionError when it is malformed or names an unknown course."""
    try:
        with path.open("rb") as handle:
            raw = tomllib.load(handle)
        original, coney = raw.get("original", {}), raw.get("coney", {})
        course = str(raw["course"])
        if course not in COURSES:
            raise MissionError(f"{path}: no course {course!r}; known: {', '.join(sorted(COURSES))}")
        until = str(raw.get("until", "")).strip()
        kind, _, name = until.partition(" ")
        return MissionScenario(
            path=path,
            description=str(raw.get("description", "")),
            research=str(raw.get("research", "")),
            course=course,
            updates=int(raw.get("updates", 30000)),
            until=(kind, name.strip()) if until else None,
            after=int(raw.get("after", 60)),
            state=str(original["state"]) if "state" in original else None,
            patches=[str(p) for p in original.get("patches", [])],
            friends=[str(f) for f in original.get("friends", [])],
            enemy_brain=int(original["enemy_brain"]) if "enemy_brain" in original else None,
            level=str(coney.get("level", "")),
            args=[str(a) for a in coney.get("args", [])],
        )
    except (OSError, tomllib.TOMLDecodeError, KeyError, TypeError, ValueError) as error:
        raise MissionError(f"{path}: {error}") from error


class Game(Protocol):
    """One game a course plays: start() gives the first update's observation and events, step() plays a pad for one
    update and gives the next ones (None when the game has ended)."""

    def start(self, objects: Sequence[str]) -> tuple[Observation, list[Event]]:
        """Start the game, observing the world objects of the type prefixes `objects`."""
        ...

    def step(self, pad: PadState) -> tuple[Observation, list[Event]] | None:
        """Play `pad` for one update."""
        ...


@dataclass
class Playthrough:
    """What play() gives: every event, the updates played, and whether the scenario's end was reached."""

    events: list[Event] = field(default_factory=list)
    updates: int = 0
    finished: bool = False


def play(
    game: Game,
    course: Course,
    rules: Rules,
    updates: int,
    until: tuple[str, str] | None = None,
    after: int = 0,
    progress: Callable[[int, Course], None] | None = None,
) -> Playthrough:
    """Play `course` on `game` for at most `updates`, or until the event `until` (normalized kind and name) and
    `after` more updates. `progress` is told every 1000 updates."""
    result = Playthrough()
    observation, events = game.start(course.objects)
    result.events += events
    stop_at: int | None = None
    while result.updates < updates:
        course.see(observation, events)
        if (
            until is not None
            and stop_at is None
            and any((n.kind, n.name) == until for n in (normalize(e, rules) for e in events))
        ):
            result.finished = True
            stop_at = result.updates + after
        if stop_at is not None and result.updates >= stop_at:
            break
        stepped = game.step(course.pad())
        if stepped is None:
            break
        observation, events = stepped
        result.events += events
        result.updates += 1
        if progress is not None and result.updates % PROGRESS_EVERY == 0:
            progress(result.updates, course)
    return result


# --- Coney --------------------------------------------------------------------------------------------------------


def parse_observation(text: str) -> Observation:
    """An observation from Coney's `@obs` JSON (src/platform/pad_pipe.h)."""
    raw = json.loads(text)
    observation = Observation(int(raw.get("frame", 0)), bool(raw.get("play", False)))
    if observation.play:
        observation.player = tuple(float(v or 0.0) for v in raw["player"])  # type: ignore[assignment]
        observation.camera = tuple(float(v or 0.0) for v in raw["camera"])  # type: ignore[assignment]
        observation.humans = [
            Seen(float(h[0] or 0.0), float(h[1] or 0.0), bool(h[2]), bool(h[3]), bool(h[4])) for h in raw["humans"]
        ]
        observation.objects = [(str(o[0]), float(o[1] or 0.0), float(o[2] or 0.0), int(o[3])) for o in raw["objects"]]
    return observation


def parse_event_line(text: str) -> Event | None:
    """An event from an `@ev` line's CSV (`step,kind,name,detail`); None when it is not one."""
    row = next(csv.reader(io.StringIO(text)), None)
    if not row or len(row) < 3:
        return None
    try:
        return Event(int(row[0]), row[1], row[2], row[3] if len(row) > 3 else "")
    except ValueError:
        return None


def pad_line(pad: PadState) -> str:
    """The `pad` line of a pad for `--pad-pipe`: the buttons in hexadecimal, then the four stick bytes."""
    return f"pad {pad.buttons:x} {' '.join(str(b) for b in pad.sticks)}\n"


class ConeyGame:
    """Coney headless under `--pad-pipe` (the module docstring); its own `--event-log` is written too."""

    def __init__(self, command: list[str], stderr: IO[str] | None = None) -> None:
        """Run `command` (a Coney command line with `--pad-pipe`) when start() is called."""
        self.command = command
        self.stderr = stderr
        self.process: subprocess.Popen[str] | None = None
        self.objects: list[str] = []

    def _read(self) -> tuple[Observation, list[Event]] | None:
        """Lines up to the next observation: the events before it, and it; None at the end of Coney's output."""
        assert self.process is not None and self.process.stdout is not None
        events: list[Event] = []
        for line in self.process.stdout:
            if line.startswith("@ev "):
                event = parse_event_line(line[4:].rstrip("\r\n"))
                if event is not None:
                    events.append(event)
            elif line.startswith("@obs "):
                return parse_observation(line[5:]), events
            elif line.startswith("@error "):
                raise MissionError(f"Coney: {line[7:].strip()}")
            elif self.stderr is not None:
                self.stderr.write(line)  # what Coney prints itself, kept with its errors
        return None

    def start(self, objects: Sequence[str]) -> tuple[Observation, list[Event]]:
        """Start Coney and read its first observation."""
        self.process = subprocess.Popen(
            self.command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr if self.stderr is not None else subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )
        self.objects = list(objects)
        first = self._read()
        if first is None:
            raise MissionError(f"Coney ended before its first frame (exit {self.process.wait()})")
        return first

    def step(self, pad: PadState) -> tuple[Observation, list[Event]] | None:
        """Send the pad (after the objects to observe, the first time) and read the next observation."""
        assert self.process is not None and self.process.stdin is not None
        try:
            if self.objects:
                self.process.stdin.write(f"observe {' '.join(self.objects)}\n")
                self.objects = []
            self.process.stdin.write(pad_line(pad))
            self.process.stdin.flush()
        except (BrokenPipeError, OSError):
            return None
        return self._read()

    def close(self) -> int:
        """End Coney (its input closed, so it plays out released pads and stops at its frame limit) and return its
        exit code."""
        if self.process is None:
            return 0
        if self.process.stdin is not None:
            with contextlib.suppress(OSError):
                self.process.stdin.close()
        self.process.kill()
        return self.process.wait()


# --- the original -------------------------------------------------------------------------------------------------


def _f32(word: int) -> float:
    """A word as a float."""
    return float(struct.unpack("<f", struct.pack("<I", word & 0xFFFFFFFF))[0])


def _s16(word: int) -> int:
    """The low half-word as a signed number."""
    word &= 0xFFFF
    return word - 0x10000 if word & 0x8000 else word


def _text(words: Sequence[int]) -> str:
    """A NUL-terminated name from 8-byte words."""
    data = b"".join(w.to_bytes(8, "little") for w in words)
    return data.split(b"\0", 1)[0].decode("latin-1")


class OriginalGame:
    """The original in a PCSX2 running a patched state, over PINE (the module docstring)."""

    def __init__(
        self,
        memory: Memory,
        hooks: list[Hook],
        mapping: Sequence[HookEvents],
        bindings: dict[int, str],
        friends: Sequence[str] = (),
        enemy_brain: int | None = None,
        arity: dict[str, int] | None = None,
        clock: Callable[[], float] = time.monotonic,
        stall: float = 10.0,
    ) -> None:
        """Play over `memory`, with `hooks` installed (in the order of their ids); `mapping` and `bindings` turn their
        log into events (`arity`: each binding's argument count). `friends` and `enemy_brain` tell the enemies from the
        rest."""
        self.memory = memory
        self.hooks = hooks
        self.mapping = mapping
        self.bindings = bindings
        self.arity = arity
        self.friends = set(friends)
        self.enemy_brain = enemy_brain
        self.clock = clock
        self.stall = stall
        self.ring = RingReader(memory, hooks) if hooks else None
        self.taken = {hook.name: 0 for hook in hooks}
        self.first_update = 0
        self.step_number = 0
        self.objects: list[str] = []
        self.humans_out: dict[int, bool] = {}
        self.names: dict[tuple[int, int], str] = {}
        self.object_names: dict[int, str] = {}

    # --- time ---

    def _wait_update(self) -> None:
        """Poll until an update after the current step has run, draining the ring meanwhile."""
        start = self.clock()
        while True:
            reads = [Read(TICK_COUNTER, 4)]
            if self.ring is not None:
                reads += [Read(RING_BASE, 4), Read(TICK_COUNTER, 4)]
            values = self.memory.batch(reads)
            ticks = values[0]
            if self.ring is not None and values[2] == ticks:
                self.ring.drain(values[1], entry_step(ticks, self.first_update))
            if (ticks + 1) // 2 - self.first_update > self.step_number:
                self.step_number = (ticks + 1) // 2 - self.first_update
                return
            if self.clock() - start > self.stall:
                raise MissionError(f"the game stood still for {self.stall:.0f} s at step {self.step_number}")

    def _new_events(self) -> list[Event]:
        """The events of the ring entries not yet turned into events."""
        if self.ring is None:
            return []
        logs: dict[str, list[dict[str, str]]] = {}
        for hook in self.hooks:
            entries = self.ring.log.entries[hook.name]
            fresh = entries[self.taken[hook.name] :]
            self.taken[hook.name] = len(entries)
            # Only the new entries are kept: a long run would otherwise hold every call in memory twice.
            logs[hook.name] = [entry_row(hook, seq, step, values) for seq, step, values in fresh]
            entries.clear()
            self.taken[hook.name] = 0
        return original_events(logs, self.mapping, self.bindings, self.arity)

    # --- the observation ---

    def _humans(self) -> tuple[list[tuple[int, int, int, int, int, str]], int | None]:
        """Each human in use: (table address, record, handle index, player number, brain type, name); and the
        player's table address."""
        records = self.memory.batch([Read(HUMAN_TABLE + i * HUMAN_SIZE + 0xD4, 4) for i in range(HUMAN_COUNT)])
        used = [(HUMAN_TABLE + i * HUMAN_SIZE, record) for i, record in enumerate(records) if record]
        reads: list[Read] = []
        for address, record in used:
            reads += [Read(address + 0x92, 2), Read(address + 0x1B0, 1)]
            if (address, record) not in self.names:
                reads += [Read(address + 0x80, 8), Read(address + 0x88, 8)]
        values = iter(self.memory.batch(reads) if reads else [])
        partial = []
        for address, record in used:
            index, number = _s16(next(values)), next(values)
            if (address, record) not in self.names:
                self.names[(address, record)] = _text([next(values), next(values)])
            partial.append((address, record, index, number - 0x100 if number & 0x80 else number))
        brains = self.memory.batch([Read(BRAIN_TABLE + index * BRAIN_SIZE + 4, 4) for _, _, index, _ in partial])
        humans = [
            (address, record, index, number, brain, self.names[(address, record)])
            for (address, record, index, number), brain in zip(partial, brains, strict=True)
        ]
        player = next((h[0] for h in humans if h[3] == 0), None)
        return humans, player

    def _objects(self) -> list[tuple[str, float, float, int]]:
        """The spawn records of the observed types that are neither removed nor hidden: the type, the place (the live
        object's, else the record's) and whether it shows (1, 0, or -1 when it has no live object)."""
        if not self.objects:
            return []
        world = self.memory.batch([Read(WORLD_POINTER, 4)])[0]
        if not world:
            return []
        manager = self.memory.batch([Read(world + 0x840, 4)])[0]
        if not manager:
            return []
        base, count = self.memory.batch([Read(manager + 0x14, 4), Read(manager + 0x18, 4)])
        count = min(count, 4096)
        if not base or not count:
            return []
        types = self.memory.batch([Read(base + i * 0x28 + 0x20, 4) for i in range(count)])
        flags = self.memory.batch([Read(base + i * 0x28 + 0x24, 4) for i in range(count)])
        unknown = sorted({t & 0x3FFF for t in types} - set(self.object_names))
        database = self.memory.batch([Read(OBJECT_DB_POINTER, 4)])[0] if unknown else 0
        if unknown and database:
            words = self.memory.batch([Read(database + t * 0x90 + 0x28 + 8 * k, 8) for t in unknown for k in range(4)])
            for n, t in enumerate(unknown):
                self.object_names[t] = _text(words[4 * n : 4 * n + 4])
        chosen = [
            i
            for i in range(count)
            if any(self.object_names.get(types[i] & 0x3FFF, "").startswith(p) for p in self.objects)
            and not flags[i] & (0x40000 | 0x100000)
        ]
        out = []
        for i in chosen:
            live = flags[i] & 0xFFFF
            x_word, y_word = self.memory.batch([Read(base + i * 0x28 + 0x08, 4), Read(base + i * 0x28 + 0x0C, 4)])
            shown = -1
            obj = self.memory.batch([Read(HANDLE_TABLE + live * 8, 4)])[0] if live != 0xFFFF else 0
            if obj:
                # A live object: its place, and whether it shows (its tint's alpha, +0xcc's first byte, above 0; an
                # objective marker fades it in on ObjShow and out on ObjHide).
                x_word, y_word, alpha = self.memory.batch(
                    [Read(obj + 0x10, 4), Read(obj + 0x14, 4), Read(obj + 0xCC, 1)]
                )
                shown = 1 if alpha else 0
            out.append((self.object_names.get(types[i] & 0x3FFF, ""), _f32(x_word), _f32(y_word), shown))
        return out

    def _observe(self) -> tuple[Observation, list[Event]]:
        """The observation now, and the events of the human watch."""
        humans, player = self._humans()
        observation = Observation(self.step_number, player is not None)
        events: list[Event] = []
        if player is None:
            return observation, events
        reads: list[Read] = []
        for _, record, index, _, _, _ in humans:
            tf = TRANSFORM_TABLE + index * TRANSFORM_SIZE
            reads += [Read(tf, 4), Read(tf + 4, 4), Read(tf + 8, 4), Read(tf + 0x18, 4), Read(tf + 0x1C, 4)]
            reads += [Read(record + 0x144, 2), Read(record, 8)]
        camera = self.memory.batch([Read(CAMERA_POINTER, 4)])[0]
        if camera:
            reads += [Read(camera + 0x10, 4), Read(camera + 0x14, 4), Read(camera + 0x180, 4), Read(camera + 0x184, 4)]
        values = self.memory.batch(reads)
        seen_now: set[int] = set()
        for n, (address, _, _, number, brain, name) in enumerate(humans):
            x, y, z, qz, qw, health, state = values[7 * n : 7 * n + 7]
            alive = _s16(health) > 0
            if address == player:
                heading = math.degrees(2.0 * math.atan2(_f32(qz), _f32(qw)))
                observation.player = (_f32(x), _f32(y), _f32(z), heading)
                continue
            standing = alive and not state & DOWN_FLAGS
            enemy = number < 0 and name not in self.friends
            enemy = enemy and (self.enemy_brain is None or brain == self.enemy_brain)
            observation.humans.append(Seen(_f32(x), _f32(y), alive, standing, enemy))
            # The human watch: in, out of health, gone.
            seen_now.add(address)
            if address not in self.humans_out:
                events.append(Event(self.step_number, "human_in", name))
            elif not alive and not self.humans_out[address]:
                events.append(Event(self.step_number, "human_out", name))
            self.humans_out[address] = not alive
        for address in sorted(set(self.humans_out) - seen_now - {player}):
            events.append(Event(self.step_number, "human_gone", ""))
            del self.humans_out[address]
        if camera:
            eye_x, eye_y, look_x, look_y = (_f32(v) for v in values[7 * len(humans) :])
            observation.camera = (eye_x, eye_y, look_x, look_y)
        observation.objects = self._objects()
        return observation, events

    # --- Game ---

    def start(self, objects: Sequence[str]) -> tuple[Observation, list[Event]]:
        """Take the update now as step 0."""
        self.objects = list(objects)
        ticks = self.memory.batch([Read(TICK_COUNTER, 4)])[0]
        self.first_update = (ticks + 1) // 2
        if self.ring is not None:
            self.ring.done = self.memory.batch([Read(RING_BASE, 4)])[0]
        observation, watched = self._observe()
        return observation, watched

    def step(self, pad: PadState) -> tuple[Observation, list[Event]] | None:
        """Write the pad, wait for the update that reads it, and observe."""
        self.memory.batch([], [Write(PAD_BYTES + k, 1, byte) for k, byte in enumerate(pad.raw())])
        self._wait_update()
        events = self._new_events()
        observation, watched = self._observe()
        return observation, events + watched

    def release(self) -> None:
        """Let go of every button and stick."""
        rest = bytes([0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80])
        self.memory.batch([], [Write(PAD_BYTES + k, 1, byte) for k, byte in enumerate(rest)])

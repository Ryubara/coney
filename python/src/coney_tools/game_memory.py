# SPDX-License-Identifier: GPL-3.0-or-later
"""Addresses in the running original, written as small expressions: `tf(player) + 0x4`, `rec(human("PoizoCiv"))`.

A scenario names what to read and write with these expressions instead of fixed addresses, because the objects move:
the player is not always human 0, and the per-human tables are indexed by the human's handle. The expressions are
plain arithmetic (`+ - * / // % & | ^ << >> ~`, numbers, strings) over the names and functions below; nothing else
of Python is allowed, so a scenario file cannot run code.

Names and functions (the layouts are on the research pages named; every address is in NTSC-U `SLUS_212.15`):

- `player`: the human the player controls, the one of the 60 humans (`0x00640c80 + i * 0x6d0`) in use (`+0xd4` not
  0) whose player number `+0x1b0` is 0 (docs/research/characters.md#the-human-object).
- `human(name)`: the human in use whose name (`+0x80`, up to 15 characters) is `name`.
- `index(h)`: the human's handle index, the `s16` at `+0x92`.
- `rec(h)`: the human's record, the pointer at `+0xd4` (docs/research/characters.md#the-record).
- `prec(h)`: its per-player record, `0x00660f50 + index * 0x2c`.
- `tf(h)`: its transform, `0x00714b00 + index * 0x20`: position `x, y, z` at `+0x0`, rotation quaternion at `+0x10`.
- `brain(h)`: its second per-human record, `0x006d53f0 + index * 0x2f0`.
- `heading(h)`: its heading in radians, `2 * atan2(qz, qw)` of the transform's rotation (0 faces +y, anticlockwise).
- `camera`: the player's follow camera, `*(0x005d9158)` (docs/research/camera.md).
- `game_time`: the address of the game time in milliseconds, `*(0x0050b734) + 0x48` (docs/research/boot.md).
- `u8 s8 u16 s16 u32 s32 f32 (address)`: the value in memory.
- `hidx(handle)`: the handle index a handle names (its high half), or -1 for the null handle `0xffffffff` or a stale
  one (its low half, the serial, differs from the handle table's at `0x006ebd38 + index * 8 + 4`; `Handle_Resolve`
  `0x00390180`). A human's handle is its `u32` at `+0x90`, so `hidx` of it is `index(h)`.
- `goal_type(vtable)`: a goal class's type id (docs/references/goal-types.md), -2 for a vtable not listed;
  `goal_at(brain, k)`: the type id of the goal in slot k of the brain's stack (`+0x40`), -1 above the top (`+0x2c`).
- `handles(address, n)`: how many of the n words from `address` are neither 0 nor the null handle (a handle
  list's length).
- `sin cos atan2 hypot sqrt abs min max`, `deg(radians)`, `rad(degrees)`, `wrap(degrees)` (to -180..180), `pi`.

Field formulas (derived columns) use the same grammar, with the scenario's other fields as names.
"""

from __future__ import annotations

import ast
import math
import struct
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from coney_tools.pine import Memory, Read, Write

HUMAN_TABLE = 0x00640C80
HUMAN_SIZE = 0x6D0
HUMAN_COUNT = 60
PER_PLAYER_TABLE = 0x00660F50
PER_PLAYER_SIZE = 0x2C
TRANSFORM_TABLE = 0x00714B00
TRANSFORM_SIZE = 0x20
BRAIN_TABLE = 0x006D53F0
BRAIN_SIZE = 0x2F0
HANDLE_TABLE = 0x006EBD38
NULL_HANDLE = 0xFFFFFFFF
CAMERA_POINTER = 0x005D9158
GAME_TIMER_POINTER = 0x0050B734
GAME_TIME_OFFSET = 0x48

#: Each memory type: its size in bytes and the struct format that reads it.
TYPES: dict[str, tuple[int, str]] = {
    "u8": (1, "<B"),
    "s8": (1, "<b"),
    "u16": (2, "<H"),
    "s16": (2, "<h"),
    "u32": (4, "<I"),
    "s32": (4, "<i"),
    "f32": (4, "<f"),
}

Number = int | float
Value = int | float | str


class ExpressionError(Exception):
    """An expression that cannot be parsed or evaluated; the message quotes it."""


def convert(raw: int, kind: str) -> Number:
    """The unsigned `raw` bytes read as a value of `kind` (a key of TYPES)."""
    size, fmt = TYPES[kind]
    value: Number = struct.unpack(fmt, raw.to_bytes(size, "little"))[0]
    return value


def to_raw(value: Number, kind: str) -> int:
    """`value` as the unsigned bytes of `kind` (a key of TYPES): what a write of that type stores."""
    size, fmt = TYPES[kind]
    data = (
        struct.pack(fmt, float(value))
        if kind == "f32"
        else struct.pack(fmt.upper(), int(value) & ((1 << (8 * size)) - 1))
    )
    return int.from_bytes(data, "little")


def _wrap_degrees(degrees: float) -> float:
    """`degrees` wrapped to (-180, 180]."""
    wrapped = math.fmod(degrees + 180.0, 360.0)
    if wrapped <= 0:
        wrapped += 360.0
    return wrapped - 180.0


#: The math every expression may use.
MATH: dict[str, Any] = {
    "sin": math.sin,
    "cos": math.cos,
    "atan2": math.atan2,
    "hypot": math.hypot,
    "sqrt": math.sqrt,
    "abs": abs,
    "min": min,
    "max": max,
    "deg": math.degrees,
    "rad": math.radians,
    "wrap": _wrap_degrees,
    "pi": math.pi,
}

_BINARY: dict[type[ast.operator], Callable[[Any, Any], Any]] = {
    ast.Add: lambda a, b: a + b,
    ast.Sub: lambda a, b: a - b,
    ast.Mult: lambda a, b: a * b,
    ast.Div: lambda a, b: a / b,
    ast.FloorDiv: lambda a, b: a // b,
    ast.Mod: lambda a, b: a % b,
    ast.Pow: lambda a, b: a**b,
    ast.BitAnd: lambda a, b: a & b,
    ast.BitOr: lambda a, b: a | b,
    ast.BitXor: lambda a, b: a ^ b,
    ast.LShift: lambda a, b: a << b,
    ast.RShift: lambda a, b: a >> b,
}
_UNARY: dict[type[ast.unaryop], Callable[[Any], Any]] = {
    ast.USub: lambda a: -a,
    ast.UAdd: lambda a: +a,
    ast.Invert: lambda a: ~a,
}


@dataclass(frozen=True)
class Expression:
    """A parsed expression; evaluate() runs it against a set of names."""

    text: str
    tree: ast.expr

    def names(self) -> set[str]:
        """Every bare name the expression uses (functions called by name excluded)."""
        called = {id(node.func) for node in ast.walk(self.tree) if isinstance(node, ast.Call)}
        return {node.id for node in ast.walk(self.tree) if isinstance(node, ast.Name) and id(node) not in called}

    def evaluate(self, names: Callable[[str], Any]) -> Any:
        """The expression's value; `names` gives each name's value (raising KeyError for an unknown one). A dict that
        is also callable (GameMemory.names()) is used as the variables as it is, which saves a call per name.

        It runs as compiled Python with no builtins and the names as its only variables: parse() has checked that the
        tree holds nothing but constants, names, arithmetic and calls by name, so nothing else can run (and a
        recording of every human works out thousands of expressions an update, too many for a tree walk)."""
        try:
            scope = names if isinstance(names, dict) else _Names(names)
            return eval(_code(self), {"__builtins__": {}}, scope)
        except KeyError as error:
            raise ExpressionError(f"{self.text!r}: unknown name {error.args[0]!r}") from error
        except NameError as error:
            raise ExpressionError(f"{self.text!r}: unknown name {error.name!r}") from error
        except (ArithmeticError, TypeError, ValueError) as error:
            raise ExpressionError(f"{self.text!r}: {error}") from error


class _Names(Mapping[str, Any]):
    """The names of one evaluation, as the variables compiled code looks up."""

    def __init__(self, names: Callable[[str], Any]) -> None:
        """Look names up with `names`."""
        self._names = names

    def __getitem__(self, key: str) -> Any:
        """One name's value (KeyError for an unknown one)."""
        return self._names(key)

    def __iter__(self) -> Iterator[str]:
        """No listing: names are only looked up."""
        return iter(())

    def __len__(self) -> int:
        """No listing: names are only looked up."""
        return 0


_CODE: dict[str, Any] = {}


def _code(expression: Expression) -> Any:
    """The compiled code of a parsed expression, made once per text."""
    code = _CODE.get(expression.text)
    if code is None:
        code = compile(ast.Expression(expression.tree), "<expression>", "eval")
        _CODE[expression.text] = code
    return code


def parse(text: str) -> Expression:
    """Parse `text`. Raises ExpressionError for a syntax error or anything outside the grammar (attributes,
    subscripts, keyword arguments, comparisons, ...)."""
    try:
        tree = ast.parse(text.strip(), mode="eval").body
    except SyntaxError as error:
        raise ExpressionError(f"{text!r}: {error.msg}") from error
    for node in ast.walk(tree):
        allowed = (
            (isinstance(node, ast.Constant) and isinstance(node.value, int | float | str))
            or isinstance(node, ast.Name | ast.Load)
            or (isinstance(node, ast.BinOp) and type(node.op) in _BINARY)
            or (isinstance(node, ast.UnaryOp) and type(node.op) in _UNARY)
            or (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and not node.keywords)
            or isinstance(node, ast.operator | ast.unaryop)
        )
        if not allowed:
            raise ExpressionError(f"{text!r}: {type(node).__name__} is not allowed in an address expression")
    return Expression(text, tree)


class GameMemory:
    """The original's objects over a Memory (a PINE connection or a fake): the names of the module docstring."""

    def __init__(self, memory: Memory, goal_types: Mapping[int, int] | None = None) -> None:
        """Wrap `memory`; nothing is read until a name is used. `goal_types` maps goal vtables to type ids
        (docs/references/goal-types.md) for goal_type() and goal_at()."""
        self.memory = memory
        self.goal_types = dict(goal_types or {})
        self._player: int | None = None
        self.functions: dict[str, Any] = {
            **MATH,
            **{kind: self._reader(kind) for kind in TYPES},
            "human": self.human,
            "index": self.index,
            "rec": lambda h: self.read(h + 0xD4, "u32"),
            "prec": lambda h: PER_PLAYER_TABLE + self.index(h) * PER_PLAYER_SIZE,
            "tf": lambda h: TRANSFORM_TABLE + self.index(h) * TRANSFORM_SIZE,
            "brain": lambda h: BRAIN_TABLE + self.index(h) * BRAIN_SIZE,
            "heading": self.heading,
            "hidx": self.hidx,
            "goal_type": self.goal_type,
            "goal_at": self.goal_at,
            "handles": self.handles,
        }

    def read(self, address: int, kind: str) -> Number:
        """One value of `kind` at `address`."""
        size = TYPES[kind][0]
        return convert(self.memory.batch([Read(address, size)])[0], kind)

    def write(self, address: int, kind: str, value: Number) -> None:
        """Write `value` as `kind` at `address`."""
        self.memory.batch([], [Write(address, TYPES[kind][0], to_raw(value, kind))])

    def _reader(self, kind: str) -> Callable[[int], Number]:
        """The expression function that reads one value of `kind`."""
        return lambda address: self.read(int(address), kind)

    def humans(self) -> list[int]:
        """The address of every human in use (its record pointer `+0xd4` set), in table order."""
        reads = [Read(HUMAN_TABLE + i * HUMAN_SIZE + 0xD4, 4) for i in range(HUMAN_COUNT)]
        records = self.memory.batch(reads)
        return [HUMAN_TABLE + i * HUMAN_SIZE for i, record in enumerate(records) if record]

    def name(self, human: int) -> str:
        """The human's name (`+0x80`, NUL-terminated)."""
        words = self.memory.batch([Read(human + 0x80 + 4 * k, 4) for k in range(4)])
        raw = b"".join(word.to_bytes(4, "little") for word in words)
        return raw.split(b"\0")[0].decode("latin-1")

    def player(self) -> int:
        """The player's human (cached after the first look). Raises ExpressionError when no human is the player."""
        if self._player is None:
            for human in self.humans():
                if self.read(human + 0x1B0, "s8") == 0:
                    self._player = human
                    break
            else:
                raise ExpressionError("no human has player number 0 (is a level running?)")
        return self._player

    def human(self, name: str) -> int:
        """The human in use called `name`. Raises ExpressionError naming the humans there are when none is."""
        found = {self.name(human): human for human in self.humans()}
        if name not in found:
            raise ExpressionError(f"no human is called {name!r}; there are {', '.join(sorted(found))}")
        return found[name]

    def index(self, human: int) -> int:
        """The human's handle index (`s16` at `+0x92`)."""
        return int(self.read(human + 0x92, "s16"))

    def heading(self, human: int) -> float:
        """The human's heading in radians, from its transform's rotation."""
        base = TRANSFORM_TABLE + self.index(human) * TRANSFORM_SIZE
        return 2.0 * math.atan2(float(self.read(base + 0x18, "f32")), float(self.read(base + 0x1C, "f32")))

    def hidx(self, handle: int) -> int:
        """The handle index `handle` names, -1 for the null handle or a stale one (`Handle_Resolve` `0x00390180`:
        the high half indexes the handle table, whose entry's serial must equal the low half)."""
        handle = int(handle) & 0xFFFFFFFF
        if handle == NULL_HANDLE or not handle & 0xFFFF:
            return -1
        index = handle >> 16
        if index >= 0x8000 or self.read(HANDLE_TABLE + index * 8 + 4, "u16") != handle & 0xFFFF:
            return -1
        return index

    def goal_type(self, vtable: int) -> int:
        """The type id of the goal class with `vtable`, -2 for one not listed."""
        return self.goal_types.get(int(vtable), -2)

    def goal_at(self, brain: int, slot: int) -> int:
        """The type id of the goal in stack slot `slot` of `brain`, -1 when the slot is empty (above the top)."""
        slot = int(slot)
        if slot < 0 or slot >= 10 or slot > int(self.read(brain + 0x2C, "s8")):
            return -1
        goal = int(self.read(brain + 0x40 + 4 * slot, "u32"))
        return self.goal_type(int(self.read(goal + 0xC, "u32")))

    def handles(self, address: int, count: int) -> int:
        """How many of the `count` words from `address` are handles: neither 0 nor the null handle (a handle list,
        whose free entries hold one or the other), read in one batch."""
        reads = [Read(int(address) + 4 * k, 4) for k in range(int(count))]
        return sum(1 for word in self.memory.batch(reads) if word not in (0, NULL_HANDLE))

    def names(self, extra: Mapping[str, Value] | None = None) -> _Scope:
        """A name lookup for Expression.evaluate(): `extra` first, then the functions and the objects above."""
        return _Scope(self, {**self.functions, **(extra or {})})


class _Scope(dict[str, Any]):
    """The names of one evaluation: the functions and bound names as a dict, so compiled code looks them up without a
    call (a recording of every human looks up hundreds of thousands a second), and the objects that need a read
    (`player`, `camera`, `game_time`) worked out when asked for. Calling it looks a name up, as a lookup function."""

    def __init__(self, game: GameMemory, names: dict[str, Any]) -> None:
        """`names` over `game`'s objects."""
        super().__init__(names)
        self.game = game

    def __missing__(self, name: str) -> Any:
        """An object that needs a read; KeyError for a name that is not one."""
        if name == "player":
            return self.game.player()
        if name == "camera":
            return self.game.read(CAMERA_POINTER, "u32")
        if name == "game_time":
            return int(self.game.read(GAME_TIMER_POINTER, "u32")) + GAME_TIME_OFFSET
        raise KeyError(name)

    def __call__(self, name: str) -> Any:
        """One name's value, as a lookup function."""
        return self[name]


class SampleMemory:
    """A Memory that answers from what one sample has read so far: a read not made yet answers 0 and is noted, so
    expressions can be worked out in rounds, each round's notes read in one message (evaluate_all())."""

    def __init__(self, known: dict[tuple[int, int], int]) -> None:
        """Answer from `known`, (address, size) to the unsigned value."""
        self.known = known
        self.missing: set[tuple[int, int]] = set()

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """The known value of each of `reads`, 0 for one not read yet (noted in `missing`); it never writes."""
        if writes:
            raise ExpressionError("an expression may not write")
        out = []
        for read in reads:
            key = (read.address, read.size)
            if key in self.known:
                out.append(self.known[key])
            elif not 0 <= read.address <= 0xFFFFFFFF - read.size:
                # An address worked out from a value not read yet can be anything; it is never read.
                out.append(0)
            else:
                self.missing.add(key)
                out.append(0)
        return out


#: Each memory type's unsigned bytes as its value, for reads answered from a sample's dict (_SampleGame.read).
_DECODE: dict[str, Callable[[int], Number]] = {
    "u8": int,
    "u16": int,
    "u32": int,
    "s8": lambda raw: raw - 0x100 if raw & 0x80 else raw,
    "s16": lambda raw: raw - 0x10000 if raw & 0x8000 else raw,
    "s32": lambda raw: raw - 0x100000000 if raw & 0x80000000 else raw,
    "f32": lambda raw: struct.unpack("<f", raw.to_bytes(4, "little"))[0],
}


class _SampleGame(GameMemory):
    """The names over a SampleMemory, its reads answered straight from the sample's dict. A recording of every human
    works out about a thousand expressions an update, each making several reads, and all of them must be done before
    the next update starts (16 ms): going through batch() and a Read for each one took too long, and samples tore.
    The answers are the same as GameMemory's over the same SampleMemory."""

    def __init__(self, sample: SampleMemory, goal_types: Mapping[int, int] | None = None) -> None:
        """Answer from `sample`; its `missing` notes every read not made yet."""
        super().__init__(sample, goal_types)
        self.sample = sample
        # Each human's handle index, once read in this sample (brain(h), tf(h) and prec(h) all ask for it).
        self._indices: dict[int, int] = {}

    def read(self, address: int, kind: str) -> Number:
        """One value of `kind` at `address` from the sample, 0 (noted as missing) when not read yet."""
        size = TYPES[kind][0]
        raw = self.sample.known.get((address, size))
        if raw is None:
            if 0 <= address <= 0xFFFFFFFF - size:
                self.sample.missing.add((address, size))
            return 0.0 if kind == "f32" else 0
        return _DECODE[kind](raw)

    def index(self, human: int) -> int:
        """The human's handle index, kept once it has been read."""
        index = self._indices.get(human)
        if index is None:
            index = int(self.read(human + 0x92, "s16"))
            if (human + 0x92, 2) in self.sample.known:
                self._indices[human] = index
        return index

    def handles(self, address: int, count: int) -> int:
        """As GameMemory.handles(), each word read from the sample."""
        words = [self.read(int(address) + 4 * k, "u32") for k in range(int(count))]
        return sum(1 for word in words if word not in (0, NULL_HANDLE))


#: A value worked out from memory within one sample (coney_tools.recorder's `value` and `follow` fields).
Job = Callable[[GameMemory], Any]


def evaluate_all(
    memory: Memory,
    known: dict[tuple[int, int], int],
    jobs: Sequence[Job],
    goal_types: Mapping[int, int] | None = None,
    rounds: int = 8,
) -> list[Any]:
    """Every job's value, worked out together in few messages: each round runs the jobs not finished yet over the
    reads made so far (`known`, which grows), then reads in one message all they asked for that was missing. A
    pointer chain of depth d takes d rounds, however many jobs there are. A job that fails, or is still unfinished
    after `rounds` rounds, gives NaN."""
    sample = SampleMemory(known)
    game = _SampleGame(sample, goal_types)
    results: list[Any] = [math.nan] * len(jobs)
    pending = list(range(len(jobs)))
    for _ in range(rounds):
        unfinished: list[int] = []
        wanted: set[tuple[int, int]] = set()
        for k in pending:
            sample.missing = set()
            try:
                value = jobs[k](game)
            except ExpressionError:
                value = math.nan
            if sample.missing:
                unfinished.append(k)
                wanted |= sample.missing
            else:
                results[k] = value
        if not unfinished:
            break
        order = sorted(wanted)
        values = memory.batch([Read(address, size) for address, size in order])
        known.update(zip(order, values, strict=True))
        pending = unfinished
    return results

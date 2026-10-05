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
- `sin cos atan2 hypot sqrt abs min max`, `deg(radians)`, `rad(degrees)`, `wrap(degrees)` (to -180..180), `pi`.

Field formulas (derived columns) use the same grammar, with the scenario's other fields as names.
"""

from __future__ import annotations

import ast
import math
import struct
from collections.abc import Callable, Mapping
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
        """The expression's value; `names` gives each name's value (raising KeyError for an unknown one)."""
        try:
            return self._eval(self.tree, names)
        except KeyError as error:
            raise ExpressionError(f"{self.text!r}: unknown name {error.args[0]!r}") from error
        except (ArithmeticError, TypeError, ValueError) as error:
            raise ExpressionError(f"{self.text!r}: {error}") from error

    def _eval(self, node: ast.expr, names: Callable[[str], Any]) -> Any:
        """Evaluate one node (the grammar is checked by parse(), so every node here is allowed)."""
        if isinstance(node, ast.Constant):
            return node.value
        if isinstance(node, ast.Name):
            return names(node.id)
        if isinstance(node, ast.BinOp):
            return _BINARY[type(node.op)](self._eval(node.left, names), self._eval(node.right, names))
        if isinstance(node, ast.UnaryOp):
            return _UNARY[type(node.op)](self._eval(node.operand, names))
        assert isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
        function = names(node.func.id)
        if not callable(function):
            raise ExpressionError(f"{self.text!r}: {node.func.id} is not a function")
        return function(*(self._eval(arg, names) for arg in node.args))


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

    def __init__(self, memory: Memory) -> None:
        """Wrap `memory`; nothing is read until a name is used."""
        self.memory = memory
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

    def names(self, extra: Mapping[str, Value] | None = None) -> Callable[[str], Any]:
        """A name lookup for Expression.evaluate(): `extra` first, then the functions and the objects above."""
        bound = dict(extra or {})

        def lookup(name: str) -> Any:
            """One name's value, in the order of the docstring above."""
            if name in bound:
                return bound[name]
            if name in self.functions:
                return self.functions[name]
            if name == "player":
                return self.player()
            if name == "camera":
                return self.read(CAMERA_POINTER, "u32")
            if name == "game_time":
                return int(self.read(GAME_TIMER_POINTER, "u32")) + GAME_TIME_OFFSET
            raise KeyError(name)

        return lookup

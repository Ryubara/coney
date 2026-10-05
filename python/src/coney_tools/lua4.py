# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the game's compiled Lua 4.0 chunks: their constants, and the calls they make with constant arguments.

The game's scripts ship as Lua 4.0.1 bytecode (header `1B 4C 75 61 40`, little-endian, 4-byte ints and
instructions, 8-byte doubles). This module parses a chunk into its function prototypes and walks each one's code
with a small symbolic stack, so that a call such as `CfgChar(32, ..., "warr_re_cv", ...)` comes out as the callee's
name and its arguments, each a number, a string, a table of constants, the name of a global, a call's result (with
that call's arguments), a function the chunk defines, or "unknown". It never
runs a script and never reproduces one: the reference tools keep only names and numbers from what it returns.

The walk is linear: it visits every instruction once, in order, and follows both sides of a branch. That suits the
configuration scripts the reference lists come from (long runs of calls with literal arguments); a value computed
at run time comes out as `UNKNOWN`.

Research: docs/research/scripting.md (the Lua state, the 62-item `SETLIST` flush), docs/research/formats/wad-contents.md
(the chunk header).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

#: Every compiled chunk on the disc starts with these 13 bytes (Lua 4.0, little-endian, 4/4/4-byte sizes, 32-bit
#: instructions with a 6-bit opcode and a 9-bit B field, 8-byte numbers).
HEADER = bytes.fromhex("1b4c7561400104040420060908")
#: This build's `LFIELDS_PER_FLUSH` (stock Lua 4.0 uses 64): a `SETLIST` with A = n stores from item n * 62 + 1.
FIELDS_PER_FLUSH = 62
_MAXARG_S = ((1 << 26) - 1) >> 1
_MULTRET = 255

OPCODES = (
    "END",
    "RETURN",
    "CALL",
    "TAILCALL",
    "PUSHNIL",
    "POP",
    "PUSHINT",
    "PUSHSTRING",
    "PUSHNUM",
    "PUSHNEGNUM",
    "PUSHUPVALUE",
    "GETLOCAL",
    "GETGLOBAL",
    "GETTABLE",
    "GETDOTTED",
    "GETINDEXED",
    "PUSHSELF",
    "CREATETABLE",
    "SETLOCAL",
    "SETGLOBAL",
    "SETTABLE",
    "SETLIST",
    "SETMAP",
    "ADD",
    "ADDI",
    "SUB",
    "MULT",
    "DIV",
    "POW",
    "CONCAT",
    "MINUS",
    "NOT",
    "JMPNE",
    "JMPEQ",
    "JMPLT",
    "JMPLE",
    "JMPGT",
    "JMPGE",
    "JMPT",
    "JMPF",
    "JMPONT",
    "JMPONF",
    "JMP",
    "PUSHNILJMP",
    "FORPREP",
    "FORLOOP",
    "LFORPREP",
    "LFORLOOP",
    "CLOSURE",
)


class LuaError(Exception):
    """A chunk that is not Lua 4.0 bytecode in the game's format, or is cut short."""


@dataclass
class Proto:
    """One function prototype: its constants, nested functions and code."""

    params: int
    is_vararg: bool
    strings: list[str | None]
    numbers: list[float]
    protos: list[Proto]
    code: list[int]
    local_names: list[str] = field(default_factory=list)


class _Reader:
    """Little-endian reads over a chunk, raising LuaError past its end."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def take(self, fmt: str) -> int | float:
        """Read one value of struct format `fmt`."""
        size = struct.calcsize(fmt)
        if self.pos + size > len(self.data):
            raise LuaError(f"chunk cut short at byte {self.pos}")
        (value,) = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += size
        return value  # type: ignore[no-any-return]

    def int32(self) -> int:
        """A 4-byte signed int."""
        return int(self.take("<i"))

    def string(self) -> str | None:
        """A Lua 4.0 string: a size_t length counting the trailing NUL, 0 for none."""
        length = int(self.take("<I"))
        if length == 0:
            return None
        if self.pos + length > len(self.data):
            raise LuaError(f"string cut short at byte {self.pos}")
        text = self.data[self.pos : self.pos + length - 1].decode("latin-1")
        self.pos += length
        return text


def _read_proto(reader: _Reader) -> Proto:
    """Read one function prototype (lundump.c's LoadFunction for 4.0)."""
    reader.string()  # source name, stripped to "=(none)" on the disc
    reader.int32()  # line defined
    params = reader.int32()
    is_vararg = bool(reader.take("<B"))
    reader.int32()  # max stack size
    local_names = []
    for _ in range(reader.int32()):
        name = reader.string()
        reader.int32()  # start pc
        reader.int32()  # end pc
        local_names.append(name or "")
    for _ in range(reader.int32()):  # line info
        reader.int32()
    strings = [reader.string() for _ in range(reader.int32())]
    numbers = [float(reader.take("<d")) for _ in range(reader.int32())]
    protos = [_read_proto(reader) for _ in range(reader.int32())]
    code = [int(reader.take("<I")) for _ in range(reader.int32())]
    return Proto(params, is_vararg, strings, numbers, protos, code, local_names)


def parse_chunk(data: bytes) -> Proto:
    """Parse a whole compiled chunk and return its main function. Raises LuaError when it is not one."""
    if not data.startswith(HEADER):
        raise LuaError("not a Lua 4.0 chunk in the game's format (header differs)")
    reader = _Reader(data)
    reader.pos = len(HEADER) + 8  # the header, then a test number (a double)
    main = _read_proto(reader)
    if reader.pos != len(data):
        raise LuaError(f"{len(data) - reader.pos} bytes left after the main function")
    return main


def all_strings(proto: Proto) -> list[str]:
    """Every string constant of `proto` and its nested functions, in order (duplicates kept)."""
    found = [text for text in proto.strings if text is not None]
    for child in proto.protos:
        found.extend(all_strings(child))
    return found


# --- symbolic values -------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Global:
    """The value of a global variable read by name (possibly dotted: `PHYS.BOX`) and not known here."""

    name: str


@dataclass(frozen=True)
class CallResult:
    """What a call returned, with the arguments the call was given (so `f = AddFlag("f1", {x, y, z}, ...)` keeps
    the flag's position; two results of the same callee compare equal whatever their arguments)."""

    callee: str
    args: tuple[Value, ...] = field(default=(), compare=False)


@dataclass(frozen=True)
class Function:
    """A function a chunk defines (`CLOSURE`), by its path among the chunk's prototypes ("main/3")."""

    path: str


@dataclass
class Table:
    """A table built by a constructor: its list part (from index 1) and its keyed part."""

    items: dict[int, Value] = field(default_factory=dict)
    fields: dict[str | float, Value] = field(default_factory=dict)

    def as_list(self) -> list[Value]:
        """The list part as a Python list (items 1..n; a gap reads as UNKNOWN)."""
        if not self.items:
            return []
        return [self.items.get(i, UNKNOWN) for i in range(1, max(self.items) + 1)]


class _Unknown:
    """A value the linear walk cannot know (computed at run time)."""

    def __repr__(self) -> str:
        return "UNKNOWN"


UNKNOWN = _Unknown()
#: Lua's nil, as a Python value distinct from UNKNOWN.
NIL = None

type Value = float | str | Global | CallResult | Function | Table | _Unknown | None


@dataclass
class Call:
    """One call found in a chunk: where it is, the callee's name ("?" when not a global) and its arguments."""

    path: str  # the function holding it: "main", "main/3", "main/3/0" ...
    pc: int
    callee: str
    args: list[Value]


@dataclass
class Assignment:
    """One `SETGLOBAL`: the global's name and the value stored."""

    path: str
    pc: int
    name: str
    value: Value


@dataclass
class ChunkFacts:
    """What `walk_chunk` finds: every call and every global assignment, in code order per function."""

    calls: list[Call] = field(default_factory=list)
    assignments: list[Assignment] = field(default_factory=list)
    #: The last table each global was given (kept when the global is later set to nil), so that `T = {}` followed
    #: by `T.X = 1` fills the table, and the reference tools can read the finished tables (`MATERIAL`, `CL`).
    tables: dict[str, Table] = field(default_factory=dict)
    #: Every table a constructor built, with the function that built it, including tables only ever held in locals
    #: (a level script's `local gangs = {AddWarriors1, ...}` indexed by the checkpoint).
    constructed: list[tuple[str, Table]] = field(default_factory=list)

    def functions(self) -> dict[str, str]:
        """Global name -> path of the function last assigned to it (`function Main() ... end` at any depth)."""
        return {a.name: a.value.path for a in self.assignments if isinstance(a.value, Function)}

    def target(self, value: Value) -> Value:
        """`value` itself, or the table a global names when the code writes into it."""
        if isinstance(value, Global) and value.name in self.tables:
            return self.tables[value.name]
        if isinstance(value, Global) and "." in value.name:
            head, _, key = value.name.rpartition(".")
            parent = self.target(Global(head))
            if isinstance(parent, Table):
                return parent.fields.get(key, value)
        return value


def _name_of(value: Value) -> str:
    """A callee's name: a global's (dotted) name, else "?"."""
    return value.name if isinstance(value, Global) else "?"


def _index(table: Value, key: Value) -> Value:
    """`table[key]` for a known table and constant key; a dotted name for a global; else UNKNOWN."""
    if isinstance(table, Table):
        if isinstance(key, float) and key.is_integer() and int(key) in table.items:
            return table.items[int(key)]
        if isinstance(key, (str, float)) and key in table.fields:
            return table.fields[key]
        return UNKNOWN
    if isinstance(table, Global) and isinstance(key, str):
        return Global(f"{table.name}.{key}")
    return UNKNOWN


class _Walker:
    """Walks one prototype's code with a symbolic stack, recording calls and global assignments."""

    def __init__(self, proto: Proto, path: str, facts: ChunkFacts) -> None:
        self.proto = proto
        self.path = path
        self.facts = facts
        self.stack: list[Value] = [UNKNOWN] * proto.params
        self.pending: dict[int, list[Value]] = {}  # stack at forward-jump targets

    def pop(self, count: int = 1) -> list[Value]:
        """Pop `count` values (UNKNOWN for any the walk lost track of), oldest first."""
        if count <= 0:
            return []
        taken = self.stack[-count:] if count <= len(self.stack) else self.stack[:]
        del self.stack[max(0, len(self.stack) - count) :]
        return [UNKNOWN] * (count - len(taken)) + taken

    def push(self, value: Value) -> None:
        """Push one value."""
        self.stack.append(value)

    def top(self) -> Value:
        """The top value, UNKNOWN on an empty stack."""
        return self.stack[-1] if self.stack else UNKNOWN

    def jump(self, pc: int, offset: int) -> None:
        """Remember the stack at a forward jump's target, for when the walk gets there."""
        target = pc + 1 + offset
        if target > pc:
            self.pending.setdefault(target, list(self.stack))

    def arrive(self, pc: int, dead: bool) -> None:
        """At `pc`: after an unconditional jump or return, take the stack a jump left for here, if any."""
        saved = self.pending.pop(pc, None)
        if saved is None:
            return
        if dead:
            self.stack = saved
        elif len(saved) != len(self.stack):
            self.stack = [UNKNOWN] * len(saved)  # two paths disagree on the depth: trust the jump's
        else:
            self.stack = [x if x is y or x == y else UNKNOWN for x, y in zip(self.stack, saved, strict=True)]

    def run(self) -> None:
        """Walk every instruction once, in order."""
        dead = False
        for pc, word in enumerate(self.proto.code):
            self.arrive(pc, dead)
            dead = self.step(pc, word)

    def step(self, pc: int, word: int) -> bool:
        """Apply one instruction; return True when the code after it is not reached by falling through."""
        op = OPCODES[word & 0x3F] if (word & 0x3F) < len(OPCODES) else "END"
        u = word >> 6
        s = u - _MAXARG_S
        a = word >> 15
        b = (word >> 6) & 0x1FF
        strings, numbers = self.proto.strings, self.proto.numbers
        if op in ("END", "RETURN"):
            return True
        if op in ("CALL", "TAILCALL"):
            # The function sits at stack[a] and its arguments run from there to the top.
            if len(self.stack) < a + 1:
                self.stack.extend([UNKNOWN] * (a + 1 - len(self.stack)))
            callee = _name_of(self.stack[a])
            args = self.stack[a + 1 :]
            self.facts.calls.append(Call(self.path, pc, callee, args))
            results = 1 if b == _MULTRET else b
            self.stack = self.stack[:a] + [CallResult(callee, tuple(args))] * (results if op == "CALL" else 0)
            return op == "TAILCALL"
        if op == "PUSHNIL":
            self.stack.extend([NIL] * u)
        elif op == "POP":
            self.pop(u)
        elif op == "PUSHINT":
            self.push(float(s))
        elif op == "PUSHSTRING":
            self.push(strings[u])
        elif op in ("PUSHNUM", "PUSHNEGNUM"):
            self.push(numbers[u] if op == "PUSHNUM" else -numbers[u])
        elif op == "PUSHUPVALUE":
            self.push(UNKNOWN)
        elif op == "GETLOCAL":
            self.push(self.stack[u] if u < len(self.stack) else UNKNOWN)
        elif op == "GETGLOBAL":
            self.push(Global(strings[u] or "?"))
        elif op == "GETTABLE":
            key = self.pop()[0]
            self.push(_index(self.pop()[0], key))
        elif op == "GETDOTTED":
            self.push(_index(self.pop()[0], strings[u]))
        elif op == "GETINDEXED":
            key = self.stack[u] if u < len(self.stack) else UNKNOWN
            self.push(_index(self.pop()[0], key))
        elif op == "PUSHSELF":
            obj = self.pop()[0]
            method = Global(f"{obj.name}:{strings[u]}") if isinstance(obj, Global) else UNKNOWN
            self.push(method)
            self.push(obj)
        elif op == "CREATETABLE":
            self.push(Table())
            self.facts.constructed.append((self.path, self.stack[-1]))  # type: ignore[arg-type]
        elif op == "SETLOCAL":
            value = self.pop()[0]
            if u < len(self.stack):
                self.stack[u] = value
        elif op == "SETGLOBAL":
            value = self.pop()[0]
            name = strings[u] or "?"
            self.facts.assignments.append(Assignment(self.path, pc, name, value))
            if isinstance(value, Table):
                self.facts.tables[name] = value
        elif op == "SETTABLE":
            self._settable(a, b)
        elif op == "SETLIST":
            values = self.pop(b)
            table = self.facts.target(self.top())
            if isinstance(table, Table):
                for i, value in enumerate(values, start=a * FIELDS_PER_FLUSH + 1):
                    table.items[i] = value
        elif op == "SETMAP":
            values = self.pop(2 * u)
            table = self.facts.target(self.top())
            if isinstance(table, Table):
                for key, value in zip(values[0::2], values[1::2], strict=True):
                    if isinstance(key, float) and key.is_integer():
                        table.items[int(key)] = value
                    elif isinstance(key, (str, float)):
                        table.fields[key] = value
        elif op in ("ADD", "SUB", "MULT", "DIV", "POW"):
            right, left = self.pop()[0], self.pop()[0]
            self.push(_arith(op, left, right))
        elif op == "ADDI":
            value = self.pop()[0]
            self.push(value + s if isinstance(value, float) else UNKNOWN)
        elif op == "CONCAT":
            parts = self.pop(u)
            self.push("".join(parts) if all(isinstance(p, str) for p in parts) else UNKNOWN)  # type: ignore[arg-type]
        elif op == "MINUS":
            value = self.pop()[0]
            self.push(-value if isinstance(value, float) else UNKNOWN)
        elif op == "NOT":
            self.pop()
            self.push(UNKNOWN)
        elif op in ("JMPNE", "JMPEQ", "JMPLT", "JMPLE", "JMPGT", "JMPGE"):
            self.pop(2)
            self.jump(pc, s)
        elif op in ("JMPT", "JMPF"):
            self.pop()
            self.jump(pc, s)
        elif op in ("JMPONT", "JMPONF"):
            self.jump(pc, s)  # the value stays on the stack when the jump is taken
            self.pop()
        elif op == "JMP":
            self.jump(pc, s)
            return s >= 0
        elif op == "PUSHNILJMP":
            self.push(NIL)
            self.pending.setdefault(pc + 2, list(self.stack))
            self.pop()
        elif op == "FORPREP":
            self.jump(pc, s)
        elif op == "FORLOOP":
            self.pop(3)
        elif op == "LFORPREP":
            self.jump(pc, s)
            self.push(UNKNOWN)
            self.push(UNKNOWN)
        elif op == "LFORLOOP":
            self.pop(3)
        elif op == "CLOSURE":
            # A is the index of the child prototype, B the number of upvalues on the stack.
            self.pop(b)
            self.push(Function(f"{self.path}/{a}"))
        return False

    def _settable(self, a: int, b: int) -> None:
        """`SETTABLE A B`: t = stack[top - A], key above it, value on top; pop B values."""
        if a <= len(self.stack) and a >= 2:
            table, key, value = self.facts.target(self.stack[-a]), self.stack[-a + 1], self.stack[-1]
            if isinstance(table, Table):
                if isinstance(key, float) and key.is_integer():
                    table.items[int(key)] = value
                elif isinstance(key, (str, float)):
                    table.fields[key] = value
        self.pop(b)


def _arith(op: str, left: Value, right: Value) -> Value:
    """Fold arithmetic on two known numbers; UNKNOWN otherwise (or on a division by zero)."""
    if not isinstance(left, float) or not isinstance(right, float):
        return UNKNOWN
    if op == "ADD":
        return left + right
    if op == "SUB":
        return left - right
    if op == "MULT":
        return left * right
    if op == "DIV":
        return left / right if right else UNKNOWN
    try:
        return float(left**right)
    except (OverflowError, ZeroDivisionError):
        return UNKNOWN


def walk_chunk(main: Proto, facts: ChunkFacts | None = None) -> ChunkFacts:
    """Walk `main` and every nested function; return all calls and global assignments found.

    Pass the facts of chunks run earlier in the same Lua state (the preloads) to let this one write into their
    tables; its own calls and assignments are appended.
    """
    facts = facts if facts is not None else ChunkFacts()

    def visit(proto: Proto, path: str) -> None:
        """Walk one function, then its children."""
        _Walker(proto, path, facts).run()
        for index, child in enumerate(proto.protos):
            visit(child, f"{path}/{index}")

    visit(main, "main")
    return facts


def plain(value: Value) -> object:
    """A value as plain data for YAML or JSON: numbers (ints when whole), strings, None, lists and dicts.

    A global comes out as `{"global": name}`, a call result as `{"call": name}`, an unknown as `"?"`.
    """
    if isinstance(value, float):
        return int(value) if value.is_integer() else value
    if value is None or isinstance(value, str):
        return value
    if isinstance(value, Global):
        return {"global": value.name}
    if isinstance(value, CallResult):
        return {"call": value.callee}
    if isinstance(value, Table):
        if value.fields:
            merged: dict[str, object] = {str(k): plain(v) for k, v in value.items.items()}
            merged.update({str(k): plain(v) for k, v in value.fields.items()})
            return merged
        return [plain(v) for v in value.as_list()]
    return "?"

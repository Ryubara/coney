# SPDX-License-Identifier: GPL-3.0-or-later
"""Load a compiled Lua 4.0 chunk (the output of `luac`) into its function prototypes.

The layout is the one Lua 4.0.1's `ldump.c` writes and `lundump.c` reads: a header (signature, version 0x40, byte
order, the sizes of `int`, `size_t` and an instruction, the instruction's field widths, the size of a number and a
test number), then the main function's prototype, which holds its nested prototypes recursively. The header's sizes
are honoured, so a chunk from a 64-bit `luac` (8-byte `size_t`) loads as well as one from a 32-bit build.

Research: docs/research/formats/lua-chunks.md (the container), docs/research/formats/lua-bytecode.md (the code).
"""

from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field

#: Every chunk starts with ESC "Lua".
SIGNATURE = b"\x1bLua"
#: Lua 4.0.
VERSION = 0x40
#: The number a chunk carries to prove its number format (`TEST_NUMBER` in Lua 4.0's lundump.h).
TEST_NUMBER = 3.14159265358979e8


class ChunkError(Exception):
    """The bytes are not a Lua 4.0 chunk this loader understands; the message says what was wrong and where."""


@dataclass(frozen=True)
class LocVar:
    """A local variable's debug record: its name and the instructions it is active for (`startpc` <= pc < `endpc`)."""

    name: str
    startpc: int
    endpc: int


@dataclass
class Proto:
    """One function prototype, as the chunk stores it."""

    source: str
    line_defined: int
    num_params: int
    is_vararg: bool
    max_stack: int
    locals: list[LocVar] = field(default_factory=list)
    #: The raw line table: a negative entry skips lines, a positive one is the pc where the next line starts, and the
    #: last entry is a large end marker (see `lines`).
    line_info: list[int] = field(default_factory=list)
    strings: list[str] = field(default_factory=list)
    numbers: list[float] = field(default_factory=list)
    protos: list[Proto] = field(default_factory=list)
    code: list[int] = field(default_factory=list)

    def lines(self) -> list[int]:
        """The source line of every instruction, decoded from the line table (0 when the table is empty).

        Lua 4.0 records a line change only when an instruction is emitted on a later line than the last one: a
        negative entry adds that many lines, a positive entry is the pc at which the line advances by one more.
        """
        result = [0] * len(self.code)
        line = 0
        marks: list[tuple[int, int]] = []
        for entry in self.line_info:
            if entry < 0:
                line -= entry
            elif entry < len(self.code):
                line += 1
                marks.append((entry, line))
        for index, (pc, value) in enumerate(marks):
            end = marks[index + 1][0] if index + 1 < len(marks) else len(self.code)
            for at in range(pc, end):
                result[at] = value
        return result

    def walk(self, path: str = "main") -> list[tuple[str, Proto]]:
        """This prototype and every nested one, depth first, each with its path (`main`, `main/0`, `main/0/2`)."""
        found = [(path, self)]
        for index, child in enumerate(self.protos):
            found += child.walk(f"{path}/{index}")
        return found


class _Reader:
    """Reads the chunk's values in the byte order and sizes its header declares."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0
        self.order = "<"
        self.int_size = 4
        self.size_t_size = 4

    def raw(self, count: int) -> bytes:
        """The next `count` bytes; ChunkError when the chunk ends first."""
        if self.pos + count > len(self.data):
            raise ChunkError(f"truncated chunk: {count} bytes wanted at offset {self.pos:#x}")
        chunk = self.data[self.pos : self.pos + count]
        self.pos += count
        return chunk

    def byte(self) -> int:
        """One unsigned byte (a header field or a flag)."""
        return self.raw(1)[0]

    def _unsigned(self, size: int) -> int:
        """An unsigned integer of `size` bytes in the chunk's byte order."""
        return int.from_bytes(self.raw(size), "little" if self.order == "<" else "big")

    def word(self) -> int:
        """One 32-bit instruction."""
        return self._unsigned(4)

    def integer(self) -> int:
        """A signed C `int` of the header's int size (a count, a line, a parameter count)."""
        value = self._unsigned(self.int_size)
        if value >= 1 << (8 * self.int_size - 1):
            value -= 1 << (8 * self.int_size)
        return value

    def size_t(self) -> int:
        """A C `size_t` of the header's size_t size (4 on the PS2, 8 from a 64-bit luac)."""
        return self._unsigned(self.size_t_size)

    def count(self, what: str) -> int:
        # A length prefix; a negative or absurd one means the bytes are not what we think they are.
        value = self.integer()
        if value < 0 or value > len(self.data):
            raise ChunkError(f"bad {what} count {value} at offset {self.pos:#x}")
        return value

    def number(self) -> float:
        """A Lua number: an 8-byte double in the chunk's byte order."""
        value: float = struct.unpack(self.order + "d", self.raw(8))[0]
        return value

    def string(self) -> str | None:
        # A size_t length that counts the final NUL (0: no string), then the bytes. Latin-1 keeps every byte.
        length = self.size_t()
        if length == 0:
            return None
        if length > len(self.data):
            raise ChunkError(f"bad string length {length} at offset {self.pos:#x}")
        return self.raw(length)[:-1].decode("latin-1")


def _header(reader: _Reader) -> None:
    """Check the chunk header and take its byte order and sizes into `reader`. Only Lua 4.0 (version 0x40)
    with 32-bit instructions (a 6-bit opcode, a 9-bit B; lopcodes.h) and 8-byte doubles is read; int and size_t may be
    2, 4 or 8 bytes, as luac writes the host's. The test number catches a wrong number format."""
    if reader.raw(4) != SIGNATURE:
        raise ChunkError("not a Lua chunk (no ESC 'Lua' signature)")
    version = reader.byte()
    if version != VERSION:
        raise ChunkError(f"Lua version {version:#x}, not 4.0 ({VERSION:#x})")
    reader.order = "<" if reader.byte() == 1 else ">"
    reader.int_size, reader.size_t_size, instruction_size = reader.byte(), reader.byte(), reader.byte()
    instruction_bits, op_bits, b_bits, number_size = reader.byte(), reader.byte(), reader.byte(), reader.byte()
    if (instruction_size, instruction_bits, op_bits, b_bits, number_size) != (4, 32, 6, 9, 8):
        raise ChunkError(
            f"unsupported instruction or number format ({instruction_size}-byte, {instruction_bits}/{op_bits}/"
            f"{b_bits} bits, {number_size}-byte numbers)"
        )
    if reader.int_size not in (2, 4, 8) or reader.size_t_size not in (2, 4, 8):
        raise ChunkError(f"unsupported int or size_t size ({reader.int_size}, {reader.size_t_size})")
    test = reader.number()
    if not math.isclose(test, TEST_NUMBER, rel_tol=1e-12):
        raise ChunkError(f"unexpected test number {test!r}")


def _proto(reader: _Reader, depth: int) -> Proto:
    """One function prototype, in luac's order (ldump.c): source, first line, parameters, vararg flag, max
    stack, local records, line table, string and number constants, nested functions, then the code, which
    must end with END. `depth` stops a corrupt chunk from recursing without end."""
    if depth > 200:
        raise ChunkError("functions nested too deeply")
    source = reader.string() or ""
    line_defined = reader.integer()
    num_params = reader.integer()
    is_vararg = reader.byte() != 0
    max_stack = reader.integer()
    proto = Proto(source, line_defined, num_params, is_vararg, max_stack)
    for _ in range(reader.count("local")):
        name = reader.string() or ""
        proto.locals.append(LocVar(name, reader.integer(), reader.integer()))
    proto.line_info = [reader.integer() for _ in range(reader.count("line"))]
    proto.strings = [reader.string() or "" for _ in range(reader.count("string constant"))]
    proto.numbers = [reader.number() for _ in range(reader.count("number constant"))]
    proto.protos = [_proto(reader, depth + 1) for _ in range(reader.count("function"))]
    size = reader.count("instruction")
    proto.code = [reader.word() for _ in range(size)]
    if not proto.code or proto.code[-1] & 0x3F != 0:
        raise ChunkError("a function's code does not end with END")
    return proto


def load(data: bytes) -> Proto:
    """Parse a whole chunk into its main function. Raises ChunkError on anything that is not a Lua 4.0 chunk."""
    reader = _Reader(data)
    _header(reader)
    main = _proto(reader, 0)
    if reader.pos != len(data):
        raise ChunkError(f"{len(data) - reader.pos} bytes after the main function")
    return main

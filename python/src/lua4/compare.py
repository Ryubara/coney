# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that two compiled chunks hold the same program: per function, recursively, the same instructions, the same
constants, the same parameters and stack size, and the same nested functions.

The debug information (the line table, the local variables' ranges, the source name) is not compared: a
decompiled chunk cannot get the original's comments and layout back, so its lines may differ. A difference names
the function, the kind and, for code, the pc and the opcodes; it never quotes a constant, so a report about the
game's scripts carries no script text.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from lua4.chunk import Proto
from lua4.opcodes import Op, decode


@dataclass(frozen=True)
class Difference:
    """One way two functions differ: `path` (`main`, `main/0/2`), `kind`, and a `detail` with no constant in it."""

    path: str
    kind: str
    detail: str

    def __str__(self) -> str:
        return f"{self.path}: {self.kind}: {self.detail}"


def _describe(word: int) -> str:
    """An instruction as its opcode name and arguments (indices only)."""
    try:
        instr = decode(word)
    except ValueError:
        return f"opcode {word & 0x3F}"
    if instr.op in (Op.CALL, Op.TAILCALL, Op.SETTABLE, Op.SETLIST, Op.CLOSURE):
        return f"{instr.op.name} {instr.a} {instr.b}"
    return f"{instr.op.name} {instr.arg}"


def _same_number(left: float, right: float) -> bool:
    """Bit-for-bit equality, so -0.0 differs from 0.0 and NaN equals NaN."""
    return struct.pack("<d", left) == struct.pack("<d", right)


def compare(original: Proto, other: Proto, path: str = "main") -> list[Difference]:
    """Every difference between `original` and `other` and their nested functions (empty when they match)."""
    found: list[Difference] = []
    for kind, mine, theirs in (
        ("params", original.num_params, other.num_params),
        ("vararg", original.is_vararg, other.is_vararg),
        ("stack", original.max_stack, other.max_stack),
    ):
        if mine != theirs:
            found.append(Difference(path, kind, f"{mine} vs {theirs}"))
    if len(original.code) != len(other.code):
        found.append(Difference(path, "code", f"{len(original.code)} vs {len(other.code)} instructions"))
    for pc, (mine_word, their_word) in enumerate(zip(original.code, other.code, strict=False)):
        if mine_word != their_word:
            found.append(Difference(path, "code", f"pc {pc}: {_describe(mine_word)} vs {_describe(their_word)}"))
            break
    if original.strings != other.strings:
        found.append(Difference(path, "strings", _list_detail(original.strings, other.strings, lambda a, b: a == b)))
    if len(original.numbers) != len(other.numbers) or not all(
        _same_number(a, b) for a, b in zip(original.numbers, other.numbers, strict=False)
    ):
        found.append(Difference(path, "numbers", _list_detail(original.numbers, other.numbers, _same_number)))
    if len(original.protos) != len(other.protos):
        found.append(Difference(path, "functions", f"{len(original.protos)} vs {len(other.protos)}"))
    for index, (mine_child, their_child) in enumerate(zip(original.protos, other.protos, strict=False)):
        found += compare(mine_child, their_child, f"{path}/{index}")
    return found


def _list_detail(mine: list, theirs: list, same: object) -> str:  # type: ignore[type-arg]
    """Where two constant lists first differ, by index and count only."""
    assert callable(same)
    for index, (left, right) in enumerate(zip(mine, theirs, strict=False)):
        if not same(left, right):
            return f"first differs at index {index} ({len(mine)} vs {len(theirs)} entries)"
    return f"{len(mine)} vs {len(theirs)} entries"


def first_code_difference(original: Proto, other: Proto) -> Difference | None:
    """The first difference found, or None."""
    differences = compare(original, other)
    return differences[0] if differences else None

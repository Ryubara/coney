# SPDX-License-Identifier: GPL-3.0-or-later
"""The Lua 4.0 instruction set: opcodes, operand decoding and each instruction's effect on the stack.

An instruction is 32 bits: a 6-bit opcode in the low bits, then either one 26-bit argument (`U`, unsigned, or `S`,
signed in excess-`MAXARG_S`) or two (`A`, the high 17 bits, and `B`, the 9 bits above the opcode). The numbering and
the operand kinds follow Lua 4.0.1's `lopcodes.h`; the stack effects follow its code generator (`lcode.c`), which
counts the stack the same way when it compiles.

Research: docs/research/formats/lua-bytecode.md.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


class Op(IntEnum):
    """Lua 4.0's opcodes, in its numbering."""

    END = 0
    RETURN = 1
    CALL = 2
    TAILCALL = 3
    PUSHNIL = 4
    POP = 5
    PUSHINT = 6
    PUSHSTRING = 7
    PUSHNUM = 8
    PUSHNEGNUM = 9
    PUSHUPVALUE = 10
    GETLOCAL = 11
    GETGLOBAL = 12
    GETTABLE = 13
    GETDOTTED = 14
    GETINDEXED = 15
    PUSHSELF = 16
    CREATETABLE = 17
    SETLOCAL = 18
    SETGLOBAL = 19
    SETTABLE = 20
    SETLIST = 21
    SETMAP = 22
    ADD = 23
    ADDI = 24
    SUB = 25
    MULT = 26
    DIV = 27
    POW = 28
    CONCAT = 29
    MINUS = 30
    NOT = 31
    JMPNE = 32
    JMPEQ = 33
    JMPLT = 34
    JMPLE = 35
    JMPGT = 36
    JMPGE = 37
    JMPT = 38
    JMPF = 39
    JMPONT = 40
    JMPONF = 41
    JMP = 42
    PUSHNILJMP = 43
    FORPREP = 44
    FORLOOP = 45
    LFORPREP = 46
    LFORLOOP = 47
    CLOSURE = 48


#: The largest unsigned 26-bit argument, and the excess of a signed one.
MAXARG_U = (1 << 26) - 1
MAXARG_S = MAXARG_U >> 1
#: `B` of a CALL whose results are left open (as many as the function returns).
MULT_RET = 255
#: A jump list's end marker in the code generator; in finished code a jump never keeps it.
NO_JUMP = -1

#: The instructions whose argument is `S` (signed); every jump is one.
SIGNED = frozenset(
    {
        Op.PUSHINT,
        Op.ADDI,
        Op.JMPNE,
        Op.JMPEQ,
        Op.JMPLT,
        Op.JMPLE,
        Op.JMPGT,
        Op.JMPGE,
        Op.JMPT,
        Op.JMPF,
        Op.JMPONT,
        Op.JMPONF,
        Op.JMP,
        Op.FORPREP,
        Op.FORLOOP,
        Op.LFORPREP,
        Op.LFORLOOP,
    }
)
#: The instructions with two arguments, `A` and `B`.
TWO_ARGS = frozenset({Op.CALL, Op.TAILCALL, Op.SETTABLE, Op.SETLIST, Op.CLOSURE})
#: The comparisons, which pop two values and jump on the result.
COMPARES = frozenset({Op.JMPNE, Op.JMPEQ, Op.JMPLT, Op.JMPLE, Op.JMPGT, Op.JMPGE})
#: Every conditional jump (a comparison, a test of one value, or a test that keeps the value when it jumps).
CONDITIONAL = COMPARES | {Op.JMPT, Op.JMPF, Op.JMPONT, Op.JMPONF}
#: Every instruction whose `S` argument is a jump offset.
JUMPS = CONDITIONAL | {Op.JMP, Op.FORPREP, Op.FORLOOP, Op.LFORPREP, Op.LFORLOOP}


@dataclass(frozen=True)
class Instr:
    """One decoded instruction. `arg` is U or S (the signed value for S); `a` and `b` are set for two-argument ones."""

    op: Op
    arg: int
    a: int
    b: int

    def target(self, pc: int) -> int:
        """The pc a jump at `pc` goes to (jumps count from the next instruction)."""
        return pc + 1 + self.arg


def decode(word: int) -> Instr:
    """Split an instruction word into its opcode and arguments. Raises ValueError on an unknown opcode."""
    code = word & 0x3F
    if code > Op.CLOSURE:
        raise ValueError(f"unknown opcode {code}")
    op = Op(code)
    u = word >> 6
    if op in TWO_ARGS:
        return Instr(op, u, word >> 15, (word >> 6) & 0x1FF)
    if op in SIGNED:
        return Instr(op, u - MAXARG_S, 0, 0)
    return Instr(op, u, 0, 0)


def stack_effect(instr: Instr) -> int:
    """How the instruction changes the code generator's count of the stack (`lcode.c`'s deltas).

    CALL, RETURN and TAILCALL are not counted here: the generator resets its count around them (`depth_after`).
    """
    op = instr.op
    if op in (Op.PUSHNIL,):
        return instr.arg
    if op in (Op.POP,):
        return -instr.arg
    if op in (Op.SETTABLE, Op.SETLIST):
        return -instr.b
    if op == Op.SETMAP:
        return -2 * instr.arg
    if op == Op.CONCAT:
        return 1 - instr.arg
    if op == Op.CLOSURE:
        return 1 - instr.b
    return _FIXED[op]


#: The fixed deltas (push minus pop) of the instructions whose effect does not depend on their arguments.
_FIXED: dict[Op, int] = {
    Op.END: 0,
    Op.RETURN: 0,
    Op.CALL: 0,
    Op.TAILCALL: 0,
    Op.PUSHINT: 1,
    Op.PUSHSTRING: 1,
    Op.PUSHNUM: 1,
    Op.PUSHNEGNUM: 1,
    Op.PUSHUPVALUE: 1,
    Op.GETLOCAL: 1,
    Op.GETGLOBAL: 1,
    Op.GETTABLE: -1,
    Op.GETDOTTED: 0,
    Op.GETINDEXED: 0,
    Op.PUSHSELF: 1,
    Op.CREATETABLE: 1,
    Op.SETLOCAL: -1,
    Op.SETGLOBAL: -1,
    Op.ADD: -1,
    Op.ADDI: 0,
    Op.SUB: -1,
    Op.MULT: -1,
    Op.DIV: -1,
    Op.POW: -1,
    Op.MINUS: 0,
    Op.NOT: 0,
    Op.JMPNE: -2,
    Op.JMPEQ: -2,
    Op.JMPLT: -2,
    Op.JMPLE: -2,
    Op.JMPGT: -2,
    Op.JMPGE: -2,
    Op.JMPT: -1,
    Op.JMPF: -1,
    Op.JMPONT: -1,
    Op.JMPONF: -1,
    Op.JMP: 0,
    Op.PUSHNILJMP: 0,
    Op.FORPREP: 0,
    Op.FORLOOP: -3,
    Op.LFORPREP: 2,
    Op.LFORLOOP: -3,
}


def depth_after(instr: Instr, depth: int) -> int:
    """The code generator's stack count after `instr`, given the count before it.

    A call leaves its `B` results where the function was (`A`), none counted when they are left open; a return
    leaves the count at its first returned slot, which is the number of active locals.
    """
    if instr.op == Op.CALL:
        return instr.a + (0 if instr.b == MULT_RET else instr.b)
    if instr.op == Op.TAILCALL:
        return instr.b
    if instr.op == Op.RETURN:
        return instr.arg
    return depth + stack_effect(instr)

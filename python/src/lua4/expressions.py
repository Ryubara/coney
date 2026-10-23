# SPDX-License-Identifier: GPL-3.0-or-later
"""Symbolic execution of Lua 4.0 expression code on a stack of expression trees.

Every instruction that only computes a value (a push, a read, an operator, a table constructor, a closure, a call
that keeps results, a value made by jumps) replaces stack slots by the tree that computes them. Anything else (a
store, a call whose results are dropped, a jump of a statement, a loop instruction, a return, a block's POP) is an
*event* that the statement reader handles; `ExpressionReader.run` stops there.

The stack (`Frame`) holds the whole frame, locals included, so the code generator's own depth
(`FunctionInfo.depth`) can be checked against it, and it remembers for every slot the pc where the code computing
it starts: in stripped code that is how the reader finds where a `local` statement ended and the next began. A
call whose results are left open counts as no slot, as the generator counts it.

Research: docs/research/formats/lua-bytecode.md#patterns.
"""

from __future__ import annotations

from collections.abc import Callable, Sequence

from lua4.analysis import DecompileError, FunctionInfo, ValueRegion
from lua4.ast import (
    BinOp,
    Call,
    Concat,
    Expr,
    ExtraResult,
    Function,
    Global,
    Index,
    Local,
    Method,
    Nil,
    Number,
    SelfArg,
    String,
    Table,
    UnOp,
    Upvalue,
)
from lua4.conditions import FALSE_VALUE, TRUE_VALUE, Leaf, build
from lua4.opcodes import COMPARES, CONDITIONAL, MULT_RET, Instr, Op

#: The comparison each compare-and-jump instruction tests (the jump is taken when it holds).
COMPARE_OPS = {Op.JMPNE: "~=", Op.JMPEQ: "==", Op.JMPLT: "<", Op.JMPLE: "<=", Op.JMPGT: ">", Op.JMPGE: ">="}
_ARITH = {Op.ADD: "+", Op.SUB: "-", Op.MULT: "*", Op.DIV: "/", Op.POW: "^"}

#: Makes the closure for nested prototype `index`, given the names of the upvalues it captures and of the locals
#: active where it is made (which its own made-up names must not hide).
ClosureMaker = Callable[[int, list[str], Sequence[str]], Function]


class PendingLocal(Exception):
    """A local-variable instruction at `pc` names stack slot `slot`, which holds a value no statement has declared
    a local yet: the code that pushed it was a `local` statement (in stripped code, only such a use shows it)."""

    def __init__(self, slot: int, pc: int) -> None:
        super().__init__(f"pc {pc}: slot {slot} is used as a local")
        self.slot = slot
        self.pc = pc


def width(expr: Expr) -> int:
    """How many stack slots the generator counts for `expr`: none for a call left open or kept with no result."""
    return 0 if isinstance(expr, Call) and (expr.results is None or expr.results == 0) else 1


class Frame:
    """The symbolic stack: each item's expression and the pc where the code computing it starts (-1 for a local
    declared before the statement being read)."""

    def __init__(self, items: Sequence[Expr] = (), starts: Sequence[int] = ()) -> None:
        self.items = list(items)
        self.starts = list(starts) if starts else [-1] * len(self.items)

    def copy(self) -> Frame:
        """An independent copy."""
        return Frame(self.items, self.starts)

    def restore(self, other: Frame) -> None:
        """Become a copy of `other`."""
        self.items, self.starts = list(other.items), list(other.starts)

    def __len__(self) -> int:
        return len(self.items)

    def depth(self) -> int:
        """The generator's count of the stack."""
        return sum(width(item) for item in self.items)

    def push(self, expr: Expr, start: int) -> None:
        """Push one item whose code starts at `start`."""
        self.items.append(expr)
        self.starts.append(start)

    def pop(self, pc: int) -> tuple[Expr, int]:
        """Pop one value and its start; an item that is not a value there is a decompile error, not a crash."""
        if not self.items:
            raise DecompileError(f"pc {pc}: stack underflow")
        item, start = self.items.pop(), self.starts.pop()
        if width(item) == 0:
            raise DecompileError(f"pc {pc}: an open call used as one value")
        return item, start

    def pop_many(self, count: int, pc: int) -> tuple[list[Expr], int]:
        """Pop the top `count` items (in stack order) and the start of the lowest."""
        if count > len(self.items):
            raise DecompileError(f"pc {pc}: stack underflow")
        items, start = self.items[len(self.items) - count :], self.starts[len(self.items) - count]
        self.cut(len(self.items) - count)
        return items, start

    def cut(self, index: int) -> None:
        """Remove every item from `index` up."""
        del self.items[index:]
        del self.starts[index:]

    def index_of(self, position: int) -> int:
        """The index of the one-slot item at generator position `position`."""
        at = 0
        for index, item in enumerate(self.items):
            size = width(item)
            if at == position and size == 1:
                return index
            at += size
        raise DecompileError(f"no value at stack position {position}")

    def items_from(self, position: int) -> list[Expr]:
        """The items at generator position `position` and above (an open call at the top included)."""
        at, found = 0, []
        for item in self.items:
            if at >= position:
                found.append(item)
            at += width(item)
        return found

    def start_at(self, position: int, fallback: int) -> int:
        """Where the code of the item at generator position `position` starts, or `fallback` when the stack is
        not that deep."""
        if position >= self.depth():
            return fallback
        at = 0
        for index, item in enumerate(self.items):
            if at >= position:
                return self.starts[index]
            at += width(item)
        return fallback


class ExpressionReader:
    """Runs one function's expression code."""

    def __init__(self, info: FunctionInfo, upvalues: list[str], closure: ClosureMaker) -> None:
        self.info = info
        self.upvalues = upvalues
        self.closure = closure

    def run(
        self,
        pc: int,
        frame: Frame,
        names: Sequence[str],
        stop: frozenset[int] = frozenset(),
        region: ValueRegion | None = None,
    ) -> int:
        """Execute expression instructions from `pc` and return the pc of the first event, of the first pc in
        `stop`, or of the first statement boundary after `pc` (a statement-level jump target).

        `names` are the declared locals' names, slot by slot; a read of a slot above them raises PendingLocal.
        `region` is the value expression being read, whose own jumps are events here.
        """
        info = self.info
        start = pc
        while pc < len(info.code):
            if pc in stop or (pc > start and pc in info.boundaries):
                return pc
            instr = info.code[pc]
            if instr.op in CONDITIONAL and pc in info.regions:
                found = info.regions[pc]
                if found is region:
                    return pc
                if pc != found.jumps[0]:
                    raise DecompileError(f"pc {pc}: value expression entered in the middle")
                pc = self.value(pc, frame, names, found)
                continue
            if not self.step(pc, instr, frame, names):
                return pc
            pc += 1
        return pc

    def step(self, pc: int, instr: Instr, frame: Frame, names: Sequence[str]) -> bool:
        """Apply one expression instruction to `frame`; False when `instr` is an event."""
        op, proto = instr.op, self.info.proto
        if op == Op.PUSHNIL:
            for _ in range(instr.arg):
                frame.push(Nil(), pc)
        elif op == Op.PUSHINT:
            frame.push(Number(float(instr.arg)), pc)
        elif op == Op.PUSHSTRING:
            frame.push(String(proto.strings[instr.arg]), pc)
        elif op in (Op.PUSHNUM, Op.PUSHNEGNUM):
            value = proto.numbers[instr.arg]
            frame.push(Number(-value if op == Op.PUSHNEGNUM else value), pc)
        elif op == Op.PUSHUPVALUE:
            if instr.arg >= len(self.upvalues):
                raise DecompileError(f"pc {pc}: upvalue {instr.arg} has no name")
            frame.push(Upvalue(self.upvalues[instr.arg]), pc)
        elif op == Op.GETLOCAL:
            frame.push(Local(_local(names, instr.arg, pc)), pc)
        elif op == Op.GETGLOBAL:
            frame.push(Global(proto.strings[instr.arg]), pc)
        elif op == Op.GETTABLE:
            key, _ = frame.pop(pc)
            table, start = frame.pop(pc)
            frame.push(Index(table, key), start)
        elif op == Op.GETDOTTED:
            table, start = frame.pop(pc)
            frame.push(Index(table, String(proto.strings[instr.arg])), start)
        elif op == Op.GETINDEXED:
            key = Local(_local(names, instr.arg, pc))  # before the pop: PendingLocal leaves the frame as it was
            table, start = frame.pop(pc)
            frame.push(Index(table, key), start)
        elif op == Op.PUSHSELF:
            obj, start = frame.pop(pc)
            frame.push(Method(obj, proto.strings[instr.arg]), start)
            frame.push(SelfArg(obj), start)
        elif op == Op.CREATETABLE:
            frame.push(Table(line=self.info.lines[pc]), pc)
        elif op == Op.SETLIST:
            self._setlist(pc, instr, frame)
        elif op == Op.SETMAP:
            self._setmap(pc, instr, frame)
        elif op in _ARITH:
            right, _ = frame.pop(pc)
            left, start = frame.pop(pc)
            frame.push(BinOp(_ARITH[op], left, right), start)
        elif op == Op.ADDI:
            left, start = frame.pop(pc)
            step = BinOp("+", left, Number(instr.arg)) if instr.arg >= 0 else BinOp("-", left, Number(-instr.arg))
            frame.push(step, start)
        elif op == Op.CONCAT:
            if instr.arg < 2 or len(frame) < instr.arg:
                raise DecompileError(f"pc {pc}: CONCAT {instr.arg}")
            parts, start = frame.pop_many(instr.arg, pc)
            frame.push(Concat(tuple(parts)), start)
        elif op == Op.NOT and pc in self.info.nil_tests:
            operand, start = frame.pop(pc)
            frame.push(BinOp("==", operand, Nil()), start)
        elif op in (Op.MINUS, Op.NOT):
            operand, start = frame.pop(pc)
            frame.push(UnOp("-" if op == Op.MINUS else "not", operand), start)
        elif op == Op.CLOSURE:
            captured, start = frame.pop_many(instr.b, pc) if instr.b else ([], pc)
            names_captured = [_upvalue_name(pc, item) for item in captured]
            frame.push(self.closure(instr.a, names_captured, names), start)
        elif op == Op.CALL and instr.b != 0:
            self.call(pc, instr.a, None if instr.b == MULT_RET else instr.b, frame)
        else:
            return False
        return True

    def call(self, pc: int, position: int, results: int | None, frame: Frame) -> Call:
        """Replace the function at generator position `position` and its arguments (everything above it) by a
        call keeping `results` values (None: open); a call kept to several values also fills their slots."""
        index = frame.index_of(position)
        func, args, start = frame.items[index], frame.items[index + 1 :], frame.starts[index]
        frame.cut(index)
        if isinstance(func, Method):
            if not args or not isinstance(args[0], SelfArg):
                raise DecompileError(f"pc {pc}: a method call without its self")
            args = args[1:]
        if isinstance(func, SelfArg | ExtraResult) or any(isinstance(arg, SelfArg | ExtraResult) for arg in args):
            raise DecompileError(f"pc {pc}: a call of or with something that is not a value")
        call = Call(func, args, results)
        frame.push(call, start)
        for number in range(1, results or 1):
            frame.push(ExtraResult(call, number), start)
        return call

    def _table_below(self, pc: int, frame: Frame, count: int) -> Table:
        """The table constructor under the `count` items a SETLIST or SETMAP at `pc` stores into it."""
        if len(frame) < count + 1 or not isinstance(frame.items[-count - 1], Table):
            raise DecompileError(f"pc {pc}: no table constructor under its items")
        table = frame.items[-count - 1]
        assert isinstance(table, Table)
        return table

    def _setlist(self, pc: int, instr: Instr, frame: Frame) -> None:
        """SETLIST: move the B list items on top of the stack into the constructor below them."""
        table = self._table_below(pc, frame, instr.b)
        if table.fields and not table.items:
            table.fields_first = True
        lines = self.info.lines
        table.item_lines.extend(lines[start] if start >= 0 else 0 for start in frame.starts[len(frame) - instr.b :])
        items, _ = frame.pop_many(instr.b, pc)
        table.items.extend(items)

    def _setmap(self, pc: int, instr: Instr, frame: Frame) -> None:
        """SETMAP: move the key-value pairs on top of the stack into the constructor below them."""
        count = 2 * instr.arg
        table = self._table_below(pc, frame, count)
        lines = self.info.lines
        starts = frame.starts[len(frame) - count :]
        table.field_lines.extend(lines[start] if start >= 0 else 0 for start in starts[::2])
        flat, _ = frame.pop_many(count, pc)
        table.fields.extend((flat[n], flat[n + 1]) for n in range(0, count, 2))

    def leaf(self, pc: int, frame: Frame, region: ValueRegion | None = None) -> Leaf:
        """Pop the operand(s) a conditional jump at `pc` tests and describe the jump as a leaf."""
        instr = self.info.code[pc]
        target = instr.target(pc)
        keeps = instr.op in (Op.JMPONT, Op.JMPONF)
        if keeps and region is not None and target == region.final:
            target = TRUE_VALUE if instr.op == Op.JMPONT else FALSE_VALUE
        if instr.op in COMPARES:
            right, _ = frame.pop(pc)
            left, _ = frame.pop(pc)
            return Leaf(BinOp(COMPARE_OPS[instr.op], left, right), True, target, pc + 1)
        operand, _ = frame.pop(pc)
        if pc in self.info.nil_tests:
            # JMPT x is `x ~= nil`, JMPF x is `x == nil` (see analysis._nil_comparisons).
            return Leaf(BinOp("~=" if instr.op == Op.JMPT else "==", operand, Nil()), True, target, pc + 1)
        return Leaf(operand, instr.op in (Op.JMPT, Op.JMPONT), target, pc + 1, keeps=keeps)

    def value(self, pc: int, frame: Frame, names: Sequence[str], region: ValueRegion) -> int:
        """Read the value expression whose first jump is at `pc` (its first operand already on `frame`), push the
        expression and return the pc where it is complete. On PendingLocal the frame is left as it was."""
        saved = frame.copy()
        try:
            return self._value(pc, frame, names, region)
        except PendingLocal:
            frame.restore(saved)
            raise

    def _value(self, pc: int, frame: Frame, names: Sequence[str], region: ValueRegion) -> int:
        """Read the value expression of `region`, which starts with the jump at `pc`: each operand's code up to its
        jump (or the last operand's value), then the and/or tree that fits every jump. Pushes the expression
        and returns the pc where its value is complete."""
        operands = 2 if self.info.code[pc].op in COMPARES else 1
        start = frame.starts[-operands] if len(frame) >= operands else pc
        leaves = [self.leaf(pc, frame, region)]
        if frame.depth() != region.level:
            raise DecompileError(f"pc {pc}: value expression at depth {frame.depth()}, expected {region.level}")
        stops = frozenset(p for p in (region.value_end, region.nil_pc) if p is not None)
        at = pc + 1
        while True:
            at = self.run(at, frame, names, stops, region)
            if at in region.jumps:
                leaves.append(self.leaf(at, frame, region))
                at += 1
                continue
            if at == region.value_end:
                operand, _ = frame.pop(at)
                leaves.append(Leaf(operand, True, at, at, value=True))
                break
            if at == region.nil_pc:
                break
            raise DecompileError(f"pc {at}: unexpected instruction inside a value expression")
        true = {TRUE_VALUE}
        false = {FALSE_VALUE}
        if region.nil_pc is not None:
            true.add(region.nil_pc + 1)
            false.add(region.nil_pc)
        expr = build(leaves, frozenset(true), frozenset(false))
        if expr is None:
            raise DecompileError(f"pc {pc}: no and/or tree fits the value expression's jumps")
        frame.push(expr, start)
        return region.final


def _local(names: Sequence[str], slot: int, pc: int) -> str:
    """The name of local `slot`; PendingLocal when no statement has declared it yet."""
    if slot >= len(names):
        raise PendingLocal(slot, pc)
    return names[slot]


def _upvalue_name(pc: int, item: Expr) -> str:
    """The name `%name` refers to: a captured local or global."""
    if isinstance(item, Local | Global):
        return item.name
    raise DecompileError(f"pc {pc}: an upvalue that is neither a local nor a global")

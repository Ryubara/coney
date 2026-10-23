# SPDX-License-Identifier: GPL-3.0-or-later
"""Read a Lua 4.0 function's code back into statements.

The reader mirrors the parser that wrote the code (`lparser.c`): a block is a run of statements, and at every
statement boundary the stack holds exactly the active locals. At such a boundary it decides what the statement is:

* the head of a loop (a backward jump returns to it): `while` when the jump is unconditional, `repeat` when the
  condition at the end jumps back;
* `break`: an optional POP of the loop's locals, a jump to the loop's exit and, when there were locals, a dead
  PUSHNIL that restores the generator's count;
* a POP of locals: the end of their block;
* otherwise expression code runs until an event, which tells the rest: a store is an assignment, a call dropping its
  results a call statement, FORPREP or LFORPREP a `for` loop, RETURN or TAILCALL a `return`, a conditional jump an
  `if`.

**Locals.** The game's chunks are stripped (no local names, no line table), so `local` statements leave no mark of
their own: their values simply stay on the stack. The reader finds them from what the code does next. Values left
under a statement's own operands (below a call's function, a store's first operand, a return's first value, a
condition's operands), values still on the stack at a statement boundary (a jump target), and a slot that a
GETLOCAL, SETLOCAL or GETINDEXED reads are locals, each declared by a `local` statement that ends where the code of
the next value begins. Their names are made up (`p1` for parameters, `l1` for locals, `i` and `k, v` for loops),
never a name the chunk uses for anything else; when a chunk keeps its debug information, its names are used.

Where two readings are possible, both are tried in a fixed order and the first that reads the whole block wins. The
generator's jump threading (`analysis.FunctionInfo.equivalents`) makes some different sources compile to the same
code (an `if` at the end of a `while` body jumps straight back to the loop's head); any reading that compiles back
to the same instructions is correct, and the order only picks the more natural one.

Research: docs/research/formats/lua-bytecode.md#statements.
"""

from __future__ import annotations

from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass

from lua4.analysis import DecompileError, FunctionInfo
from lua4.ast import (
    Assign,
    Break,
    Call,
    CallStat,
    Do,
    Expr,
    ExtraResult,
    Function,
    GenericFor,
    Global,
    If,
    Index,
    Local,
    LocalStat,
    Nil,
    Number,
    NumericFor,
    Repeat,
    Return,
    Stmt,
    While,
)
from lua4.chunk import Proto
from lua4.conditions import Leaf, build
from lua4.expressions import ExpressionReader, Frame, PendingLocal
from lua4.opcodes import CONDITIONAL, Op

Stop = Callable[[int], bool] | None
Scope = tuple[str, ...]
#: What a statement reading gives: its statements, the pc after it and the locals active there.
Reading = tuple[list[Stmt], int, Scope]

#: The names of a for loop's hidden locals (never referenced by source).
_HIDDEN = ("(limit)", "(step)", "(table)")


@dataclass(frozen=True)
class _Loop:
    """The innermost loop around a block: where `break` goes."""

    exit: frozenset[int]


@dataclass(frozen=True)
class _Cond:
    """A condition read from code: its tree, the pc after it (where the true exits go) and its false exit."""

    expr: Expr
    after: int
    false: int


class Names:
    """Makes up names for a stripped function's locals: never a string the chunk uses (so never a global it
    reads), never an enclosing function's active local."""

    def __init__(self, avoid: frozenset[str]) -> None:
        self.avoid = set(avoid)
        self.counters: dict[str, int] = {}

    def fresh(self, prefix: str, numbered: bool = True) -> str:
        """`prefix1`, `prefix2`, ... (or `prefix`, `prefix2`, ... when not `numbered`), the first one free."""
        number = self.counters.get(prefix, 0)
        while True:
            number += 1
            name = f"{prefix}{number}" if numbered or number > 1 else prefix
            if name not in self.avoid:
                self.counters[prefix] = number
                self.avoid.add(name)
                return name


def chunk_strings(proto: Proto) -> frozenset[str]:
    """Every string constant of a chunk, all functions included."""
    return frozenset(text for _, function in proto.walk() for text in function.strings)


def decompile_function(
    proto: Proto,
    upvalues: Sequence[str] = (),
    avoid: frozenset[str] | None = None,
) -> Function:
    """Decompile one prototype (and, through its closures, every nested one) into a Function node.

    `avoid` is the set of names a made-up local name must not take (by default every string of the chunk). A
    function the reader cannot read whole comes back with `error` set and an empty body. Never raises for bad code.
    """
    avoid = chunk_strings(proto) if avoid is None else avoid
    function = Function([], proto.is_vararg, [], line=proto.line_defined)
    try:
        reader = _Reader(proto, list(upvalues), avoid)
        function.params = list(reader.params)
        function.body = reader.function_body()
        lines = reader.info.lines
        function.end_line = lines[-1] if lines else 0
    except (DecompileError, RecursionError) as error:
        function.error = str(error) if isinstance(error, DecompileError) else "nesting too deep"
    return function


class _Reader:
    """Reads one function."""

    def __init__(self, proto: Proto, upvalues: list[str], avoid: frozenset[str]) -> None:
        self.info = FunctionInfo.build(proto)
        self.avoid = avoid
        self.names = Names(avoid | frozenset(upvalues))
        self.closures: dict[int, Function] = {}
        self.exprs = ExpressionReader(self.info, upvalues, self._closure)
        count = proto.num_params + int(proto.is_vararg)
        debug = [var.name for var in self.info.active(0)][:count]
        if len(debug) == count:
            self.params: Scope = tuple(debug[: proto.num_params])
            self.start_scope: Scope = tuple(debug)
        else:
            self.params = tuple(self.names.fresh("p") for _ in range(proto.num_params))
            self.start_scope = self.params + (("arg",) if proto.is_vararg else ())

    def _closure(self, index: int, upvalues: list[str], active: Sequence[str]) -> Function:
        # Backtracking may read a CLOSURE more than once; its function is read once.
        if index not in self.closures:
            if index >= len(self.info.proto.protos):
                raise DecompileError(f"no nested function {index}")
            avoid = self.avoid | frozenset(active)
            self.closures[index] = decompile_function(self.info.proto.protos[index], upvalues, avoid)
        return self.closures[index]

    def function_body(self) -> list[Stmt]:
        """The whole function: every statement up to the final END."""
        end = len(self.info.code) - 1
        body, at = self.block(0, end, None, self.start_scope)
        if at != end:
            raise DecompileError(f"pc {at}: the function body stops before its end")
        return body

    def debug_names(self, pc: int, first: int, count: int) -> list[str] | None:
        """The debug names of slots first .. first+count-1 at `pc`, when the chunk kept them."""
        active = self.info.active(pc)
        if len(active) < first + count:
            return None
        return [var.name for var in active[first : first + count]]

    # --- blocks ---------------------------------------------------------------------------------------------------

    def block(
        self, start: int, limit: int, loop: _Loop | None, scope: Scope, stop: Stop = None
    ) -> tuple[list[Stmt], int]:
        """Statements from `start` until `limit` or the first boundary where `stop` holds; returns them and the pc
        reached. Locals that end before the block does are wrapped in `do ... end`, and so is a `return` or `break`
        that more statements follow."""
        base = len(scope)
        stmts: list[Stmt] = []
        declared: list[int] = []  # for each local this block declared, the index of its statement in `stmts`
        pc = start
        while pc < limit and not (stop is not None and stop(pc)):
            new, pc, after = self.statement(pc, limit, loop, scope, stop)
            for _ in range(len(after) - len(scope)):
                declared.append(len(stmts))
            stmts.extend(new)
            popped = len(scope) - len(after)
            if popped > 0 and self.merged_break(pc, loop, popped):
                # A `break` whose POP of the loop's locals merged into the store before it: the locals stay active
                # for the code after it (which the dead PUSHNIL accounts for).
                stmts.append(Break(line=self.info.lines[pc]))
                pc += 2
                continue
            if len(after) < len(scope):
                if len(after) < base:
                    raise DecompileError(f"pc {pc}: a block pops locals it did not declare")
                natural = len(after) == base and (pc >= limit or (stop is not None and stop(pc)))
                if not natural:
                    first = declared[len(after) - base]
                    stmts[first:] = [Do(stmts[first:])]
                del declared[len(after) - base :]
            scope = after
        for index, stmt in enumerate(stmts[:-1]):
            if isinstance(stmt, Return | Break):
                stmts[index] = Do([stmt], line=stmt.line)
        return stmts, pc

    def statement(self, pc: int, limit: int, loop: _Loop | None, scope: Scope, stop: Stop) -> Reading:
        """One statement (or none, for the POP that ends a block's locals) at boundary `pc`."""
        info = self.info
        back = info.backward_jumps_to(pc, limit)
        readings: list[Callable[[], Reading]] = []
        plain = lambda: self.plain(pc, limit, loop, scope, stop)  # noqa: E731
        if back:
            last = back[-1]
            if info.code[last].op == Op.JMP:
                looped = lambda: self.while_loop(pc, last, scope)  # noqa: E731
            else:
                looped = lambda: self.repeat_loop(pc, last, scope)  # noqa: E731
            # An `if` that ends where a loop jumps back to its head jumps straight to the head: when this block's
            # end leads back here, a backward jump is more likely such an `if` than a loop of its own.
            readings = [plain, looped] if pc in info.equivalents(limit) else [looped, plain]
        else:
            readings = [plain]
        error: DecompileError | None = None
        for reading in readings:
            try:
                return reading()
            except DecompileError as failed:
                error = error or failed
        assert error is not None
        raise error

    # --- simple statements ----------------------------------------------------------------------------------------

    def plain(self, start: int, limit: int, loop: _Loop | None, scope: Scope, stop: Stop) -> Reading:
        """A statement that is not a loop head: run its expression code to the first event and dispatch on it."""
        info = self.info
        base = len(scope)
        if info.depth[start] != base:
            raise DecompileError(f"pc {start}: depth {info.depth[start]} with {base} locals: not a boundary")
        line = info.lines[start]
        broken = self.break_at(start, loop)
        if broken is not None:
            return [Break(line=line)], broken, scope
        first = info.code[start]
        if first.op == Op.POP:
            if not 0 < first.arg <= base:
                raise DecompileError(f"pc {start}: POP {first.arg} with {base} locals")
            return [], start + 1, scope[: base - first.arg]
        frame = Frame([Local(name) for name in scope])
        extras: list[Expr] = []
        pc = start
        try:
            while True:
                pc = self.exprs.run(pc, frame, scope)
                if pc >= len(info.code):
                    raise DecompileError(f"pc {start}: a statement runs past the end")
                instr = info.code[pc]
                if instr.op == Op.POP and pc > start and self._is_store(pc + 1) and frame.depth() - instr.arg > base:
                    # More values than variables: the extra ones are evaluated and dropped.
                    dropped, _ = frame.pop_many(instr.arg, pc)
                    extras = dropped + extras
                    pc += 1
                    continue
                break
            own = self._own_position(pc, frame, base, scope)
        except PendingLocal as pending:
            if pending.slot < base:
                raise DecompileError(f"pc {pending.pc}: local slot {pending.slot} is not pending") from None
            after = frame.start_at(pending.slot + 1, pending.pc)
            return self.declare(start, frame, base, pending.slot + 1, after, scope)
        if own is None or own > base:
            upto = frame.depth() if own is None else own
            if extras:
                raise DecompileError(f"pc {pc}: dropped values before a local statement")
            return self.declare(start, frame, base, upto, frame.start_at(upto, pc), scope)
        if own < base:
            raise DecompileError(f"pc {pc}: a statement that uses its locals' slots")
        return self.dispatch(start, pc, frame, base, extras, limit, loop, scope)

    def _is_store(self, pc: int) -> bool:
        return pc < len(self.info.code) and self.info.code[pc].op in (Op.SETLOCAL, Op.SETGLOBAL, Op.SETTABLE)

    def _own_position(self, pc: int, frame: Frame, base: int, scope: Scope) -> int | None:
        """The stack position where the operands of the event at `pc` begin (everything below is locals), or None
        when the event begins no statement (a jump target, a block's POP, the end): then all values are locals."""
        info = self.info
        instr = info.code[pc]
        op = instr.op
        if pc in info.boundaries and len(frame) > base and frame.starts[base] < pc:
            return None
        if op == Op.CALL and instr.b == 0:
            return instr.a
        if op == Op.RETURN:
            return instr.arg
        if op == Op.TAILCALL:
            return instr.b
        if op == Op.FORPREP:
            return frame.depth() - 3
        if op == Op.LFORPREP:
            return frame.depth() - 1
        if op in CONDITIONAL and pc not in info.regions:
            return info.depth[pc + 1]
        if op in (Op.SETLOCAL, Op.SETGLOBAL, Op.SETTABLE):
            return self._stores(pc, frame, scope)[2]
        return None

    def declare(self, start: int, frame: Frame, base: int, upto: int, after: int, scope: Scope) -> Reading:
        """`local` statements for the values at stack positions base .. upto-1, which the code shows are locals:
        one statement per value (a call kept to several values, or one PUSHNIL of several nils, makes one)."""
        info = self.info
        # A value made by one instruction with several results (a call kept to two values, PUSHNIL 2) is declared
        # whole: the next statement starts after all of it.
        while upto < frame.depth() and frame.start_at(upto, -2) == frame.start_at(upto - 1, -3):
            upto += 1
            after = frame.start_at(upto, after)
        if upto <= base or after <= start:
            raise DecompileError(f"pc {start}: no local statement here")
        if after > len(info.depth) - 1 or info.depth[after] != upto:
            raise DecompileError(f"pc {after}: locals end where the depth is not {upto}")
        first, last = frame.index_of(base), frame.index_of(upto - 1)
        items, starts = frame.items[first : last + 1], frame.starts[first : last + 1]
        stmts: list[Stmt] = []
        names: list[str] = []
        group: list[Expr] = []
        for position, (item, item_start) in enumerate(zip(items, starts, strict=True)):
            group.append(item)
            if position + 1 < len(items) and starts[position + 1] == item_start:
                continue
            slot = base + len(names)
            debug = self.debug_names(after, slot, len(group))
            new = debug or [self.names.fresh("l") for _ in group]
            values = _values(group)
            if all(isinstance(value, Nil) for value in values):
                values = []
            stmts.append(LocalStat(new, values, line=info.lines[item_start] if item_start >= 0 else 0))
            names += new
            group = []
        return stmts, after, scope + tuple(names)

    def dispatch(
        self,
        start: int,
        pc: int,
        frame: Frame,
        base: int,
        extras: list[Expr],
        limit: int,
        loop: _Loop | None,
        scope: Scope,
    ) -> Reading:
        """The statement whose operands start right at `base`, by the event at `pc`."""
        info = self.info
        instr = info.code[pc]
        op = instr.op
        line = info.lines[start]
        if op == Op.CALL and instr.b == 0:
            call = self.exprs.call(pc, instr.a, 0, frame)
            return [CallStat(call, line=line)], pc + 1, scope
        if op in (Op.SETLOCAL, Op.SETGLOBAL, Op.SETTABLE):
            targets, values, _, at, depth = self._stores(pc, frame, scope)
            values = _trim_nils(_values(values + extras), len(targets), allow_empty=False)
            return [Assign(targets, values, line=line)], at, scope[: min(depth, base)]
        if op in (Op.RETURN, Op.TAILCALL):
            return [Return(self._returned(pc, frame), line=line)], pc + 1, scope
        if op == Op.FORPREP:
            return self.numeric_for(start, pc, frame, base, scope)
        if op == Op.LFORPREP:
            return self.generic_for(start, pc, frame, base, scope)
        if op in CONDITIONAL and pc not in info.regions:
            return self.if_stat(start, limit, loop, scope)
        raise DecompileError(f"pc {pc}: {op.name} where a statement was expected")

    def break_at(self, pc: int, loop: _Loop | None) -> int | None:
        """The pc after a `break` at `pc`, or None: [POP n] JMP exit [PUSHNIL n]."""
        if loop is None:
            return None
        code = self.info.code
        at, pops = pc, 0
        if code[at].op == Op.POP:
            pops = code[at].arg
            at += 1
        if at >= len(code) or code[at].op != Op.JMP or self.info.jump_target(at) not in loop.exit:
            return None
        at += 1
        if pops:
            if at >= len(code) or code[at].op != Op.PUSHNIL or code[at].arg != pops:
                return None
            at += 1
        return at

    def merged_break(self, pc: int, loop: _Loop | None, pops: int) -> bool:
        """Whether `JMP exit; PUSHNIL pops` at `pc` is a `break` whose POP merged into the store before it."""
        code = self.info.code
        return (
            loop is not None
            and pc + 1 < len(code)
            and code[pc].op == Op.JMP
            and self.info.jump_target(pc) in loop.exit
            and code[pc + 1].op == Op.PUSHNIL
            and code[pc + 1].arg == pops
        )

    def _returned(self, pc: int, frame: Frame) -> list[Expr]:
        instr = self.info.code[pc]
        if instr.op == Op.TAILCALL:
            # `return f(x)`: the open call became a tail call; values below it (from B) are returned too.
            self.exprs.call(pc, instr.a, None, frame)
            first = instr.b
        else:
            first = instr.arg
        return _values(frame.items_from(first))

    def _stores(self, pc: int, frame: Frame, scope: Scope) -> tuple[list[Expr], list[Expr], int, int, int]:
        """Read the run of stores of an assignment from `pc` on a copy of `frame`: (targets, values, the lowest
        position the assignment uses, the pc after it, the depth after it). Stores run from the last target to the
        first, each taking the top value; a POP after them drops table-and-key pairs left under the values."""
        info = self.info
        work = frame.copy()
        targets: list[Expr] = []
        values: list[Expr] = []
        low = work.depth()
        at = pc
        while at < len(info.code):
            instr = info.code[at]
            if instr.op == Op.SETLOCAL:
                name = _scope_name(scope, instr.arg, at)
                value, _ = work.pop(at)
                values.append(value)
                targets.append(Local(name))
                low = min(low, work.depth())
            elif instr.op == Op.SETGLOBAL:
                value, _ = work.pop(at)
                values.append(value)
                targets.append(Global(info.proto.strings[instr.arg]))
                low = min(low, work.depth())
            elif instr.op == Op.SETTABLE:
                position = work.depth() - instr.a
                index = work.index_of(position)
                if index + 1 >= len(work):
                    raise DecompileError(f"pc {at}: SETTABLE without its key")
                targets.append(Index(work.items[index], work.items[index + 1]))
                values.append(work.items[-1])
                low = min(low, position)
                work.pop_many(instr.b, at)
            elif instr.op == Op.POP and targets and work.depth() > low:
                work.pop_many(instr.arg, at)
            else:
                break
            at += 1
        targets.reverse()
        values.reverse()
        return targets, values, low, at, work.depth()

    # --- conditions -----------------------------------------------------------------------------------------------

    def leaves(self, start: int, end: int, scope: Scope) -> list[Leaf]:
        """The operands of a condition starting at boundary `start`, read greedily while each is expression code
        ending in a statement-level conditional jump before `end`."""
        info = self.info
        base = len(scope)
        found: list[Leaf] = []
        pc = start
        while pc < end:
            if info.depth[pc] != base:
                break
            frame = Frame([Local(name) for name in scope])
            try:
                at = self.exprs.run(pc, frame, scope)
            except (DecompileError, PendingLocal):
                break
            if at >= end or at == pc:
                break
            instr = info.code[at]
            if instr.op not in CONDITIONAL or at in info.regions or info.depth[at + 1] != base:
                break
            try:
                found.append(self.exprs.leaf(at, frame))
            except DecompileError:
                break
            if frame.depth() != base:
                break
            pc = at + 1
        return found

    def conditions(self, start: int, end: int, scope: Scope, accept: Callable[[int, int], bool]) -> Iterator[_Cond]:
        """Every condition that starts at `start`, longest first, whose false exit `accept(false, after)` allows."""
        leaves = self.leaves(start, end, scope)
        for count in range(len(leaves), 0, -1):
            last = leaves[count - 1]
            after, false = last.next, last.target
            if not accept(false, after):
                continue
            expr = build(leaves[:count], self.info.equivalents(after), frozenset({false}))
            if expr is not None:
                yield _Cond(expr, after, false)

    # --- if -------------------------------------------------------------------------------------------------------

    def is_escape(self, pc: int, false: int, loop: _Loop | None, first: int) -> bool:
        """Whether the jump at `pc` ends a `then` part and skips the `else` part that starts at pc + 1."""
        info = self.info
        if pc < first or pc >= len(info.code) or info.code[pc].op != Op.JMP:
            return False
        if false not in info.equivalents(pc + 1):
            return False
        return loop is None or info.jump_target(pc) not in loop.exit

    def if_stat(self, start: int, limit: int, loop: _Loop | None, scope: Scope) -> Reading:
        """`if`, with its `elseif` and `else` parts."""
        error: DecompileError | None = None
        for cond in self.conditions(start, limit, scope, lambda false, after: True):
            try:
                return self._if_rest(start, cond, limit, loop, scope)
            except DecompileError as failed:
                error = error or failed
        raise error or DecompileError(f"pc {start}: no condition fits this if")

    def _if_rest(self, start: int, cond: _Cond, limit: int, loop: _Loop | None, scope: Scope) -> Reading:
        info = self.info
        false = cond.false

        def then_ends(pc: int) -> bool:
            return self.is_escape(pc, false, loop, cond.after) or false in info.equivalents(pc)

        then, at = self.block(cond.after, limit, loop, scope, then_ends)
        stmt = If([(cond.expr, then)], line=info.lines[start], clause_lines=[info.lines[start]])
        if at < limit and self.is_escape(at, false, loop, cond.after):
            escape = info.jump_target(at)
            orelse, end = self.block(at + 1, limit, loop, scope, lambda pc: escape in info.equivalents(pc))
            if escape not in info.equivalents(end):
                raise DecompileError(f"pc {end}: the else part does not end where the if does")
            if len(orelse) == 1 and isinstance(orelse[0], If):
                inner = orelse[0]
                stmt.clauses += inner.clauses
                stmt.clause_lines += inner.clause_lines
                stmt.orelse = inner.orelse
            else:
                stmt.orelse = orelse
            return [stmt], end, scope
        if false not in info.equivalents(at):
            raise DecompileError(f"pc {at}: the then part does not end where the condition jumps")
        return [stmt], at, scope

    # --- loops ----------------------------------------------------------------------------------------------------

    def while_loop(self, head: int, last: int, scope: Scope) -> Reading:
        """`while cond do ... end`: the condition at the head, the body, and the jump back at `last`."""
        info = self.info
        exit_ = info.equivalents(last + 1)
        inner = _Loop(exit_)
        line = info.lines[head]
        error: DecompileError | None = None
        for cond in self.conditions(head, last, scope, lambda false, after: false in exit_ and after <= last):
            try:
                body, at = self.block(cond.after, last, inner, scope)
                if at == last:
                    return [While(cond.expr, body, line=line, end_line=info.lines[last])], last + 1, scope
            except DecompileError as failed:
                error = error or failed
        try:
            body, at = self.block(head, last, inner, scope)
        except DecompileError as failed:
            raise error or failed from None
        if at != last:
            raise DecompileError(f"pc {at}: a while body stops before its jump back")
        # `while 1` compiles to no instruction of its own: its line is not known.
        return [While(None, body, end_line=info.lines[last])], last + 1, scope

    def repeat_loop(self, head: int, last: int, scope: Scope) -> Reading:
        """`repeat ... until cond`: the body, then the condition whose false exits jump back to the head."""
        info = self.info
        inner = _Loop(info.equivalents(last + 1))
        error: DecompileError | None = None
        for start in range(head, last + 1):
            if info.depth[start] != len(scope):
                continue
            leaves = self.leaves(start, last + 1, scope)
            if not leaves or leaves[-1].next != last + 1 or leaves[-1].target != head:
                continue
            expr = build(leaves, info.equivalents(last + 1), frozenset({head}))
            if expr is None:
                continue
            try:
                body, at = self.block(head, start, inner, scope)
            except DecompileError as failed:
                error = error or failed
                continue
            if at == start:
                # `repeat` itself compiles to nothing: its line is not known.
                return [Repeat(body, expr, until_line=info.lines[last])], last + 1, scope
        raise error or DecompileError(f"pc {head}: no until condition fits this repeat")

    def numeric_for(self, start: int, prep: int, frame: Frame, base: int, scope: Scope) -> Reading:
        """`for v = a, b[, c] do ... end`: three values, FORPREP, the body, FORLOOP back to the body's start."""
        info = self.info
        if frame.depth() != base + 3:
            raise DecompileError(f"pc {prep}: FORPREP without its three values")
        start_value, limit_value, step = frame.items[len(frame) - 3 :]
        end = self._loop_end(prep, Op.FORLOOP)
        names = self.debug_names(prep + 1, base, 3)
        var = names[0] if names else self.names.fresh("i", numbered=False)
        body, at = self.block(prep + 1, end, _Loop(info.equivalents(end + 1)), (*scope, var, *_HIDDEN[:2]))
        if at != end:
            raise DecompileError(f"pc {at}: a for body stops before its FORLOOP")
        default = isinstance(step, Number) and step.value == 1
        stmt = NumericFor(var, start_value, limit_value, None if default else step, body, line=info.lines[start])
        stmt.end_line = info.lines[end]
        return [stmt], end + 1, scope

    def generic_for(self, start: int, prep: int, frame: Frame, base: int, scope: Scope) -> Reading:
        """`for k, v in t do ... end`: the table, LFORPREP, the body, LFORLOOP back to the body's start."""
        info = self.info
        if frame.depth() != base + 1:
            raise DecompileError(f"pc {prep}: LFORPREP without its table")
        end = self._loop_end(prep, Op.LFORLOOP)
        names = self.debug_names(prep + 1, base, 3)
        if names:
            key, value = names[1], names[2]
        else:
            key, value = self.names.fresh("k", numbered=False), self.names.fresh("v", numbered=False)
        body, at = self.block(prep + 1, end, _Loop(info.equivalents(end + 1)), (*scope, _HIDDEN[2], key, value))
        if at != end:
            raise DecompileError(f"pc {at}: a for body stops before its LFORLOOP")
        stmt = GenericFor(key, value, frame.items[-1], body, line=info.lines[start])
        stmt.end_line = info.lines[end]
        return [stmt], end + 1, scope

    def _loop_end(self, prep: int, op: Op) -> int:
        """The FORLOOP/LFORLOOP that jumps back to the body after `prep`."""
        for pc in range(prep + 1, len(self.info.code)):
            instr = self.info.code[pc]
            if instr.op == op and instr.target(pc) == prep + 1:
                return pc
        raise DecompileError(f"pc {prep}: no {op.name} closes this loop")


def _scope_name(scope: Scope, slot: int, pc: int) -> str:
    """The name of local `slot`; PendingLocal when it is not declared yet."""
    if slot >= len(scope):
        raise PendingLocal(slot, pc)
    return scope[slot]


def _values(items: list[Expr]) -> list[Expr]:
    """An expression list as written: the extra results of a call adjusted to several values are implied by it."""
    return [item for item in items if not isinstance(item, ExtraResult)]


def _trim_nils(values: list[Expr], names: int, allow_empty: bool) -> list[Expr]:
    """Drop the trailing `nil`s the generator adds itself when there are fewer values than names; a call before
    them keeps them, since it would otherwise be adjusted to fill those names, and so do values beyond the names,
    which the code evaluates and drops."""
    trimmed = list(values)
    if len(trimmed) > names:
        return trimmed
    while trimmed and isinstance(trimmed[-1], Nil):
        rest = trimmed[:-1]
        if rest and isinstance(rest[-1], Call):
            break
        if not rest and not allow_empty:
            break
        trimmed = rest
    return trimmed

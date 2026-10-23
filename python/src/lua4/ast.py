# SPDX-License-Identifier: GPL-3.0-or-later
"""The decompiler's syntax tree: Lua 4.0 expressions and statements, as the renderer writes them.

The tree holds only what source can say: names, constants, operators and blocks. Each statement keeps the source
line of its first instruction (from the chunk's line table), so the renderer can put it back on that line.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# --- expressions --------------------------------------------------------------------------------------------------


class Expr:
    """Any expression."""


@dataclass(frozen=True)
class Nil(Expr):
    """`nil`."""


@dataclass(frozen=True)
class Number(Expr):
    """A number literal (negative values are written with a unary minus)."""

    value: float


@dataclass(frozen=True)
class String(Expr):
    """A string literal."""

    value: str


@dataclass(frozen=True)
class Global(Expr):
    """A global variable."""

    name: str


@dataclass(frozen=True)
class Local(Expr):
    """A local variable (or parameter), by its debug name."""

    name: str


@dataclass(frozen=True)
class Upvalue(Expr):
    """`%name`: the value a closure captured from the enclosing function when it was made."""

    name: str


@dataclass(frozen=True)
class Index(Expr):
    """`table[key]`, written `table.key` when the key is a name."""

    table: Expr
    key: Expr


@dataclass(frozen=True)
class Method(Expr):
    """The function slot PUSHSELF leaves: `object:name`, called with `object` as the first argument."""

    obj: Expr
    name: str


@dataclass(frozen=True)
class SelfArg(Expr):
    """The `self` slot PUSHSELF leaves above the method; it is the call's first argument and is not written."""

    obj: Expr


@dataclass(eq=False)
class Call(Expr):
    """A call; `results` is how many values the code kept (None: left open, as many as returned)."""

    func: Expr
    args: list[Expr]
    results: int | None = 1


@dataclass(frozen=True)
class ExtraResult(Expr):
    """A second, third, ... result of a call that the code adjusted to several values; never written itself."""

    call: Call
    index: int


@dataclass(eq=False)
class Function(Expr):
    """A closure: its parameters and body (filled in by the decompiler)."""

    params: list[str]
    is_vararg: bool
    body: list[Stmt]
    line: int = 0
    end_line: int = 0
    #: When set, the function could not be decompiled; the reason is written as a comment.
    error: str | None = None


@dataclass(eq=False)
class Table(Expr):
    """A table constructor: its list part and record part, in the order the source wrote them."""

    items: list[Expr] = field(default_factory=list)
    fields: list[tuple[Expr, Expr]] = field(default_factory=list)
    #: True when the record part came first (`{x = 1; 2, 3}`).
    fields_first: bool = False
    #: The line of each list item and record field (for placing long constructors), parallel to the lists.
    item_lines: list[int] = field(default_factory=list)
    field_lines: list[int] = field(default_factory=list)
    line: int = 0


@dataclass(frozen=True)
class BinOp(Expr):
    """A binary operator: arithmetic (`+ - * / ^`), comparison (`== ~= < <= > >=`) or logic (`and or`)."""

    op: str
    left: Expr
    right: Expr


@dataclass(frozen=True)
class Concat(Expr):
    """`a .. b .. c`: one CONCAT of several operands."""

    parts: tuple[Expr, ...]


@dataclass(frozen=True)
class UnOp(Expr):
    """A unary operator: `not` or `-`."""

    op: str
    operand: Expr


# --- statements ---------------------------------------------------------------------------------------------------


@dataclass(eq=False)
class Stmt:
    """Any statement; `line` is the source line of its first instruction (0 when unknown)."""

    line: int = field(default=0, kw_only=True)


@dataclass(eq=False)
class LocalStat(Stmt):
    """`local a, b = x, y`."""

    names: list[str]
    values: list[Expr]


@dataclass(eq=False)
class Assign(Stmt):
    """`a, t.k = x, y`."""

    targets: list[Expr]
    values: list[Expr]


@dataclass(eq=False)
class CallStat(Stmt):
    """A call whose results are dropped."""

    call: Call


@dataclass(eq=False)
class If(Stmt):
    """`if c then ... elseif c then ... else ... end`."""

    clauses: list[tuple[Expr, list[Stmt]]]
    orelse: list[Stmt] | None = None
    end_line: int = 0
    #: The source line of each clause's condition (`if`, then each `elseif`).
    clause_lines: list[int] = field(default_factory=list)


@dataclass(eq=False)
class While(Stmt):
    """`while c do ... end` (`cond` None: `while 1`)."""

    cond: Expr | None
    body: list[Stmt]
    end_line: int = 0


@dataclass(eq=False)
class Repeat(Stmt):
    """`repeat ... until c`."""

    body: list[Stmt]
    cond: Expr
    until_line: int = 0


@dataclass(eq=False)
class NumericFor(Stmt):
    """`for v = start, limit[, step] do ... end` (`step` None: the default 1)."""

    var: str
    start: Expr
    limit: Expr
    step: Expr | None
    body: list[Stmt]
    end_line: int = 0


@dataclass(eq=False)
class GenericFor(Stmt):
    """`for k, v in t do ... end`."""

    key: str
    value: str
    table: Expr
    body: list[Stmt]
    end_line: int = 0


@dataclass(eq=False)
class Do(Stmt):
    """`do ... end`: a block that ends local variables early, or holds a return or break that is not last."""

    body: list[Stmt]
    end_line: int = 0


@dataclass(eq=False)
class Return(Stmt):
    """`return a, b`."""

    values: list[Expr]


@dataclass(eq=False)
class Break(Stmt):
    """`break`."""

# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the decompiler's tree as Lua 4.0 source.

Each statement starts on the source line its first instruction came from (blank lines fill the gap), so a line
number in an error message or the debugger still points at the same statement. Comments and the original layout
inside a statement cannot come back: `luac` keeps neither. Parentheses follow Lua 4.0's operator priorities
(`lparser.c`'s `priority` table): `and` and `or` share the lowest priority and group to the left, `..` and `^` group
to the right, and the unary operators bind tighter than everything but `^`.

Research: docs/research/formats/lua-bytecode.md#rendering.
"""

from __future__ import annotations

import math
import re
from collections.abc import Callable

from lua4.ast import (
    Assign,
    BinOp,
    Break,
    Call,
    CallStat,
    Concat,
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
    Method,
    Nil,
    Number,
    NumericFor,
    Repeat,
    Return,
    SelfArg,
    Stmt,
    String,
    Table,
    UnOp,
    Upvalue,
    While,
)

#: Lua 4.0's reserved words; a table key that is one of them must be written `["word"]`.
RESERVED = frozenset(
    (
        "and", "break", "do", "else", "elseif", "end", "for", "function", "if", "local",
        "nil", "not", "or", "repeat", "return", "then", "until", "while",
    )
)  # fmt: skip
_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
#: (left, right) priority of each binary operator, from lparser.c.
_PRIORITY = {
    "or": (1, 1),
    "and": (1, 1),
    "==": (2, 2),
    "~=": (2, 2),
    "<": (2, 2),
    "<=": (2, 2),
    ">": (2, 2),
    ">=": (2, 2),
    "..": (4, 3),
    "+": (5, 5),
    "-": (5, 5),
    "*": (6, 6),
    "/": (6, 6),
    "^": (9, 8),
}
_UNARY = 7
_ESCAPES = {"\\": "\\\\", '"': '\\"', "\n": "\\n", "\r": "\\r", "\t": "\\t", "\a": "\\a", "\b": "\\b", "\f": "\\f"}
_ESCAPES["\v"] = "\\v"


def is_name(text: str) -> bool:
    """Whether `text` can be written as a bare name (`t.text`, `{text = ...}`)."""
    return bool(_NAME.match(text)) and text not in RESERVED


def quote(text: str) -> str:
    """A double-quoted Lua 4.0 string literal for `text` (bytes 0-255); unprintable bytes as 3-digit escapes."""
    out = []
    for char in text:
        if char in _ESCAPES:
            out.append(_ESCAPES[char])
        elif " " <= char <= "~":
            out.append(char)
        else:
            out.append(f"\\{ord(char):03d}")
    return '"' + "".join(out) + '"'


def number(value: float) -> str:
    """A literal that reads back as exactly `value` (Lua 4.0 reads numbers with `strtod`)."""
    if math.isnan(value) or math.isinf(value):
        # No literal makes these; a division compiles to the same constant-free code, so this is only a fallback.
        return "(0/0)" if math.isnan(value) else ("1e999" if value > 0 else "-1e999")
    if value == int(value) and abs(value) < 1e16:
        return str(int(value))
    return repr(value)


def _priority(expr: Expr) -> tuple[int, int] | None:
    """(left, right) priority of an operator expression; None for an operand that never needs parentheses."""
    if isinstance(expr, BinOp):
        return _PRIORITY[expr.op]
    if isinstance(expr, Concat):
        return _PRIORITY[".."]
    if isinstance(expr, UnOp) or (isinstance(expr, Number) and (expr.value < 0 or math.copysign(1, expr.value) < 0)):
        return (_UNARY, _UNARY)
    return None


class Writer:
    """Builds the output line by line, placing statements on their source lines."""

    def __init__(self, indent: str = "    ") -> None:
        self.lines: list[str] = []
        self.current = ""
        self.current_level = 0
        self.level = 0
        self.indent = indent

    @property
    def line_no(self) -> int:
        """The number of the line being written (1-based)."""
        return len(self.lines) + 1

    def write(self, text: str) -> None:
        """Append to the line being written."""
        self.current += text

    def start(self, hint: int = 0) -> None:
        """Start a statement: on the line being written when `hint` is that line (several statements shared a
        source line), else on a new line, moved down to `hint` when that is further."""
        if self.current and hint and hint == self.line_no:
            self.write(" ")
            return
        if self.current:
            self.lines.append(self.indent * self.current_level + self.current)
            self.current = ""
        while hint > self.line_no:
            self.lines.append("")
        self.current_level = self.level

    def close(self, keyword: str, next_hint: int, pending: int) -> None:
        """Write a closing keyword (`end`, `else`) that no instruction places. It gets a line of its own when that
        leaves room for the `pending` closing keywords still to come before what follows (on source line
        `next_hint`), else it stays on the line being written, so what follows still lands on its line."""
        room = next_hint - self.line_no - 1
        if self.current and next_hint and room < 1 + pending:
            self.write(" " + keyword)
        else:
            self.start()
            self.write(keyword)

    def text(self) -> str:
        """Everything written, ending with a newline."""
        self.start()
        return "\n".join(self.lines) + "\n"

    # --- statements ---------------------------------------------------------------------------------------------

    def block(self, stmts: list[Stmt], after: int, pending: int) -> None:
        """Write statements one level deeper. `after` is the source line of what follows the block and `pending`
        how many closing keywords come before it (this block's own `end` included)."""
        self.level += 1
        for index, stmt in enumerate(stmts):
            if index + 1 < len(stmts):
                self.statement(stmt, first_line(stmts[index + 1]), 0)
            else:
                self.statement(stmt, after, pending)
        self.level -= 1

    def statement(self, stmt: Stmt, after: int = 0, pending: int = 0) -> None:
        """Write one statement. `after` is the source line of what follows it (0: unknown) and `pending` how many
        closing keywords of enclosing statements come first."""
        function = _function_statement(stmt)
        if function is not None:
            name, func, method = function
            self.start(first_line(stmt))
            self.write(f"function {name}")
            self.function_rest(func, skip_self=method)
            return
        if stmt.line:
            self.start(stmt.line)
        else:
            # `repeat`, `while 1` and `do` compile to nothing: put the keyword just above the body when there is
            # room, else on the body's first line.
            body = first_line(stmt)
            self.start(body - 1 if body - 1 > self.line_no else body)
        if isinstance(stmt, LocalStat):
            self.write("local " + ", ".join(stmt.names))
            if stmt.values:
                self.write(" = ")
                self.exprs(stmt.values)
        elif isinstance(stmt, Assign):
            self.exprs(stmt.targets)
            self.write(" = ")
            self.exprs(stmt.values)
        elif isinstance(stmt, CallStat):
            self.expr(stmt.call)
        elif isinstance(stmt, Return):
            self.write("return")
            if stmt.values:
                self.write(" ")
                self.exprs(stmt.values)
        elif isinstance(stmt, Break):
            self.write("break")
        elif isinstance(stmt, If):
            self._if(stmt, after, pending)
        elif isinstance(stmt, Repeat):
            self.write("repeat")
            self.block(stmt.body, stmt.until_line, 0)
            # The condition's code is on the `until` line, so that line is known.
            self.start(stmt.until_line)
            self.write("until ")
            self.expr(stmt.cond)
        else:
            self._loop_head(stmt)
            self.block(_body(stmt), after, pending + 1)
            self.close("end", after, pending)

    def _loop_head(self, stmt: Stmt) -> None:
        """The opening of a statement that ends with `end`: `do`, `while ... do`, `for ... do`."""
        if isinstance(stmt, Do):
            self.write("do")
        elif isinstance(stmt, While):
            self.write("while ")
            if stmt.cond is None:
                self.write("1")
            else:
                self.expr(stmt.cond)
            self.write(" do")
        elif isinstance(stmt, NumericFor):
            self.write(f"for {stmt.var} = ")
            self.exprs([stmt.start, stmt.limit] + ([stmt.step] if stmt.step is not None else []))
            self.write(" do")
        elif isinstance(stmt, GenericFor):
            self.write(f"for {stmt.key}, {stmt.value} in ")
            self.expr(stmt.table)
            self.write(" do")
        else:  # pragma: no cover - every statement kind is handled
            raise TypeError(f"unknown statement {stmt!r}")

    def _if(self, stmt: If, after: int, pending: int) -> None:
        """`if ... then ... elseif ... else ... end`. An `elseif`'s condition has instructions, so its line is
        known; `else` and `end` have none."""
        for index, (cond, body) in enumerate(stmt.clauses):
            if index:
                self.start(stmt.clause_lines[index] if index < len(stmt.clause_lines) else 0)
                self.write("elseif ")
            else:
                self.write("if ")
            self.expr(cond)
            self.write(" then")
            if index + 1 < len(stmt.clauses):
                following = stmt.clause_lines[index + 1] if index + 1 < len(stmt.clause_lines) else 0
                self.block(body, following, 0)
            elif stmt.orelse is not None:
                self.block(body, first_line(stmt.orelse[0]) if stmt.orelse else after, 1)
            else:
                self.block(body, after, pending + 1)
        if stmt.orelse is not None:
            self.close("else", first_line(stmt.orelse[0]) if stmt.orelse else after, 0 if stmt.orelse else pending + 1)
            self.block(stmt.orelse, after, pending + 1)
        self.close("end", after, pending)

    def function_rest(self, func: Function, skip_self: bool = False) -> None:
        """`(params) body end` of a function."""
        params = func.params[1:] if skip_self else list(func.params)
        if func.is_vararg:
            params.append("...")
        self.write("(" + ", ".join(params) + ")")
        if func.error is not None:
            self.level += 1
            self.start()
            self.write(f"-- coney-tools could not decompile this function: {func.error}")
            self.level -= 1
        else:
            self.block(func.body, func.end_line, 0)
        self.start(func.end_line)
        self.write("end")

    # --- expressions --------------------------------------------------------------------------------------------

    def exprs(self, exprs: list[Expr]) -> None:
        """A comma-separated expression list."""
        for index, expr in enumerate(exprs):
            if index:
                self.write(", ")
            self.expr(expr)

    def expr(self, expr: Expr) -> None:
        """One expression."""
        if isinstance(expr, Nil):
            self.write("nil")
        elif isinstance(expr, Number):
            self.write(number(expr.value))
        elif isinstance(expr, String):
            self.write(quote(expr.value))
        elif isinstance(expr, Global | Local):
            self.write(expr.name)
        elif isinstance(expr, Upvalue):
            self.write("%" + expr.name)
        elif isinstance(expr, Index):
            self.prefix(expr.table)
            if isinstance(expr.key, String) and is_name(expr.key.value):
                self.write("." + expr.key.value)
            else:
                self.write("[")
                self.expr(expr.key)
                self.write("]")
        elif isinstance(expr, Call):
            if isinstance(expr.func, Method):
                self.prefix(expr.func.obj)
                self.write(":" + expr.func.name)
            else:
                self.prefix(expr.func)
            self.write("(")
            self.exprs(expr.args)
            self.write(")")
        elif isinstance(expr, Function):
            self.write("function")
            self.function_rest(expr)
        elif isinstance(expr, Table):
            self.table(expr)
        elif isinstance(expr, BinOp):
            left, right = _PRIORITY[expr.op]
            self.operand(expr.left, lambda p: left <= p[1])
            self.write(f" {expr.op} ")
            # A unary operator may start any operand: `2 ^ -1` needs no parentheses.
            self.operand(expr.right, lambda p: p == (_UNARY, _UNARY) or p[0] > right)
        elif isinstance(expr, Concat):
            left, right = _PRIORITY[".."]
            for index, part in enumerate(expr.parts):
                if index:
                    self.write(" .. ")
                if index < len(expr.parts) - 1:
                    self.operand(part, lambda p: left <= p[1])
                else:
                    self.operand(part, lambda p: p == (_UNARY, _UNARY) or p[0] > right)
        elif isinstance(expr, UnOp):
            self.write("not " if expr.op == "not" else "-")
            inner = expr.operand
            if expr.op == "-" and (
                (isinstance(inner, UnOp) and inner.op == "-") or _priority(inner) == (_UNARY, _UNARY)
            ):
                self.write(" ")  # `- -x`: `--` would start a comment
            self.operand(inner, lambda p: p[0] >= _UNARY)
        elif isinstance(expr, Method | SelfArg | ExtraResult):
            raise TypeError(f"{type(expr).__name__} is not an expression of its own")
        else:  # pragma: no cover
            raise TypeError(f"unknown expression {expr!r}")

    def operand(self, expr: Expr, fits: Callable[[tuple[int, int]], bool]) -> None:
        """An operator's operand, in parentheses unless `fits(priority)` says the parser groups it right."""
        priority = _priority(expr)
        if priority is None or fits(priority):
            self.expr(expr)
        else:
            self.write("(")
            self.expr(expr)
            self.write(")")

    def prefix(self, expr: Expr) -> None:
        """The part before `.name`, `[key]` or `(args)`: Lua 4.0 allows only a name, `%name` or such a chain there."""
        if isinstance(expr, Global | Local | Upvalue | Index | Call):
            self.expr(expr)
        else:
            self.write("(")
            self.expr(expr)
            self.write(")")

    def table(self, table: Table) -> None:
        """A table constructor, list part and record part in their source order."""
        self.write("{")
        list_part = [("item", item) for item in table.items]
        record_part = [("field", pair) for pair in table.fields]
        sections = [record_part, list_part] if table.fields_first else [list_part, record_part]
        sections = [section for section in sections if section]
        for number_, section in enumerate(sections):
            if number_:
                self.write("; ")
            for index, (kind, entry) in enumerate(section):
                if index:
                    self.write(", ")
                if kind == "item":
                    assert not isinstance(entry, tuple)
                    self.expr(entry)
                else:
                    assert isinstance(entry, tuple)
                    key, value = entry
                    if isinstance(key, String) and is_name(key.value):
                        self.write(key.value)
                    else:
                        self.write("[")
                        self.expr(key)
                        self.write("]")
                    self.write(" = ")
                    self.expr(value)
        self.write("}")


def _body(stmt: Stmt) -> list[Stmt]:
    """The block of a statement that has one."""
    assert isinstance(stmt, Do | While | NumericFor | GenericFor)
    return stmt.body


def first_line(stmt: Stmt) -> int:
    """The source line a statement starts on: a function statement's is where its `function` keyword was (its
    CLOSURE instruction comes after the body, on the line of its `end`)."""
    function = _function_statement(stmt)
    if function is not None:
        return function[1].line or stmt.line
    if not stmt.line and isinstance(stmt, Repeat | While | Do) and stmt.body:
        return first_line(stmt.body[0])
    return stmt.line


def _function_statement(stmt: Stmt) -> tuple[str, Function, bool] | None:
    """(name, function, is_method) when `stmt` can be written `function name(...)`: one target that is a name or a
    name's field, and a function value. A method's first parameter is `self`."""
    if not isinstance(stmt, Assign) or len(stmt.targets) != 1 or len(stmt.values) != 1:
        return None
    target, value = stmt.targets[0], stmt.values[0]
    if not isinstance(value, Function):
        return None
    if isinstance(target, Global | Local):
        return target.name, value, False
    if (
        isinstance(target, Index)
        and isinstance(target.table, Global | Local)
        and isinstance(target.key, String)
        and is_name(target.key.value)
    ):
        method = bool(value.params) and value.params[0] == "self"
        separator = ":" if method else "."
        return f"{target.table.name}{separator}{target.key.value}", value, method
    return None


def render(main: Function) -> str:
    """The source of a whole chunk (its main function's body)."""
    writer = Writer()
    if main.error is not None:
        writer.write(f"-- coney-tools could not decompile the main chunk: {main.error}")
    for index, stmt in enumerate(main.body):
        writer.statement(stmt, first_line(main.body[index + 1]) if index + 1 < len(main.body) else 0)
    return writer.text()

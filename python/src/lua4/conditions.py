# SPDX-License-Identifier: GPL-3.0-or-later
"""Rebuild an `and`/`or` tree from the jumps its operands compiled to.

Lua 4.0 compiles a condition as a run of *leaves*: each leaf is an operand's code ending in a conditional jump (or,
for the last operand of a value, no jump at all). In `A and B`, every exit of A on false goes where the whole
expression goes on false, and A on true falls through to B; in `A or B`, A on true goes to the whole's true exit and
A on false falls through to B (`luaK_goiftrue`, `luaK_goiffalse` and `luaK_posfix` in `lcode.c`). `and` and `or`
have the same priority in Lua 4.0 and group to the left.

`build` searches the splits that agree with every jump, preferring the left-deep tree the parser makes for a chain
(`a and b or c` is `(a and b) or c`). A leaf whose jump is taken on the opposite sense to what its place needs is
written negated; Lua 4.0 compiles `not x` there to the same jump, so the code comes back the same.
"""

from __future__ import annotations

from dataclasses import dataclass

from lua4.ast import BinOp, Expr, UnOp

#: Pseudo-targets for the jumps that keep their value (JMPONT, JMPONF) and land on a value expression's end.
TRUE_VALUE = -10
FALSE_VALUE = -11

_NEGATED = {"==": "~=", "~=": "==", "<": ">=", ">=": "<", "<=": ">", ">": "<="}


@dataclass(frozen=True)
class Leaf:
    """One operand: its expression and how its code leaves.

    `on_true`: the jump is taken when `expr` is true (else when it is false). `target`: where the jump goes (a pc or
    a pseudo-target). `next`: where the code falls through to. `value`: the operand is a value with no jump (the last
    operand of a value expression).
    """

    expr: Expr
    on_true: bool
    target: int
    next: int
    value: bool = False
    #: The jump keeps the operand's value (JMPONT/JMPONF), so the operand cannot be written negated.
    keeps: bool = False


def negate(expr: Expr) -> Expr:
    """`not expr`, written the way that compiles to the same jump: a comparison inverted (`not (a < b)` compiles to
    the same jump as `a >= b`). A `not` is kept even on a `not`: the operand's NOT instruction is in the code, and
    `not not x` compiles to it followed by the jump, where `x` alone would lose it."""
    if isinstance(expr, BinOp) and expr.op in _NEGATED:
        return BinOp(_NEGATED[expr.op], expr.left, expr.right)
    return UnOp("not", expr)


def build(leaves: list[Leaf], true_exits: frozenset[int], false_exits: frozenset[int]) -> Expr | None:
    """The expression whose leaves are `leaves`, true going to `true_exits` and false to `false_exits`, or None
    when no tree agrees with the jumps."""
    memo: dict[tuple[int, int, frozenset[int], frozenset[int]], Expr | None] = {}

    def sub(first: int, last: int, true: frozenset[int], false: frozenset[int]) -> Expr | None:
        key = (first, last, true, false)
        if key in memo:
            return memo[key]
        memo[key] = None  # a guard against cycles while this one is being worked out
        result = _single(leaves[first], true, false) if first == last else None
        # The rightmost split first: a chain groups to the left.
        for split in range(last - 1, first - 1, -1) if first != last else ():
            between = frozenset({leaves[split].next})
            for op, left_true, left_false in (("and", between, false), ("or", true, between)):
                left = sub(first, split, left_true, left_false)
                if left is None:
                    continue
                right = sub(split + 1, last, true, false)
                if right is not None:
                    result = BinOp(op, left, right)
                    break
            if result is not None:
                break
        memo[key] = result
        return result

    return sub(0, len(leaves) - 1, true_exits, false_exits) if leaves else None


def _single(leaf: Leaf, true: frozenset[int], false: frozenset[int]) -> Expr | None:
    """One leaf as a whole (sub)expression, plain or negated, if its jump and fall-through fit the exits."""
    if leaf.value:
        return leaf.expr
    taken, falls = leaf.target, leaf.next
    on_true, on_false = (taken, falls) if leaf.on_true else (falls, taken)
    plain = on_true in true and on_false in false
    negated = not leaf.keeps and on_true in false and on_false in true
    if plain and negated:
        # The jump lands where the code falls through anyway (`if x then end`): both exits are the same pc. The
        # generator only emits such a jump as the condition's last, jump-if-false one, so read it that way.
        return leaf.expr if not leaf.on_true else negate(leaf.expr)
    if plain:
        return leaf.expr
    if negated:
        return negate(leaf.expr)
    return None

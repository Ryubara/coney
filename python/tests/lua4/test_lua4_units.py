# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the lua4 package's parts: the chunk loader, the instruction decoder, the condition builder, the
renderer and the comparison. All on bytes and trees built here or on the synthetic fixtures (lua4_support)."""

from __future__ import annotations

import struct

import pytest
from lua4_support import FIXTURES

import lua4
from lua4.analysis import FunctionInfo
from lua4.ast import BinOp, Function, Global, Local, Nil, Number, String, UnOp
from lua4.chunk import ChunkError, Proto
from lua4.conditions import FALSE_VALUE, TRUE_VALUE, Leaf, build, negate
from lua4.decompiler import Names, decompile_function
from lua4.opcodes import MAXARG_S, Op, decode, depth_after
from lua4.render import Writer, is_name, number, quote

HEADER = bytes.fromhex("1b4c7561400104040420060908") + struct.pack("<d", 3.14159265358979e8)


def _string(text: str | None, size: int = 4) -> bytes:
    if text is None:
        return (0).to_bytes(size, "little")
    data = text.encode("latin-1") + b"\0"
    return len(data).to_bytes(size, "little") + data


def _proto(code: list[int], strings: tuple[str, ...] = (), params: int = 0, size: int = 4) -> bytes:
    """A minimal stripped prototype: no locals, no lines, no numbers, no nested functions."""
    out = _string("=test", size) + struct.pack("<iiBi", 0, params, 0, 4)
    out += struct.pack("<i", 0) + struct.pack("<i", 0)
    out += struct.pack("<i", len(strings)) + b"".join(_string(text, size) for text in strings)
    out += struct.pack("<i", 0) + struct.pack("<i", 0)
    out += struct.pack("<i", len(code)) + b"".join(struct.pack("<I", word) for word in code)
    return out


def _u(op: Op, arg: int = 0) -> int:
    return int(op) | (arg << 6)


def _s(op: Op, arg: int) -> int:
    return int(op) | ((arg + MAXARG_S) << 6)


def _ab(op: Op, a: int, b: int) -> int:
    return int(op) | (b << 6) | (a << 15)


# --- chunk ----------------------------------------------------------------------------------------------------------


def test_loads_a_minimal_chunk() -> None:
    """`x = 1` by hand: GETGLOBAL-free PUSHINT, SETGLOBAL, END."""
    chunk = HEADER + _proto([_s(Op.PUSHINT, 1), _u(Op.SETGLOBAL, 0), _u(Op.END)], ("x",))
    proto = lua4.load(chunk)
    assert proto.strings == ["x"]
    assert [decode(word).op for word in proto.code] == [Op.PUSHINT, Op.SETGLOBAL, Op.END]
    assert lua4.decompile(proto) == "x = 1\n"


def test_loads_a_chunk_with_8_byte_size_t() -> None:
    """A 64-bit luac writes 8-byte string lengths; the header says so and the loader follows it."""
    header = bytes.fromhex("1b4c7561400104080420060908") + struct.pack("<d", 3.14159265358979e8)
    chunk = header + _proto([_s(Op.PUSHINT, 2), _u(Op.SETGLOBAL, 0), _u(Op.END)], ("y",), size=8)
    assert lua4.decompile(chunk) == "y = 2\n"


@pytest.mark.parametrize(
    ("data", "message"),
    [
        (b"\x1bLub", "signature"),
        (bytes.fromhex("1b4c756150"), "version"),
        (HEADER[:-3], "truncated"),
        (HEADER + _proto([_u(Op.RETURN)]), "END"),
        (HEADER + _proto([_u(Op.END)]) + b"x", "after the main function"),
    ],
)
def test_refuses_what_is_not_a_chunk(data: bytes, message: str) -> None:
    """A wrong signature, version, truncation, missing END or trailing bytes is a ChunkError naming the problem."""
    with pytest.raises(ChunkError, match=message):
        lua4.load(data)


def test_line_table_decoding() -> None:
    """A negative entry skips lines, each pc entry advances by one: pcs 0-1 on line 3, 2 on line 4, 3 on line 7."""
    proto = Proto("", 0, 0, False, 0, line_info=[-2, 0, 2, -2, 3, 2**31 - 1], code=[0, 0, 0, 0])
    assert proto.lines() == [3, 3, 4, 7]


def test_walk_names_nested_functions() -> None:
    """Every prototype comes with its path, as the comparison reports it."""
    proto = lua4.load((FIXTURES / "closures.s.luac").read_bytes())
    paths = [path for path, _ in proto.walk()]
    assert paths[0] == "main" and "main/0/0" in paths


# --- opcodes and analysis ---------------------------------------------------------------------------------------


def test_decodes_each_argument_kind() -> None:
    """U, S and A/B arguments come out of the same word layout luac writes."""
    assert decode(_u(Op.GETGLOBAL, 7)).arg == 7
    assert decode(_s(Op.JMP, -3)).arg == -3
    call = decode(_ab(Op.CALL, 2, 255))
    assert (call.op, call.a, call.b) == (Op.CALL, 2, 255)
    with pytest.raises(ValueError, match="opcode"):
        decode(63)


def test_depth_follows_the_code_generator() -> None:
    """A call leaves its results where the function was; RETURN leaves the active locals; SETTABLE pops B."""
    assert depth_after(decode(_ab(Op.CALL, 3, 2)), 9) == 5
    assert depth_after(decode(_ab(Op.CALL, 3, 255)), 9) == 3
    assert depth_after(decode(_u(Op.RETURN, 2)), 6) == 2
    assert depth_after(decode(_ab(Op.SETTABLE, 3, 5)), 7) == 2


def test_value_regions_and_boundaries() -> None:
    """In the logic fixture every JMPONT/JMPONF and PUSHNILJMP belongs to a value region, and no region jump is a
    statement boundary's source."""
    proto = lua4.load((FIXTURES / "logic.s.luac").read_bytes())
    values = dict(proto.walk())["main/1"]
    info = FunctionInfo.build(values)
    keeping = [pc for pc, instr in enumerate(info.code) if instr.op in (Op.JMPONT, Op.JMPONF)]
    assert keeping and all(pc in info.regions for pc in keeping)
    assert any(region.nil_pc is not None for region in info.regions.values())


def test_equivalents_follow_jump_chains() -> None:
    """A pc holding an unconditional jump is equivalent to where the chain ends."""
    code = [_s(Op.JMP, 1), _u(Op.END), _s(Op.JMP, -3), _u(Op.END)]
    info = FunctionInfo.build(lua4.load(HEADER + _proto(code)))
    assert info.equivalents(0) == frozenset({0, 2})
    assert info.equivalents(1) == frozenset({1})


# --- conditions -------------------------------------------------------------------------------------------------


A, B, C = Global("a"), Global("b"), Global("c")


def test_builds_and_or_chains_left_deep() -> None:
    """`a and b or c`: a jumps on false to c's start (4), b on true to the then-part (6), c on false to the end (9)."""
    leaves = [Leaf(A, False, 4, 2), Leaf(B, True, 6, 4), Leaf(C, False, 9, 6)]
    expr = build(leaves, frozenset({6}), frozenset({9}))
    assert expr == BinOp("or", BinOp("and", A, B), C)


def test_negates_a_leaf_that_jumps_the_other_way() -> None:
    """`not a and b`: a's jump is taken when it is true, and goes to the false exit."""
    leaves = [Leaf(A, True, 9, 2), Leaf(B, False, 9, 4)]
    assert build(leaves, frozenset({4}), frozenset({9})) == BinOp("and", UnOp("not", A), B)


def test_value_leaves_keep_their_polarity() -> None:
    """`a or b` as a value: a keeps its value on true (JMPONT) and b is the value operand."""
    leaves = [Leaf(A, True, TRUE_VALUE, 2, keeps=True), Leaf(B, True, 3, 3, value=True)]
    assert build(leaves, frozenset({TRUE_VALUE}), frozenset({FALSE_VALUE})) == BinOp("or", A, B)
    assert build([Leaf(A, True, FALSE_VALUE, 2, keeps=True)], frozenset({2}), frozenset({FALSE_VALUE})) is None


def test_negate_inverts_comparisons_and_keeps_a_not() -> None:
    """`not (a < b)` is `a >= b`; `not (not a)` stays, since its NOT instruction is in the code."""
    assert negate(BinOp("<", A, B)) == BinOp(">=", A, B)
    assert negate(UnOp("not", A)) == UnOp("not", UnOp("not", A))


def test_no_tree_when_jumps_disagree() -> None:
    """A leaf that jumps somewhere neither exit is gives no tree."""
    assert build([Leaf(A, False, 77, 2)], frozenset({2}), frozenset({9})) is None


# --- rendering --------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("text", "written"),
    [("plain", '"plain"'), ('q"b\\', '"q\\"b\\\\"'), ("\n\t\a", '"\\n\\t\\a"'), ("\x00\x80", '"\\000\\128"')],
)
def test_quote(text: str, written: str) -> None:
    """Strings come back byte for byte; unprintable bytes as three-digit escapes."""
    assert quote(text) == written


@pytest.mark.parametrize(
    ("value", "written"),
    [(5.0, "5"), (0.1, "0.1"), (1e300, "1e+300"), (123456789012.0, "123456789012"), (-2.5, "-2.5")],
)
def test_number(value: float, written: str) -> None:
    """Numbers are written so strtod reads back the same double."""
    assert number(value) == written


def test_is_name() -> None:
    """Reserved words and non-identifiers need `[...]` as table keys."""
    assert is_name("value") and not is_name("end") and not is_name("two words") and not is_name("1x")


def _expr(expr: object) -> str:
    writer = Writer()
    assert isinstance(expr, lua4.ast.Expr)
    writer.expr(expr)
    return writer.current


@pytest.mark.parametrize(
    ("expr", "written"),
    [
        (BinOp("-", A, BinOp("-", B, C)), "a - (b - c)"),
        (BinOp("-", BinOp("-", A, B), C), "a - b - c"),
        (BinOp("^", A, BinOp("^", B, C)), "a ^ b ^ c"),
        (BinOp("^", BinOp("^", A, B), C), "(a ^ b) ^ c"),
        (BinOp("^", Number(-2), Number(2)), "(-2) ^ 2"),
        (UnOp("-", BinOp("^", Number(2), Number(2))), "-2 ^ 2"),
        (BinOp("^", Number(2), Number(-1)), "2 ^ -1"),
        (BinOp("or", A, BinOp("and", B, C)), "a or (b and c)"),
        (BinOp("and", BinOp("or", A, B), C), "a or b and c"),
        (UnOp("not", UnOp("not", A)), "not not a"),
        (UnOp("-", UnOp("-", A)), "- -a"),
        (BinOp("==", A, Nil()), "a == nil"),
        (lua4.ast.Index(Global("t"), String("end")), 't["end"]'),
        (lua4.ast.Index(Global("t"), String("k")), "t.k"),
        (lua4.ast.Index(Local("t"), Number(1)), "t[1]"),
    ],
)
def test_parentheses_follow_lua_4_priorities(expr: lua4.ast.Expr, written: str) -> None:
    """`and` and `or` share a priority and group left; `..` and `^` group right; unary binds below `^`."""
    assert _expr(expr) == written


def test_function_statements_use_the_sugar() -> None:
    """A function stored in a name or a name's field is written `function name()`; `self` first makes a method."""
    method = Function(["self", "x"], False, [])
    stmt = lua4.ast.Assign([lua4.ast.Index(Global("obj"), String("m"))], [method])
    writer = Writer()
    writer.statement(stmt)
    assert writer.text() == "function obj:m(x)\nend\n"


# --- names and errors -------------------------------------------------------------------------------------------


def test_made_up_names_avoid_the_chunks_strings() -> None:
    """A made-up name never takes a string the chunk uses (it could be a global the code reads)."""
    names = Names(frozenset({"l1", "i"}))
    assert names.fresh("l") == "l2"
    assert names.fresh("i", numbered=False) == "i2"


def test_unreadable_function_is_reported_not_raised() -> None:
    """Code no statement pattern fits gives a Function with `error`, and the output says so in a comment."""
    proto = lua4.load(HEADER + _proto([_s(Op.JMPF, 0), _u(Op.END)]))
    function = decompile_function(proto)
    assert function.error is not None
    assert "could not decompile" in lua4.render(function)
    assert "coney" not in lua4.render(function).lower()  # the package knows nothing of the program using it


def test_failures_lists_every_unreadable_function_at_any_depth() -> None:
    """`failures` walks the tree: a nested function the reader gave up on is found, with its path and reason."""
    proto = lua4.load((FIXTURES / "closures.s.luac").read_bytes())
    assert lua4.failures(decompile_function(proto)) == []
    proto.protos[0].code[:] = [_s(Op.JMPF, 0), _u(Op.END)]
    found = lua4.failures(decompile_function(proto))
    assert len(found) == 1 and found[0].startswith("pc ")


# --- comparison -------------------------------------------------------------------------------------------------


def test_compare_names_the_first_difference_without_constants() -> None:
    """Differences name the function, the kind and opcodes or indices, never a constant's text."""
    left = lua4.load(HEADER + _proto([_u(Op.GETGLOBAL, 0), _u(Op.SETGLOBAL, 0), _u(Op.END)], ("secret",)))
    right = lua4.load(HEADER + _proto([_u(Op.PUSHSTRING, 0), _u(Op.SETGLOBAL, 0), _u(Op.END)], ("other",)))
    differences = lua4.compare(left, right)
    text = " ".join(str(difference) for difference in differences)
    assert "pc 0: GETGLOBAL 0 vs PUSHSTRING 0" in text and "strings" in text
    assert "secret" not in text and "other" not in text
    assert lua4.compare(left, left) == []

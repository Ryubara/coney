# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the statement numbering and piece cutting of `orig study`, on invented decompiler-style text only
(no text from the game, ever)."""

import pytest

from coney_tools import orig_cut
from coney_tools.orig_cut import CutError, cut_decompile, format_lines, parse_lines

WIDGET = """
/* Widget_Update: an invented plate comment. */

int Widget_Update(long param_1,int param_2)

{
  int iVar1;
  long lVar2;
  float fVar3;

  iVar1 = *(int *)(param_1 + 0x1c);
  if (param_2 < iVar1) {
    return 0;
  }
  lVar2 = Widget_GetOwner(param_1);
  if (lVar2 == 0) {
    return 1;
  }
  fVar3 = 0.5;
  *(float *)(param_1 + 0x20) = fVar3;
  Widget_PlaySound(param_1,0x2a);
  DAT_00501234 = DAT_00501234 + 1;
  return 2;
}
"""


def kinds(text: str) -> list[str]:
    """The statement kinds in order."""
    return [s.kind for s in cut_decompile(text).statements]


def pieces(text: str) -> list[tuple[str, str]]:
    """(kind, lines) of each piece."""
    return [(p.kind, format_lines(p.lines)) for p in cut_decompile(text).pieces]


def body(*lines: str, header: str = "int F(long param_1,int param_2)", decls: str = "  int a;\n  int b;\n") -> str:
    """A function around the given body lines."""
    return f"\n{header}\n\n{{\n{decls}  \n" + "\n".join(f"  {line}" for line in lines) + "\n}\n"


def test_numbers_skip_header_declarations_and_braces() -> None:
    cut = cut_decompile(WIDGET)
    assert [s.number for s in cut.statements] == list(range(1, 12))
    assert cut.statements[0].text == "iVar1 = *(int *)(param_1 + 0x1c);"
    assert cut.statements[1].text == "if (param_2 < iVar1)"
    assert cut.statements[-1].text == "return 2;"


def test_kinds_of_the_widget() -> None:
    assert kinds(WIDGET) == [
        "compute",
        "branch",
        "return",
        "call",
        "branch",
        "return",
        "constant",
        "field",
        "call",
        "field",
        "return",
    ]


def test_widget_pieces_attach_feeders_and_guard_returns() -> None:
    assert pieces(WIDGET) == [
        ("branch", "1-3"),
        ("branch", "4-6"),
        ("constant", "7"),
        ("field", "8"),
        ("call", "9"),
        ("field", "10"),
        ("return", "11"),
    ]


def test_piece_ids_run_from_one_in_order_of_first_line() -> None:
    cut = cut_decompile(WIDGET)
    assert [p.id for p in cut.pieces] == list(range(1, 8))
    assert [p.lines[0] for p in cut.pieces] == sorted(p.lines[0] for p in cut.pieces)


def test_every_statement_is_in_exactly_one_piece() -> None:
    cut = cut_decompile(WIDGET)
    seen = [n for p in cut.pieces for n in p.lines]
    assert sorted(seen) == [s.number for s in cut.statements]


def test_a_multi_line_condition_is_one_statement() -> None:
    text = body("if ((param_1 == 0) ||", "   (param_2 < 3)) {", "  return 1;", "}", "return 0;")
    cut = cut_decompile(text)
    assert [s.kind for s in cut.statements] == ["branch", "return", "return"]
    assert cut.statements[0].text == "if ((param_1 == 0) || (param_2 < 3))"
    assert cut.statements[0].first_line < cut.statements[0].last_line


def test_one_line_if_goto_is_one_branch_statement() -> None:
    text = body("if (param_1 == 0) goto LAB_00100010;", "a = 1;", "LAB_00100010:", "return a;")
    assert kinds(text) == ["branch", "constant", "label", "return"]


def test_else_if_is_a_branch_and_bare_else_is_not_a_statement() -> None:
    text = body(
        "if (param_1 == 0) {", "a = 1;", "}", "else if (param_1 == 1) {", "a = 2;", "}", "else {", "a = 3;", "}"
    )
    assert kinds(text) == ["branch", "constant", "branch", "constant", "constant"]


def test_switch_case_labels_attach_to_the_code_they_select() -> None:
    text = body(
        "switch(param_2) {",
        "case 0:",
        "case 1:",
        "a = 5;",
        "break;",
        "default:",
        "a = 7;",
        "}",
        "return a;",
    )
    cut = cut_decompile(text)
    assert [s.kind for s in cut.statements] == [
        "branch",
        "label",
        "label",
        "constant",
        "jump",
        "label",
        "constant",
        "return",
    ]
    assert [(p.kind, format_lines(p.lines)) for p in cut.pieces] == [
        ("branch", "1"),
        ("constant", "2-5"),
        ("constant", "6-7"),
        ("return", "8"),
    ]


def test_case_that_only_jumps_becomes_a_branch_piece() -> None:
    text = body(
        "switch(param_2) {", "case 3:", "goto LAB_00100020;", "case 4:", "a = 1;", "}", "LAB_00100020:", "return a;"
    )
    result = pieces(text)
    assert ("branch", "2-3") in result


def test_result_feeder_needs_exactly_one_consumer() -> None:
    once = body("a = Foo(param_1);", "if (a == 0) {", "return 1;", "}", "return 0;")
    assert pieces(once) == [("branch", "1-3"), ("return", "4")]
    twice = body("a = Foo(param_1);", "if (a == 0) {", "return 1;", "}", "Bar(a);", "return 0;")
    assert pieces(twice) == [("call", "1"), ("branch", "2-3"), ("call", "4"), ("return", "5")]


def test_a_redefinition_ends_the_search_for_consumers() -> None:
    text = body("a = Foo(param_1);", "a = Foo(param_2);", "if (a == 0) {", "return 1;", "}", "return 0;")
    assert pieces(text) == [("call", "1"), ("branch", "2-4"), ("return", "5")]


def test_compute_attaches_to_its_first_consumer() -> None:
    text = body("a = param_1 * 2;", "b = Foo(param_2);", "Bar(a);")
    assert pieces(text) == [("call", "1,3"), ("call", "2")]


def test_a_value_computed_and_never_read_attaches_to_the_next_piece() -> None:
    text = body("a = param_1 + 1;", "Bar(param_2);")
    assert pieces(text) == [("call", "1-2")]


def test_a_trailing_unused_compute_attaches_to_the_previous_piece() -> None:
    text = body("Bar(param_2);", "a = param_1 + 1;")
    assert pieces(text) == [("call", "1-2")]


def test_loops_and_do_while() -> None:
    text = body("do {", "a = a + 1;", "} while (a < 4);", "while (b != 0) {", "b = Foo(b);", "}")
    cut = cut_decompile(text)
    assert [s.kind for s in cut.statements] == ["compute", "loop", "loop", "call"]
    assert cut.statements[1].text == "while (a < 4);"


def test_field_writes_include_globals_pointers_and_members() -> None:
    text = body("DAT_00500000 = 4;", "param_1->x = 2;", "*(int *)(param_1 + 8) = a;", "buf[2] = 1;")
    assert kinds(text) == ["field", "field", "field", "field"]


def test_local_assignment_of_a_parameter_is_local() -> None:
    assert kinds(body("param_2 = 3;", "return param_2;")) == ["constant", "return"]


def test_constants_in_all_their_literal_forms() -> None:
    text = body("a = 0x18;", "b = -1;", "a = 2.0;", "b = false;", "a = (int)0x40;", "b = a + 1;", "return b;")
    assert kinds(text) == ["constant"] * 5 + ["compute", "return"]


def test_calls_by_name_and_by_function_pointer() -> None:
    text = body("Foo(param_1);", "a = (*(code *)param_1)(param_2);", "return;")
    assert kinds(text) == ["call", "call", "return"]


def test_a_cast_is_not_a_call() -> None:
    assert kinds(body("a = (int)param_1;", "return a;")) == ["compute", "return"]


def test_compound_assignment_and_increment_are_compute() -> None:
    assert kinds(body("a += 2;", "b++;", "return a;")) == ["compute", "compute", "return"]


def test_comments_and_strings_do_not_confuse_the_scan() -> None:
    text = body("a = 1; /* if (x) { */", "b = 2; // }", 'Foo("a;b{");', "return b;")
    assert kinds(text) == ["constant", "constant", "call", "return"]


def test_goto_attaches_to_the_previous_statement() -> None:
    text = body("Foo(param_1);", "goto LAB_00100030;", "Bar(param_2);", "LAB_00100030:", "return;")
    assert pieces(text) == [("call", "1-2"), ("call", "3"), ("return", "4-5")]


def test_declaration_block_is_not_numbered_but_assignments_with_equals_are() -> None:
    text = body("a = 1;", decls="  int a;\n  char acStack_20 [32];\n  undefined8 uVar5;\n")
    assert len(cut_decompile(text).statements) == 1


def test_crlf_and_blank_line_differences_do_not_change_the_cut() -> None:
    crlf = WIDGET.replace("\n", "\r\n")
    spaced = WIDGET.replace("\n  ", "\n\n      ")
    base = cut_decompile(WIDGET)
    for other in (cut_decompile(crlf), cut_decompile(spaced)):
        assert orig_cut.cut_sha256(other) == orig_cut.cut_sha256(base)
        assert orig_cut.shape_sha256(other) == orig_cut.shape_sha256(base)


def test_a_rename_changes_the_text_but_not_the_shape() -> None:
    renamed = WIDGET.replace("Widget_GetOwner", "Widget_Owner").replace("DAT_00501234", "g_counter")
    assert orig_cut.shape_sha256(cut_decompile(renamed)) == orig_cut.shape_sha256(cut_decompile(WIDGET))
    changed = WIDGET.replace("return 1;", "Foo(); return 1;")
    assert orig_cut.shape_sha256(cut_decompile(changed)) != orig_cut.shape_sha256(cut_decompile(WIDGET))


def test_unbalanced_or_empty_text_is_refused() -> None:
    with pytest.raises(CutError, match="body"):
        cut_decompile("int F(void)")
    with pytest.raises(CutError, match="brace"):
        cut_decompile("int F(void)\n{\n  a = 1;\n")
    with pytest.raises(CutError, match="no statements"):
        cut_decompile("int F(void)\n{\n  int a;\n}\n")


def test_format_and_parse_lines_round_trip() -> None:
    assert format_lines((1, 2, 3, 7, 9, 10)) == "1-3,7,9-10"
    assert parse_lines("1-3,7,9-10") == (1, 2, 3, 7, 9, 10)
    assert parse_lines(" 4 ") == (4,)
    for bad in ("", "3-1", "a", "1,,2", "0", "1-"):
        with pytest.raises(CutError):
            parse_lines(bad)


def test_numbered_listing_marks_statements_and_pieces() -> None:
    listing = orig_cut.render_numbered(WIDGET, cut_decompile(WIDGET))
    lines = listing.splitlines()
    assert any(
        line.startswith("  1 ") and "iVar1 = *(int *)(param_1 + 0x1c);" in line and line.rstrip().endswith("P1")
        for line in lines
    )
    assert any("return 2;" in line and line.rstrip().endswith("P7") for line in lines)
    assert any(line.startswith("    ") and line.strip() == "}" for line in lines)


@pytest.mark.parametrize(
    "statement",
    [
        "x++;",
        "goto LAB_00100010;",
        "LAB_00100010:",
        "iVar1 = iVar1 * 2;",
        "x = 0;",
        "return;",
        "Foo();",
        "break;",
        "case 3:",
    ],
)
def test_a_body_of_one_statement_cuts_into_one_piece(statement: str) -> None:
    text = body(statement, decls="  int iVar1;\n  int x;\n")
    cut = cut_decompile(text)
    assert [format_lines(p.lines) for p in cut.pieces] == ["1"]
    assert cut.pieces[0].kind in orig_cut.ANCHORS


def test_two_statements_of_the_kinds_that_attach_cut_without_a_crash() -> None:
    for first in ("x++;", "goto LAB_1;", "LAB_1:", "break;", "case 3:", "iVar1 = iVar1 * 2;"):
        for second in ("x++;", "goto LAB_2;", "LAB_2:", "break;", "default:", "iVar1 = iVar1 + 1;"):
            cut = cut_decompile(body(first, second, decls="  int iVar1;\n  int x;\n"))
            assert sorted(n for p in cut.pieces for n in p.lines) == [1, 2]


def test_random_bodies_never_crash_the_cutter() -> None:
    import random

    templates = [
        "iVar1 = *(int *)(param_1 + 0x1c);", "x = 0;", "lVar2 = Foo(param_1);", "Bar(param_1,2);",
        "*(int *)(param_1+4) = iVar1;", "DAT_00501234 = DAT_00501234 + 1;", "iVar1 = iVar1 * 2;", "return iVar1;",
        "return 0;", "goto LAB_001;", "LAB_001:", "break;", "case 3:", "default:", "if (iVar1 < 3) {",
        "if (lVar2 == 0) {", "} else {", "}", "while (x < 3) {", "switch(iVar1) {", "do {", "} while (x < 3);",
        "(*(code *)p)(param_1);", "iVar1 = (*(code *)p)(2);", "if (a &&\n   b) goto LAB_002;", "x = a ? b : c;",
        "x += 1;", "x++;",
    ]  # fmt: skip
    for seed in range(400):
        rnd = random.Random(seed)
        text = body(
            *(rnd.choice(templates) for _ in range(rnd.randint(1, 25))), decls="  int iVar1;\n  long lVar2;\n  int x;\n"
        )
        try:
            cut = cut_decompile(text)
        except CutError:
            continue
        assert sorted(n for p in cut.pieces for n in p.lines) == [s.number for s in cut.statements], seed
        assert all(p.kind in orig_cut.ANCHORS for p in cut.pieces), seed
        assert orig_cut.cut_sha256(cut_decompile(text.replace("\n", "\r\n"))) == orig_cut.cut_sha256(cut), seed


def test_the_anchor_kinds_are_the_pair_kinds_of_the_conventions() -> None:
    from stamp_support import rules

    assert set(orig_cut.ANCHORS) == set(rules().pairs.kinds)


def test_statements_on_one_line_keep_every_number_in_the_reading_copy() -> None:
    text = body("Foo(param_1); return 2;", "Bar(param_2);")
    cut = cut_decompile(text)
    assert [s.first_line for s in cut.statements][:2] == [cut.statements[0].first_line] * 2
    listing = orig_cut.render_numbered(text, cut)
    row = next(line for line in listing.splitlines() if "Foo(param_1);" in line)
    assert row.lstrip().startswith("1,2") and "P1" in row and "P2" in row
    other = next(line for line in listing.splitlines() if "Bar(param_2);" in line)
    assert other.lstrip().startswith("3 ")


def test_the_skeleton_ignores_names_and_comments_but_not_operators() -> None:
    base = orig_cut.skeleton_sha256(WIDGET)
    assert orig_cut.skeleton_sha256(WIDGET.replace("Widget_GetOwner", "Owner_Of").replace("iVar1", "uVar9")) == base
    assert orig_cut.skeleton_sha256(WIDGET.replace("Widget_Update:", "Widget_Update: changed")) == base
    assert orig_cut.skeleton_sha256(WIDGET.replace("param_2 < iVar1", "param_2 <= iVar1")) != base
    assert orig_cut.skeleton_sha256(WIDGET.replace("0x2a", "0x2b")) != base


def test_the_skeleton_tells_a_rename_from_a_different_name() -> None:
    # Ghidra renames one-to-one, so the skeleton numbers names by first appearance: a consistent rename keeps it,
    # swapped operands or a different variable, global or callee do not.
    base = orig_cut.skeleton_sha256(WIDGET)
    assert orig_cut.skeleton_sha256(WIDGET.replace("iVar1", "health")) == base
    assert orig_cut.skeleton_sha256(WIDGET.replace("param_2 < iVar1", "iVar1 < param_2")) != base
    assert orig_cut.skeleton_sha256(WIDGET.replace("param_2 < iVar1", "param_1 < iVar1")) != base
    assert (
        orig_cut.skeleton_sha256(WIDGET.replace("DAT_00501234 = DAT_00501234", "DAT_00501234 = DAT_00509999")) != base
    )
    # two names merged into one is not a rename either
    assert orig_cut.skeleton_sha256(WIDGET.replace("Widget_GetOwner", "Widget_PlaySound")) != base

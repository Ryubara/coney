# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the scan that finds the comments of C++ lines (comment_spans)."""

from __future__ import annotations

from coney_tools.comment_spans import comment_spans, is_comment


def text_of(line: str) -> str:
    """The comment text of a single line, spans joined."""
    return "|".join(line[a:b] for a, b in comment_spans([line])[0])


def test_a_line_comment_runs_to_the_end_of_the_line() -> None:
    assert text_of("int a;  // docs/x.md") == "// docs/x.md"
    assert text_of("int a;") == ""


def test_a_comment_marker_inside_a_string_or_character_literal_is_no_comment() -> None:
    assert text_of('f("http://x");') == ""
    assert text_of('auto s = "/* not */";') == ""
    assert text_of('auto s = "a\\"b // still the string"; // comment') == "// comment"
    assert text_of("char c = '\"'; // comment") == "// comment"
    assert text_of("int n = 1'000; // comment") == "// comment"


def test_a_prefixed_character_literal_holding_a_quote_does_not_open_a_string() -> None:
    for prefix in ("L", "u", "U", "u8"):
        assert text_of(f"auto c = {prefix}'\"'; // comment") == "// comment", prefix
    # a digit separator after a hexadecimal letter is still a separator
    assert text_of("int n = 0xAB'CD; // comment") == "// comment"


def test_a_string_ended_by_a_backslash_continues_on_the_next_line() -> None:
    lines = ['auto s = "one \\', '// two"; // after']
    found = [[line[a:b] for a, b in spans] for line, spans in zip(lines, comment_spans(lines), strict=True)]
    assert found == [[], ["// after"]]


def test_a_raw_string_is_skipped_over_lines() -> None:
    lines = ['auto s = R"x(', "// inside", ')x"; // after']
    assert [[line[a:b] for a, b in spans] for line, spans in zip(lines, comment_spans(lines), strict=True)] == [
        [],
        [],
        ["// after"],
    ]


def test_a_block_comment_is_followed_over_lines_and_ends_at_its_closing() -> None:
    lines = ["int a; /* start", " * middle", " end */ int b; // tail", " * p = 1;"]
    found = [[line[a:b] for a, b in spans] for line, spans in zip(lines, comment_spans(lines), strict=True)]
    assert found == [["/* start"], [" * middle"], [" end */", "// tail"], []]


def test_a_line_is_comment_when_nothing_but_blanks_is_outside_its_spans() -> None:
    for line in ("// x", "    /// x", "/* x */", "  /* x */  "):
        assert is_comment(line, comment_spans([line])[0]), line
    assert is_comment(" * x", [(0, 4)])
    for line in ("int a; // x", "/* x */ int a;", 'f("//");'):
        assert not is_comment(line, comment_spans([line])[0]), line

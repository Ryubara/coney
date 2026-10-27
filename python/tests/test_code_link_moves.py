# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the comment research link that follows a page move and is no touch of a source or test file
(docs/guides/coney-tools-standards.md#standards)."""

from __future__ import annotations

from coney_tools.code_link_moves import link_move_hunk, touched_code_lines

#: The made-up change moved `ai.md` to `ai/index.md` and split `ai-fights.md` off it; `old.md` was deleted.
GONE = {"docs/research/ai.md", "docs/research/old.md"}
PAGES = {
    "docs/research/ai/index.md": {"brain", "update"},
    "docs/research/ai-fights.md": {"fight", "pick-attack"},
    "docs/research/tasks.md": {"held-flags"},
    "docs/research/stays.md": {"stays"},
    # Two pages that stayed: `moves.md` gave `targeting` to `targets.md`, which also holds `shared` as it did.
    "docs/research/moves.md": {"moves", "shared"},
    "docs/research/targets.md": {"targeting", "shared"},
    # A page that stayed and split its `rage` section: the flash moved out under `flash`, `rage` is still there.
    "docs/research/rage.md": {"rage", "flash", "power"},
}
#: What the pages held before the change: `ai.md` held every section it gave away.
BEFORE = {
    "docs/research/ai.md": {"brain", "update", "fight", "pick-attack"},
    "docs/research/tasks.md": {"held-flags"},
    "docs/research/moves.md": {"moves", "targeting", "shared"},
    "docs/research/targets.md": {"shared"},
    "docs/research/rage.md": {"rage", "power"},
}


def resolves(path: str, anchor: str) -> bool:
    """Whether `path` exists after the change and holds `anchor` (any, when empty)."""
    return path in PAGES and (not anchor or anchor in PAGES[path])


def held(path: str, anchor: str) -> bool:
    """Whether `path` held `anchor` before the change."""
    return anchor in BEFORE.get(path, set())


def moved(removed: list[str], added: list[str]) -> bool:
    """Whether the hunk is a research link move."""
    return link_move_hunk(removed, added, GONE, resolves, held)


# --- a pure link move is no touch ------------------------------------------------------------------------------------


def test_a_reference_to_a_moved_page_rewritten_to_its_new_path_is_no_touch() -> None:
    assert moved(["// Research: docs/research/ai.md#brain"], ["// Research: docs/research/ai/index.md#brain"])
    assert moved(["// Research: docs/research/ai.md"], ["// Research: docs/research/ai/index.md"])
    assert moved(["    /// Thinks (docs/research/old.md)."], ["    /// Thinks (docs/research/ai/index.md)."])


def test_a_reference_to_a_section_that_moved_to_another_page_is_no_touch() -> None:
    assert moved(["// Research: docs/research/ai.md#fight"], ["// Research: docs/research/ai-fights.md#fight"])


def test_every_changed_reference_of_a_line_must_follow_a_move_and_the_others_may_stay() -> None:
    line = "// Research: docs/research/ai.md#brain, docs/research/tasks.md#held-flags, docs/research/ai.md#fight"
    assert moved(
        [line],
        [
            "// Research: docs/research/ai/index.md#brain, docs/research/tasks.md#held-flags, "
            "docs/research/ai-fights.md#fight"
        ],
    )


def test_a_reference_in_a_trailing_comment_and_a_one_line_block_comment_is_read() -> None:
    assert moved(["int a = 1;  // docs/research/ai.md#brain"], ["int a = 1;  // docs/research/ai/index.md#brain"])
    assert moved(["/* docs/research/ai.md#brain */ g();"], ["/* docs/research/ai/index.md#brain */ g();"])
    assert moved(["int a = 1; /* docs/research/ai.md */"], ["int a = 1; /* docs/research/ai/index.md */"])
    # a quote in a character literal does not open a string, nor does the digit separator a character
    assert moved(["char q = '\"'; // docs/research/ai.md#brain"], ["char q = '\"'; // docs/research/ai/index.md#brain"])
    assert moved(["int n = 1'000; // docs/research/ai.md#brain"], ["int n = 1'000; // docs/research/ai/index.md#brain"])


def test_a_line_of_a_block_comment_is_comment_only_with_the_lines_above_it() -> None:
    old = ["/**", " * See docs/research/ai.md#brain.", " */", "int a;"]
    new = ["/**", " * See docs/research/ai/index.md#brain.", " */", "int a;"]
    diff = "@@ -2 +2 @@\n- * See docs/research/ai.md#brain.\n+ * See docs/research/ai/index.md#brain.\n"
    assert touched_code_lines(diff, GONE, resolves, held, old, new).link_moves_only
    # without the lines above, a line that begins with `*` is code (a dereference)
    assert not touched_code_lines(diff, GONE, resolves, held).link_moves_only
    # the same line below the end of the comment is code
    shut = ["/** x */", " * See docs/research/ai.md#brain.", "int a;"]
    assert not touched_code_lines(diff, GONE, resolves, held, shut, shut).link_moves_only


def test_a_test_citation_without_its_folder_is_read_on_an_expected_line() -> None:
    assert moved(
        ["    // Expected: ai.md#brain, confirmed (code)"], ["    // Expected: ai/index.md#brain, confirmed (code)"]
    )
    assert moved(
        ["    // Expected: research/ai.md#brain, confirmed (code)"],
        ["    // Expected: ai/index.md#brain, confirmed (code)"],
    )


def test_a_comment_reflowed_by_a_longer_reference_is_no_touch() -> None:
    assert moved(
        [
            "    /// for the level scripts' calls (docs/research/ai.md#brain), which a scene",
            "    /// without its script needs. On by default.",
        ],
        [
            "    /// for the level scripts' calls (docs/research/ai/index.md#brain), which a",
            "    /// scene without its script needs. On by default.",
        ],
    )
    # the reflow can split a line in two
    assert moved(
        ["// The command (docs/research/ai.md#brain); kinds 24 and 35 are -1."],
        ["// The command (docs/research/ai/index.md#brain); kinds 24 and 35 are", "// -1."],
    )


def test_a_section_that_moved_between_pages_that_stayed_is_no_touch_when_the_old_page_held_it() -> None:
    # `moves.md` stayed and gave `targeting` to `targets.md`, which stayed too: the old page held the anchor before the
    # change and no longer does, and the new page holds it now but did not before
    assert moved(["// docs/research/moves.md#targeting"], ["// docs/research/targets.md#targeting"])
    # the old page still holds it, or the new page held it already, or the lookup of the old pages is not given
    assert not moved(["// docs/research/moves.md#moves"], ["// docs/research/targets.md#moves"])
    assert not moved(["// docs/research/moves.md#shared"], ["// docs/research/targets.md#shared"])
    assert not link_move_hunk(
        ["// docs/research/moves.md#targeting"], ["// docs/research/targets.md#targeting"], GONE, resolves
    )


def test_a_section_split_off_its_page_keeps_its_old_anchor_and_gains_a_new_one() -> None:
    assert moved(["// docs/research/rage.md#rage"], ["// docs/research/rage.md#flash"])
    assert not moved(["// docs/research/rage.md#rage"], ["// docs/research/rage.md#power"])


# --- anything else is a touch ----------------------------------------------------------------------------------------


def test_a_link_move_with_another_edit_on_the_line_is_a_touch() -> None:
    assert not moved(["// Research: docs/research/ai.md#brain"], ["// Research: docs/research/ai/index.md#brain, more"])
    assert not moved(["int a = 1;  // docs/research/ai.md#brain"], ["int a = 2;  // docs/research/ai/index.md#brain"])
    assert not moved(["    // docs/research/ai.md#brain"], ["  // docs/research/ai/index.md#brain"])


def test_a_link_move_with_another_edit_in_a_reflowed_comment_is_a_touch() -> None:
    assert not moved(
        ["// Thinks (docs/research/ai.md#brain), then waits.", "// Done."],
        ["// Thinks (docs/research/ai/index.md#brain), then", "// stops. Done."],
    )
    # a blank comment line is a paragraph break, and a change of the comment's kind is an edit
    assert not moved(
        ["// A docs/research/ai.md#brain words", "//", "// b"],
        ["// A docs/research/ai/index.md#brain", "// words", "//", "// b"],
    )
    assert not moved(["// A docs/research/ai.md#brain", "// b"], ["/// A docs/research/ai/index.md#brain", "/// b"])


def test_a_link_move_in_code_and_not_in_a_comment_is_a_touch() -> None:
    assert not moved(['auto page = "docs/research/ai.md#brain";'], ['auto page = "docs/research/ai/index.md#brain";'])


def test_a_link_move_in_a_string_that_looks_like_a_comment_is_a_touch() -> None:
    # a `//` or `/*` inside a string, the code beside a block comment, and a line that starts with `*` are code
    cases = [
        'f("http://x", "docs/research/ai.md#brain");',
        'auto s = "// docs/research/ai.md#brain";',
        'auto s = "/* docs/research/ai.md#brain */";',
        '/* c */ g("docs/research/ai.md#brain");',
        '* p = "docs/research/ai.md#brain";',
        'char c = \'"\'; auto s = "docs/research/ai.md#brain";',
        'auto c = L\'"\'; auto s = "// docs/research/ai.md#brain";',
        'auto c = u8\'"\'; auto s = "// docs/research/ai.md#brain";',
        'auto c = u\'"\'; auto s = "// docs/research/ai.md#brain";',
        'auto c = U\'"\'; auto s = "// docs/research/ai.md#brain";',
        'auto s = R"(// docs/research/ai.md#brain)";',
        'auto s = R"x(" // docs/research/ai.md#brain)x";',
    ]
    for case in cases:
        assert not moved([case], [case.replace("ai.md", "ai/index.md")]), case


def test_a_reflow_of_code_beside_a_comment_is_a_touch() -> None:
    assert not moved(
        ["/* c */ g(); // docs/research/ai.md#brain, words", "// more"],
        ["/* c */ g(); // docs/research/ai/index.md#brain,", "// words more"],
    )
    assert not moved(
        ["* p = 1; // docs/research/ai.md#brain, words", "* q"],
        ["* p = 1; // docs/research/ai/index.md#brain,", "* words q"],
    )
    assert not moved(
        ["// docs/research/ai.md#brain", '/* x */ f("a");'],
        ["// docs/research/ai/index.md#brain", '/* x */ f("a");', "//"],
    )


def test_a_rewrite_with_no_matching_move_is_a_touch() -> None:
    # the page stayed
    assert not moved(["// Research: docs/research/stays.md#stays"], ["// Research: docs/research/ai/index.md#brain"])
    assert not moved(["// Research: docs/research/tasks.md#held-flags"], ["// Research: docs/research/stays.md#stays"])
    # the new target resolves nowhere
    assert not moved(["// Research: docs/research/ai.md#brain"], ["// Research: docs/research/ai/index.md#nowhere"])
    assert not moved(["// Research: docs/research/ai.md#brain"], ["// Research: docs/research/missing.md#brain"])
    # the reference only changed its form
    assert not moved(["// Research: research/tasks.md#held-flags"], ["// Research: docs/research/tasks.md#held-flags"])
    # a bare page outside an Expected line is no reference
    assert not moved(["// See ai.md#brain"], ["// See ai/index.md#brain"])


def test_a_hunk_with_nothing_to_compare_is_a_touch() -> None:
    assert not moved([], ["// Research: docs/research/ai/index.md#brain"])
    assert not moved(["// Research: docs/research/ai.md#brain"], [])
    assert not moved(["// same docs/research/tasks.md"], ["// same docs/research/tasks.md"])


# --- the diff of a file ----------------------------------------------------------------------------------------------

DIFF = """--- a/src/ai/brain.cpp
+++ b/src/ai/brain.cpp
@@ -3 +3 @@
-// Research: docs/research/ai.md#brain
+// Research: docs/research/ai/index.md#brain
@@ -10,0 +11 @@
+int added = 1;
@@ -20 +21 @@
-int a = 1;  // docs/research/ai.md#brain
+int a = 2;  // docs/research/ai/index.md#brain
@@ -30,2 +31,0 @@
-// docs/research/ai.md#brain
-// docs/research/ai.md#fight
"""


def test_the_diff_of_a_file_touches_every_line_but_those_of_a_link_move() -> None:
    result = touched_code_lines(DIFF, GONE, resolves, held)
    assert result.hunks == 4
    assert result.lines == {11, 21, 31}
    assert not result.link_moves_only


def test_a_diff_of_link_moves_alone_touches_nothing() -> None:
    result = touched_code_lines(DIFF.split("@@ -10,0")[0], GONE, resolves, held)
    assert result.hunks == 1 and result.lines == frozenset() and result.link_moves_only


def test_a_diff_with_no_hunk_is_not_a_link_move() -> None:
    assert not touched_code_lines("", GONE, resolves, held).link_moves_only


def test_a_link_move_in_a_string_continued_over_a_backslash_is_a_touch() -> None:
    old = ['auto s = "one \\', '// docs/research/ai.md#brain";']
    new = ['auto s = "one \\', '// docs/research/ai/index.md#brain";']
    diff = '@@ -2 +2 @@\n-// docs/research/ai.md#brain";\n+// docs/research/ai/index.md#brain";\n'
    assert not touched_code_lines(diff, GONE, resolves, held, old, new).link_moves_only
    # the same line after a string that ended is a comment
    shut = ['auto s = "one";', "// docs/research/ai.md#brain"]
    shut_new = ['auto s = "one";', "// docs/research/ai/index.md#brain"]
    diff = "@@ -2 +2 @@\n-// docs/research/ai.md#brain\n+// docs/research/ai/index.md#brain\n"
    assert touched_code_lines(diff, GONE, resolves, held, shut, shut_new).link_moves_only

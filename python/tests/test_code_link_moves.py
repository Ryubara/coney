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
}
#: What the pages held before the change: `ai.md` held every section it gave away.
BEFORE = {
    "docs/research/ai.md": {"brain", "update", "fight", "pick-attack"},
    "docs/research/tasks.md": {"held-flags"},
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


def test_a_reference_in_a_trailing_comment_and_a_block_comment_is_read() -> None:
    assert moved(["int a = 1;  // docs/research/ai.md#brain"], ["int a = 1;  // docs/research/ai/index.md#brain"])
    assert moved([" * See docs/research/ai.md#brain."], [" * See docs/research/ai/index.md#brain."])


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

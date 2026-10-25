# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the link rewrite that is no touch: a line whose only change is the target of a link to a page the same
change renamed or deleted (docs/guides/writing-docs.md#legacy-pages)."""

from __future__ import annotations

from pathlib import Path

from git_support import git
from research_support import LEGACY_FILE, checkout, context, page, write

from coney_tools import docs_check, docs_legacy
from coney_tools.docs_link_moves import link_only_change, touched_lines

FOLDER = "docs/research"
#: The pages the made-up change renamed or deleted, and the pages and anchors that exist after it.
GONE = {"docs/research/combat.md", "docs/research/old-name.md"}
PAGES = {"docs/research/combat-grabs.md": {"grab", "release"}, "docs/research/combat/index.md": {"summary"}}


def resolves(path: str, anchor: str) -> bool:
    """Whether `path` exists after the change and holds `anchor` (any, when empty)."""
    return path in PAGES and (not anchor or anchor in PAGES[path])


def same(old: str, new: str) -> bool:
    """Whether the change of `old` to `new` is only a link rewrite."""
    return link_only_change(old, new, FOLDER, GONE, resolves)


# --- one line ---------------------------------------------------------------------------------------------------------


def test_a_link_to_a_renamed_page_rewritten_to_its_new_path_is_no_touch() -> None:
    assert same("See [the grabs](combat.md#grab).", "See [the grabs](combat/index.md#summary).")
    assert same("See [it](old-name.md).", "See [it](combat/index.md).")


def test_a_link_rewritten_to_another_page_and_anchor_is_no_touch() -> None:
    # The stub's own target, not the renamed page's new path.
    assert same("See [grabs](combat.md#grab).", "See [grabs](combat-grabs.md#grab).")
    assert same("See [grabs](combat.md#anything).", "See [grabs](combat-grabs.md#release).")


def test_each_changed_link_of_a_line_is_judged_and_unchanged_ones_pass() -> None:
    old = "[a](combat.md#grab), [b](https://example.org/x) and [c](combat.md#release)"
    new = "[a](combat-grabs.md#grab), [b](https://example.org/x) and [c](combat-grabs.md#release)"
    assert same(old, new)


def test_a_link_to_a_page_that_was_not_renamed_is_a_touch() -> None:
    assert not same("See [it](camera.md#a).", "See [it](combat-grabs.md#grab).")


def test_a_target_that_does_not_resolve_is_a_touch() -> None:
    assert not same("See [it](combat.md#grab).", "See [it](combat-grabs.md#nowhere).")
    assert not same("See [it](combat.md#grab).", "See [it](missing.md#grab).")


def test_a_link_rewrite_with_any_other_edit_on_the_line_is_a_touch() -> None:
    assert not same("See [the grabs](combat.md#grab).", "See [the grab](combat-grabs.md#grab).")
    assert not same("See [the grabs](combat.md#grab).", "See also [the grabs](combat-grabs.md#grab).")
    assert not same("See [the grabs](combat.md#grab).", "See [the grabs](combat-grabs.md#grab) too.")
    assert not same("See [a](combat.md#grab).", "See [a](combat-grabs.md#grab) and [b](combat-grabs.md#release).")


def test_a_line_that_did_not_change_its_links_is_no_link_rewrite() -> None:
    assert not same("Plain text.", "Plain text, edited.")
    assert not same("See [it](combat-grabs.md#grab).", "See [it](combat-grabs.md#release).")


def test_an_image_or_an_address_outside_the_pages_is_no_link_rewrite() -> None:
    assert not same("![a](combat.md)", "![a](combat-grabs.md)")
    assert not same("See [it](combat.md).", "See [it](repo:src/a.cpp).")
    assert not same("See [it](combat.md).", "See [it](https://example.org/).")


def test_targets_are_read_from_the_pages_own_folder() -> None:
    assert link_only_change(
        "[a](../research/combat.md#grab)", "[a](../research/combat-grabs.md#grab)", "docs/guides", GONE, resolves
    )
    assert not link_only_change("[a](combat.md#grab)", "[a](combat-grabs.md#grab)", "docs/guides", GONE, resolves)


# --- a diff -----------------------------------------------------------------------------------------------------------

DIFF = """diff --git a/docs/research/ai.md b/docs/research/ai.md
index 1..2 100644
--- a/docs/research/ai.md
+++ b/docs/research/ai.md
@@ -3 +3 @@ Heading
-See [grabs](combat.md#grab).
+See [grabs](combat-grabs.md#grab).
@@ -10,2 +10,2 @@ Heading
-A [grab](combat.md#grab) line.
-B [grab](combat.md#grab) line.
+A [grab](combat-grabs.md#grab) line.
+B grab line, [edited](combat-grabs.md#grab).
@@ -20 +20,2 @@
-Old [x](combat.md).
+New [x](combat-grabs.md).
+Added.
@@ -30,2 +31,0 @@
-Removed one.
-Removed two.
@@ -40 +39 @@
-Plain.
+Plain, edited.
\\ No newline at end of file
"""


def test_the_lines_a_diff_touches_leave_out_the_link_rewrites() -> None:
    # Line 3 and line 10 are link rewrites; 11 is edited; 20 and 21 come with an added line; a deletion marks the line
    # it was at (31); 39 is edited.
    assert touched_lines(DIFF, FOLDER, GONE, resolves) == frozenset({11, 20, 21, 31, 39})


# --- a whole change ---------------------------------------------------------------------------------------------------

LINKS = "# Links\n\nSee [grabs](combat.md#grab) for the grab.\n\nSee [more](combat.md#grab) too.\n"


def linked_checkout(tmp_path: Path, links: str = LINKS) -> Path:
    """`ai.md` listed as legacy, linking to `combat.md`, with `combat.md` moved to `combat/index.md` on the branch."""
    root = checkout(
        tmp_path,
        files={
            "docs/research/combat.md": page("Combat.\n\n" + "A fact about combat.\n\n" * 40, meta=""),
            "docs/research/ai.md": links,
            "docs/research/combat-grabs.md": "# Grabs\n\n## Grab {#grab}\n\nText.\n",
        },
    )
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "legacy")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "links")
    git(root, "mv", "docs/research/combat.md", "docs/research/combat-gone.md")
    return root


def touches(root: Path) -> dict[str, frozenset[int] | None]:
    """The lines the change touches of `ai.md`."""
    return {
        name: touch.lines for name, touch in docs_check.change_touches(root, "main", ["docs/research/ai.md"]).items()
    }


def test_a_page_whose_only_change_is_links_to_a_moved_page_is_untouched(tmp_path: Path) -> None:
    root = linked_checkout(tmp_path)
    write(root, "docs/research/ai.md", LINKS.replace("combat.md#grab", "combat-grabs.md#grab"))
    assert touches(root) == {"docs/research/ai.md": frozenset()}
    result = docs_check.check_changed(root, "main", context(root))
    assert result.findings["docs/research/ai.md"][0].severity == "warning"
    assert "docs/research/ai.md" not in result.failed


def test_a_link_to_a_page_that_stays_is_a_touch(tmp_path: Path) -> None:
    root = linked_checkout(tmp_path)
    write(root, "docs/research/ai.md", LINKS.replace("combat.md#grab", "good.md#grab", 1))
    assert touches(root) == {"docs/research/ai.md": frozenset({3})}
    assert "docs/research/ai.md" in docs_check.check_changed(root, "main", context(root)).failed


def test_a_rewrite_next_to_another_edit_touches_the_line_of_the_edit_only(tmp_path: Path) -> None:
    root = linked_checkout(tmp_path)
    text = LINKS.replace("combat.md#grab", "combat-grabs.md#grab").replace("for the grab", "for the grabs")
    write(root, "docs/research/ai.md", text)
    assert touches(root) == {"docs/research/ai.md": frozenset({3})}


# --- a page that moved to another folder ------------------------------------------------------------------------------


def test_a_moved_pages_links_rewritten_for_its_new_folder_are_no_touch() -> None:
    def moved(old: str, new: str) -> bool:
        return link_only_change(old, new, "docs/research/ai", GONE, resolves, old_folder="docs/research")

    assert moved("[a](combat-grabs.md#grab)", "[a](../combat-grabs.md#grab)")
    assert moved(
        "[a](combat-grabs.md#grab) and [b](old-name.md)", "[a](../combat-grabs.md#grab) and [b](../combat/index.md)"
    )
    assert not moved("[a](combat-grabs.md#grab)", "[a](../combat-grabs.md#release)")  # another anchor
    assert not moved("[a](combat-grabs.md#grab)", "[a](../camera.md#grab)")  # another page
    assert not moved("[a](combat-grabs.md#grab)", "[b](../combat-grabs.md#grab)")  # another text
    assert not moved("[a](combat-grabs.md#grab)", "[a](combat-grabs.md#grab)")  # no change at all


def test_the_lines_of_a_moved_page_leave_out_its_own_relative_links() -> None:
    diff = (
        "@@ -3 +3 @@\n-See [grabs](combat-grabs.md#grab).\n+See [grabs](../combat-grabs.md#grab).\n"
        "@@ -5 +5 @@\n-Plain.\n+Plain, edited.\n"
    )
    assert touched_lines(diff, "docs/research/ai", GONE, resolves, "docs/research") == frozenset({5})


def test_a_page_moved_to_another_folder_is_graded_only_where_it_changed(tmp_path: Path) -> None:
    old, new = "docs/research/ai.md", "docs/research/ai/index.md"
    text = "# Links\n\nSee [grabs](combat-grabs.md#grab) for the grab.\n\n" + "A fact about the page.\n\n" * 30
    root = checkout(
        tmp_path, files={old: text, "docs/research/combat-grabs.md": "# Grabs\n\n## Grab {#grab}\n\nText.\n"}
    )
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "legacy")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "move")
    (root / "docs/research/ai").mkdir()
    git(root, "mv", old, new)
    write(root, new, text.replace("(combat-grabs.md", "(../combat-grabs.md"))
    git(root, "add", "-A")
    touch = docs_check.change_touches(root, "main", [new])[new]
    assert touch.moved_from == old and touch.lines == frozenset()
    write(root, new, text.replace("(combat-grabs.md", "(../combat-grabs.md").replace("A fact", "An edited fact", 1))
    git(root, "add", "-A")
    assert docs_check.change_touches(root, "main", [new])[new].lines == frozenset({5})


# --- a link split over lines, a reflow the rewrite caused -------------------------------------------------------------

SPLIT = """@@ -3,2 +3,2 @@
-See [the long
-grabs](combat.md#grab) for it.
+See [the long
+grabs](combat-grabs.md#grab) for it.
"""
REFLOWED = """@@ -3,2 +3,3 @@
-A long sentence about [grabs](combat.md#grab) in the
-game.
+A long sentence about
+[grabs](combat-grabs.md#grab) in the
+game.
"""


def test_a_link_split_over_two_lines_is_judged_as_one() -> None:
    assert touched_lines(SPLIT, FOLDER, GONE, resolves) == frozenset()
    elsewhere = SPLIT.replace("combat-grabs.md#grab", "camera.md#grab")
    assert touched_lines(elsewhere, FOLDER, GONE, resolves) == frozenset({3, 4})


def test_a_reflow_that_only_a_longer_link_target_caused_is_no_touch() -> None:
    assert touched_lines(REFLOWED, FOLDER, GONE, resolves) == frozenset()
    # With a word changed, or with no link rewritten at all, it is a touch.
    assert touched_lines(REFLOWED.replace("+game.", "+games."), FOLDER, GONE, resolves) == {3, 4, 5}
    plain = REFLOWED.replace("combat-grabs.md#grab", "combat.md#grab")
    assert touched_lines(plain, FOLDER, GONE, resolves) == frozenset({3, 4, 5})


def test_line_breaks_and_spaces_do_not_hide_an_edit_of_the_text() -> None:
    assert same("See [it](combat.md#grab)   for  the grab.", "See [it](combat-grabs.md#grab) for the grab.")
    assert not same("See [it](combat.md#grab) for the grab.", "See [it](combat-grabs.md#grab) for the grabs.")
    assert link_only_change(
        "See [the\ngrabs](combat.md#grab) now.", "See [the grabs](combat-grabs.md#grab) now.", FOLDER, GONE, resolves
    )


def test_a_paragraph_reflowed_after_a_link_rewrite_is_no_touch(tmp_path: Path) -> None:
    root = linked_checkout(tmp_path)
    # The longer target pushed the end of the sentence to a line of its own (markdownlint's 120 columns).
    text = LINKS.replace(
        "See [grabs](combat.md#grab) for the grab.", "See [grabs](combat-grabs.md#grab)\nfor the grab."
    )
    write(root, "docs/research/ai.md", text.replace("combat.md#grab", "combat-grabs.md#grab"))
    assert touches(root) == {"docs/research/ai.md": frozenset()}
    # The same reflow with the sentence changed is a touch of the lines it changed.
    write(
        root, "docs/research/ai.md", text.replace("combat.md#grab", "combat-grabs.md#grab").replace("the grab", "grabs")
    )
    assert touches(root) == {"docs/research/ai.md": frozenset({3, 4})}


def test_a_line_break_that_is_structure_is_a_touch() -> None:
    def hunk(old: list[str], new: list[str]) -> frozenset[int]:
        """The touched lines of one hunk at line 3 that turns `old` into `new`."""
        header = f"@@ -3,{len(old)} +3,{len(new)} @@\n"
        diff = header + "".join(f"-{line}\n" for line in old) + "".join(f"+{line}\n" for line in new)
        return touched_lines(diff, FOLDER, GONE, resolves)

    # A heading joined to the line above disappears, and a list item appears where a line was broken.
    assert hunk(["See [g](combat.md#grab).", "## Data"], ["See [g](combat-grabs.md#grab). ## Data"]) == {3}
    assert hunk(["See [g](combat.md#grab) - a note."], ["See [g](combat-grabs.md#grab)", "- a note."]) == {3, 4}
    # So do a blank line, a quotation, a table row and a code fence on either side.
    for marker in ["", "> quoted", "| a | b |", "```", "1. first"]:
        assert hunk(["See [g](combat.md#grab)", marker], ["See [g](combat-grabs.md#grab) " + marker.strip()]) == {3}
    # A rewrite on one list item, kept a list item, is no touch; a changed indent is.
    assert hunk(["- See [g](combat.md#grab)."], ["- See [g](combat-grabs.md#grab)."]) == frozenset()
    assert hunk(["  - See [g](combat.md#grab)."], ["- See [g](combat-grabs.md#grab)."]) == {3}


# --- MkDocs block starts, a split link with an unchanged first line, a list item rewrap -------------------------------


def test_a_mkdocs_block_start_joined_onto_a_link_line_is_a_touch() -> None:
    def hunk(old: list[str], new: list[str]) -> frozenset[int]:
        """The touched lines of one hunk at line 3 that turns `old` into `new`."""
        header = f"@@ -3,{len(old)} +3,{len(new)} @@\n"
        diff = header + "".join(f"-{line}\n" for line in old) + "".join(f"+{line}\n" for line in new)
        return touched_lines(diff, FOLDER, GONE, resolves)

    starts = [
        "!!! note",
        "??? tip",
        "???+ tip",
        "    !!! nested",
        "===",
        "---",
        "<div>",
        "</div>",
        "<!-- comment -->",
        "[^1]: a footnote",
        "[ref]: https://example.org",
        "{: .class }",
        '=== "Tab"',
    ]
    for marker in starts:
        old, new = ["See [g](combat.md#grab)", marker], ["See [g](combat-grabs.md#grab) " + marker.strip()]
        assert hunk(old, new) == {3}, marker
    # Plain text on the next line is still a reflow.
    assert hunk(["See [g](combat.md#grab)", "plain text"], ["See [g](combat-grabs.md#grab) plain text"]) == frozenset()


FILE = ["# Links", "", "See [the long", "grabs](combat-grabs.md#grab) for it.", "", "Plain."]
SPLIT_TAIL = "@@ -4 +4 @@\n-grabs](combat.md#grab) for it.\n+grabs](combat-grabs.md#grab) for it.\n"


def test_a_split_link_whose_target_line_alone_changed_is_no_touch() -> None:
    assert touched_lines(SPLIT_TAIL, FOLDER, GONE, resolves, new_lines=FILE) == frozenset()
    # A list item holding the first half of the link is unchanged structure.
    item = ["# Links", "", "- See [the long", "  grabs](combat-grabs.md#grab) for it."]
    diff = "@@ -4 +4 @@\n-  grabs](combat.md#grab) for it.\n+  grabs](combat-grabs.md#grab) for it.\n"
    assert touched_lines(diff, FOLDER, GONE, resolves, new_lines=item) == frozenset()
    # A link text split over three lines.
    three = ["# Links", "", "See [the", "long", "grabs](combat-grabs.md#grab) for it."]
    diff = "@@ -5 +5 @@\n-grabs](combat.md#grab) for it.\n+grabs](combat-grabs.md#grab) for it.\n"
    assert touched_lines(diff, FOLDER, GONE, resolves, new_lines=three) == frozenset()


def test_a_split_link_is_still_a_touch_when_anything_else_changed() -> None:
    # Without the file there is nothing to widen to.
    assert touched_lines(SPLIT_TAIL, FOLDER, GONE, resolves) == frozenset({4})
    # The target is not a moved page's, or the rest of the line changed.
    elsewhere = SPLIT_TAIL.replace("combat-grabs.md#grab", "camera.md#grab")
    assert touched_lines(elsewhere, FOLDER, GONE, resolves, new_lines=FILE) == frozenset({4})
    edited = SPLIT_TAIL.replace("+grabs](combat-grabs.md#grab) for it.", "+grabs](combat-grabs.md#grab) for them.")
    assert touched_lines(edited, FOLDER, GONE, resolves, new_lines=FILE) == frozenset({4})
    # The line above is blank: there is no link text to join.
    blank = ["# Links", "", "", "grabs](combat-grabs.md#grab) for it."]
    assert touched_lines(SPLIT_TAIL, FOLDER, GONE, resolves, new_lines=blank) == frozenset({4})


def test_a_list_item_rewrapped_with_the_same_markers_is_no_touch() -> None:
    def hunk(old: list[str], new: list[str]) -> frozenset[int]:
        """The touched lines of one hunk at line 3 that turns `old` into `new`."""
        header = f"@@ -3,{len(old)} +3,{len(new)} @@\n"
        diff = header + "".join(f"-{line}\n" for line in old) + "".join(f"+{line}\n" for line in new)
        return touched_lines(diff, FOLDER, GONE, resolves)

    old = ["- See [g](combat.md#grab) a long item text that", "  continues here."]
    same_markers = ["- See [g](combat-grabs.md#grab) a long item text", "  that continues here."]
    assert hunk(old, same_markers) == frozenset()
    numbered = ["1. See [g](combat.md#grab) text that", "   goes on."]
    assert hunk(numbered, ["1. See [g](combat-grabs.md#grab) text", "   that goes on."]) == frozenset()
    # A marker that appeared or lost its indent, or a word that changed, is a touch.
    assert hunk(old, [*same_markers[:1], "- that continues here."]) == {3, 4}
    assert hunk(old, ["- See [g](combat-grabs.md#grab) a long item text that continues here."]) == frozenset()
    assert hunk(old, [*same_markers[:1], "  that continue here."]) == {3, 4}
    assert hunk(["- See [g](combat.md#grab)"], ["* See [g](combat-grabs.md#grab)"]) == {3}


def test_a_split_link_with_only_its_target_line_changed_is_no_touch_in_a_checkout(tmp_path: Path) -> None:
    split = LINKS.replace(
        "See [grabs](combat.md#grab) for the grab.", "See [the long\ngrabs](combat.md#grab) for the grab."
    )
    root = linked_checkout(tmp_path, split)
    write(root, "docs/research/ai.md", split.replace("combat.md#grab", "combat-grabs.md#grab"))
    assert touches(root) == {"docs/research/ai.md": frozenset()}


def test_indented_lines_continue_a_list_item_but_a_code_block_is_structure() -> None:
    diff = "\n".join(
        [
            "@@ -3,2 +3,2 @@",
            "-     [it](combat.md#grab)): when the state is not the one, the",
            "-     word lacks it:",
            "+     [it](combat-grabs.md#grab)): when the state is not the one,",
            "+     the word lacks it:",
        ]
    )
    item = ["- A nested item that", "  goes on and", "     [it](combat-grabs.md#grab)): when the state", "     the"]
    assert touched_lines(diff, FOLDER, GONE, resolves) == frozenset({3, 4})  # nothing above: read as code
    assert touched_lines(diff, FOLDER, GONE, resolves, new_lines=item) == frozenset()
    # After a blank line the same indent is a code block.
    code = ["- A nested item that", "", *item[2:]]
    assert touched_lines(diff, FOLDER, GONE, resolves, new_lines=code) == frozenset({3, 4})

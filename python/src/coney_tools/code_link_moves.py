# SPDX-License-Identifier: GPL-3.0-or-later
"""A comment's research link that follows a page move is no touch of the file that holds the comment.

A source or test file that a change touches is held to the whole code or test standard (`standards_check`). Moving a
research page makes every file that cites it change a comment line, and those files did nothing but keep their
citations true, as the pages that link to a moved page do (`docs_link_moves`). So a hunk of a file's diff is no touch
when it only rewrites research references in comments and every reference it changes follows a move by the rules of
`docs_link_moves.target_rewritten`:

* a reference is `docs/research/<page>.md`, `research/<page>.md` or (on a `// Expected:` line, the form of a test's
  citation) `<page>.md`, with an optional `#anchor`, and it counts only inside a comment: after a `//` or on a line of
  a block comment;
* the rest of the hunk is unchanged: line by line (every other character, the indent included, is equal), or, when
  every line on both sides is a comment line of the same kind (`//`, `///`, `*`) and none is empty, as one paragraph
  that clang-format reflowed because a longer reference pushed a line past the width (the words are equal);
* any other edit in the hunk, a reference to a page that was not moved, one that resolves nowhere, and a reference
  whose form changed with the page and anchor the same are all a touch.

The functions are pure: the caller says which paths are gone, whether a target resolves after the change and, for a
section that moved between two pages, whether the old page held its anchor before it.
"""

from __future__ import annotations

import re
from collections.abc import Callable, Collection
from dataclasses import dataclass

from coney_tools.docs_link_moves import target_rewritten

#: A research reference: with a folder (`docs/research/` or `research/`) or the bare `<page>.md`, and its anchor.
_REF = re.compile(
    r"(?<![\w/.-])(?P<root>(?:docs/)?research/)?(?P<page>[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*\.md)"
    r"(?:#(?P<anchor>[A-Za-z0-9_-]+))?(?![\w/-])"
)
#: A unified diff's hunk header, for the new file's side.
_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,\d+)? @@")
#: A line that is only a comment: its indent, its marker and its text. The marker is what a reflow keeps.
_COMMENT = re.compile(r"^(?P<lead>\s*)(?P<mark>///|//!|//|/\*\*|/\*|\*(?=\s|/|$))(?P<text>.*)$")
#: A line of a block comment, which holds a reference from its start.
_BLOCK_LINE = re.compile(r"^\s*(?:/\*|\*(?:\s|/|$))")
#: The mark of a test case's citation line, where a page is cited without its folder.
_EXPECTED = "Expected:"
#: Stands in for a reference when two texts are compared without them.
_MASK = "\x00"
#: The folder the research pages live in.
_FOLDER = "docs/research/"

#: A reference as read: its text and the page it names (its path in the repository) with the anchor.
_Ref = tuple[str, str, str]


@dataclass(frozen=True)
class CodeDiff:
    """What the zero-context diff of one source or test file touches."""

    #: The lines of the new file the diff touches, a research link move in comments left out.
    lines: frozenset[int]
    #: The hunks the diff holds.
    hunks: int

    @property
    def link_moves_only(self) -> bool:
        """Whether the diff has hunks and every one is a research link move: the file is no touch."""
        return self.hunks > 0 and not self.lines


def _comment_start(line: str) -> int:
    """Where the comment of `line` begins: 0 for a line of a block comment, the `//` for a trailing one, and the line's
    length when it has none."""
    if _BLOCK_LINE.match(line):
        return 0
    found = line.find("//")
    return len(line) if found < 0 else found


def _mask(text: str, begin: int) -> tuple[str, list[_Ref]]:
    """`text` with every research reference from `begin` on replaced by a mask, and the references read; a bare
    `<page>.md` counts only on a line with `Expected:`."""
    refs: list[_Ref] = []
    bare_ok = _EXPECTED in text

    def take(match: re.Match[str]) -> str:
        """Mask the reference `match` when it is one."""
        if match.start() < begin or not (match.group("root") or bare_ok):
            return match.group(0)
        refs.append((match.group(0), _FOLDER + match.group("page"), match.group("anchor") or ""))
        return _MASK

    return _REF.sub(take, text), refs


def _target(ref: _Ref) -> str:
    """The link target a reference names: its page, then `#anchor` when it has one."""
    return f"{ref[1]}#{ref[2]}" if ref[2] else ref[1]


def _same_but_refs(
    was: str,
    now: str,
    begin: tuple[int, int],
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None,
) -> bool:
    """Whether the text `was` became `now` by research references that follow a move and nothing else; `begin` is where
    the comment starts in each."""
    plain_was, before = _mask(was, begin[0])
    plain_now, after = _mask(now, begin[1])
    if plain_was != plain_now or len(before) != len(after):
        return False
    changed = [(a, b) for a, b in zip(before, after, strict=True) if a[0] != b[0]]
    return bool(changed) and all(target_rewritten(_target(a), _target(b), gone, resolves, held) for a, b in changed)


def _reflowed(
    removed: list[str],
    added: list[str],
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None,
) -> bool:
    """Whether `removed` and `added` are one comment's lines (the same kind and indent on every line, none empty) whose
    words are equal but for references that follow a move, however the lines break."""
    rows = [_COMMENT.match(line) for line in (*removed, *added)]
    parsed = [row for row in rows if row is not None and row.group("text").strip()]
    if len(parsed) != len(rows):
        return False
    if len({row.group("mark") for row in parsed}) != 1 or len({row.group("lead") for row in parsed}) != 1:
        return False
    was = " ".join(" ".join(row.group("text").split()) for row in parsed[: len(removed)])
    now = " ".join(" ".join(row.group("text").split()) for row in parsed[len(removed) :])
    return _same_but_refs(was, now, (0, 0), gone, resolves, held)


def link_move_hunk(
    removed: list[str],
    added: list[str],
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None = None,
) -> bool:
    """Whether a hunk that replaced the lines `removed` with `added` only rewrote research references in comments to
    follow a moved page."""
    if not removed or not added:
        return False
    if len(removed) == len(added):
        changed = [(was, now) for was, now in zip(removed, added, strict=True) if was != now]
        if changed and all(
            _same_but_refs(was, now, (_comment_start(was), _comment_start(now)), gone, resolves, held)
            for was, now in changed
        ):
            return True
    return _reflowed(removed, added, gone, resolves, held)


def touched_code_lines(
    diff: str,
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None = None,
) -> CodeDiff:
    """The lines of the new file that the zero-context `diff` of one file touches: every line a hunk adds, the line a
    pure deletion sits at, and none of a hunk that is a research link move (`link_move_hunk`)."""
    touched: set[int] = set()
    hunks = 0
    start = 0
    removed: list[str] = []
    added: list[str] = []
    opened = False

    def close() -> None:
        """Count the hunk read so far."""
        if opened and not link_move_hunk(removed, added, gone, resolves, held):
            touched.update(range(start, start + max(len(added), 1)))

    for raw in diff.splitlines():
        header = _HUNK.match(raw)
        if header:
            close()
            hunks += 1
            start, removed, added, opened = int(header.group("start")), [], [], True
        elif opened and raw.startswith("-"):
            removed.append(raw[1:])
        elif opened and raw.startswith("+"):
            added.append(raw[1:])
    close()
    return CodeDiff(frozenset(touched), hunks)

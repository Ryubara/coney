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
  every line on both sides is nothing but a comment of the same kind (`//`, `///`, `*`) and none is empty, as one
  paragraph that clang-format reflowed because a longer reference pushed a line past the width (the words are equal);
* any other edit in the hunk, a reference to a page that was not moved, one that resolves nowhere, and a reference
  whose form changed with the page and anchor the same are all a touch.

The functions are pure: the caller says which paths are gone, whether a target resolves after the change and, for a
section that moved between two pages, whether the old page held its anchor before it.
"""

from __future__ import annotations

import re
from collections.abc import Callable, Collection, Sequence
from dataclasses import dataclass

from coney_tools.comment_spans import Span, comment_spans, is_comment
from coney_tools.docs_link_moves import target_rewritten

#: A research reference: with a folder (`docs/research/` or `research/`) or the bare `<page>.md`, and its anchor.
_REF = re.compile(
    r"(?<![\w/.-])(?P<root>(?:docs/)?research/)?(?P<page>[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+)*\.md)"
    r"(?:#(?P<anchor>[A-Za-z0-9_-]+))?(?![\w/-])"
)
#: A unified diff's hunk header, for the new file's side.
_HUNK = re.compile(r"^@@ -(?P<old>\d+)(?:,(?P<count>\d+))? \+(?P<start>\d+)(?:,\d+)? @@")
#: A line that is only a comment: its indent, its marker and its text. The marker is what a reflow keeps.
_COMMENT = re.compile(r"^(?P<lead>\s*)(?P<mark>///|//!|//|/\*\*|/\*|\*(?=\s|/|$))(?P<text>.*)$")
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


def _mask(text: str, spans: Sequence[Span]) -> tuple[str, list[_Ref]]:
    """`text` with every research reference that starts in one of the comment `spans` replaced by a mask, and the
    references read; a bare `<page>.md` counts only on a line with `Expected:`."""
    refs: list[_Ref] = []
    bare_ok = _EXPECTED in text

    def take(match: re.Match[str]) -> str:
        """Mask the reference `match` when it is one."""
        if not any(a <= match.start() < b for a, b in spans) or not (match.group("root") or bare_ok):
            return match.group(0)
        refs.append((match.group(0), _FOLDER + match.group("page"), match.group("anchor") or ""))
        return _MASK

    return _REF.sub(take, text), refs


def _target(ref: _Ref) -> str:
    """The link target a reference names: its page, then `#anchor` when it has one."""
    return f"{ref[1]}#{ref[2]}" if ref[2] else ref[1]


def _same_but_refs(
    was: tuple[str, Sequence[Span]],
    now: tuple[str, Sequence[Span]],
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None,
) -> bool:
    """Whether the text `was` became `now` by research references that follow a move and nothing else; each text comes
    with the spans of it that a comment holds."""
    plain_was, before = _mask(*was)
    plain_now, after = _mask(*now)
    if plain_was != plain_now or len(before) != len(after):
        return False
    changed = [(a, b) for a, b in zip(before, after, strict=True) if a[0] != b[0]]
    return bool(changed) and all(target_rewritten(_target(a), _target(b), gone, resolves, held) for a, b in changed)


def _reflowed(
    removed: list[str],
    added: list[str],
    spans: tuple[list[list[Span]], list[list[Span]]],
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None,
) -> bool:
    """Whether `removed` and `added` are one comment's lines (nothing but comment, of the same kind and indent on every
    line, none empty) whose words are equal but for references that follow a move, however the lines break."""
    lines = [*removed, *added]
    rows = [_COMMENT.match(line) for line in lines]
    parsed = [row for row in rows if row is not None and row.group("text").strip()]
    pure = all(is_comment(line, found) for line, found in zip(lines, [*spans[0], *spans[1]], strict=True))
    if len(parsed) != len(rows) or not pure:
        return False
    if len({row.group("mark") for row in parsed}) != 1 or len({row.group("lead") for row in parsed}) != 1:
        return False
    was = " ".join(" ".join(row.group("text").split()) for row in parsed[: len(removed)])
    now = " ".join(" ".join(row.group("text").split()) for row in parsed[len(removed) :])
    return _same_but_refs((was, [(0, len(was))]), (now, [(0, len(now))]), gone, resolves, held)


def link_move_hunk(
    removed: list[str],
    added: list[str],
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None = None,
    spans: tuple[list[list[Span]], list[list[Span]]] | None = None,
) -> bool:
    """Whether a hunk that replaced the lines `removed` with `added` only rewrote research references in comments to
    follow a moved page. `spans` are the comment spans of the removed and of the added lines (`comment_spans` of the
    file, for lines inside a block comment); without them the lines are read alone, from outside any comment."""
    if not removed or not added:
        return False
    spans = spans or (comment_spans(removed), comment_spans(added))
    if len(removed) == len(added):
        changed = [
            ((was, spans[0][index]), (now, spans[1][index]))
            for index, (was, now) in enumerate(zip(removed, added, strict=True))
            if was != now
        ]
        if changed and all(_same_but_refs(was, now, gone, resolves, held) for was, now in changed):
            return True
    return _reflowed(removed, added, spans, gone, resolves, held)


def touched_code_lines(
    diff: str,
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None = None,
    old_lines: Sequence[str] | None = None,
    new_lines: Sequence[str] | None = None,
) -> CodeDiff:
    """The lines of the new file that the zero-context `diff` of one file touches: every line a hunk adds, the line a
    pure deletion sits at, and none of a hunk that is a research link move (`link_move_hunk`). `old_lines` and
    `new_lines` are the lines of the file before and after the change, which tell the lines of a block comment from
    code; they are scanned only when a hunk needs it."""
    touched: set[int] = set()
    hunks = 0
    start = old = 0
    removed: list[str] = []
    added: list[str] = []
    opened = False
    scans: dict[str, list[list[Span]]] = {}

    def spans_of(which: str, lines: Sequence[str] | None, first: int, count: int) -> list[list[Span]] | None:
        """The comment spans of `count` lines of `lines` from the 1-based line `first`, scanning the file once."""
        if lines is None or first < 1 or first - 1 + count > len(lines):
            return None
        if which not in scans:
            scans[which] = comment_spans(lines)
        return scans[which][first - 1 : first - 1 + count]

    def close() -> None:
        """Count the hunk read so far."""
        if not opened:
            return
        before = after = None
        if any(".md" in line for line in removed):  # a hunk that removes no page name is no link move: skip the scan
            before = spans_of("old", old_lines, old, len(removed))
            after = spans_of("new", new_lines, start, len(added))
        spans = (before, after) if before is not None and after is not None else None
        if not link_move_hunk(removed, added, gone, resolves, held, spans):
            touched.update(range(start, start + max(len(added), 1)))

    for raw in diff.splitlines():
        header = _HUNK.match(raw)
        if header:
            close()
            hunks += 1
            start, old = int(header.group("start")), int(header.group("old"))
            removed, added, opened = [], [], True
        elif opened and raw.startswith("-"):
            removed.append(raw[1:])
        elif opened and raw.startswith("+"):
            added.append(raw[1:])
    close()
    return CodeDiff(frozenset(touched), hunks)

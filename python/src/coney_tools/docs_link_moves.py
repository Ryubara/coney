# SPDX-License-Identifier: GPL-3.0-or-later
"""A link rewrite that follows a move is no touch of the page that holds the link.

A legacy research page ends its excuse when a change touches it (`research_legacy`). Moving a page makes every page
that links to it change a line, and those pages did nothing but keep their links true. So a line whose only change is
the target of an inline link `[text](target)` does not count as touched when, for every link it changes:

* the old target's page is one the same change renamed or deleted (its anchor is not looked at), and the new target
  resolves: the page exists after the change and, when the target names an anchor, holds it; or
* the new target names the same page and anchor as the old one, which a page that moved to another folder needs for
  every relative link it holds (the old target is read from the page's old folder, the new from its new one).

The text of the link, the rest of the line and the number of links must be equal (spaces and line breaks do not count,
so a link split over two lines is read whole, and so is the paragraph or list item that a longer target made the editor
reflow), so any other edit on the line is a touch. A link to a page that was not moved, a link to something outside the
pages (a scheme, `repo:`, an image) and a target that resolves nowhere are all touches (the last also fails the build:
MkDocs checks the link).

The functions are pure: the caller says which paths are gone and whether a target resolves.
"""

from __future__ import annotations

import posixpath
import re
from collections.abc import Callable, Collection, Sequence

#: An inline link, not an image: its text, its target and whatever follows the target inside the parentheses (a title).
_LINK = re.compile(r"(?<!!)\[(?P<text>[^\]\n]*)\]\((?P<target>[^)\s]*)(?P<tail>[^)\n]*)\)")
#: A unified diff's hunk header, for the new file's side.
_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,\d+)? @@")
#: A target with a scheme (`https:`, `repo:`, `mailto:`) leaves the pages.
_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
#: A line that starts or ends a Markdown block, so a line break before it is structure, not layout: a heading, a list
#: item, a quotation, a table row or a code fence; and what MkDocs adds: an admonition
#: (`!!!`, `???`), a content tab (`===`), a setext underline or rule (`===`, `---`, `***`), an HTML block, a footnote
#: or link definition (`[^1]:`, `[x]: url`) and an attribute list (`{: ...}`).
_BLOCK_START = re.compile(
    r"^(?:\s*(?:#|[-*+]\s|>|\||\d+[.)]\s|```|~~~|!!!|\?\?\?|===|<[A-Za-z/!]|\[[^\]]+\]:|\{:)"
    r"|\s*(?:=+|-+|\*{3,}|_{3,})\s*$)"
)
#: An indented line, which is a code block only after a blank line: after text it continues a paragraph or list item.
_INDENTED = re.compile(r"^(?: {4}|\t)")
#: A list item's marker with the whitespace before it.
_LIST_ITEM = re.compile(r"^\s*(?:[-*+]|\d+[.)])\s")
#: The first line of a link split over lines: it holds the end of the link text, `](target)`, and no `[` before it.
_LINK_TAIL = re.compile(r"^[^\[\]]*\]\(")
#: How many lines above a hunk are read to find the `[` of a link split over lines.
_LINK_REACH = 5
#: Stands in for a target when two lines are compared without their targets.
_TARGET = "\x00"


def _without_targets(line: str) -> str:
    """`line` with the target of every inline link blanked."""
    return _LINK.sub(lambda match: f"[{match.group('text')}]({_TARGET}{match.group('tail')})", line)


def _split(target: str, folder: str) -> tuple[str, str] | None:
    """The page a link target names (repository-relative, forward slashes) and its anchor, or None when it names no
    page of the repository: a scheme, a same-page anchor, an absolute path."""
    if not target or _SCHEME.match(target) or target.startswith(("#", "/")):
        return None
    path, _, anchor = target.partition("#")
    path = path.split("?", 1)[0]
    return posixpath.normpath(posixpath.join(folder, path)), anchor


def _rewritten(
    old: str,
    new: str,
    folder: str,
    old_folder: str,
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
) -> bool:
    """Whether the target `old` (read from `old_folder`) was rewritten to `new` (read from `folder`) because a page
    moved: the old page is in `gone` and the new target resolves, or both name the same page and anchor."""
    before, after = _split(old, old_folder), _split(new, folder)
    if before is None or after is None:
        return False
    return before == after or (before[0] in gone and resolves(*after))


def link_only_change(
    old: str,
    new: str,
    folder: str,
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    old_folder: str | None = None,
) -> bool:
    """Whether the text `old` (a line, or the lines of a hunk) became `new` by rewriting link targets to moved pages and
    nothing else; runs of spaces and line breaks count as one space.

    `folder` is the folder of the page that holds the line (targets are relative to it), `gone` the repository-relative
    paths the change renamed or deleted, and `resolves(path, anchor)` whether `path` exists after the change and holds
    `anchor` (any, when it is empty). `old_folder` is the folder the page held `old` in when the change moved the page
    (None: it did not move)."""
    from_folder = folder if old_folder is None else old_folder
    old, new = " ".join(old.split()), " ".join(new.split())
    before, after = list(_LINK.finditer(old)), list(_LINK.finditer(new))
    if not before or len(before) != len(after) or _without_targets(old) != _without_targets(new):
        return False
    changed = [(a.group("target"), b.group("target")) for a, b in zip(before, after, strict=True)]
    changed = [pair for pair in changed if pair[0] != pair[1]]
    return bool(changed) and all(_rewritten(a, b, folder, from_folder, gone, resolves) for a, b in changed)


def _starts_block(line: str, previous: str | None) -> bool:
    """Whether `line` is blank or starts a block, given the `previous` line (None when it is not known). An indented
    line is code only when no text comes before it."""
    if not line.strip() or _BLOCK_START.match(line):
        return True
    return bool(_INDENTED.match(line)) and not (previous is not None and previous.strip())


def _prose(lines: list[str], above: str | None) -> bool:
    """Whether `lines` are the lines of one paragraph, which may be broken anywhere: none is blank or starts a block.
    `above` is the line before the first."""
    return not any(_starts_block(line, previous) for line, previous in zip(lines, [above, *lines], strict=False))


def _item_markers(lines: list[str], above: str | None) -> list[tuple[int, str]] | None:
    """The list markers of `lines` with the line each stands on, or None when a line is blank or starts any other block:
    the lines are the lines of list items, whose text may be broken anywhere but whose markers are structure."""
    markers: list[tuple[int, str]] = []
    for index, (line, previous) in enumerate(zip(lines, [above, *lines], strict=False)):
        item = _LIST_ITEM.match(line)
        if item:
            markers.append((index, item.group(0)))
        elif _starts_block(line, previous):
            return None
    return markers


def _rewrapped(removed: list[str], added: list[str], above: str | None) -> bool:
    """Whether `removed` and `added` are both one paragraph's lines, or both list items' lines with the same markers on
    the same lines of the hunk: a rewrap that moves words between lines changes no structure. `above` is the unchanged
    line before the hunk."""
    if _prose(removed, above) and _prose(added, above):
        return True
    before = _item_markers(removed, above)
    return before is not None and before == _item_markers(added, above)


def _link_head(lines: list[str], start: int, file: Sequence[str] | None) -> list[str]:
    """The unchanged lines of `file` above a hunk at `start` (1-based, new side) up to the one holding the `[` of the
    link that the hunk's first line closes, or an empty list when it closes none or the lines above are not text."""
    if file is None or not lines or not _LINK_TAIL.match(lines[0]):
        return []
    above: list[str] = []
    for number in range(start - 1, max(start - 1 - _LINK_REACH, 0), -1):
        if number > len(file) or not file[number - 1].strip():
            return []
        above.insert(0, file[number - 1])
        if "[" in file[number - 1] and not _LINK_TAIL.match(file[number - 1]):
            return above
    return []


def _lead(line: str) -> str:
    """The whitespace a line starts with, which is structure (an indented list item, a code line)."""
    return line[: len(line) - len(line.lstrip())]


def touched_lines(
    diff: str,
    folder: str,
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    old_folder: str | None = None,
    new_lines: Sequence[str] | None = None,
) -> frozenset[int]:
    """The lines of the new file that the zero-context `diff` of one file touches, a link rewrite to a moved page
    (`link_only_change`) left out. A hunk whose removed and added lines read the same but for such rewrites touches
    nothing, however the lines break, as long as both sides are one paragraph's lines (no blank line, none that starts
    a heading, a list item, a quotation, a table row or code: a line break there is structure). Otherwise a hunk that
    removes and adds the same number of lines is read line by line (a line whose indent changed is touched), any other
    hunk touches all its added lines, and a pure deletion touches the line it sits at.

    A hunk of list items is read the same way when the markers (`-`, `1.`) and the lines they stand on are the same on
    both sides: a longer target pushed a first line past the width and the editor rewrapped the item. A hunk that begins
    inside a link split over lines (its first line holds `](target)` and no `[`) is widened upwards, through the
    unchanged lines of `new_lines` (the new file's lines), to the line holding the `[`, so the link is read whole.
    `old_folder` is as for `link_only_change`."""
    touched: set[int] = set()
    start = 0
    removed: list[str] = []
    added: list[str] = []
    opened = False

    def only_links(was: list[str], now: list[str]) -> bool:
        """Whether the hunk's lines `was` became `now` by link rewrites alone, however the lines break; a hunk that
        begins inside a split link is read with the unchanged lines above it."""
        previous = new_lines[start - 2] if new_lines is not None and 2 <= start <= len(new_lines) + 1 else None
        if not _rewrapped(was, now, previous):
            return False
        above = _link_head(now, start, new_lines)
        return link_only_change(" ".join([*above, *was]), " ".join([*above, *now]), folder, gone, resolves, old_folder)

    def close() -> None:
        """Count the hunk read so far."""
        if not opened:
            return
        if not added:
            touched.add(start)
        elif removed and only_links(removed, added):
            return
        elif len(removed) != len(added):
            touched.update(range(start, start + len(added)))
        else:
            touched.update(
                start + offset
                for offset, (was, now) in enumerate(zip(removed, added, strict=True))
                if _lead(was) != _lead(now) or not link_only_change(was, now, folder, gone, resolves, old_folder)
            )

    for raw in diff.splitlines():
        header = _HUNK.match(raw)
        if header:
            close()
            start, removed, added, opened = int(header.group("start")), [], [], True
        elif opened and raw.startswith("-"):
            removed.append(raw[1:])
        elif opened and raw.startswith("+"):
            added.append(raw[1:])
    close()
    return frozenset(touched)

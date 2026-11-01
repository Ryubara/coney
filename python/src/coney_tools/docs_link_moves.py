# SPDX-License-Identifier: GPL-3.0-or-later
"""A link rewrite that follows a move is no touch of the page that holds the link.

A legacy research page ends its excuse when a change touches it (`research_legacy`). Moving a page makes every page
that links to it change a line, and those pages did nothing but keep their links true. So a line whose only change is
the target of an inline link `[text](target)` does not count as touched when, for every link it changes:

* the old target's page is one the same change renamed or deleted (its anchor is not looked at), and the new target
  resolves: the page exists after the change and, when the target names an anchor, holds it; or
* the new target names the same page and anchor as the old one, which a page that moved to another folder needs for
  every relative link it holds (the old target is read from the page's old folder, the new from its new one); or
* the new target names the same anchor on another page, the old page held that anchor before the change and no longer
  does, and the new page holds it now but did not before: the section moved between two pages (the old page stayed, so
  its anchor is looked at), and its id and the text of the link did not change; or
* the new target names another anchor on the same page, the old anchor is still there, and the new one is new: the
  page held the old anchor before the change but not the new one, and holds both now. Any anchor new on that page
  qualifies: the reason is a section split in two, the part the link cites moving out under its own id (a link to
  `#rage` that cites the flash, now `#flash`), but only the anchors are looked at, not where the new section came
  from. An anchor that replaced the old one (a renamed section) is a touch; or
* both name a file of the repository, as a `repo:<path>` link or a code span that holds only a path (`repo:<path>`, or
  a bare `src/platform/play_level_objects.cpp`), the same change renamed the old file to the new path (git's rename
  pair: a source file moved with `git mv`) and the new path resolves: a path repointed to where its file went. A file
  only deleted (perhaps split) says nothing of where its code went, so a path repointed from it is a touch.

The text of the link, the rest of the line and the number of links and paths must be equal (spaces and line breaks do
not count, so a link split over two lines is read whole, and so is the paragraph or list item that a longer target made
the editor reflow), so any other edit on the line is a touch: a name renamed beside the path (`PlayLevelMode::trySwap`
to `PlayLevelSimulation::trySwap`) as much as a word. A link to a page that was not moved, a page link turned into a
`repo:` path or back, a link to something else outside the pages (a scheme, an image) and a target that resolves
nowhere are all touches (the last also fails the build: MkDocs checks the link).

The functions are pure: the caller says which paths are gone, whether a target resolves after the change and, for a
section that moved, whether the old page held its anchor before it.
"""

from __future__ import annotations

import posixpath
import re
from collections.abc import Callable, Collection, Mapping, Sequence

#: An inline link, not an image: its text, its target and whatever follows the target inside the parentheses (a title).
_LINK = re.compile(r"(?<!!)\[(?P<text>[^\]\n]*)\]\((?P<target>[^)\s]*)(?P<tail>[^)\n]*)\)")
#: A unified diff's hunk header, for the new file's side.
_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,\d+)? @@")
#: A target with a scheme (`https:`, `repo:`, `mailto:`) leaves the pages.
_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
#: A line that starts or ends a Markdown block, so a line break before it is structure, not layout: a heading, a list
#: item, a quotation, a table row or a code fence; and what MkDocs adds: an admonition
#: (`!!!`, `???`), a content tab (`===`), a setext underline or rule (`===`, `---`, `***`), an HTML block, a footnote
#: or link definition (`[^1]:`, `[x]: url`), an attribute list (`{: ...}`) and a definition (`: text`, the `def_list`
#: extension).
_BLOCK_START = re.compile(
    r"^(?:\s*(?:#|[-*+]\s|>|\||\d+[.)]\s|```|~~~|!!!|\?\?\?|===|<[A-Za-z/!]|\[[^\]]+\]:|\{:|:\s)"
    r"|\s*(?:=+|-+|\*{3,}|_{3,})\s*$)"
)
#: An indented line, which is a code block only after a blank line: after text it continues a paragraph or list item.
_INDENTED = re.compile(r"^(?: {4}|\t)")
#: A list item's marker with the whitespace before it.
_LIST_ITEM = re.compile(r"^\s*(?:[-*+]|\d+[.)])\s")
#: The first line of a link split over lines: it holds the end of the link text, `](target)`, and no `[` before it.
_LINK_TAIL = re.compile(r"^[^\[\]]*\]\(")
#: How many lines above a hunk are read to find the `[` of a link split over lines. A link text longer than five lines
#: is not seen in the pages, and a short reach keeps the read of the lines above cheap.
_LINK_REACH = 5
#: Stands in for a target when two lines are compared without their targets.
_TARGET = "\x00"
#: The prefix of a link to a file of the repository outside the pages (writing-docs.md, step 3 of Adding a page).
_REPO = "repo:"
#: A code span that holds only a repository path: `repo:<path>` (an anchor allowed), or a bare path with a folder.
_PATH_SPAN = re.compile(r"`(?P<path>repo:[\w./-]+(?:#[\w-]+)?|[\w.-]+(?:/[\w.-]+)+)`")


def _without_targets(line: str) -> str:
    """`line` with the target of every inline link and every path span (`_PATH_SPAN`) blanked."""
    line = _LINK.sub(lambda match: f"[{match.group('text')}]({_TARGET}{match.group('tail')})", line)
    return _PATH_SPAN.sub(f"`{_TARGET}`", line)


def _targets(line: str) -> tuple[list[str], list[str]]:
    """The targets of `line`'s inline links, and its path spans as `repo:` targets, each in order."""
    paths = [m.group("path") for m in _PATH_SPAN.finditer(line)]
    return (
        [m.group("target") for m in _LINK.finditer(line)],
        [p if p.startswith(_REPO) else f"{_REPO}{p}" for p in paths],
    )


def _repo_moved(old: str, new: str, renamed: Mapping[str, str] | None, resolves: Callable[[str, str], bool]) -> bool:
    """Whether the `repo:` target `old` became the `repo:` target `new` because its file moved: the change renamed the
    old file (any anchor) to the new path (`renamed`: old path -> new, as git pairs them), which resolves, with the new
    anchor when it names one. A file only deleted (perhaps split in two) does not say where its code went, so a path
    repointed from it is a touch."""
    if renamed is None or not (old.startswith(_REPO) and new.startswith(_REPO)):
        return False
    before = old.removeprefix(_REPO).partition("#")[0]
    after, _, anchor = new.removeprefix(_REPO).partition("#")
    return renamed.get(before) == after and resolves(after, anchor)


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
    held: Callable[[str, str], bool] | None,
    renamed: Mapping[str, str] | None = None,
) -> bool:
    """Whether the target `old` (read from `old_folder`) was rewritten to `new` (read from `folder`) because a page
    moved: the old page is in `gone` and the new target resolves, both name the same page and anchor, or both name the
    same anchor, which the old page `held` before the change and the new one did not hold but does now (a section that
    moved between two pages that stayed), or both name the same page, which `held` the old anchor and still holds it
    but holds the new one only now (an anchor new on the page, as when a section is split). Without `held` the last
    two cases are not looked at."""
    if old.startswith(_REPO) or new.startswith(_REPO):
        return _repo_moved(old, new, renamed, resolves)
    before, after = _split(old, old_folder), _split(new, folder)
    if before is None or after is None:
        return False
    if before == after or (before[0] in gone and resolves(*after)):
        return True
    if held is None or not after[1] or not held(*before) or held(*after) or not resolves(*after):
        return False
    # The same id on another page: the section moved there and left the old page.
    if before[1] == after[1]:
        return not resolves(*before)
    # Another id on the same page, new on it while the old one stays (a split section); a renamed id is a touch.
    return bool(before[1]) and before[0] == after[0] and resolves(*before)


def target_rewritten(
    old: str,
    new: str,
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool] | None = None,
) -> bool:
    """Whether the repository-relative link target `old` (`docs/research/ai.md#fight`) was rewritten to `new` because a
    page moved, by the same cases as a link in a page (`link_only_change`); the two must name different pages or
    anchors. The source and test files' comments use it (`code_link_moves`)."""
    return _rewritten(old, new, "", "", gone, resolves, held) and _split(old, "") != _split(new, "")


def link_only_change(
    old: str,
    new: str,
    folder: str,
    gone: Collection[str],
    resolves: Callable[[str, str], bool],
    old_folder: str | None = None,
    held: Callable[[str, str], bool] | None = None,
    renamed: Mapping[str, str] | None = None,
) -> bool:
    """Whether the text `old` (a line, or the lines of a hunk) became `new` by rewriting link targets to moved pages and
    nothing else; runs of spaces and line breaks count as one space.

    `folder` is the folder of the page that holds the line (targets are relative to it), `gone` the repository-relative
    paths the change renamed or deleted, and `resolves(path, anchor)` whether `path` exists after the change and holds
    `anchor` (any, when it is empty). `old_folder` is the folder the page held `old` in when the change moved the page
    (None: it did not move). `held(path, anchor)` is whether `path` held `anchor` before the change (None: a section
    that moved between pages is not looked for). `renamed` maps each file the change renamed to its new path, for a
    `repo:` path that follows one (None: none is looked for)."""
    from_folder = folder if old_folder is None else old_folder
    old, new = " ".join(old.split()), " ".join(new.split())
    (links, paths), (new_links, new_paths) = _targets(old), _targets(new)
    if len(links) != len(new_links) or len(paths) != len(new_paths):
        return False
    if not (links or paths) or _without_targets(old) != _without_targets(new):
        return False
    pairs = [*zip(links, new_links, strict=True), *zip(paths, new_paths, strict=True)]
    changed = [pair for pair in pairs if pair[0] != pair[1]]
    return bool(changed) and all(
        _rewritten(a, b, folder, from_folder, gone, resolves, held, renamed) for a, b in changed
    )


def _continues(before: Sequence[str]) -> bool:
    """Whether a line placed after the lines `before` continues text (a paragraph or list item) instead of starting
    something: the nearest line above that is not an indented non-item line is text, not blank and not a block start
    (a heading, fence, table row, definition and the like). Nothing known above: it does not."""
    for line in reversed(before):
        if not line.strip():
            return False
        if _INDENTED.match(line) and not _LIST_ITEM.match(line):
            continue  # code, or a continuation: the lines further up decide
        return bool(_LIST_ITEM.match(line)) or not _BLOCK_START.match(line)
    return False


def _starts_block(line: str, before: Sequence[str]) -> bool:
    """Whether `line` is blank or starts a block, given the lines `before` it. An indented line is code unless it
    continues text."""
    if not line.strip() or _BLOCK_START.match(line):
        return True
    return bool(_INDENTED.match(line)) and not _continues(before)


def _prose(lines: list[str], before: Sequence[str]) -> bool:
    """Whether `lines` are the lines of one paragraph, which may be broken anywhere: none is blank or starts a block.
    `before` are the unchanged lines above the first."""
    return not any(_starts_block(line, [*before, *lines[:index]]) for index, line in enumerate(lines))


def _item_markers(lines: list[str], before: Sequence[str]) -> list[tuple[int, str]] | None:
    """The list markers of `lines` with the line each stands on, or None when a line is blank or starts any other block:
    the lines are the lines of list items, whose text may be broken anywhere but whose markers are structure."""
    markers: list[tuple[int, str]] = []
    for index, line in enumerate(lines):
        item = _LIST_ITEM.match(line)
        if item:
            markers.append((index, item.group(0)))
        elif _starts_block(line, [*before, *lines[:index]]):
            return None
    return markers


def _indents(lines: list[str]) -> set[str]:
    """The indents of the lines that are no list item: a rewrap moves words between lines, not into another indent."""
    return {_lead(line) for line in lines if not _LIST_ITEM.match(line)}


def _rewrapped(removed: list[str], added: list[str], before: Sequence[str]) -> bool:
    """Whether `removed` and `added` are both one paragraph's lines, or both list items' lines with the same markers on
    the same lines of the hunk. Their other lines keep their indents (when both sides have any): a rewrap moves words
    between lines and changes no structure. `before` are the unchanged lines above the hunk."""
    # Lines may join into one or split in two, which leaves a side with no continuation line to compare.
    was, now = _indents(removed), _indents(added)
    if was and now and was != now:
        return False
    if _prose(removed, before) and _prose(added, before):
        return True
    markers = _item_markers(removed, before)
    return markers is not None and markers == _item_markers(added, before)


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
    held: Callable[[str, str], bool] | None = None,
    renamed: Mapping[str, str] | None = None,
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
    `old_folder`, `held` and `renamed` are as for `link_only_change`."""
    touched: set[int] = set()
    start = 0
    removed: list[str] = []
    added: list[str] = []
    opened = False

    def only_links(was: list[str], now: list[str]) -> bool:
        """Whether the hunk's lines `was` became `now` by link rewrites alone, however the lines break; a hunk that
        begins inside a split link is read with the unchanged lines above it."""
        above_lines = list(new_lines[: start - 1]) if new_lines is not None else []
        if not _rewrapped(was, now, above_lines):
            return False
        above = _link_head(now, start, new_lines)
        return link_only_change(
            " ".join([*above, *was]), " ".join([*above, *now]), folder, gone, resolves, old_folder, held, renamed
        )

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
                if _lead(was) != _lead(now)
                or not link_only_change(was, now, folder, gone, resolves, old_folder, held, renamed)
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

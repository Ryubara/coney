# SPDX-License-Identifier: GPL-3.0-or-later
"""A link rewrite that follows a move is no touch of the page that holds the link.

A legacy research page ends its excuse when a change touches it (`research_legacy`). Moving a page makes every page
that links to it change a line, and those pages did nothing but keep their links true. So a line whose only change is
the target of an inline link `[text](target)` does not count as touched when, for every link it changes:

* the old target's page is one the same change renamed or deleted (its anchor is not looked at), and
* the new target resolves: the page exists after the change and, when the target names an anchor, holds it.

The text of the link, the rest of the line and the number of links must be equal, so any other edit on the line is a
touch. A link to a page that was not moved, a link to something outside the pages (a scheme, `repo:`, an image) and a
target that resolves nowhere are all touches (the last also fails the build: MkDocs checks the link).

The functions are pure: the caller says which paths are gone and whether a target resolves.
"""

from __future__ import annotations

import posixpath
import re
from collections.abc import Callable, Collection

#: An inline link, not an image: its text, its target and whatever follows the target inside the parentheses (a title).
_LINK = re.compile(r"(?<!!)\[(?P<text>[^\]\n]*)\]\((?P<target>[^)\s]*)(?P<tail>[^)\n]*)\)")
#: A unified diff's hunk header, for the new file's side.
_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,\d+)? @@")
#: A target with a scheme (`https:`, `repo:`, `mailto:`) leaves the pages.
_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
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


def _rewritten(old: str, new: str, folder: str, gone: Collection[str], resolves: Callable[[str, str], bool]) -> bool:
    """Whether the target `old` was rewritten to `new` because its page moved: the old page is in `gone` and the new
    target resolves."""
    before, after = _split(old, folder), _split(new, folder)
    if before is None or after is None:
        return False
    return before[0] in gone and resolves(*after)


def link_only_change(
    old: str, new: str, folder: str, gone: Collection[str], resolves: Callable[[str, str], bool]
) -> bool:
    """Whether the line `old` became `new` by rewriting link targets to moved pages and nothing else.

    `folder` is the folder of the page that holds the line (targets are relative to it), `gone` the repository-relative
    paths the change renamed or deleted, and `resolves(path, anchor)` whether `path` exists after the change and holds
    `anchor` (any, when it is empty)."""
    before, after = list(_LINK.finditer(old)), list(_LINK.finditer(new))
    if not before or len(before) != len(after) or _without_targets(old) != _without_targets(new):
        return False
    changed = [(a.group("target"), b.group("target")) for a, b in zip(before, after, strict=True)]
    changed = [pair for pair in changed if pair[0] != pair[1]]
    return bool(changed) and all(_rewritten(a, b, folder, gone, resolves) for a, b in changed)


def touched_lines(
    diff: str, folder: str, gone: Collection[str], resolves: Callable[[str, str], bool]
) -> frozenset[int]:
    """The lines of the new file that the zero-context `diff` of one file touches, a link rewrite to a moved page
    (`link_only_change`) left out. A hunk that removes and adds the same number of lines is read line by line; any other
    hunk touches all its added lines, and a pure deletion touches the line it sits at."""
    touched: set[int] = set()
    start = 0
    removed: list[str] = []
    added: list[str] = []
    opened = False

    def close() -> None:
        """Count the hunk read so far."""
        if not opened:
            return
        if not added:
            touched.add(start)
        elif len(removed) != len(added):
            touched.update(range(start, start + len(added)))
        else:
            touched.update(
                start + offset
                for offset, (was, now) in enumerate(zip(removed, added, strict=True))
                if not link_only_change(was, now, folder, gone, resolves)
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

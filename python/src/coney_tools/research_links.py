# SPDX-License-Identifier: GPL-3.0-or-later
"""Reading a research page the way MkDocs does: its headings, the anchors a link can name, and its relative links.

The function database (research_functions.py) points at sections of pages (`combat/grabs.md#grab`), and the tools that
check or move those pointers need the page's anchors without building the site. The rules are those of the `toc` and
`attr_list` extensions of mkdocs.yml: a heading's anchor is the `{#id}` it ends with, else its text's slug, numbered
when two headings share one; a `{#id}` anywhere else on a line is an anchor too.

Research: docs/guides/research-workflow.md#function-database
"""

from __future__ import annotations

import posixpath
import re
import unicodedata
from dataclasses import dataclass

_HEADING = re.compile(r"^(#{1,6})[ \t]+(.*?)[ \t]*#*[ \t]*$")
_ATTR_ID = re.compile(r"\{#([A-Za-z0-9_:.-]+)[^}]*\}")
_ATTR_ID_AT_END = re.compile(r"\s*\{#([A-Za-z0-9_:.-]+)[^}]*\}\s*$")
_FENCE = re.compile(r"^\s*(```|~~~)")
_LINK = re.compile(r"(\]\()([^)\s]+)(\))")
_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
_COUNTED = re.compile(r"^(.*)_([0-9]+)$")
#: The root every research-relative path is resolved against when links move between folders. Only its shape matters.
_ROOT = "/docs/research"


@dataclass(frozen=True)
class Heading:
    """One heading of a page: its level, its text as written, the anchor MkDocs gives it and its line (from 1)."""

    level: int
    text: str
    anchor: str
    line: int


def slug(text: str) -> str:
    """MkDocs's anchor for heading text: markup removed, ASCII letters, digits, `_` and `-`, lower case, hyphenated."""
    plain = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)
    plain = re.sub(r"[`*]", "", plain)
    plain = unicodedata.normalize("NFKD", plain).encode("ascii", "ignore").decode("ascii")
    plain = re.sub(r"[^\w\s-]", "", plain).strip().lower()
    return re.sub(r"[-\s]+", "-", plain)


def _unique(anchor: str, used: set[str]) -> str:
    """`anchor`, or the next free `anchor_1`, `anchor_2`, ... when the page already has it (python-markdown's rule)."""
    while anchor in used or not anchor:
        counted = _COUNTED.match(anchor)
        anchor = f"{counted.group(1)}_{int(counted.group(2)) + 1}" if counted else f"{anchor}_1"
    return anchor


def body_lines(markdown: str) -> list[tuple[int, str]]:
    """Every (line number from 1, text) of `markdown` that is not inside a fenced code block."""
    lines = []
    fenced = False
    for number, line in enumerate(markdown.splitlines(), start=1):
        if _FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            lines.append((number, line))
    return lines


def headings(markdown: str) -> list[Heading]:
    """The page's headings in order, each with its anchor."""
    found = []
    used: set[str] = set()
    for number, line in body_lines(markdown):
        match = _HEADING.match(line)
        if not match:
            continue
        text = match.group(2)
        explicit = _ATTR_ID_AT_END.search(text)
        if explicit:
            text = text[: explicit.start()]
            anchor = _unique(explicit.group(1), used)
        else:
            anchor = _unique(slug(text), used)
        used.add(anchor)
        found.append(Heading(len(match.group(1)), text, anchor, number))
    return found


def anchors(markdown: str) -> set[str]:
    """Every anchor a link to the page can name: its headings' and any `{#id}` written elsewhere on a line."""
    names = {h.anchor for h in headings(markdown)}
    for _number, line in body_lines(markdown):
        names.update(m.group(1) for m in _ATTR_ID.finditer(line))
    return names


def rebase_target(target: str, from_dir: str, to_dir: str, page: str) -> str:
    """A link target written on a page in `from_dir`, as it reads from `to_dir`.

    Both folders are below docs/research (`""` for docs/research itself), written with `/`. `page` is the file the
    target was written on, from the research root: a bare `#anchor` means that page. A target with a scheme (`https:`,
    `repo:`) is returned as it is.
    """
    if _SCHEME.match(target):
        return target
    path, hash_, fragment = target.partition("#")
    # A bare `#anchor` names the page itself, which `page` already gives from the research root.
    start = posixpath.join(_ROOT, from_dir, path) if path else posixpath.join(_ROOT, page)
    relative = posixpath.relpath(posixpath.normpath(start), posixpath.join(_ROOT, to_dir))
    return f"{relative}{hash_}{fragment}"


def rebase_links(text: str, from_dir: str, to_dir: str, page: str) -> str:
    """`text` with each Markdown link `](target)` rewritten by `rebase_target`, still pointing at the same file."""
    return _LINK.sub(lambda m: f"{m.group(1)}{rebase_target(m.group(2), from_dir, to_dir, page)}{m.group(3)}", text)

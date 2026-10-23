# SPDX-License-Identifier: GPL-3.0-or-later
"""Moving a research page's relative links between folders.

The function database (research_functions.py) keeps an entry's `role` with its links relative to docs/research, so a
role can be written into any system's generated list: the importer rewrites a link from the page's folder to the
research root, and the list page from the root to `research/<system>/`. A page's headings and anchors are read by
`docs_page`, the one Markdown reader `docs check` uses too.

Research: docs/guides/research-function-database.md
"""

from __future__ import annotations

import posixpath
import re

_LINK = re.compile(r"(\]\()([^)\s]+)(\))")
_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
#: The root every research-relative path is resolved against when links move between folders. Only its shape matters.
_ROOT = "/docs/research"


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

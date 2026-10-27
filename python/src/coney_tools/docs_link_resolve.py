# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether a page holds an anchor, in the checkout or as a commit held it: what the link-move exemptions ask.

`docs_check` (the links of a page) and `standards_check` (the research links of a comment) both judge a link rewrite by
whether its new target resolves and whether its old target held the anchor before the change (`docs_link_moves`).
"""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from coney_tools import local_gate
from coney_tools.docs_page import read_page


def page_text(root: Path, path: str, since: str | None) -> str | None:
    """The text of `path` in the checkout, or as the commit `since` held it, or None when it is no file there."""
    if since is None:
        file = root / path
        return file.read_text(encoding="utf-8", errors="replace") if file.is_file() else None
    code, text = local_gate.run_command(["git", "-C", str(root), "show", f"{since}:{path}"], root)
    return text if code == 0 else None


def link_resolver(root: Path, since: str | None = None) -> Callable[[str, str], bool]:
    """A test for "this page exists in the checkout and holds this anchor" (any, when the anchor is empty); each page is
    read once. With `since` (a commit) it reads the pages as that commit held them instead of the checkout."""
    anchors: dict[str, set[str] | None] = {}

    def resolves(path: str, anchor: str) -> bool:
        """Whether `path` exists and, for a Markdown page, holds `anchor`."""
        if path not in anchors:
            text = page_text(root, path, since)
            if text is None:
                anchors[path] = None
            elif path.endswith(".md"):
                anchors[path] = read_page(text).anchors
            else:
                anchors[path] = set()
        found = anchors[path]
        return found is not None and (not anchor or anchor in found)

    return resolves

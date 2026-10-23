# SPDX-License-Identifier: GPL-3.0-or-later
"""The Style rules of docs/guides/writing-docs.md that a program can see, on any checked page (`docs check`).

No machine paths, no wording that says when something is true instead of what is true, no repository or site address,
roles instead of people, and `repo:` links for files outside `docs/`. Every rule is a function of the page text; the
heuristics are deliberately forgiving (they flag what is surely a gap, not what might be one). The research standard
is `research_check`; `docs_check` runs both and applies the legacy file.
"""

from __future__ import annotations

import posixpath
import re
from collections.abc import Iterable

from coney_tools.docs_page import Finding, Page

__all__ = ["ORIGINAL_BUILD_PATHS", "check_style"]


#: Wording that says when something is true instead of saying what is true ("say what is true now").
STALE = re.compile(
    r"\bstill small\b|\bnot yet\b|\bonce (?:it|this|that|they) (?:exists?|is built|are built)\b"
    r"|\b(?:does|do)(?:n't| not) exist yet\b|\byet to be (?:written|built|done|implemented)\b",
    re.IGNORECASE,
)
#: On a guide or another page, "not yet" dates a claim when it says what the project has not done ("not yet
#: implemented"); "not yet looked at" (an issue's state) or "scenery not yet loaded" (the game) describe something else.
#: A research page tells them apart by section and by naming Coney instead (`_is_status`).
NOT_YET_WORK = re.compile(
    r"\bnot yet (?:been )?(?:implemented|written|built|done|ported|supported|listed|documented|wired|available|merged"
    r"|exposed|added|covered|tested|checked|known|understood|decided|in place)\b",
    re.IGNORECASE,
)
#: A path on somebody's machine: a user folder, or a drive-letter path that goes two folders deep. `H:\`, a file at a
#: drive's root, `C:\Program Files\...` (where a tool installs) and the original's build paths (`c:/Warriors/...`) are
#: not the writer's.
_USER_PATH = re.compile(
    r"(?i)\b[a-z]:[\\/]users[\\/](?!<)[^\\/\s<`\"']+|(?<![\w.:\\/])/(?:home|users)/(?!<)[^/\s<`\"']+"
)
_DRIVE_PATH = re.compile(r"(?<![\w])[A-Za-z]:[\\/](?P<first>[^\\/\s`'\"<>|*?:]+)[\\/]")
_DRIVE_OK = frozenset({"program", "windows", "users"})
#: The original's own build paths, which its executable carries as debug strings and the research pages quote.
ORIGINAL_BUILD_PATHS: tuple[str, ...] = ("c:/warriors", "d:/warriors", "/home/ack")
#: Text that mentions a phrase instead of using it: inline code and a short quotation.
_MENTION = re.compile(r"`[^`\n]*`|\"[^\"\n]{1,60}\"|\u201c[^\u201d\n]{1,60}\u201d")
_CODE = re.compile(r"`[^`\n]*`")
_LINK = re.compile(r"!?\[[^\]\n]*\]\(<?(?P<target>[^)\s>]+)")
_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
_REF_DEF = re.compile(r"^[ \t]{0,3}\[[^\]\n]+\]:[ \t]*<?(?P<target>[^\s>]+)")
#: The sections of a research page that say what Coney has, where "not yet" dates a claim.
_STATUS_SECTIONS = frozenset({"in coney", "coney's implementation", "open questions"})


def _without_mentions(text: str) -> str:
    """`text` without inline code, short quotations and URLs: a phrase there is mentioned, not used."""
    return re.sub(r"https?://\S+", " ", _MENTION.sub(" ", text))


def _machine_path(line: int, path: str) -> Iterable[Finding]:
    """A finding for `path` unless it is one of the original's build paths (`ORIGINAL_BUILD_PATHS`)."""
    if not path.casefold().replace(chr(92), "/").startswith(ORIGINAL_BUILD_PATHS):
        yield Finding(
            "machine-path", line, f"'{path}' is a path on a machine: write <workspace>, scratch/ or a relative path"
        )


def _line_sections(page: Page) -> list[str]:
    """For each line, the lower-case name of the level-2 section it sits in ('' above the first one)."""
    names: list[str] = []
    current = ""
    headings = {heading.line: heading for heading in page.headings}
    for line in page.lines:
        heading = headings.get(line.number)
        if heading is not None and heading.level <= 2:
            current = heading.text.replace(chr(0x2019), "'").casefold().strip("`*_ ") if heading.level == 2 else ""
        names.append(current)
    return names


def _is_status(section: str, text: str) -> bool:
    """Whether a line of a research page says what Coney has (so "not yet" dates it) and not what the original does
    ("a store that is not yet broken"): it sits in Coney's implementation or the open questions, or names Coney."""
    return section in _STATUS_SECTIONS or "coney" in text.casefold()


def _person_words(words: tuple[str, ...]) -> re.Pattern[str]:
    """The pattern for the avoided words in their person sense: "the owner", "the project owner", "repository owner".
    "An object's owner" and `owner/repo` are not people and stay."""
    alternatives = "|".join(re.escape(word) for word in words)
    return re.compile(
        rf"\b(?:the|project|repository|repo)\s+(?:{alternatives})(?:'s|s)?\b(?![/_])(?!\s+of\b)", re.IGNORECASE
    )


def check_style(
    path: str, page: Page, kind: str, avoid: tuple[str, ...], address_terms: tuple[str, ...]
) -> Iterable[Finding]:
    """The Style rules a program can see, line by line: `avoid` is the wording that names a person
    (`repo.wording.avoid`) and `address_terms` the strings that spell the repository's and the docs site's address."""
    folder = posixpath.dirname(path)
    sections = _line_sections(page)
    for index, line in enumerate(page.lines):
        text = line.text
        if not text.strip():
            continue
        lowered = text.casefold()
        for term in address_terms:
            if term in lowered:
                yield Finding(
                    "address", line.number, "names the repository's or the docs site's address; only the README may"
                )
                break
        for found in _USER_PATH.finditer(text):
            yield from _machine_path(line.number, found.group(0))
        for found in _DRIVE_PATH.finditer(text):
            if found.group("first").casefold() not in _DRIVE_OK:
                yield from _machine_path(line.number, found.group(0))
        if line.fenced:
            continue
        mentioned = _without_mentions(text)
        for stale in STALE.finditer(mentioned):
            if stale.group(0).casefold() == "not yet":
                if kind == "research" and not _is_status(sections[index], text):
                    continue
                if kind != "research" and not NOT_YET_WORK.match(mentioned, stale.start()):
                    continue
            yield Finding(
                "stale-wording",
                line.number,
                f"'{stale.group(0)}' says when it is true: say what is true now, or 'planned' in the page that owns it",
            )
        if kind != "research" and avoid:
            for person in _person_words(avoid).finditer(mentioned):
                yield Finding(
                    "person-wording",
                    line.number,
                    f"'{person.group(0)}' names a person: say 'the user' or 'a maintainer'",
                )
        for target in [*_LINK.finditer(_CODE.sub(" ", text)), *_REF_DEF.finditer(text)]:
            link = target.group("target")
            if _SCHEME.match(link) or link.startswith(("#", "/")):
                continue
            resolved = posixpath.normpath(posixpath.join(folder, link.split("#", 1)[0].split("?", 1)[0]))
            if link.split("#", 1)[0] and resolved != "docs" and not resolved.startswith("docs/"):
                yield Finding("repo-link", line.number, f"'{link}' leaves docs/: write repo:<path>")

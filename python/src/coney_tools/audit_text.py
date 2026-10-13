# SPDX-License-Identifier: GPL-3.0-or-later
"""Reading the text of an issue or a pull request for the audit: its sections, links, wording and checklist items.

Everything here works on strings, so the checks that use it are tested with made-up bodies and no network. GitHub
sends line ends as CRLF when a person edits a body in the browser; `clean` turns them into LF first.
"""

from __future__ import annotations

import re
from collections.abc import Collection, Iterable
from dataclasses import dataclass

_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
#: A second- or third-level heading; the forms write `###`, an issue filed from the command line often `##`.
_HEADING = re.compile(r"^(?P<hashes>#{2,3})[ \t]+(?P<name>\S.*?)[ \t]*#*[ \t]*$")
#: A fence line (``` or ~~~) that opens or closes a code block.
_FENCE = re.compile(r"^[ \t]*(```|~~~)")
#: What a list item starts with, which is not content (`- `, `- [ ] `, `* [x] `, a lone `-`). A marker is followed by
#: a space or ends the line, so a word that starts with a dash (the option `--play-level`, `-5`) is content.
LIST_MARKER = re.compile(r"^[-*+](?:[ \t]+\[[ xX]\][ \t]*|[ \t]+|$)")
#: What a form writes for a field the reporter left empty.
_NO_RESPONSE = "_no response_"
#: A fenced block or an inline code span.
_CODE = re.compile(r"^[ \t]*(?P<fence>```|~~~).*?^[ \t]*(?P=fence)[^\n]*$|`[^`\n]*`", re.DOTALL | re.MULTILINE)
#: A URL, which may hold any word.
_URL = re.compile(r"https?://\S+", re.IGNORECASE)
#: A Markdown link: its text and its target.
_LINK = re.compile(r"(?<!!)\[(?P<text>[^\]\n]*)\]\((?P<target>[^)\s]*)[^)\n]*\)")
#: A reference to an issue or pull request, `#123` (also what the audit looks for in issues).
NUMBER_REFERENCE = re.compile(r"(?<![\w&/])#\d+\b")
#: An unchecked task-list item, `- [ ] text`.
OPEN_ITEM = re.compile(r"^[ \t]*[-*+][ \t]+\[ \][ \t]*(?P<text>.*)$")


def clean(body: str) -> str:
    """`body` with LF line ends and without HTML comments (the forms and the template keep their guidance in them)."""
    return _COMMENT.sub("", body.replace("\r\n", "\n").replace("\r", "\n"))


@dataclass(frozen=True)
class Section:
    """The text under one `##` or `###` heading of an issue."""

    heading: str
    level: int
    text: str


def sections(body: str) -> list[Section]:
    """The sections of an issue body in order. A heading inside a fenced code block is not one; text before the first
    heading belongs to no section."""
    found: list[tuple[str, int, list[str]]] = []
    fenced = False
    for line in clean(body).splitlines():
        if _FENCE.match(line):
            fenced = not fenced
        heading = None if fenced else _HEADING.match(line)
        if heading:
            found.append((heading.group("name"), len(heading.group("hashes")), []))
        elif found:
            found[-1][2].append(line)
    return [Section(name, level, "\n".join(lines).strip()) for name, level, lines in found]


def find_section(found: Iterable[Section], heading: str) -> Section | None:
    """The first section whose heading is `heading` (case and surrounding space ignored), or None."""
    wanted = " ".join(heading.casefold().split())
    for section in found:
        if " ".join(section.heading.casefold().split()) == wanted:
            return section
    return None


def answer(text: str) -> str:
    """What a section really says: `text` without list markers, the form's `_No response_` and empty lines."""
    lines = []
    for line in text.splitlines():
        content = LIST_MARKER.sub("", line.strip()).strip()
        if content and content.casefold() != _NO_RESPONSE:
            lines.append(content)
    return "\n".join(lines)


def is_placeholder(text: str, placeholders: Collection[str]) -> bool:
    """Whether a section says nothing: it is empty, or all it holds is a placeholder such as `x` (a trailing full stop,
    case and surrounding space ignored)."""
    said = answer(text).strip().casefold()
    if not said:
        return True
    wanted = {item.casefold() for item in placeholders}
    return said in wanted or said.rstrip(".").strip() in wanted


def prose(text: str) -> str:
    """`text` without comments, code, URLs and link targets: what a person reads as words."""
    plain = _CODE.sub(" ", clean(text))
    plain = _URL.sub(" ", plain)
    return _LINK.sub(lambda match: match.group("text"), plain)


def word_uses(text: str, words: Iterable[str]) -> list[str]:
    """Each use of one of `words` (as a whole word, plural or possessive too) in the prose of `text`, with a few words
    around it. A name like `owner/repo`, `{owner}` or `owner_name` is not a use."""
    alternatives = "|".join(re.escape(word) for word in words)
    pattern = re.compile(rf"(?<![\w/{{.$-])\b(?:{alternatives})(?:'s|s)?\b(?![/}}_])", re.IGNORECASE)
    plain = prose(text)
    found = []
    for match in pattern.finditer(plain):
        start = max(0, match.start() - 25)
        snippet = " ".join(plain[start : match.end() + 25].split())
        found.append(f"...{snippet}...")
    return found


def unchecked_items(body: str) -> list[str]:
    """The text of each unchecked task-list item (`- [ ] text`) outside code, a wrapped line joined to its item."""
    items: list[list[str]] = []
    fenced = False
    continuing = False
    for line in clean(body).splitlines():
        if _FENCE.match(line):
            fenced = not fenced
            continuing = False
            continue
        if fenced:
            continue
        item = OPEN_ITEM.match(line)
        stripped = line.strip()
        if item:
            items.append([item.group("text").strip()])
            continuing = True
        elif continuing and stripped and line[:1] in (" ", "\t") and not LIST_MARKER.match(stripped):
            items[-1].append(stripped)
        else:
            continuing = False
    texts = [" ".join(parts) for parts in items]
    return [text for text in texts if text]


def cites_number(text: str) -> bool:
    """Whether `text` names an issue or pull request (`#123`)."""
    return NUMBER_REFERENCE.search(text) is not None


# --- links and paths ----------------------------------------------------------------------------------------------

#: A link target that works from an issue page and points into the repository: `../blob/main/<path>[#anchor]`.
_REPO_LINK = re.compile(r"^\.\./(?:blob|tree)/(?P<ref>[^/]+)/(?P<path>[^#]+)(?:#(?P<anchor>.+))?$")
#: A link that names a repository path directly, which breaks on an issue page.
_RELATIVE_DOC_LINK = re.compile(r"^(?:\./)?(?:docs|research)/")
#: A documentation or research file written out: `docs/guides/x.md`, `research/missions.yaml`.
_DOC_PATH = re.compile(
    r"(?<![\w/.-])(?:docs|research)/[\w./-]*[\w-]\.(?:md|ya?ml|toml|json|csv|py|cpp|h|txt|sh)\b(?:#[\w{}-]+)?"
)
_EXPLICIT_ANCHOR = re.compile(r"\{#(?P<id>[\w-]+)\}")
_ATX_HEADING = re.compile(r"^#{1,6}[ \t]+(?P<name>.+?)[ \t]*$", re.MULTILINE)


@dataclass(frozen=True)
class RepoLink:
    """A link into the repository: the page, the ref it is on and the anchor (empty when none)."""

    path: str
    ref: str
    anchor: str


def repo_links(text: str) -> list[RepoLink]:
    """The `../blob/<ref>/<path>#anchor` links of `text` (outside code)."""
    found = []
    for match in _LINK.finditer(_CODE.sub(" ", clean(text))):
        link = _REPO_LINK.match(match.group("target"))
        if link:
            found.append(RepoLink(link.group("path"), link.group("ref"), link.group("anchor") or ""))
    return found


def broken_relative_links(text: str) -> list[str]:
    """Link targets that name a docs or research path directly (`docs/x.md`), which an issue page cannot open."""
    return [
        match.group("target")
        for match in _LINK.finditer(_CODE.sub(" ", clean(text)))
        if _RELATIVE_DOC_LINK.match(match.group("target"))
    ]


def plain_doc_paths(text: str) -> list[str]:
    """The docs and research file paths `text` writes out as plain words or in code instead of as a link.

    A path that is only the visible text of a link (`[docs/guides/x.md](../blob/main/docs/guides/x.md)`) is a link;
    code fences are examples and are left alone.
    """
    body = clean(text)
    without_fences = re.sub(r"^[ \t]*(```|~~~).*?^[ \t]*\1[^\n]*$", " ", body, flags=re.DOTALL | re.MULTILINE)
    without_links = _LINK.sub(" ", without_fences)
    found = []
    for match in _DOC_PATH.finditer(without_links):
        if match.group(0) not in found:
            found.append(match.group(0))
    return found


def anchors_of(markdown: str) -> frozenset[str]:
    """The anchors a Markdown page offers: each `{#id}` and the slug of each heading (lower case, punctuation dropped,
    spaces as hyphens), as the docs site and GitHub make them."""
    found = {match.group("id") for match in _EXPLICIT_ANCHOR.finditer(markdown)}
    for heading in _ATX_HEADING.finditer(markdown):
        name = _EXPLICIT_ANCHOR.sub("", heading.group("name")).strip().casefold()
        found.add(re.sub(r"\s", "-", re.sub(r"[^\w\s-]", "", name)))
    return frozenset(found)


# --- statements about pull requests ---------------------------------------------------------------------------------

#: `#55 (open)`, `#55 (merged in #60)`: a number with a state in parentheses right after it.
_STATE_CLAIM = re.compile(r"#(?P<number>\d+)\s*\((?P<state>open|merged|closed)\b", re.IGNORECASE)


def state_claims(text: str) -> list[tuple[int, str]]:
    """Each `#N (open)` / `#N (merged ...)` / `#N (closed ...)` of `text` as (number, claimed state, lower case)."""
    return [
        (int(match.group("number")), match.group("state").casefold()) for match in _STATE_CLAIM.finditer(prose(text))
    ]

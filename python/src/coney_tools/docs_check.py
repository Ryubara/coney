# SPDX-License-Identifier: GPL-3.0-or-later
"""The page standards of docs/guides/writing-docs.md, checked by program: `coney-tools docs check`.

A page is held to the rules of its kind (`kind_of`): a research page keeps the subsystem skeleton, a "Verified against"
line and an evidence level beside every address it cites; every hand-written page keeps the Style rules that a program
can see (no machine paths, no time-bound wording, `repo:` links, roles not people, no repository or site address). The
check runs on the pages a change touches and on those only, so the standard ratchets: whoever touches a page brings the
whole page up to it, and untouched pages wait until someone does.

Everything here is a function of the page text, so the tests use made-up pages. The heuristics are deliberately
forgiving (they flag what is surely a gap, not what might be one); each is described where it is implemented and in
docs/guides/coney-tools.md#docs.
"""

from __future__ import annotations

import posixpath
import re
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import local_gate
from coney_tools.audit_files import repository_address_terms
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions

#: The kinds of file `kind_of` tells apart. Only the first three are checked.
CHECKED_KINDS: tuple[str, ...] = ("research", "guide", "page")
#: Lines above which a page gets a length warning (never a failure). A reader takes a page in whole before changing
#: it, and the Read tool shows 2,000 lines at a time; a research page past 1,500 lines (the top sixth of the pages
#: today, median 530) no longer fits one sitting with room for the code beside it, and a guide past 800 (the top
#: tenth, median 260) is several guides. A warning, because splitting a page is the author's call: the cut is usually
#: along a sub-subject that deserves its own page.
LENGTH_WARNING: dict[str, int] = {"research": 1500, "guide": 800}
#: The sections of a subsystem page, in order. `names` are lower case; a heading counts when it equals one of them or
#: starts with one followed by a space, colon or dash (so "Data layout" is the Data section).
SKELETON: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("Purpose", ("purpose",)),
    ("Original structure", ("original structure",)),
    ("Data", ("data",)),
    ("Behaviour", ("behaviour", "behavior")),
    ("Coney's implementation", ("coney's implementation",)),
    ("Open questions", ("open questions",)),
)
#: The sections that state no behaviour of the original, so an address in them needs no evidence level: what the page
#: is for, where Coney's code lives (an `@orig` tag cites an address without claiming anything) and the questions.
UNGRADED_SECTIONS = frozenset({"purpose", "coney's implementation", "open questions"})
#: An address in the executable, written as the conventions say: `0x` and eight hex digits (`0x00149160`). A field
#: offset, a mask or a flag value is written shorter.
ADDRESS = re.compile(r"(?<![\w])0x[0-9a-fA-F]{8}\b")
#: A sentence in the opening of a page that grades every claim at once ("every claim is confirmed (code) at the cited
#: address unless it says otherwise"); the page then needs no level beside each address.
PAGE_LEVEL = re.compile(
    r"\b(?:every|all|each)\b[^.]{0,120}\b(?:confirmed|inferred|speculative)\b"
    r"|\b(?:confirmed|inferred|speculative)\b[^.]{0,120}\bunless\b",
    re.IGNORECASE,
)
#: A word that grades a claim (research-workflow.md#evidence-levels), or the label `Evidence:`.
LEVEL = re.compile(r"\b(?:confirmed|inferred|speculative)\b|\bevidence\s*:", re.IGNORECASE)
#: A table column that holds the level of each row.
LEVEL_COLUMN = re.compile(r"(?:evidence(?: level)?|confidence)", re.IGNORECASE)
#: Wording that says when something is true instead of saying what is true ("say what is true now").
STALE = re.compile(
    r"\bstill small\b|\bnot yet\b|\bonce (?:it|this|that|they) (?:exists?|is built|are built)\b"
    r"|\b(?:does|do)(?:n't| not) exist yet\b|\byet to be (?:written|built|done|implemented)\b",
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
_FENCE = re.compile(r"^[ \t]*(```|~~~)")
_HEADING = re.compile(r"^(?P<hashes>#{1,6})[ \t]+(?P<text>\S.*?)[ \t]*#*[ \t]*$")
_ANCHOR = re.compile(r"[ \t]*\{#[^}]*\}[ \t]*$")
_ITEM = re.compile(r"^[ \t]*(?:[-*+]|\d+[.)])[ \t]+")
_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
_VERIFIED = re.compile(r"^[ \t>*_]*verified against\b[ \t*_]*:?[ \t*_]*(?P<rest>.*)$", re.IGNORECASE)


@dataclass(frozen=True)
class Finding:
    """One thing a page gets wrong: the rule, the line (1-based) and what to do. `severity` is `error` or `warning`."""

    rule: str
    line: int
    message: str
    severity: str = "error"


@dataclass(frozen=True)
class Context:
    """What the rules take from the repository: the words that must not name a person (`repo.wording.avoid`) and the
    lower-case strings that spell the repository's and the docs site's address."""

    avoid: tuple[str, ...] = ()
    address_terms: tuple[str, ...] = ()


@dataclass
class Result:
    """The outcome of checking a set of pages: the pages in order, and the findings of those that have any."""

    pages: list[str] = field(default_factory=list)
    findings: dict[str, list[Finding]] = field(default_factory=dict)

    @property
    def failed(self) -> dict[str, list[Finding]]:
        """The pages with at least one error, with all their findings (warnings too)."""
        return {page: found for page, found in self.findings.items() if any(item.severity == "error" for item in found)}

    def by_kind(self) -> dict[str, tuple[int, int]]:
        """For each kind of page checked: how many pass (no error) and how many there are."""
        failed = self.failed
        totals: dict[str, list[int]] = {}
        for page in self.pages:
            counts = totals.setdefault(kind_of(page), [0, 0])
            counts[1] += 1
            counts[0] += page not in failed
        return {kind: (counts[0], counts[1]) for kind, counts in totals.items()}

    def rule_counts(self) -> Counter[str]:
        """For each rule, the number of pages with an error of that rule."""
        counts: Counter[str] = Counter()
        for found in self.findings.values():
            counts.update({item.rule for item in found if item.severity == "error"})
        return counts


def kind_of(path: str) -> str:
    """The kind of file at `path` (forward slashes, relative to the repository): `research`, `guide`, `page` (any other
    hand-written page of the site), `generated` (rebuilt from its source, so never edited or checked) or `other`."""
    if not path.endswith(".md") or not path.startswith("docs/"):
        return "other"
    if path.startswith(("docs/missions/", "docs/progress/")):
        return "generated"
    if path.startswith("docs/references/"):
        return "page" if path == "docs/references/entities.md" else "generated"
    if path.startswith("docs/research/"):
        return "research"
    if path.startswith("docs/guides/"):
        return "guide"
    return "page"


# --- reading a page ---------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class _Line:
    """One line of the page: its number, its text and whether it is inside a fenced code block (fence lines too)."""

    number: int
    text: str
    fenced: bool


@dataclass(frozen=True)
class _Heading:
    """A heading outside code: its line, level, text without the `{#anchor}`, and the index of the next block."""

    line: int
    level: int
    text: str


@dataclass
class _Block:
    """A paragraph, a list item, a table row or a heading, which is the unit an evidence level covers."""

    kind: str  # "heading", "text" or "row"
    line: int
    text: str
    table: int | None = None  # the table a row belongs to
    section: int = -1  # index of the nearest heading before it
    offsets: list[tuple[int, int]] = field(default_factory=list)  # (offset in text, line number) of each source line


@dataclass
class _Table:
    """A run of table rows: whether its header has a level column, and the first and last block index."""

    graded: bool
    first: int
    last: int


@dataclass
class _Page:
    """A page read into lines, headings and blocks."""

    lines: list[_Line]
    headings: list[_Heading]
    blocks: list[_Block]
    tables: list[_Table]
    front_matter: int  # lines of front matter at the top


def _blank_comments(text: str) -> str:
    """`text` with every HTML comment replaced by blank lines of the same count, so line numbers hold."""
    return _COMMENT.sub(lambda match: "\n" * match.group(0).count("\n"), text)


def _read(text: str) -> _Page:
    """Split `text` into lines (comments and front matter blanked), headings and blocks."""
    raw = _blank_comments(text.replace("\r\n", "\n").replace("\r", "\n")).split("\n")
    front = 0
    if raw and raw[0].strip() == "---":
        for index in range(1, len(raw)):
            if raw[index].strip() == "---":
                front = index + 1
                break
    lines: list[_Line] = []
    fenced = False
    for number, content in enumerate(raw, start=1):
        if number <= front:
            lines.append(_Line(number, "", False))
            continue
        if _FENCE.match(content):
            lines.append(_Line(number, content, True))
            fenced = not fenced
            continue
        lines.append(_Line(number, content, fenced))
    page = _Page(lines, [], [], [], front)
    _split_blocks(page)
    return page


def _has_level_column(header: str) -> bool:
    """Whether a table header row has a cell headed Evidence, Evidence level or Confidence. A bare "Level" is not one:
    it is as likely a game level."""
    return any(LEVEL_COLUMN.fullmatch(cell.strip(" *`")) for cell in header.strip().strip("|").split("|"))


def _split_blocks(page: _Page) -> None:
    """Fill `page.headings`, `page.blocks` and `page.tables` from its lines."""
    current: _Block | None = None
    open_table: _Table | None = None
    header_seen = False

    def close() -> None:
        nonlocal current
        if current is not None:
            page.blocks.append(current)
            current = None

    for line in page.lines:
        content = line.text
        if line.fenced or not content.strip():
            close()
            open_table = None
            continue
        section = len(page.headings) - 1
        heading = _HEADING.match(content)
        if heading:
            close()
            open_table = None
            title = _ANCHOR.sub("", heading.group("text"))
            page.headings.append(_Heading(line.number, len(heading.group("hashes")), title))
            page.blocks.append(_Block("heading", line.number, title, section=len(page.headings) - 1))
            continue
        if content.lstrip().startswith("|"):
            close()
            cells = content.strip()
            if open_table is None:
                open_table = _Table(_has_level_column(cells), len(page.blocks), len(page.blocks))
                page.tables.append(open_table)
                header_seen = False
                page.blocks.append(_Block("row", line.number, cells, len(page.tables) - 1, section))
                continue
            if not header_seen:
                header_seen = True
                if re.fullmatch(r"[|\s:-]+", cells):
                    continue
            page.blocks.append(_Block("row", line.number, cells, len(page.tables) - 1, section))
            open_table.last = len(page.blocks) - 1
            continue
        open_table = None
        if _ITEM.match(content):
            close()
        if current is None:
            current = _Block("text", line.number, content.strip(), section=section, offsets=[(0, line.number)])
        else:
            current.offsets.append((len(current.text) + 1, line.number))
            current.text += " " + content.strip()
    close()
    # A table whose header row was the only one read still needs its last index.
    for table in page.tables:
        table.last = max(table.last, table.first)


# --- the skeleton -----------------------------------------------------------------------------------------------------


def _name(heading: str) -> str:
    """A heading's text as the skeleton compares it: lower case, curly apostrophes straight, markup trimmed."""
    return heading.replace(chr(0x2019), "'").casefold().strip("`*_ ")


def _skeleton_index(headings: list[_Heading], names: tuple[str, ...]) -> int | None:
    """The index in `headings` of the first level-2 heading that is one of `names` (or starts with one, then a space,
    colon or dash); None when there is none."""
    for index, heading in enumerate(headings):
        if heading.level != 2:
            continue
        text = _name(heading.text)
        if any(text == name or re.match(rf"{re.escape(name)}[\s:\u2013\u2014-]", text) for name in names):
            return index
    return None


def _section_text(page: _Page, index: int) -> str:
    """The text under heading `index` up to the next heading of the same or a higher level (subsections included)."""
    start = page.headings[index]
    end = len(page.lines) + 1
    for later in page.headings[index + 1 :]:
        if later.level <= start.level:
            end = later.line
            break
    return "\n".join(line.text for line in page.lines if start.line < line.number < end).strip()


def _check_skeleton(page: _Page) -> Iterable[Finding]:
    """The title, the six sections in order, none of them empty, and the "Verified against" line."""
    if not page.headings or page.headings[0].level != 1:
        yield Finding("skeleton", 1, "the page has no `# Title` before its first section")
    positions: list[int] = []
    for name, aliases in SKELETON:
        index = _skeleton_index(page.headings, aliases)
        if index is None:
            yield Finding(
                "skeleton",
                1,
                f"missing the section '{name}' (a section with nothing known says so in one line, "
                "writing-docs.md#subsystem-page)",
            )
            continue
        positions.append(index)
        if not _section_text(page, index):
            yield Finding(
                "skeleton",
                page.headings[index].line,
                f"the section '{name}' is empty: say in one line that nothing is known",
            )
    if positions != sorted(positions):
        yield Finding(
            "skeleton",
            page.headings[min(positions)].line,
            "the skeleton sections are out of order; the order is " + ", ".join(name for name, _ in SKELETON),
        )
    yield from _check_verified(page)


def _check_verified(page: _Page) -> Iterable[Finding]:
    """A "Verified against: ..." line before the first level-2 heading, saying what the page was checked against."""
    first_section = next((heading.line for heading in page.headings if heading.level == 2), len(page.lines) + 1)
    for index, line in enumerate(page.lines):
        if line.number >= first_section:
            break
        found = _VERIFIED.match(line.text) if not line.fenced else None
        if not found:
            continue
        following = page.lines[index + 1].text if index + 1 < len(page.lines) else ""
        if found.group("rest").strip() or (following.strip() and line.number + 1 < first_section):
            return
    yield Finding(
        "verified",
        1,
        "no 'Verified against: ...' line before the first section (the executable and, for runtime claims, the PCSX2 "
        "version)",
    )


# --- evidence levels --------------------------------------------------------------------------------------------------


def _section_name(page: _Page, index: int) -> str:
    """The lower-case name of the level-2 section that heading `index` sits in ('' above the first one)."""
    for position in range(index, -1, -1):
        heading = page.headings[position]
        if heading.level == 1:
            return ""
        if heading.level == 2:
            return _name(heading.text)
    return ""


def _lead_in(page: _Page, index: int) -> str:
    """The first block under heading `index`, when it is a statement and not itself a claim: not a heading and
    citing no address ('' otherwise). "Everything here is confirmed (code)" is such a line."""
    heading_line = page.headings[index].line
    for block in page.blocks:
        if block.line > heading_line:
            return "" if block.kind == "heading" or ADDRESS.search(block.text) else block.text
    return ""


def _enclosing(page: _Page, index: int) -> list[int]:
    """The headings a block under heading `index` sits in, nearest first: that heading and each shallower one before
    it, up to the level-2 section (the title is left out)."""
    chain = [index]
    level = page.headings[index].level
    for position in range(index - 1, -1, -1):
        heading = page.headings[position]
        if heading.level < level and heading.level >= 2:
            chain.append(position)
            level = heading.level
    return chain


def _has_page_level(page: _Page) -> bool:
    """Whether the opening of the page (above its first level-2 heading) grades every claim at once."""
    first = next((heading.line for heading in page.headings if heading.level == 2), len(page.lines) + 1)
    opening = " ".join(line.text.strip() for line in page.lines if line.number < first and not line.fenced)
    return bool(PAGE_LEVEL.search(opening))


def _address_line(block: _Block, found: re.Match[str]) -> int:
    """The line of the source file that holds the address `found` in `block`."""
    line = block.line
    for offset, number in block.offsets:
        if offset <= found.start():
            line = number
    return line


def _check_evidence(page: _Page) -> Iterable[Finding]:
    """Every block that cites an address and states behaviour has an evidence level nearby.

    "Nearby" is: the block itself, the block before or after it, the heading it sits under or any heading above that
    up to its level-2 section, or the first block under such a heading when that is a statement and not a claim (a
    lead-in such as "everything here is confirmed (code) unless it says otherwise"). A table row is a block of its
    own; a row of a table with a level column must hold the level itself, otherwise the table is covered like a
    paragraph. Not checked: a page whose opening grades every claim at once (`PAGE_LEVEL`), the sections that grade
    nothing (`UNGRADED_SECTIONS`), the part above the first level-2 heading and code blocks. One finding per block, for
    its first address.
    """
    if _has_page_level(page):
        return
    blocks = page.blocks
    for index, block in enumerate(blocks):
        if block.kind == "heading" or block.section < 0 or _is_header_row(page, index):
            continue
        address = ADDRESS.search(block.text)
        name = _section_name(page, block.section)
        if not address or not name or name in UNGRADED_SECTIONS or LEVEL.search(block.text):
            continue
        line = _address_line(block, address) if block.kind == "text" else block.line
        if block.kind == "row" and block.table is not None and page.tables[block.table].graded:
            yield Finding("evidence", line, f"the row cites {address.group(0)} but its level column is empty")
            continue
        around = [
            blocks[other].text for other in (_outer(page, index, -1), _outer(page, index, +1)) if other is not None
        ]
        for heading in _enclosing(page, block.section):
            around += [page.headings[heading].text, _lead_in(page, heading)]
        if any(LEVEL.search(text) for text in around):
            continue
        yield Finding(
            "evidence",
            line,
            f"{address.group(0)} is cited with no evidence level nearby (confirmed (code), confirmed (runtime), "
            "inferred or speculative; research-workflow.md#evidence-levels)",
        )


def _is_header_row(page: _Page, index: int) -> bool:
    """Whether block `index` is the first row of its table (the header, which labels columns and claims nothing)."""
    block = page.blocks[index]
    return block.kind == "row" and block.table is not None and page.tables[block.table].first == index


def _outer(page: _Page, index: int, step: int) -> int | None:
    """The block before (`step` -1) or after (+1) the block at `index`; for a table row, the block outside the table."""
    block = page.blocks[index]
    position = index + step
    if block.kind == "row" and block.table is not None:
        table = page.tables[block.table]
        position = table.first - 1 if step < 0 else table.last + 1
    if 0 <= position < len(page.blocks):
        return position
    return None


# --- style ------------------------------------------------------------------------------------------------------------


def _without_mentions(text: str) -> str:
    """`text` without inline code, short quotations and URLs: a phrase there is mentioned, not used."""
    return re.sub(r"https?://\S+", " ", _MENTION.sub(" ", text))


def _machine_path(line: int, path: str) -> Iterable[Finding]:
    """A finding for `path` unless it is one of the original's build paths (`ORIGINAL_BUILD_PATHS`)."""
    if not path.casefold().replace(chr(92), "/").startswith(ORIGINAL_BUILD_PATHS):
        yield Finding(
            "machine-path", line, f"'{path}' is a path on a machine: write <workspace>, scratch/ or a relative path"
        )


def _line_sections(page: _Page) -> list[str]:
    """For each line, the lower-case name of the level-2 section it sits in ('' above the first one)."""
    names: list[str] = []
    current = ""
    headings = {heading.line: heading for heading in page.headings}
    for line in page.lines:
        heading = headings.get(line.number)
        if heading is not None and heading.level <= 2:
            current = _name(heading.text) if heading.level == 2 else ""
        names.append(current)
    return names


def _is_status(section: str, text: str) -> bool:
    """Whether a line of a research page says what Coney has (so "not yet" dates it) and not what the original does
    ("a store that is not yet broken"): it sits in Coney's implementation or the open questions, or names Coney."""
    return section in ("coney's implementation", "open questions") or "coney" in text.casefold()


def _person_words(words: tuple[str, ...]) -> re.Pattern[str]:
    """The pattern for the avoided words in their person sense: "the owner", "the project owner", "repository owner".
    "An object's owner" and `owner/repo` are not people and stay."""
    alternatives = "|".join(re.escape(word) for word in words)
    return re.compile(rf"\b(?:the|project|repository|repo)\s+(?:{alternatives})(?:'s|s)?\b(?![/_])", re.IGNORECASE)


def _check_style(path: str, page: _Page, kind: str, context: Context) -> Iterable[Finding]:
    """The Style rules a program can see, line by line."""
    folder = posixpath.dirname(path)
    sections = _line_sections(page)
    for index, line in enumerate(page.lines):
        text = line.text
        if not text.strip():
            continue
        lowered = text.casefold()
        for term in context.address_terms:
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
            if kind == "research" and stale.group(0).casefold() == "not yet" and not _is_status(sections[index], text):
                continue
            yield Finding(
                "stale-wording",
                line.number,
                f"'{stale.group(0)}' says when it is true: say what is true now, or 'planned' in the page that owns it",
            )
        if kind != "research" and context.avoid:
            for person in _person_words(context.avoid).finditer(mentioned):
                yield Finding(
                    "person-wording",
                    line.number,
                    f"'{person.group(0)}' names a person: say 'the user' or 'a maintainer'",
                )
        for target in _LINK.finditer(_CODE.sub(" ", text)):
            link = target.group("target")
            if _SCHEME.match(link) or link.startswith(("#", "/")):
                continue
            resolved = posixpath.normpath(posixpath.join(folder, link.split("#", 1)[0].split("?", 1)[0]))
            if link.split("#", 1)[0] and not resolved.startswith("docs/"):
                yield Finding("repo-link", line.number, f"'{link}' leaves docs/: write repo:<path>")


def _check_length(kind: str, text: str) -> Iterable[Finding]:
    """A warning when the page is longer than `LENGTH_WARNING` allows for its kind."""
    limit = LENGTH_WARNING.get(kind)
    count = len(text.splitlines())
    if limit and count > limit:
        yield Finding(
            "length",
            1,
            f"{count} lines is over the {limit} of a {kind} page: split off the sub-subject that has its own page",
            "warning",
        )


# --- checking ---------------------------------------------------------------------------------------------------------


def check_page(path: str, text: str, context: Context | None = None) -> list[Finding]:
    """Every finding for the page at `path` (relative to the repository, forward slashes) with content `text`, by line.
    A file that is not a checked page (`kind_of`) has none."""
    kind = kind_of(path)
    if kind not in CHECKED_KINDS:
        return []
    context = context or Context()
    page = _read(text)
    found: list[Finding] = []
    if kind == "research":
        found.extend(_check_skeleton(page))
        found.extend(_check_evidence(page))
    found.extend(_check_style(path, page, kind, context))
    found.extend(_check_length(kind, text))
    return sorted(found, key=lambda item: item.line)


def context_for(root: Path) -> Context:
    """The `Context` of the checkout at `root`: the wording to avoid and the address of the repository."""
    conventions = load_conventions(root)
    return Context(conventions.repo.wording.avoid, tuple(repository_address_terms(root)))


def check_files(root: Path, names: Iterable[str], context: Context) -> Result:
    """Check each of `names` (repository-relative, forward slashes) that is a checked page and exists."""
    result = Result()
    for name in names:
        if kind_of(name) not in CHECKED_KINDS or not (root / name).is_file():
            continue
        result.pages.append(name)
        found = check_page(name, (root / name).read_text(encoding="utf-8", errors="replace"), context)
        if found:
            result.findings[name] = found
    return result


def check_changed(root: Path, base: str, context: Context) -> Result:
    """Check the pages changed since the branch left `base`, committed or not, untracked pages included."""
    return check_files(root, local_gate.changed_files(root, base), context)


def check_paths(root: Path, paths: Iterable[str], context: Context) -> Result:
    """Check the named files, and for a folder every Markdown page under it, whatever has changed."""
    names: list[str] = []
    for path in paths:
        target = Path(path)
        target = (target if target.is_absolute() else root / target).resolve()
        top = root.resolve()
        if top != target and top not in target.parents:
            raise ConfigError(f"{path} is outside the checkout")
        if target.is_dir():
            names.extend(sorted(item.relative_to(top).as_posix() for item in target.rglob("*.md")))
        else:
            names.append(target.relative_to(top).as_posix())
    return check_files(root, names, context)

# SPDX-License-Identifier: GPL-3.0-or-later
"""A Markdown page read into lines, headings and blocks: what `docs_check` and `research_check` look at.

The unit every rule works on is the *block*: a paragraph, a list item, a table row or a heading. Fenced code and HTML
comments never reach a block (a comment is blanked to empty lines of the same count, so line numbers hold), and
the YAML front matter at the top is kept apart as text.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

_FENCE = re.compile(r"^[ \t]*(?P<marker>`{3,}|~{3,})")
_HEADING = re.compile(r"^(?P<hashes>#{1,6})[ \t]+(?P<text>\S.*?)[ \t]*#*[ \t]*$")
#: A heading's trailing `{#anchor}` (attribute lists may carry classes too: `{#id .class}`).
_HEADING_ATTRIBUTES = re.compile(r"[ \t]*\{(?P<attributes>[^}]*#[^}]*)\}[ \t]*$")
#: An anchor written anywhere in a line, as a heading or a bold lead of a list item takes it: `{#q-counter}`.
ANCHOR = re.compile(r"\{#(?P<id>[A-Za-z0-9_.:-]+)[^}]*\}")
_ITEM = re.compile(r"^[ \t]*(?:[-*+]|\d+[.)])[ \t]+")
_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
#: A table header cell that labels the level of each row.
LEVEL_COLUMN = re.compile(r"(?:evidence(?: level)?|confidence)", re.IGNORECASE)


def kind_of(path: str) -> str:
    """The kind of file at `path` (forward slashes, relative to the repository): `research`, `guide`, `page` (any other
    hand-written page of the site), `generated` (rebuilt from its source, so never edited or checked) or `other`."""
    if not path.endswith(".md") or not path.startswith("docs/"):
        return "other"
    if path.startswith(("docs/missions/", "docs/progress/", "docs/research/find/")):
        return "generated"
    if path.startswith("docs/references/"):
        return "page" if path == "docs/references/entities.md" else "generated"
    if path.startswith("docs/research/"):
        # The landing page is not a page of a kind; a system's function list is rebuilt from research/functions/.
        if path == "docs/research/index.md":
            return "page"
        return "generated" if path.endswith("/functions.md") else "research"
    if path.startswith("docs/guides/"):
        return "guide"
    return "page"


@dataclass(frozen=True)
class Finding:
    """One thing a page gets wrong: the rule, the line (1-based) and what to do. `severity` is `error` or `warning`.
    `by_section` marks a finding that grades a block (an address with no level): a legacy page owes that only in the
    sections a change touches, where every other finding is owed page-wide (writing-docs.md#legacy-pages)."""

    rule: str
    line: int
    message: str
    severity: str = "error"
    by_section: bool = False


@dataclass(frozen=True)
class Line:
    """One line of the page: its number, its text and whether it is inside a fenced code block (fence lines too)."""

    number: int
    text: str
    fenced: bool


@dataclass(frozen=True)
class Heading:
    """A heading outside code: its line, level, text without the `{#anchor}`, and that anchor ('' for none)."""

    line: int
    level: int
    text: str
    anchor: str = ""


@dataclass
class Block:
    """A paragraph, a list item, a table row or a heading, which is the unit an evidence level covers."""

    kind: str  # "heading", "text" or "row"
    line: int
    text: str
    table: int | None = None  # the table a row belongs to
    section: int = -1  # index of the nearest heading before it
    offsets: list[tuple[int, int]] = field(default_factory=list)  # (offset in text, line number) of each source line
    item: bool = False  # a "text" block that opened with a list marker


@dataclass
class Table:
    """A run of table rows: the column that holds each row's level (None for no such column), first and last block."""

    level_column: int | None
    first: int
    last: int

    @property
    def graded(self) -> bool:
        """Whether the header has a column for the level of each row."""
        return self.level_column is not None


@dataclass
class Page:
    """A page read into lines, headings and blocks."""

    lines: list[Line]
    headings: list[Heading]
    blocks: list[Block]
    tables: list[Table]
    front_matter: int  # lines of front matter at the top, fences included (0 for none)
    front_text: str = ""  # the YAML between the fences
    line_count: int = 0  # the lines of the page, as an editor counts them (a final newline starts no line)

    @property
    def body_lines(self) -> int:
        """The number of lines after the front matter."""
        return self.line_count - self.front_matter

    @property
    def anchors(self) -> set[str]:
        """Every `{#id}` written outside code, and the generated ids of the headings (as MkDocs slugs them)."""
        found = {heading.anchor for heading in self.headings if heading.anchor}
        found.update(slug(heading.text) for heading in self.headings)
        for line in self.lines:
            if not line.fenced:
                found.update(match.group("id") for match in ANCHOR.finditer(line.text))
        return found


def slug(text: str) -> str:
    """The id MkDocs gives a heading without an explicit one: lower case, words joined by hyphens."""
    words = re.sub(r"[^\w\s-]", "", re.sub(r"`", "", text).casefold())
    return re.sub(r"[-\s]+", "-", words).strip("-")


def blank_comments(text: str) -> str:
    """`text` with every HTML comment replaced by blank lines of the same count, so line numbers hold."""
    return _COMMENT.sub(lambda match: "\n" * match.group(0).count("\n"), text)


def read_page(text: str) -> Page:
    """Split `text` into lines (comments and front matter blanked), headings and blocks."""
    raw = blank_comments(text.replace("\r\n", "\n").replace("\r", "\n")).split("\n")
    front = 0
    front_text = ""
    if raw and raw[0].strip() == "---":
        for index in range(1, len(raw)):
            if raw[index].strip() == "---":
                front = index + 1
                front_text = "\n".join(raw[1:index])
                break
    lines: list[Line] = []
    opener = ""  # the fence that opened the code block we are in ("" outside one)
    for number, content in enumerate(raw, start=1):
        if number <= front:
            lines.append(Line(number, "", False))
            continue
        fence = _FENCE.match(content)
        if fence:
            marker = fence.group("marker")
            # A block closes only on a fence of the same character, at least as long, with nothing after it, so a
            # four-backtick block can show a three-backtick one.
            if not opener:
                opener = marker
            elif marker[0] == opener[0] and len(marker) >= len(opener) and not content.strip()[len(marker) :].strip():
                opener = ""
            lines.append(Line(number, content, True))
            continue
        lines.append(Line(number, content, bool(opener)))
    page = Page(lines, [], [], [], front, front_text, len(text.splitlines()))
    _split_blocks(page)
    return page


def split_cells(row: str) -> list[str]:
    """The cells of a table row, without the outer pipes. A pipe inside inline code or after a backslash is not a
    separator."""
    cells: list[str] = []
    current: list[str] = []
    in_code = False
    previous = ""
    for char in row.strip():
        if char == "`":
            in_code = not in_code
        if char == "|" and not in_code and previous != "\\":
            cells.append("".join(current))
            current = []
        else:
            current.append(char)
        previous = char
    cells.append("".join(current))
    if cells and not cells[0].strip():
        cells = cells[1:]
    if cells and not cells[-1].strip():
        cells = cells[:-1]
    return [cell.strip() for cell in cells]


def level_column(header: str) -> int | None:
    """The index of the cell of a table header row headed Evidence, Evidence level or Confidence; None when there is
    none. A bare "Level" is not one: it is as likely a game level."""
    for index, cell in enumerate(split_cells(header)):
        if LEVEL_COLUMN.fullmatch(cell.strip(" *`")):
            return index
    return None


def _split_blocks(page: Page) -> None:
    """Fill `page.headings`, `page.blocks` and `page.tables` from its lines."""
    current: Block | None = None
    open_table: Table | None = None
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
            text = heading.group("text")
            attributes = _HEADING_ATTRIBUTES.search(text)
            anchor = ""
            if attributes:
                found = ANCHOR.search("{" + attributes.group("attributes") + "}")
                anchor = found.group("id") if found else ""
                text = text[: attributes.start()]
            page.headings.append(Heading(line.number, len(heading.group("hashes")), text, anchor))
            page.blocks.append(Block("heading", line.number, text, section=len(page.headings) - 1))
            continue
        if content.lstrip().startswith("|"):
            close()
            cells = content.strip()
            if open_table is None:
                open_table = Table(level_column(cells), len(page.blocks), len(page.blocks))
                page.tables.append(open_table)
                header_seen = False
                page.blocks.append(Block("row", line.number, cells, len(page.tables) - 1, section))
                continue
            if not header_seen:
                header_seen = True
                if re.fullmatch(r"[|\s:-]+", cells):
                    continue
            page.blocks.append(Block("row", line.number, cells, len(page.tables) - 1, section))
            open_table.last = len(page.blocks) - 1
            continue
        open_table = None
        if _ITEM.match(content):
            close()
        if current is None:
            current = Block(
                "text",
                line.number,
                content.strip(),
                section=section,
                offsets=[(0, line.number)],
                item=bool(_ITEM.match(content)),
            )
        else:
            current.offsets.append((len(current.text) + 1, line.number))
            current.text += " " + content.strip()
    close()
    # A table whose header row was the only one read still needs its last index.
    for table in page.tables:
        table.last = max(table.last, table.first)


def heading_name(heading: str) -> str:
    """A heading's text as sections are compared: lower case, curly apostrophes straight, markup trimmed."""
    return heading.replace(chr(0x2019), "'").casefold().strip("`*_ ")


def section_text(page: Page, index: int) -> str:
    """The text under heading `index` up to the next heading of the same or a higher level (subsections included)."""
    start = page.headings[index]
    end = len(page.lines) + 1
    for later in page.headings[index + 1 :]:
        if later.level <= start.level:
            end = later.line
            break
    return "\n".join(line.text for line in page.lines if start.line < line.number < end).strip()


def address_line(block: Block, offset: int) -> int:
    """The line of the source file that holds character `offset` of `block`'s text."""
    line = block.line
    for start, number in block.offsets:
        if start <= offset:
            line = number
    return line

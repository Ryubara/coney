# SPDX-License-Identifier: GPL-3.0-or-later
"""The page of how Coney goes beyond the original, held to the code: part of `coney-tools docs check`.

The page (`standards.beyond.page` in .github/conventions.yml, `docs/engine/beyond-the-original.md`) lists every
deliberate difference from the PS2 build in tables, one section for each kind of difference. The lifted limits are the
one kind the code itself marks: a doc comment with `@coney-only limit:`
(docs/guides/conventions.md#original-limits). So that a new lift cannot land without its entry, and an entry cannot
outlive its lift, the check reads the markers from the sources and the entries from the page and compares them; there
is no list of markers to keep apart from the code.

A marker is keyed by the file and the name the comment documents (`src/core/crowd_limits.h:scanTokens`): the name of the
declaration under the comment, or of the one on the same line, or `(file)` for a marker above no declaration. An entry
names its markers in its row of the lifted limits table, in the marker column, in code spans of that form.

Everything here is a function of the text it is given, so the tests use made-up sources and pages
(`python/tests/test_docs_beyond.py`); `check_root` reads a checkout. The rules are all one rule, `beyond`, and a finding
is reported under the file the author has to edit: the source for a marker with no entry, the page for the rest.
"""

from __future__ import annotations

import re
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.conventions import BeyondPage
from coney_tools.docs_page import Finding, Heading, Page, heading_ids, read_page

__all__ = ["Marker", "check", "check_root", "markers_in", "scan_markers", "split_row"]

#: What a marker's name is when no declaration sits under the comment: the whole file is the lift.
WHOLE_FILE = "(file)"
#: The lines under a comment that are read for the declaration it documents.
_DECLARATION_LINES = 8
#: A line, or the rest of one, that is a comment: the start of `//`, `/*` or the middle of a block comment.
_COMMENT_START = ("//", "/*", "*")
#: What precedes a marker that opens a comment: any code, then `//`, `///`, `/*` or `/**` and blanks.
_OPENER = re.compile(r"^(?P<code>.*?)(?://+|/\*+)[!<]?\s*$")
#: What precedes a marker on a continuation line of a block comment: blanks, `*`, blanks.
_CONTINUATION = re.compile(r"^\s*\*+\s*$")
#: An operator's name in a declaration: `operator==(`, `operator()(`, `operator bool(`.
_OPERATOR = re.compile(r"\boperator\s*(\(\)|[^\s(\w]+|\w+)\s*\(")
#: A `path:Symbol` in a code span: how a table cell names a marker. The symbol is a name, `operator` and its sign,
#: or `(file)`, and may end `#2`, `#3` for the second and third marker of one name in a file (overloads).
_KEY = re.compile(r"`(?P<path>[\w./+-]+):(?P<symbol>(?:\(file\)|operator[^`\s]+|[A-Za-z_]\w*)(?:#\d+)?)`")
#: The separator row of a table.
_SEPARATOR = re.compile(r"^\|?[ \t]*:?-{2,}:?[ \t]*(?:\|[ \t]*:?-{2,}:?[ \t]*)*\|?[ \t]*$")
#: The last word of a declaration's head, the name it declares.
_LAST_WORD = re.compile(r"([A-Za-z_]\w*)\s*$")
#: A C++ attribute, `[[nodiscard]]`.
_ATTRIBUTE = re.compile(r"\[\[[^\]]*\]\]")


@dataclass(frozen=True)
class Marker:
    """One `@coney-only limit:` marker in the sources: the file, the line of the marker and the declared name."""

    path: str
    line: int
    symbol: str

    @property
    def key(self) -> str:
        """The key the page's entry names it by: `path:Symbol`."""
        return f"{self.path}:{self.symbol}"


# --- reading the markers from the sources ---------------------------------------------------------------------------


def _strip_templates(text: str) -> str:
    """`text` without its leading `template <...>` headers (nested angle brackets counted), which sit above the
    declaration the comment documents but do not name it."""
    text = text.lstrip()
    while text.startswith("template"):
        start = text.find("<")
        if start < 0:
            return text
        depth = 0
        for index in range(start, len(text)):
            depth += text[index] == "<"
            depth -= text[index] == ">"
            if depth == 0:
                text = text[index + 1 :].lstrip()
                break
        else:
            return text
    return text


def _declared_name(head: str) -> str:
    """The name that the declaration beginning `head` declares: the last word before its parameters, value, body or base
    list. `WHOLE_FILE` when it is no declaration (a preprocessor line, a namespace or nothing)."""
    text = _ATTRIBUTE.sub(" ", _strip_templates(head)).strip()
    if not text or text.startswith("#") or re.match(r"namespace\b", text):
        return WHOLE_FILE
    end = len(text)
    for index, char in enumerate(text):
        # A single colon starts a base list or an enum's type; `::` is a scope.
        if char in "({=;" or (char == ":" and text[index - 1 : index] != ":" and text[index + 1 : index + 2] != ":"):
            end = index
            break
    operator = _OPERATOR.search(text)
    if operator:
        return f"operator{operator.group(1)}"
    word = _LAST_WORD.search(text[:end])
    return word.group(1) if word else WHOLE_FILE


def _opening(before: str) -> str | None:
    """The code that precedes a marker whose text `before` ends in a comment opener (`//`, `///`, `/*`, `/**`, or the
    `*` that continues a block comment), or None when the marker is not the first words of a comment: a comment that
    only mentions it in prose, a string or code never counts."""
    if _CONTINUATION.match(before):
        return ""
    opener = _OPENER.match(before)
    return opener.group("code") if opener else None


def markers_in(path: str, text: str, marker: str) -> list[Marker]:
    """Every `marker` that begins a comment of the source `text` at `path`, with the name its comment documents: the
    declaration on the marker's own line when code precedes the comment (`// ...` or `/* ... */` after it), else the one
    that follows the comment block. A second marker of one name in the file is `name#2`, a third `name#3`, so overloads
    have keys of their own."""
    lines = text.splitlines()
    found: list[Marker] = []
    seen: dict[str, int] = {}
    for index, line in enumerate(lines):
        at = line.find(marker)
        if at < 0:
            continue
        code = _opening(line[:at])
        if code is None:
            continue
        if code.strip():
            symbol = _declared_name(code)
        else:
            end = index
            while end + 1 < len(lines) and lines[end + 1].lstrip().startswith(_COMMENT_START):
                end += 1
            head = " ".join(item.strip() for item in lines[end + 1 : end + 1 + _DECLARATION_LINES])
            symbol = _declared_name(head)
        seen[symbol] = seen.get(symbol, 0) + 1
        found.append(Marker(path, index + 1, symbol if seen[symbol] == 1 else f"{symbol}#{seen[symbol]}"))
    return found


def scan_markers(root: Path, folders: Sequence[str], suffixes: Sequence[str], marker: str) -> list[Marker]:
    """The markers of every source under `folders` of the checkout `root` that ends in one of `suffixes`, by path."""
    found: list[Marker] = []
    for folder in folders:
        base = root / folder
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in suffixes or not path.is_file():
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            if marker in text:
                found.extend(markers_in(path.relative_to(root).as_posix(), text, marker))
    return sorted(found, key=lambda item: (item.path, item.line))


# --- reading the page -----------------------------------------------------------------------------------------------


def split_row(line: str) -> list[str]:
    """The cells of a table row, trimmed, without the outer pipes; a pipe inside a code span stays in its cell."""
    cells: list[str] = []
    current = ""
    in_code = False
    for char in line.strip():
        if char == "`":
            in_code = not in_code
        if char == "|" and not in_code:
            cells.append(current.strip())
            current = ""
        else:
            current += char
    cells.append(current.strip())
    if cells and cells[0] == "":
        cells.pop(0)
    if cells and cells[-1] == "":
        cells.pop()
    return cells


@dataclass(frozen=True)
class _Row:
    """One entry of a table: its line, its cells by lower-cased column name and its number of cells."""

    line: int
    cells: dict[str, str]
    count: int


@dataclass(frozen=True)
class _Table:
    """The first table of a section: the line of its header, its header cells and its rows."""

    line: int
    header: list[str]
    rows: list[_Row]


def _section_span(page: Page, ids: list[str], anchor: str) -> tuple[Heading, int] | None:
    """The heading whose id is `anchor` and the line where its section ends (the next heading of its level or above,
    else the end of the page), or None when the page has no such heading."""
    for index, heading in enumerate(page.headings):
        if ids[index] != anchor:
            continue
        end = page.line_count + 1
        for later in page.headings[index + 1 :]:
            if later.level <= heading.level:
                end = later.line
                break
        return heading, end
    return None


def _first_table(page: Page, start: int, end: int) -> _Table | None:
    """The first table between lines `start` and `end` of `page` (outside code), or None."""
    lines = [line for line in page.lines if start <= line.number < end]
    for position, line in enumerate(lines):
        follows = lines[position + 1] if position + 1 < len(lines) else None
        if line.fenced or not line.text.lstrip().startswith("|") or follows is None:
            continue
        if follows.fenced or not _SEPARATOR.match(follows.text.strip()):
            continue
        header = split_row(line.text)
        names = [cell.casefold() for cell in header]
        rows: list[_Row] = []
        for item in lines[position + 2 :]:
            if item.fenced or not item.text.lstrip().startswith("|"):
                break
            cells = split_row(item.text)
            rows.append(_Row(item.number, dict(zip(names, cells, strict=False)), len(cells)))
        return _Table(line.number, header, rows)
    return None


def _keys(cell: str) -> list[str]:
    """The `path:Symbol` keys written in code spans in `cell`."""
    return [f"{match.group('path')}:{match.group('symbol')}" for match in _KEY.finditer(cell)]


def _status_ok(status: str, words: Sequence[str]) -> bool:
    """Whether `status` starts with one of `words`, ignoring case and any emphasis marks."""
    plain = status.lstrip("*_ ").casefold()
    return any(plain.startswith(word.casefold()) for word in words)


# --- the rules ------------------------------------------------------------------------------------------------------


def _section_findings(page: Page, ids: list[str], beyond: BeyondPage, anchor: str, add: _Add) -> _Table | None:
    """The structure rules for the section `anchor`: it exists, has a table with the columns, and every row has the
    cells and a status of the allowed words. Returns the table when there is one to read."""
    span = _section_span(page, ids, anchor)
    if span is None:
        add(1, f"the page has no section with the id `{anchor}` (a heading ending `{{#{anchor}}}`)")
        return None
    heading, end = span
    table = _first_table(page, heading.line, end)
    if table is None:
        add(heading.line, f"the section `{anchor}` has no table: list its entries as rows of one")
        return None
    names = {cell.casefold() for cell in table.header}
    required = [*beyond.columns, *([beyond.marker_column] if anchor == beyond.limits_section else [])]
    for column in required:
        if column.casefold() not in names:
            add(table.line, f"the table of `{anchor}` has no column `{column}`")
    if any(column.casefold() not in names for column in required):
        return None
    status = beyond.status_column.casefold()
    for row in table.rows:
        if row.count != len(table.header):
            add(row.line, f"the row has {row.count} cells where the header has {len(table.header)}")
            continue
        if not _status_ok(row.cells[status], beyond.statuses):
            words = " or ".join(f"`{word}`" for word in beyond.statuses)
            add(row.line, f"the status '{row.cells[status]}' starts with neither {words}")
    return table


class _Add:
    """A finding collector that files each finding under one path (the page's)."""

    def __init__(self, found: dict[str, list[Finding]], path: str) -> None:
        self._found = found
        self._path = path

    def __call__(self, line: int, message: str) -> None:
        """Record an error at `line` of the page."""
        self._found.setdefault(self._path, []).append(Finding("beyond", line, message))


def check(page_path: str, text: str | None, markers: Sequence[Marker], beyond: BeyondPage) -> dict[str, list[Finding]]:
    """Every finding of the page at `page_path` (its `text`, None when the file is missing) against the `markers` of
    the sources, by the file to edit. A checkout with no markers and no page has none."""
    found: dict[str, list[Finding]] = {}
    if text is None:
        if markers:
            found[page_path] = [
                Finding(
                    "beyond",
                    1,
                    f"{page_path} is missing, and the sources have {len(markers)} `{beyond.marker}` marker(s) "
                    "that need entries on it",
                )
            ]
        return found
    page = read_page(text)
    ids = heading_ids(page)
    add = _Add(found, page_path)
    tables = {anchor: _section_findings(page, ids, beyond, anchor, add) for anchor in beyond.sections}
    limits = tables.get(beyond.limits_section)
    if limits is None:
        return found
    named: dict[str, int] = {}
    column = beyond.marker_column.casefold()
    status = beyond.status_column.casefold()
    existing = {marker.key for marker in markers}
    for row in limits.rows:
        if column not in row.cells or status not in row.cells:
            continue
        keys = _keys(row.cells[column])
        planned = row.cells[status].lstrip("*_ ").casefold().startswith("planned")
        if not keys and not planned and _status_ok(row.cells[status], beyond.statuses):
            add(
                row.line,
                f"the entry is on main in the lifted limits table with no marker named: put the `path:Symbol` of its "
                f"`{beyond.marker}` marker in the {beyond.marker_column} column",
            )
        for key in keys:
            if key in named:
                add(row.line, f"`{key}` is named twice (also by line {named[key]}): one entry for each marker")
                continue
            named[key] = row.line
            if key not in existing:
                add(
                    row.line,
                    f"`{key}` names no marker in the sources: the lift is gone or renamed, so take the entry out or "
                    "give it the marker's new name",
                )
            elif planned:
                add(
                    row.line,
                    f"`{key}` is planned but its marker exists: the lift is on main, so change the status to "
                    f"{beyond.statuses[0]}",
                )
    for marker in markers:
        if marker.key not in named:
            found.setdefault(marker.path, []).append(
                Finding(
                    "beyond",
                    marker.line,
                    f"the `{beyond.marker}` marker of `{marker.key}` has no entry: add a row to the lifted limits "
                    f"table of {page_path} with `{marker.key}` in its {beyond.marker_column} column "
                    "(docs/guides/conventions.md#original-limits)",
                )
            )
    return found


def check_root(
    root: Path, beyond: BeyondPage, folders: Sequence[str], suffixes: Sequence[str]
) -> dict[str, list[Finding]]:
    """`check` for the checkout at `root`: the page as the checkout holds it against every marker under `folders`."""
    page = root / beyond.page
    text = page.read_text(encoding="utf-8", errors="replace") if page.is_file() else None
    markers = scan_markers(root, folders, suffixes, beyond.marker)
    found = check(beyond.page, text, markers, beyond)
    for findings in found.values():
        findings.sort(key=lambda item: item.line)
    return found

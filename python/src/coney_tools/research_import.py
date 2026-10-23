# SPDX-License-Identifier: GPL-3.0-or-later
"""Importing a research page's hand-written code-index tables into the function database.

A page's code index is a table of `Address | Name | Role | Evidence` (the columns vary a little from page to page). This
module turns the rows into entries mechanically, so moving a page's index into `research/functions/<system>.toml`
costs a tool run and a review, not a rewrite:

* the address cell holds one address (a shorter one is padded to eight digits and reported);
* the name is the code span of the name cell, without a signature or a tag (`Foo(a, b)`, `Foo (tagged)`), which are
  reported as dropped;
* the evidence cell gives the level; a cell that says more than the level keeps its whole text as the entry's note and
  is reported as mixed, because the level then names only its first claim;
* the section the entry points at is the heading above its table, and links in the role are rewritten relative to
  docs/research, where the database keeps them (research_links.rebase_links).

A row that cannot be imported is skipped with its line and the reason, never guessed. Tables that are not function lists
(no address and name columns) are ignored; one that has them but no Evidence column is reported, since the level is
required. `merge` adds the rows to a system's existing file without overwriting what a person edited.

Research: docs/guides/research-function-database.md
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field, replace
from pathlib import Path

from coney_tools import research_functions as rf
from coney_tools import research_links
from coney_tools.config import ConfigError
from coney_tools.docs_page import heading_ids, read_page, split_cells

#: Column headings (lower case) that mean each part of a row.
_ADDRESS_HEADS = frozenset({"address", "addr"})
_NAME_HEADS = frozenset({"name", "function"})
_EVIDENCE_HEADS = frozenset({"evidence"})
#: The columns that say what the function does, and the one (`role`) that is a short tag when another says it.
_TEXT_HEADS = frozenset({"role", "what it does", "does", "description", "purpose"})
_KIND_HEAD = "role"

_HEX = re.compile(r"0x([0-9a-fA-F]+)")
_CODE_SPAN = re.compile(r"`([^`]+)`")
_SEPARATOR_CELL = re.compile(r"^:?-+:?$")
_SIGNATURE = re.compile(r"^(?P<name>[A-Za-z_~][A-Za-z0-9_:~]*)\s*(?P<rest>\(.*)$")
_LEVEL_PATTERNS = (
    ("confirmed-code", re.compile(r"confirmed\s*\(\s*code\s*\)|confirmed\s+from\s+code", re.IGNORECASE)),
    ("confirmed-runtime", re.compile(r"confirmed\s*\(\s*runtime\s*\)|confirmed\s+from\s+runtime", re.IGNORECASE)),
    ("inferred", re.compile(r"\binferred\b", re.IGNORECASE)),
    ("speculative", re.compile(r"\bspeculative\b", re.IGNORECASE)),
    ("not-needed", re.compile(r"\bnot\s+needed\b", re.IGNORECASE)),
)
_BARE_PUNCTUATION = " \t.;:,"


class ResearchImportError(ConfigError):
    """The import cannot start (an unknown section, an unreadable page); the message says why."""


@dataclass
class ImportResult:
    """What a page gave: the entries, the rows left out with their reasons, and what a reviewer should look at."""

    entries: list[rf.Entry] = field(default_factory=list)
    skipped: list[str] = field(default_factory=list)  # `source:line: reason`
    notes: list[str] = field(default_factory=list)  # `source:line: name: what was done`


@dataclass
class _Table:
    """A Markdown table of a page: its header cells, its rows with their lines, and the headings above it."""

    line: int
    header: list[str]
    rows: list[tuple[int, list[str]]]
    chain: tuple[str, ...]  # the anchors of the headings above it, outermost first
    anchor: str  # the nearest heading's anchor, or "" before the first heading


@dataclass
class _Columns:
    """Which table column holds what (an index into a row), -1 when the table has none."""

    address: int = -1
    name: int = -1
    evidence: int = -1
    role: int = -1
    kind: int = -1
    unknown: list[str] = field(default_factory=list)


def _split_row(line: str) -> list[str]:
    """The cells of a table row (`docs_page.split_cells`), with `\\|` read back as `|`."""
    return [cell.replace("\\|", "|") for cell in split_cells(line)]


def _tables(markdown: str) -> list[_Table]:
    """Every table of the page outside code fences, each with the anchors of the headings above it."""
    page = read_page(markdown)
    headings = {h.line: (h.level, anchor) for h, anchor in zip(page.headings, heading_ids(page), strict=True)}
    stack: list[tuple[int, str]] = []  # (level, anchor) of the headings above the current line, outermost first
    tables: list[_Table] = []
    block: list[tuple[int, str]] = []

    def close() -> None:
        # A table is a header row, a separator row of dashes, then the body rows.
        if len(block) >= 2:
            header = _split_row(block[0][1])
            separator = _split_row(block[1][1])
            if separator and all(_SEPARATOR_CELL.match(c) for c in separator):
                rows = [(number, _split_row(text)) for number, text in block[2:]]
                chain = tuple(anchor for _level, anchor in stack)
                tables.append(_Table(block[0][0], header, rows, chain, stack[-1][1] if stack else ""))
        block.clear()

    for line in page.lines:
        if line.fenced:
            close()
            continue
        if line.text.lstrip().startswith("|"):
            if block and block[-1][0] != line.number - 1:
                close()
            block.append((line.number, line.text))
            continue
        close()
        heading = headings.get(line.number)
        if heading is not None:
            while stack and stack[-1][0] >= heading[0]:
                stack.pop()
            stack.append(heading)
    close()
    return tables


def _columns(header: list[str]) -> _Columns:
    """Map a table's headings to the parts of a row. `unknown` lists the headings that mean nothing to the import."""
    columns = _Columns()
    texts: list[tuple[int, str]] = []
    for index, cell in enumerate(header):
        head = cell.strip().lower()
        if head in _ADDRESS_HEADS and columns.address < 0:
            columns.address = index
        elif head in _NAME_HEADS and columns.name < 0:
            columns.name = index
        elif head in _EVIDENCE_HEADS and columns.evidence < 0:
            columns.evidence = index
        elif head in _TEXT_HEADS:
            texts.append((index, head))
        else:
            columns.unknown.append(cell)
    # A short Role column beside a longer one is the kind; alone it is the role.
    if len(texts) == 1:
        columns.role = texts[0][0]
    elif len(texts) == 2 and any(head == _KIND_HEAD for _index, head in texts):
        columns.kind = next(i for i, head in texts if head == _KIND_HEAD)
        columns.role = next(i for i, head in texts if head != _KIND_HEAD)
    else:
        columns.unknown.extend(header[i] for i, _head in texts)
    return columns


def _addresses(cell: str) -> list[tuple[int, bool]] | str:
    """The addresses of an address cell, each with whether it was padded, or the reason the cell has none.

    A cell may list several (`0x1 / 0x2`, `0x1, 0x2`): a row that two or three functions share.
    """
    found = _HEX.findall(cell)
    rest = _HEX.sub("", cell).replace("`", "")
    rest = re.sub(r"\band\b|[/,&\s]", "", rest)
    if not found or rest:
        return "the address cell does not hold addresses only"
    result = []
    for digits in found:
        if not 6 <= len(digits) <= 8:
            return f"0x{digits} is not an address (six to eight hex digits)"
        result.append((int(digits, 16), len(digits) < 8))
    return result


def _name(cell: str) -> tuple[str, str] | str:
    """The function's name from a name cell and what the cell wrote besides it, or the reason there is no name."""
    span = _CODE_SPAN.search(cell)
    text = span.group(1) if span else cell.strip()
    outside = (cell[: span.start()] + cell[span.end() :]).strip() if span else ""
    return _bare_name(text, outside)


def _bare_name(text: str, outside: str) -> tuple[str, str] | str:
    """`text` (the inside of a code span) as a name, with a signature after it moved to what was dropped."""
    if rf.FUNCTION_NAME.match(text):
        return text, outside
    signature = _SIGNATURE.match(text)
    if signature and rf.FUNCTION_NAME.match(signature["name"]):
        return signature["name"], f"{signature['rest']} {outside}".strip()
    return f"{text!r} is not a Ghidra name (letters, digits, `_` and `::`)"


def _names(cell: str, count: int) -> list[tuple[str, str]] | str:
    """The names of a name cell that shares a row between `count` functions, one code span each, in order."""
    if count == 1:
        one = _name(cell)
        return [one] if isinstance(one, tuple) else one
    spans = _CODE_SPAN.findall(cell)
    if len(spans) != count:
        return f"{count} addresses but {len(spans)} names in the name cell"
    names = [_bare_name(span, "") for span in spans]
    for name in names:
        if isinstance(name, str):
            return name
    return [name for name in names if isinstance(name, tuple)]


def _level(cell: str) -> tuple[str, str, str] | None:
    """The level an evidence cell gives, the whole cell when it says more than the level, and a `not needed` reason.

    None when the cell names no level. The first level named is the entry's: `confirmed (code); the rock inferred` is
    confirmed-code with the whole cell as its note.
    """
    best: tuple[int, int, str] | None = None
    for key, pattern in _LEVEL_PATTERNS:
        match = pattern.search(cell)
        if match and (best is None or match.start() < best[0]):
            best = (match.start(), match.end(), key)
    if best is None:
        return None
    start, end, key = best
    if key == "not-needed":
        reason = cell[end:].strip(_BARE_PUNCTUATION)
        return key, "", reason
    rest = (cell[:start] + cell[end:]).strip(_BARE_PUNCTUATION)
    return key, cell.strip().rstrip(".") if rest else "", ""


def _page_fields(entry: rf.Entry) -> rf.Entry:
    """`entry` without the fields a page row never has (`coney`, `status`, `gaps`), to compare it with a row."""
    return replace(entry, coney="", status="", gaps=())


def _entry_problem(entry: rf.Entry) -> str:
    """Why the schema would refuse `entry`, or an empty string when it is valid."""
    system = rf.SystemFunctions("import", Path("import.toml"), "", (entry,))
    try:
        rf.parse_system("import", rf.dumps(system), Path("import.toml"))
    except rf.ResearchFunctionsError as error:
        return str(error).splitlines()[0].split(": ", 2)[-1]
    return ""


def _add(
    result: ImportResult, seen: dict[int, rf.Entry], entry: rf.Entry, row: str, padded: bool, dropped: str
) -> None:
    """Add one entry from the row `row` to `result`, or skip it with the reason: invalid, or an address seen before."""
    if problem := _entry_problem(entry):
        result.skipped.append(f"{row}: {entry.name}: {problem}")
        return
    if entry.address in seen:
        if seen[entry.address] != entry:
            result.skipped.append(f"{row}: {entry.name}: listed again and differs from the first row; kept the first")
        return
    seen[entry.address] = entry
    result.entries.append(entry)
    if padded:
        result.notes.append(f"{row}: {entry.name}: address padded to {entry.address:#010x}")
    if dropped:
        result.notes.append(f"{row}: {entry.name}: dropped {dropped!r} from the name cell")
    if entry.note and entry.level != "not-needed":
        result.notes.append(
            f"{row}: {entry.name}: mixed or qualified evidence ({entry.note!r}); the level is the first one named, "
            "the whole cell is the note"
        )


def import_tables(markdown: str, page: str, *, source: str, section: str = "") -> ImportResult:
    """The entries of the function tables of a page.

    `page` is the page's path below docs/research (`combat/grabs.md`), which the entries point at; `source` names it in
    messages (`docs/research/combat/grabs.md`); `section` limits the import to the tables under that heading's anchor
    (its subheadings included). Raises ResearchImportError for a section the page does not have.
    """
    if section and section not in read_page(markdown).anchors:
        raise ResearchImportError(f"{source}: the page has no section {section!r}")
    result = ImportResult()
    seen: dict[int, rf.Entry] = {}
    page_dir = page.rpartition("/")[0]
    for table in _tables(markdown):
        if section and section not in table.chain:
            continue
        columns = _columns(table.header)
        if columns.address < 0 or columns.name < 0:
            continue
        where = f"{source}:{table.line}"
        if columns.evidence < 0:
            result.skipped.append(f"{where}: a table of functions with no Evidence column, so no level to import")
            continue
        if columns.unknown:
            result.skipped.append(
                f"{where}: a table with columns the import does not know ({', '.join(columns.unknown)})"
            )
            continue
        target = f"{page}#{table.anchor}" if table.anchor else page
        for number, cells in table.rows:
            row = f"{source}:{number}"
            cells = cells + [""] * (len(table.header) - len(cells))
            addresses = _addresses(cells[columns.address])
            if isinstance(addresses, str):
                result.skipped.append(f"{row}: {addresses}")
                continue
            names = _names(cells[columns.name], len(addresses))
            if isinstance(names, str):
                result.skipped.append(f"{row}: {names}")
                continue
            level = _level(cells[columns.evidence])
            if level is None:
                result.skipped.append(
                    f"{row}: {', '.join(n for n, _ in names)}: the evidence cell names no evidence level"
                )
                continue
            key, note, reason = level
            role = research_links.rebase_links(cells[columns.role], page_dir, "", page) if columns.role >= 0 else ""
            kind = cells[columns.kind] if columns.kind >= 0 else ""
            if key == "not-needed":
                if not role and not reason:
                    result.skipped.append(f"{row}: {names[0][0]}: not needed gives no reason (the role should)")
                    continue
                if role and reason:
                    note = f"not needed: {reason}"
                else:
                    role = role or reason
            if len(addresses) > 1:
                result.notes.append(f"{row}: one row of {len(addresses)} functions split; each has the row's role")
            for (value, padded), (function, dropped) in zip(addresses, names, strict=True):
                _add(
                    result,
                    seen,
                    rf.Entry(value, function, target, key, role=role, kind=kind, note=note),
                    row,
                    padded,
                    dropped,
                )
    return result


# --- merging ------------------------------------------------------------------------------------------------------


@dataclass
class MergeResult:
    """A system file with imported entries merged in, and what happened to each row."""

    system: rf.SystemFunctions
    added: int = 0
    unchanged: int = 0
    replaced: int = 0
    kept: list[str] = field(default_factory=list)  # existing entries that differ from the page's row
    foreign: list[str] = field(default_factory=list)  # rows whose address another system file owns


def merge(
    existing: rf.SystemFunctions, imported: list[rf.Entry], owners: dict[int, str], *, replace_existing: bool
) -> MergeResult:
    """`imported` merged into `existing`.

    An address already in the file stays as it is when the row differs (a person may have improved it), unless
    `replace_existing`, which takes the page's row but keeps the entry's `coney`, `status` and `gaps` (not on the
    page). `owners` maps an address to the other system file that lists it: such a row is left out, one owner per
    function.
    """
    entries = {e.address: e for e in existing.entries}
    result = MergeResult(existing)
    for row in imported:
        label = f"{row.address:#010x} {row.name}"
        if row.address in owners:
            result.foreign.append(f"{label}: already in {owners[row.address]}")
        elif row.address not in entries:
            entries[row.address] = row
            result.added += 1
        elif _page_fields(entries[row.address]) == _page_fields(row):
            result.unchanged += 1
        elif replace_existing:
            old = entries[row.address]
            entries[row.address] = replace(row, coney=old.coney, status=old.status, gaps=old.gaps)
            result.replaced += 1
        else:
            result.kept.append(
                f"{label}: the file's entry differs from the page's row; kept (use --replace to take the row)"
            )
    result.system = rf.SystemFunctions(
        existing.system, existing.path, existing.header, tuple(sorted(entries.values(), key=lambda e: e.address))
    )
    return result

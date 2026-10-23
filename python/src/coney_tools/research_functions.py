# SPDX-License-Identifier: GPL-3.0-or-later
"""The function database: what we know about the original's functions, one TOML file per system.

`research/functions/<system>.toml` holds one `[[function]]` per researched function of the original: its address, the
name it has in the shared Ghidra project, the research page and section that describe it, a one-line role, how we know
(the evidence level), and, once Coney ports it, where the port lives and what is left out. Tools look functions up here
directly; `coney-tools research render` turns each file into a list page for readers
(research_render.py), `research import-table` fills a file from an old hand-written table (research_import.py), and
the Understood measure of the progress tracker counts the entries as cited (progress_research.py).

This module is the data model and its schema, with no dependence on the rest of the tracker: it parses and validates
a file, and writes the one canonical text for it. The checks that need the checkout (the page and anchor exist, the
name is Ghidra's) are research_functions_check.py.

Research: docs/guides/research-function-database.md
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError

FUNCTIONS_DIR = Path("research/functions")
#: The evidence levels (docs/guides/research-workflow.md#evidence-levels) as the files spell them, with their labels
#: on the pages. `not-needed` is the Understood measure's "not needed: <reason>"; the reason is the entry's role.
LEVELS = {
    "confirmed-code": "confirmed (code)",
    "confirmed-runtime": "confirmed (runtime)",
    "inferred": "inferred",
    "speculative": "speculative",
    "not-needed": "not needed",
}
#: What a gap is: a piece of the original that Coney does differently on purpose with the reason recorded (`stand-in`),
#: or does not do yet with the issue that will (`not-ported`).
RELATIONS = ("stand-in", "not-ported")
#: How far a ported function has been checked against the original: `legacy` (ported before the side-by-side
#: comparison existed) or `compared`.
STATUSES = ("legacy", "compared")

SYSTEM_NAME = re.compile(r"^[a-z][a-z0-9-]*$")
#: A function's name as Ghidra has it: C identifiers joined by `::`, a destructor's `~`.
FUNCTION_NAME = re.compile(r"^~?[A-Za-z_][A-Za-z0-9_]*(?:::~?[A-Za-z_][A-Za-z0-9_]*)*$")
_PAGE_FILE = r"[a-z0-9][a-z0-9_-]*(?:/[a-z0-9][a-z0-9_-]*)*\.md"
#: A page below docs/research with an optional section: `combat/grabs.md#grab`.
PAGE = re.compile(rf"^({_PAGE_FILE})(?:#([A-Za-z0-9_.:-]+))?$")
QUESTION = re.compile(rf"^({_PAGE_FILE})#([A-Za-z0-9_.:-]+)$")
#: Where Coney's port lives: a source file and, after a colon, the function or class.
CONEY = re.compile(r"^(?P<file>[A-Za-z0-9_./+-]+)(?::(?P<symbol>[^\s:]\S*))?$")
_MAX_ADDRESS = 0xFFFFFFFF

_ENTRY_KEYS = frozenset(
    {"address", "name", "page", "kind", "role", "level", "note", "coney", "status", "gaps"},
)
_GAP_KEYS = frozenset({"what", "relation", "issue", "question"})
_DEFAULT_HEADER = (
    "# The researched functions of the {system} system: one [[function]] per entry, in address order.\n"
    "# The fields: docs/guides/research-function-database.md; `research render --check`."
)


class ResearchFunctionsError(ConfigError):
    """A function database file is malformed; the message names the file and every problem found in it."""


@dataclass(frozen=True)
class Gap:
    """A piece of the original that Coney's port leaves out or does differently, and why it is acceptable."""

    what: str
    relation: str  # one of RELATIONS
    issue: int | None = None  # the issue that ports it (`not-ported`) or tracks the stand-in
    question: str = ""  # an open question of a research page, `page.md#q-...`


@dataclass(frozen=True)
class Entry:
    """One researched function."""

    address: int
    name: str
    page: str  # `<path below docs/research>.md` with an optional `#section`
    level: str  # a key of LEVELS
    role: str = ""  # one line; Markdown links are relative to docs/research
    kind: str = ""  # a short tag for the sort of function (a table's Role column): helper, init, binding, ...
    note: str = ""  # what the level alone does not say: a part of the claim graded lower, an address it rests on
    coney: str = ""  # where the port lives: `src/core/combat/grab.cpp:Grab::start`
    status: str = ""  # one of STATUSES; only with `coney`
    gaps: tuple[Gap, ...] = field(default_factory=tuple)

    @property
    def page_path(self) -> str:
        """The page's file below docs/research, without the section."""
        return self.page.partition("#")[0]

    @property
    def anchor(self) -> str:
        """The section of the page, or an empty string for the page as a whole."""
        return self.page.partition("#")[2]


@dataclass(frozen=True)
class SystemFunctions:
    """The file of one system."""

    system: str
    path: Path
    header: str  # the leading comment lines, kept when the file is rewritten
    entries: tuple[Entry, ...]


# --- reading ------------------------------------------------------------------------------------------------------


def _header_of(text: str) -> str:
    """The comment lines at the top of a file."""
    lines = []
    for line in text.splitlines():
        if not line.startswith("#"):
            break
        lines.append(line.rstrip())
    return "\n".join(lines)


def _is_int(value: object) -> bool:
    """True for an integer that is not a boolean."""
    return isinstance(value, int) and not isinstance(value, bool)


def _string(item: dict[str, Any], key: str, label: str, problems: list[str], *, one_line: bool = True) -> str:
    """The optional string `key` of `item`, or "" with a problem noted when it is not a string (or spans lines)."""
    value = item.get(key, "")
    if not isinstance(value, str):
        problems.append(f"{label}: {key} must be a string, got {value!r}")
        return ""
    if one_line and ("\n" in value or "\r" in value):
        problems.append(f"{label}: {key} must be one line")
        return ""
    return value


def _decompiled(text: str, key: str, label: str, problems: list[str]) -> None:
    """Note a problem when the free text `text` carries decompiler output (LEGAL.md#research-limits).

    The marks are `orig copy-check`'s (Ghidra's made-up names and types, a raw read at a byte offset), so the database
    and the C++ refuse the same things, as the research pages do (`docs check`'s `decompile`: an unnamed function is
    cited by its address, not as `FUN_...`). `goto` is left out: in prose it is a word.
    """
    # Imported here: orig_copy reaches this module through orig_index and progress_research.
    from coney_tools import orig_copy

    for kind, pattern in orig_copy.FINGERPRINTS:
        if kind == "goto":
            continue
        match = pattern.search(text)
        if match:
            problems.append(
                f"{label}: {key} carries decompiler output (`{match.group(0)}`): "
                "describe what the code does in your own words, and cite a function by its address "
                "(LEGAL.md#research-limits)"
            )
            return


def _gap(item: object, label: str, problems: list[str]) -> Gap | None:
    """One `[[function.gaps]]` table as a Gap, or None with its problems noted."""
    if not isinstance(item, dict):
        problems.append(f"{label}: gaps must be an array of tables ([[function.gaps]])")
        return None
    start = len(problems)
    for key in sorted(set(item) - _GAP_KEYS):
        problems.append(f"{label}: unknown key {key!r} (gap keys: {', '.join(sorted(_GAP_KEYS))})")
    what = _string(item, "what", label, problems)
    _decompiled(what, "what", label, problems)
    if not what.strip():
        problems.append(f"{label}: what must say, in a line, what is left out or done differently")
    relation = item.get("relation")
    if relation not in RELATIONS:
        problems.append(f"{label}: relation must be one of {', '.join(RELATIONS)}, got {relation!r}")
    issue = item.get("issue")
    if issue is not None and (not _is_int(issue) or issue < 1):
        problems.append(f"{label}: issue must be a positive issue number, got {issue!r}")
        issue = None
    question = _string(item, "question", label, problems)
    if question and not QUESTION.match(question):
        problems.append(f"{label}: question must name a section as `page.md#q-...`, got {question!r}")
    if issue is None and not question and len(problems) == start:
        problems.append(f"{label}: a gap needs an issue or a question that tracks it")
    return Gap(what, str(relation), issue, question) if len(problems) == start else None


def _entry(item: object, number: int, where: str, problems: list[str]) -> Entry | None:
    """One `[[function]]` table as an Entry, or None with its problems noted."""
    if not isinstance(item, dict):
        problems.append(f"{where}: function {number} is not a table")
        return None
    start = len(problems)
    address = item.get("address")
    name = item.get("name")
    label = f"{where}: function {number}"
    if isinstance(address, int) and not isinstance(address, bool) and 0 <= address <= _MAX_ADDRESS:
        label += f" ({address:#010x}"
        label += f" {name})" if isinstance(name, str) else ")"
    else:
        problems.append(f"{label}: address must be an integer from 0 to 0xffffffff, got {address!r}")
        address = None
    for key in sorted(set(item) - _ENTRY_KEYS):
        problems.append(f"{label}: unknown key {key!r} (keys: {', '.join(sorted(_ENTRY_KEYS))})")
    if not isinstance(name, str) or not FUNCTION_NAME.match(name):
        problems.append(f"{label}: name must be the Ghidra name (letters, digits, `_` and `::`), got {name!r}")
    page = item.get("page")
    if not isinstance(page, str) or not PAGE.match(page):
        problems.append(f"{label}: page must be `<file>.md` or `<file>.md#section` below docs/research, got {page!r}")
    level = item.get("level")
    if level not in LEVELS:
        problems.append(f"{label}: level must be one of {', '.join(LEVELS)}, got {level!r}")
    role = _string(item, "role", label, problems)
    kind = _string(item, "kind", label, problems)
    note = _string(item, "note", label, problems)
    for key, text in (("role", role), ("kind", kind), ("note", note)):
        _decompiled(text, key, label, problems)
    if level == "not-needed" and not role.strip():
        problems.append(f"{label}: level not-needed needs a role that gives the reason")
    coney = _string(item, "coney", label, problems)
    if isinstance(item.get("coney", ""), str) and "coney" in item and not CONEY.match(coney):
        problems.append(f"{label}: coney must be `path/to/file.cpp` or `path/to/file.cpp:Symbol`, got {coney!r}")
    status = _string(item, "status", label, problems)
    if status and status not in STATUSES:
        problems.append(f"{label}: status must be one of {', '.join(STATUSES)}, got {status!r}")
    if status and not coney:
        problems.append(f"{label}: status needs coney: only a ported function has been compared")
    raw_gaps = item.get("gaps", [])
    if not isinstance(raw_gaps, list):
        problems.append(f"{label}: gaps must be an array of tables ([[function.gaps]])")
        raw_gaps = []
    gaps = [_gap(gap, f"{label} gap {i}", problems) for i, gap in enumerate(raw_gaps, start=1)]
    if len(problems) != start or address is None:
        return None
    return Entry(
        address,
        str(name),
        str(page),
        str(level),
        role,
        kind,
        note,
        coney,
        status,
        tuple(g for g in gaps if g is not None),
    )


def parse_system(system: str, text: str, path: Path) -> SystemFunctions:
    """The system file `text` (read from `path`) as entries. Raises ResearchFunctionsError listing every problem."""
    where = path.as_posix()
    problems: list[str] = []
    if not SYSTEM_NAME.match(system):
        raise ResearchFunctionsError(f"{where}: the system name {system!r} must be lower case with hyphens")
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise ResearchFunctionsError(f"{where}: invalid TOML ({error})") from error
    for key in sorted(set(data) - {"function"}):
        problems.append(f"{where}: unknown key {key!r} (a system file holds only [[function]] tables)")
    raw = data.get("function", [])
    if not isinstance(raw, list):
        raise ResearchFunctionsError(f"{where}: function must be an array of tables ([[function]])")
    entries = [e for i, item in enumerate(raw, start=1) if (e := _entry(item, i, where, problems)) is not None]
    seen: dict[int, Entry] = {}
    previous = -1
    for entry in entries:
        if entry.address in seen:
            problems.append(f"{where}: {entry.address:#010x} {entry.name} is listed twice")
        elif entry.address < previous:
            problems.append(
                f"{where}: {entry.address:#010x} {entry.name} is out of order; entries go in ascending address order"
            )
        seen.setdefault(entry.address, entry)
        previous = max(previous, entry.address)
    if problems:
        raise ResearchFunctionsError("\n".join(problems))
    return SystemFunctions(system, path, _header_of(text), tuple(entries))


def _read(path: Path) -> str:
    """The text of a system file. Raises ResearchFunctionsError when it cannot be read as UTF-8."""
    try:
        return path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ResearchFunctionsError(f"{path.as_posix()}: cannot be read ({error})") from error


def load_system(path: Path) -> SystemFunctions:
    """Read one system file; the system is the file's name. Raises ResearchFunctionsError."""
    return parse_system(path.stem, _read(path), path)


def load_all(root: Path, functions_dir: Path = FUNCTIONS_DIR) -> list[SystemFunctions]:
    """Every system file under `root / functions_dir`, by system name; none when the folder is missing.

    The folder is `files.functions` of the research conventions; the default is the same path, for the progress
    tracker, which reads no conventions (a test keeps the two equal).

    Raises ResearchFunctionsError naming every problem of every file, and any address that two files both list.
    """
    folder = root / functions_dir
    systems: list[SystemFunctions] = []
    problems: list[str] = []
    for path in sorted(folder.glob("*.toml")) if folder.is_dir() else []:
        # The system keeps its path from the root, which is how messages and the other tools name it.
        relative = functions_dir / path.name
        try:
            systems.append(parse_system(path.stem, _read(path), relative))
        except ResearchFunctionsError as error:
            problems.append(str(error))
    owners: dict[int, SystemFunctions] = {}
    for system in systems:
        for entry in system.entries:
            first = owners.setdefault(entry.address, system)
            if first is not system:
                problems.append(
                    f"{system.path.as_posix()}: {entry.address:#010x} {entry.name} is also in {first.path.as_posix()}; "
                    "a function belongs to one system"
                )
    if problems:
        raise ResearchFunctionsError("\n".join(problems))
    return systems


# --- writing ------------------------------------------------------------------------------------------------------


def _quote(text: str) -> str:
    """`text` as a TOML basic string: quotes, backslashes and control characters escaped, the rest kept as written."""
    out = ['"']
    for char in text:
        code = ord(char)
        if char == '"':
            out.append('\\"')
        elif char == "\\":
            out.append("\\\\")
        elif char == "\n":
            out.append("\\n")
        elif char == "\t":
            out.append("\\t")
        elif code < 0x20 or code == 0x7F:
            out.append(f"\\u{code:04x}")
        else:
            out.append(char)
    out.append('"')
    return "".join(out)


def _entry_text(entry: Entry) -> str:
    """One entry as the canonical `[[function]]` block, its gaps after it."""
    lines = ["[[function]]", f"address = 0x{entry.address:08x}", f"name = {_quote(entry.name)}"]
    lines.append(f"page = {_quote(entry.page)}")
    for key in ("kind", "role"):
        if getattr(entry, key):
            lines.append(f"{key} = {_quote(getattr(entry, key))}")
    lines.append(f"level = {_quote(entry.level)}")
    for key in ("note", "coney", "status"):
        if getattr(entry, key):
            lines.append(f"{key} = {_quote(getattr(entry, key))}")
    for gap in entry.gaps:
        lines += ["", "[[function.gaps]]", f"what = {_quote(gap.what)}", f"relation = {_quote(gap.relation)}"]
        if gap.issue is not None:
            lines.append(f"issue = {gap.issue}")
        if gap.question:
            lines.append(f"question = {_quote(gap.question)}")
    return "\n".join(lines)


def dumps(system: SystemFunctions) -> str:
    """The canonical text of a system file: its header, then its entries in address order, one blank line apart."""
    header = system.header or _DEFAULT_HEADER.format(system=system.system)
    blocks = [header, *(_entry_text(e) for e in sorted(system.entries, key=lambda e: e.address))]
    return "\n\n".join(blocks) + "\n"

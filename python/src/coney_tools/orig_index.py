# SPDX-License-Identifier: GPL-3.0-or-later
"""The `@orig` index: every tag in `src/`, the Coney function that carries it, and what the source does not tag.

`coney-tools orig index` (docs/guides/coney-tools.md#orig) builds it for the fidelity audit, which compares each
reimplemented function with the original function at its `@orig` address in Ghidra. From the source alone it lists:

* every tag: the Coney file, line and function, the original's address, name and source file, and the research
  pages the function's comment links;
* the functions of the gameplay directories with no tag and no Coney-only marker
  (docs/guides/conventions.md#coney-only);
* the research sections that tagged code links which cite no original address, and the tags whose address the
  linked pages never cite, so an audit can put the address and name next to the behaviour the page describes.

The Ghidra checks are in orig_ghidra.py. Nothing here reads the game or Ghidra's code: the rows hold addresses and
names, which LEGAL.md allows as reference data.
"""

from __future__ import annotations

import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from coney_tools.orig_scan import CONEY_ONLY_TAG, LEGACY_CONEY_ONLY, RESEARCH_LINK, Function, SourceFile, read_source
from coney_tools.progress_research import claim_blocks

SOURCE_DIR = Path("src")
RESEARCH_DIR = Path("docs/research")
SOURCE_SUFFIXES = frozenset({".h", ".hpp", ".cpp", ".cc", ".inl"})
_ADDRESS = re.compile(r"\b0x([0-9a-fA-F]{8})\b")
#: A research link cut after a hyphen of its anchor at a line's end, and the next line's comment opener.
_WRAPPED_LINK = re.compile(r"(docs/research/[A-Za-z0-9_./-]+?\.md#[A-Za-z0-9_-]*-)[ \t]*\r?\n[ \t]*(?://+!?|\*)?[ \t]*")
_EVIDENCE_WORDS = re.compile(r"\b(confirmed|inferred|speculative)\b", re.IGNORECASE)
_HEADING = re.compile(r"^(?P<hashes>#{1,6})\s+(?P<name>.*?)\s*$")
_EXPLICIT_ANCHOR = re.compile(r"\{#(?P<id>[A-Za-z0-9_-]+)\}")
#: An anchor inside a paragraph or list item: `<span id="warrior-attack"></span>`, `<a id="x"></a>`.
_HTML_ANCHOR = re.compile(r'<(?:span|a)\s+id="(?P<id>[A-Za-z0-9_-]+)"')


@dataclass(frozen=True)
class IndexRow:
    """One `@orig` tag and where it sits."""

    address: int
    name: str  # the original's name, as the tag gives it
    file: str  # the original's source file, "" when the tag gives none
    where: str  # "src/ai/brain.h:42"
    coney: str  # the Coney function (or class) the tag documents
    placement: str  # "comment" (above a function), "body" (inside one) or "other" (a class or file comment)
    research: tuple[str, ...]  # research links in the function's comment, else in its file's header

    @property
    def path(self) -> str:
        """The Coney file."""
        return self.where.rsplit(":", 1)[0]

    @property
    def directory(self) -> str:
        """The `src/` directory the file is in (`ai`, `human`, ...)."""
        parts = PurePosixPath(self.path).parts
        return parts[1] if len(parts) > 2 else ""


@dataclass(frozen=True)
class Untagged:
    """A function of a gameplay directory with no `@orig` tag and no Coney-only marker."""

    where: str
    coney: str
    kind: str  # "member", "free", "helper" (a file's private function) or "inline" (a member defined in its class)
    lines: int
    exempt: bool = False  # needs no tag or marker: a one-statement inline member, an empty constructor or destructor
    params: str = ""  # the parameter types, which tell overloads apart
    legacy: bool = False  # marked only by the older wording, which `orig check` reads as no marker on new code

    @property
    def key(self) -> tuple[str, str, str, str]:
        """(file, scope, name, parameter types): what the function is recognised by at another commit."""
        path = self.where.rsplit(":", 1)[0]
        scope, _, name = self.coney.rpartition("::")
        return (path, scope, name, self.params)


@dataclass(frozen=True)
class Malformed:
    """A comment line that starts an `@orig` tag without its whole form `@orig 0x<address> <Name> (<File>.cpp)`."""

    where: str
    text: str


@dataclass
class Index:
    """What `orig index` finds in the source."""

    rows: list[IndexRow]
    untagged: list[Untagged]
    in_scope: int  # function definitions in the gameplay directories
    tagged: int  # ... of them covered by a tag (their own or their declaration's)
    marked: int  # ... marked Coney-only with `@coney-only`
    marked_legacy: int  # ... marked only by the older wording ("Coney's own", "(no @orig)")
    files: int = 0
    malformed: list[Malformed] = field(default_factory=list)


@dataclass(frozen=True)
class SectionGap:
    """A research section that tagged code links which cites no original address at all."""

    link: str  # "docs/research/ai.md#brain"
    heading: str  # "" for a whole page, "(missing)" when the anchor is not on the page
    tags: tuple[IndexRow, ...]


@dataclass
class ResearchReport:
    """How the research pages that tagged code links refer back to the original."""

    gaps: list[SectionGap] = field(default_factory=list)
    missing: list[SectionGap] = field(default_factory=list)  # links to a page or anchor that does not exist
    uncited: list[IndexRow] = field(default_factory=list)  # tags whose address no linked page cites
    unlinked: list[IndexRow] = field(default_factory=list)  # tags whose comment and file link no research page


# --- the source ---------------------------------------------------------------------------------------------------


def read_sources(root: Path) -> list[SourceFile]:
    """Read every C++ file under `root/src`, in path order."""
    base = root / SOURCE_DIR
    if not base.is_dir():
        return []
    paths = sorted(p for p in base.rglob("*") if p.suffix in SOURCE_SUFFIXES and p.is_file())
    return [read_source(p.relative_to(root).as_posix(), p.read_text(encoding="utf-8", errors="replace")) for p in paths]


def _links(text: str) -> tuple[str, ...]:
    """The research links in a comment, in order, each once."""
    found: list[str] = []
    # A link whose anchor breaks at a hyphen at the end of a comment line goes on at the start of the next one.
    text = _WRAPPED_LINK.sub(r"\1", text)
    for match in RESEARCH_LINK.finditer(text):
        if match.group(0) not in found:
            found.append(match.group(0))
    return tuple(found)


class _ResearchLinks:
    """Where a function's research link is found when its own comment has none: its declaration's comment, its
    class's comment, its file's header, then the header of the file's header (`brain.cpp` -> `brain.h`)."""

    def __init__(self, sources: list[SourceFile]) -> None:
        self.declared: dict[tuple[str, tuple[str, str]], tuple[str, ...]] = {}
        self.classes: dict[tuple[str, str], tuple[str, ...]] = {}
        self.headers: dict[tuple[str, str], tuple[str, ...]] = {}
        for source in sources:
            directory = _stem(source.path)[0]
            for qualified, comment in source.class_comments.items():
                if _links(comment):
                    self.classes.setdefault((directory, qualified), _links(comment))
            for function in source.functions:
                if _links(function.comment):
                    self.declared.setdefault((directory, function.key), _links(function.comment))
            if _links(source.header_comment) and source.path.endswith((".h", ".hpp")):
                self.headers[_stem(source.path)] = _links(source.header_comment)

    def of(self, source: SourceFile, function: Function) -> tuple[str, ...]:
        """The research links that document `function`."""
        directory = _stem(source.path)[0]
        scope = function.scope
        return (
            _links(function.comment)
            or self.declared.get((directory, function.key), ())
            or (self.classes.get((directory, scope), ()) if scope else ())
            or _links(source.header_comment)
            or self.headers.get(_stem(source.path), ())
        )


def build_rows(sources: list[SourceFile]) -> list[IndexRow]:
    """One row per `@orig` tag, in file and line order."""
    rows: list[IndexRow] = []
    research = _ResearchLinks(sources)
    for source in sources:
        header = _links(source.header_comment) or research.headers.get(_stem(source.path), ())
        found: list[tuple[int, IndexRow]] = []
        for function in source.functions:
            links = research.of(source, function)
            for tag in function.tags:
                row = IndexRow(
                    tag.address, tag.name, tag.file, f"{source.path}:{tag.line}", function.qualified, "comment", links
                )
                found.append((tag.line, row))
            for tag in function.body_tags:
                row = IndexRow(
                    tag.address, tag.name, tag.file, f"{source.path}:{tag.line}", function.qualified, "body", links
                )
                found.append((tag.line, row))
        for tag in source.loose_tags:
            owner = source.tag_owner.get(tag.line, "(file)")
            found.append(
                (
                    tag.line,
                    IndexRow(tag.address, tag.name, tag.file, f"{source.path}:{tag.line}", owner, "other", header),
                )
            )
        rows.extend(row for _, row in sorted(found, key=lambda item: item[0]))
    return rows


def _stem(path: str) -> tuple[str, str]:
    """(directory path, file stem): what a header and its source file share."""
    pure = PurePosixPath(path)
    return (pure.parent.as_posix(), pure.stem)


def _marker(text: str) -> str:
    """ "marker" for `@coney-only`, "legacy" for the older wording, "" for neither."""
    if CONEY_ONLY_TAG.search(text):
        return "marker"
    if LEGACY_CONEY_ONLY.search(text):
        return "legacy"
    return ""


class _Coverage:
    """Which functions are tagged or marked, through their own comment or their declaration's.

    A declaration covers a definition with the same scope, name and parameter types, looked up in the same file
    pair (`brain.h`, `brain.cpp`), then the same folder, then (for a member) anywhere. When the parameter types differ
    (a declaration and its definition spelt differently) a declaration still covers it if its name is not
    overloaded. A file's private helper is covered only by a declaration in its own file.
    """

    def __init__(self, sources: list[SourceFile]) -> None:
        # (table, key) -> every declaration and definition with that scope and name: (params, state, where).
        self.by_file: dict[str, dict[tuple[str, str], list[tuple[str, str, str]]]] = defaultdict(dict)
        self.by_stem: dict[tuple[str, str], dict[tuple[str, str], list[tuple[str, str, str]]]] = defaultdict(dict)
        self.by_dir: dict[str, dict[tuple[str, str], list[tuple[str, str, str]]]] = defaultdict(dict)
        self.scoped: dict[tuple[str, str], list[tuple[str, str, str]]] = {}
        self.file_marks: dict[tuple[str, str], str] = {}
        for source in sources:
            stem = _stem(source.path)
            mark = _marker(source.header_comment)
            if mark:
                self.file_marks[stem] = self.file_marks.get(stem) or mark
            for function in source.functions:
                entry = (function.params, self.state(function), f"{function.path}:{function.line}")
                tables = [self.by_file[source.path], self.by_stem[stem], self.by_dir[stem[0]]]
                if function.scope:
                    tables.append(self.scoped)
                for table in tables:
                    table.setdefault(function.key, []).append(entry)

    @staticmethod
    def state(function: Function) -> str:
        """ "tagged", "marker", "legacy" or "" from the function's own comments."""
        if function.tags or function.body_tags:
            return "tagged"
        return _marker(function.comment)

    @staticmethod
    def _decide(function: Function, entries: list[tuple[str, str, str]]) -> str | None:
        """The state the declarations in `entries` give `function`; None when none of them is its declaration."""
        where = f"{function.path}:{function.line}"
        others = [e for e in entries if e[2] != where]
        exact = [state for params, state, _ in others if params == function.params]
        if exact:
            return max(exact, key=_STATE_ORDER.index)
        # Spelt differently, but the name is not overloaded (no file has two of it): still its declaration.
        per_file: dict[str, set[str]] = defaultdict(set)
        for params, _, place in entries:
            per_file[place.rsplit(":", 1)[0]].add(params)
        if others and all(len(kinds) == 1 for kinds in per_file.values()):
            return max((state for _, state, _ in others), key=_STATE_ORDER.index)
        return None

    def lookup(self, function: Function) -> str:
        """The function's state: the strongest of its own comments', its declaration's and its file header's."""
        own = self.state(function)
        if own == "tagged":
            return own
        stem = _stem(function.path)
        tables = [self.by_file[function.path]]
        if not function.helper:
            tables += [self.by_stem[stem], self.by_dir[stem[0]]]
            if function.scope:
                tables.append(self.scoped)
        declared = ""
        for table in tables:
            decided = self._decide(function, table.get(function.key, []))
            if decided is not None:
                declared = decided
                break
        return max(own, declared, self.file_marks.get(stem, ""), key=_STATE_ORDER.index)


#: The states from weakest to strongest: a tag wins over a marker, a marker over the older wording.
_STATE_ORDER = ("", "legacy", "marker", "tagged")


def _kind(function: Function) -> str:
    """How an untagged function is listed."""
    if function.helper:
        return "helper"
    if function.in_class and function.path.endswith((".h", ".hpp")):
        return "inline"
    return "member" if function.scope else "free"


def is_exempt(function: Function) -> bool:
    """Whether a function needs neither `@orig` nor `@coney-only` (docs/guides/conventions.md#coney-only): a member
    defined in its class whose body is at most one statement (an accessor, a setter, a forwarder), or a
    constructor or destructor with an empty body. A file's private helpers are not exempt."""
    if function.blocks:
        return False
    if function.in_class and function.statements <= 1:
        return True
    last = function.scope.rsplit("::", 1)[-1]
    special = function.name.startswith("~") or (bool(last) and function.name == last)
    return special and function.statements == 0


def find_untagged(
    sources: list[SourceFile], gameplay: tuple[str, ...], paths: set[str] | None = None, with_legacy: bool = False
) -> tuple[list[Untagged], dict[str, int]]:
    """The functions defined in the `gameplay` folders (or only in `paths` among them) with no tag and no marker, and
    the counts of what was looked at: `in_scope`, `tagged`, `marker` and `legacy` (the older wording). With
    `with_legacy`, the functions marked only by the older wording are listed too (`legacy` set): `orig check` accepts
    that wording only on functions that had it before the check existed."""
    coverage = _Coverage(sources)
    untagged: list[Untagged] = []
    counts = {"in_scope": 0, "tagged": 0, "marker": 0, "legacy": 0}
    for source in sources:
        if not source.path.startswith(gameplay) or (paths is not None and source.path not in paths):
            continue
        for function in source.functions:
            if not function.body:
                continue
            counts["in_scope"] += 1
            state = coverage.lookup(function)
            if state:
                counts[state] += 1
            if state and not (with_legacy and state == "legacy"):
                continue
            where = f"{function.path}:{function.line}"
            untagged.append(
                Untagged(
                    where,
                    function.qualified,
                    _kind(function),
                    function.lines,
                    is_exempt(function),
                    function.params,
                    state == "legacy",
                )
            )
    return untagged, counts


def build_index(root: Path, gameplay: tuple[str, ...]) -> Index:
    """The index of `root`'s source: the tag rows and the gameplay functions that are neither tagged nor marked.

    `gameplay` is `paths.gameplay` of .github/conventions.yml (`src/ai/`, ...).
    """
    sources = read_sources(root)
    untagged, counts = find_untagged(sources, gameplay)
    return Index(
        build_rows(sources),
        untagged,
        counts["in_scope"],
        counts["tagged"],
        counts["marker"],
        counts["legacy"],
        len(sources),
        [Malformed(f"{s.path}:{line}", text) for s in sources for line, text in s.malformed],
    )


# --- the research pages -------------------------------------------------------------------------------------------


def _slug(heading: str) -> str:
    """The anchor the docs site gives a heading without an explicit `{#id}`."""
    name = _EXPLICIT_ANCHOR.sub("", heading).strip().casefold()
    return re.sub(r"\s", "-", re.sub(r"[^\w\s-]", "", name))


def page_sections(text: str) -> dict[str, tuple[str, str]]:
    """Each anchor of a page -> (its heading, the section's text up to the next heading of the same or a higher
    level); the empty anchor is the whole page."""
    lines = text.splitlines()
    headings: list[tuple[int, int, str]] = []  # (line index, level, heading text)
    in_fence = False
    for number, line in enumerate(lines):
        if line.lstrip().startswith(("```", "~~~")):
            in_fence = not in_fence
        match = None if in_fence else _HEADING.match(line)
        if match:
            headings.append((number, len(match["hashes"]), match["name"]))
    sections: dict[str, tuple[str, str]] = {"": ("", text)}
    for i, (start, level, name) in enumerate(headings):
        end = next((h[0] for h in headings[i + 1 :] if h[1] <= level), len(lines))
        body = "\n".join(lines[start:end])
        explicit = _EXPLICIT_ANCHOR.search(name)
        title = _EXPLICIT_ANCHOR.sub("", name).strip()
        for anchor in {explicit["id"] if explicit else "", _slug(name)} - {""}:
            sections.setdefault(anchor, (title, body))
    # An inline anchor's section runs from its line to the next heading.
    for number, line in enumerate(lines):
        for match in _HTML_ANCHOR.finditer(line):
            end = next((h[0] for h in headings if h[0] > number), len(lines))
            sections.setdefault(match["id"], (f"#{match['id']}", "\n".join(lines[number:end])))
    return sections


def check_research(root: Path, rows: list[IndexRow]) -> ResearchReport:
    """Where the research that tagged code links does not point back at the original (see ResearchReport)."""
    report = ResearchReport()
    pages: dict[str, dict[str, tuple[str, str]] | None] = {}
    by_link: dict[str, list[IndexRow]] = defaultdict(list)
    for row in rows:
        if not row.research:
            report.unlinked.append(row)
        for link in row.research:
            by_link[link].append(row)
    page_text: dict[str, str] = {}
    for link, tagged in sorted(by_link.items()):
        page, _, anchor = link.partition("#")
        if page not in pages:
            path = root / page
            text = path.read_text(encoding="utf-8", errors="replace") if path.is_file() else None
            pages[page] = page_sections(text) if text is not None else None
            page_text[page] = text or ""
        sections = pages[page]
        if sections is None or anchor not in sections:
            report.missing.append(SectionGap(link, "(missing)", tuple(tagged)))
            continue
        heading, body = sections[anchor]
        if not _ADDRESS.search(body):
            report.gaps.append(SectionGap(link, heading, tuple(tagged)))
    # Each page's addresses are read once, not once per tag that links it.
    page_addresses = {page: {int(m.group(1), 16) for m in _ADDRESS.finditer(text)} for page, text in page_text.items()}
    for row in rows:
        cited = set().union(*(page_addresses.get(link.split("#")[0], set()) for link in row.research))
        if row.research and row.address not in cited:
            report.uncited.append(row)
    return report


def evidence_levels(root: Path) -> dict[int, set[str]]:
    """Each address the research pages cite -> the evidence levels its claims state ("none" for a claim with none).

    A claim is a table row or a paragraph (progress_research.claim_blocks), as for the Understood measure.
    """
    levels: dict[int, set[str]] = defaultdict(set)
    research = root / RESEARCH_DIR
    for path in sorted(research.rglob("*.md")) if research.is_dir() else []:
        for block in claim_blocks(path.read_text(encoding="utf-8", errors="replace")):
            text = "\n".join(block)
            words = {w.casefold() for w in _EVIDENCE_WORDS.findall(text)} or {"none"}
            for match in _ADDRESS.finditer(text):
                levels[int(match.group(1), 16)] |= words
    return dict(levels)


def weakest_evidence(levels: set[str] | None) -> str:
    """The grade an audit should start from: "uncited", "speculative", "inferred", "none" (cited with no level) or
    "confirmed". A function cited as inferred anywhere is read again, even when another claim confirms a part."""
    if not levels:
        return "uncited"
    for level in ("speculative", "inferred"):
        if level in levels:
            return level
    return "confirmed" if "confirmed" in levels else "none"

# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools research where`: where in the research a function, a source file or a player's word lives.

A reader with an address, a Ghidra name, a source file or a word wants the page that describes it before anything else
(docs/guides/research-where.md). The index answers from the data the tools already keep: the Ghidra listing, the source
files and their ranges, the pages' `owns`, the function database and the glossary, plus a read of the research pages
for the other places that cite an address and the open questions that name it. `original study` prints the same lines
for the function it studies, so the engineer knows where to write before reading the decompile.

Research: docs/guides/research-where.md
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools import progress, source_files
from coney_tools.config import ConfigError
from coney_tools.conventions import Research, load_conventions
from coney_tools.docs_page import ANCHOR, address_line, heading_ids, heading_name, read_page
from coney_tools.research_catalog import Catalog, load_catalog
from coney_tools.research_functions import Entry, SystemFunctions, load_all
from coney_tools.research_glossary import Glossary, Term
from coney_tools.research_glossary import load as load_glossary
from coney_tools.research_owners import Claim, Owners
from coney_tools.research_pages import Corpus
from coney_tools.research_subject import ADDRESS, section_names
from coney_tools.source_files import SourceFile, SourceFiles

#: The most places listed for one kind of citation, so a function every page mentions stays readable.
SHOWN = 8
#: The most partial matches of a name or word listed.
MATCHES = 15


@dataclass(frozen=True)
class Place:
    """A place in a research page: the page, the line and the section's anchor ('' above the first heading)."""

    page: str
    line: int
    anchor: str

    def text(self) -> str:
        """`docs/research/combat.md:120 (#state-flags)`."""
        return f"{self.page}:{self.line}" + (f" (#{self.anchor})" if self.anchor else "")


@dataclass(frozen=True)
class AddressReport:
    """Everything the index knows about one address."""

    query: str
    address: int
    #: Whether the checkout has the Ghidra listing: without it no address can be placed in a function.
    listed: bool = True
    entry: int | None = None  # the entry of the function that holds it
    name: str = ""  # Ghidra's name for it, '' while Ghidra's default
    size: int = 0
    section: str = ""  # the section, for an address outside any function
    code: bool = False  # whether that section is the code (`.text`)
    source_file: str = ""
    claim: Claim | None = None
    record: tuple[str, Entry] | None = None  # the function database's system and entry
    orig: tuple[str, ...] = ()  # `src/core/x.cpp:41` of each `@orig` tag for the function
    cited: tuple[Place, ...] = ()
    questions: tuple[Place, ...] = ()

    @property
    def offset(self) -> int:
        """How far the address lies inside its function (0 at the entry)."""
        return self.address - self.entry if self.entry is not None else 0


@dataclass(frozen=True)
class FileReport:
    """A source file of the original and the page that owns it."""

    file: SourceFile
    claim: Claim | None = None


@dataclass(frozen=True)
class WordReport:
    """A word the player uses, from the glossary or found in page headings."""

    word: str
    term: Term | None = None
    names: tuple[tuple[int, str], ...] = ()  # the term's functions with their Ghidra names
    headings: tuple[Place, ...] = ()  # pages whose heading holds the word (when the glossary has no such term)


@dataclass(frozen=True)
class Result:
    """What a query found."""

    query: str
    addresses: tuple[AddressReport, ...] = ()
    files: tuple[FileReport, ...] = ()
    words: tuple[WordReport, ...] = ()
    #: Names that only contain the query: (address, name), for the reader to narrow down.
    similar: tuple[tuple[int, str], ...] = ()

    @property
    def found(self) -> bool:
        """Whether anything matched."""
        return bool(self.addresses or self.files or self.words or self.similar)


@dataclass
class Index:
    """The research data of one checkout, read once."""

    root: Path
    research: Research
    catalog: Catalog
    corpus: Corpus
    owners: Owners
    systems: list[SystemFunctions]
    files: SourceFiles
    glossary: Glossary
    _entries: dict[int, tuple[str, Entry]] = field(default_factory=dict)
    _citations: tuple[dict[int, list[Place]], dict[int, list[Place]]] | None = None
    _orig: dict[int, list[str]] | None = None

    @classmethod
    def load(cls, root: Path) -> Index:
        """The index of the checkout at `root`. Raises ConfigError when a data file is malformed."""
        research = load_conventions(root).research
        files = source_files.load(root, Path(research.files.source_files))
        corpus = Corpus.from_root(root, research)
        systems = load_all(root, Path(research.files.functions))
        index = cls(
            root,
            research,
            load_catalog(root, research),
            corpus,
            Owners(corpus, files.ranges()),
            systems,
            files,
            load_glossary(root, Path(research.files.glossary)),
        )
        index._entries = {e.address: (s.system, e) for s in systems for e in s.entries}
        return index

    # --- addresses -------------------------------------------------------------------------------------------------

    def _scan(self) -> tuple[dict[int, list[Place]], dict[int, list[Place]]]:
        """Where each address is cited on the research pages, and which open questions name it (read once)."""
        if self._citations is None:
            cited: dict[int, list[Place]] = {}
            questions: dict[int, list[Place]] = {}
            for path, facts in sorted(self.corpus.pages.items()):
                try:
                    page = read_page((self.root / path).read_text(encoding="utf-8", errors="replace"))
                except OSError:
                    continue
                ids = heading_ids(page)
                kind = facts.front.kind or self.research.kind_of_path(path)
                sections = section_names(page, self.research, kind)
                for block in page.blocks:
                    if block.kind == "heading":
                        continue
                    anchor = ids[block.section] if block.section >= 0 else ""
                    in_questions = block.section >= 0 and sections[block.section] == "Open questions"
                    for match in ADDRESS.finditer(block.text):
                        address = int(match.group(0), 16)
                        place = Place(path, address_line(block, match.start()), anchor)
                        cited.setdefault(address, []).append(place)
                        if in_questions:
                            named = ANCHOR.search(block.text)
                            questions.setdefault(address, []).append(
                                Place(path, place.line, named.group("id") if named else anchor)
                            )
            self._citations = (cited, questions)
        return self._citations

    def _orig_tags(self) -> dict[int, list[str]]:
        """The `@orig` tags of src/ by original address, as `file:line` (read once)."""
        if self._orig is None:
            self._orig = {}
            for tag in progress.scan_orig_tags(self.root):
                self._orig.setdefault(tag.address, []).append(tag.where)
        return self._orig

    def address(self, address: int, query: str = "") -> AddressReport:
        """The report for `address`: the function that holds it, its file and owning page, and where it is cited."""
        holder = self.catalog.containing(address)
        entry = holder.address if holder else None
        section, code = "", False
        if holder is None:
            found = self.catalog.section_of(address)
            section, code = (found.name, found.code) if found else ("", False)
        key = entry if entry is not None else address
        ranges = self.files.ranges()
        cited, questions = self._scan()
        return AddressReport(
            query=query or f"{address:#010x}",
            address=address,
            listed=self.catalog.can_resolve,
            entry=entry,
            name=holder.name if holder and holder.named else "",
            size=holder.size if holder else 0,
            section=section,
            code=code,
            source_file=source_files.file_of(ranges, [r.start for r in ranges], key),
            claim=self.owners.claim(key),
            record=self._entries.get(key),
            orig=tuple(self._orig_tags().get(key, ())),
            cited=tuple(cited.get(key, ())),
            questions=tuple(questions.get(key, ())),
        )

    # --- the other things a query can name -------------------------------------------------------------------------

    def file(self, query: str) -> list[FileReport]:
        """The source files whose path is `query` or ends in `/query` (case does not matter)."""
        folded = query.casefold().strip("/")
        reports = []
        for file in self.files.files:
            path = file.path.casefold()
            if path == folded or path.endswith("/" + folded):
                reports.append(FileReport(file, self._file_claim(file)))
        return reports

    def _file_claim(self, file: SourceFile) -> Claim | None:
        """The claim that makes a page the owner of the whole file, if any."""
        found = file.range()
        return self.owners.file_owner(found) if found is not None else None

    def word(self, query: str) -> WordReport | None:
        """The glossary term `query` names, or the pages with a heading that holds it."""
        term = self.glossary.find(query)
        if term is not None:
            return WordReport(query, term, tuple((a, self._name(a)) for a in term.functions))
        folded = query.casefold()
        found: list[Place] = []
        for path in sorted(self.corpus.pages):
            try:
                page = read_page((self.root / path).read_text(encoding="utf-8", errors="replace"))
            except OSError:
                continue
            for heading, anchor in zip(page.headings, heading_ids(page), strict=True):
                if folded in heading_name(heading.text):
                    found.append(Place(path, heading.line, anchor))
        return WordReport(query, None, (), tuple(found)) if found else None

    def _name(self, address: int) -> str:
        """The Ghidra name of the function at `address`, or ''."""
        entry = self.catalog.entry(address)
        return entry.name if entry and entry.named else ""

    def by_name(self, query: str) -> tuple[list[int], list[tuple[int, str]]]:
        """The functions named `query` exactly, and the ones whose name only contains it (case does not matter)."""
        exact = [f.address for f in self.catalog.functions if f.named and f.name == query]
        if not exact:
            exact = [f.address for f in self.catalog.functions if f.named and f.name.casefold() == query.casefold()]
        folded = query.casefold()
        similar = [
            (f.address, f.name)
            for f in self.catalog.functions
            if f.named and folded in f.name.casefold() and f.address not in exact
        ]
        return exact, similar

    def search(self, query: str) -> Result:
        """Answer `query`: a hex address, a Ghidra name, a source file or a word."""
        text = query.strip()
        if not text:
            raise ConfigError(
                "the query is empty: give an address (0x00100200), a Ghidra function name, a source file or a word"
            )
        if text.lower().startswith("0x"):
            try:
                address = int(text, 16)
            except ValueError as error:
                raise ConfigError(f"{query!r} is not an address (0x and hex digits)") from error
            return Result(query, addresses=(self.address(address, text),))
        files = self.file(text) if "/" in text or text.lower().endswith((".cpp", ".inl", ".h")) else []
        exact, similar = self.by_name(text)
        addresses = tuple(self.address(a, text) for a in exact[:SHOWN])
        word = self.word(text) if not files and not addresses else None
        near = tuple(similar[:MATCHES])
        return Result(query, addresses, tuple(files), (word,) if word else (), near)


# --- the text ---------------------------------------------------------------------------------------------------------


def _count(count: int, noun: str) -> str:
    """`1 function`, `2 functions`."""
    return f"{count} {noun}" if count == 1 else f"{count} {noun}s"


def _places(places: tuple[Place, ...]) -> str:
    """The first few places, `docs/research/a.md:12 (#x), ...`, and how many more there are."""
    text = ", ".join(p.text() for p in places[:SHOWN])
    return text + (f", and {len(places) - SHOWN} more" if len(places) > SHOWN else "")


def address_lines(report: AddressReport) -> list[str]:
    """The report of an address as lines of text (the caller says what the address is and indents them)."""
    lines: list[str] = []
    if report.entry is None and report.listed:
        if report.code:
            where = f"in the code of {report.section}"
        else:
            where = f"in the {report.section} data" if report.section else "outside the executable's image"
        lines.append(f"not in a function ({where})")
    elif report.entry is not None and report.offset:
        lines.append(f"{report.offset:#x} bytes into {report.name or 'a function'} at {report.entry:#010x}")
    if report.source_file:
        lines.append(f"source file: {report.source_file} (the map's range for it is inferred)")
    if report.claim is not None:
        lines.append(f"owning page: {report.claim.page} (lists {report.claim.via} {report.claim.what})")
    else:
        lines.append("owning page: none (no page owns this address yet)")
    if report.record is not None:
        system, entry = report.record
        lines.append(f"function database: {system}.toml: {entry.level}, described at {entry.page}")
        if entry.role:
            lines.append(f"  role: {entry.role}")
        if entry.coney:
            lines.append(f"  port: {entry.coney}" + (f" ({entry.status})" if entry.status else ""))
    elif report.entry is not None or not report.listed:
        lines.append("function database: no entry")
    if report.orig:
        lines.append("@orig in: " + ", ".join(report.orig))
    own = report.claim.page if report.claim else ""
    elsewhere = tuple(p for p in report.cited if p.page != own)
    if elsewhere:
        lines.append(f"{'also ' if report.claim else ''}cited on: {_places(elsewhere)}")
    if report.questions:
        lines.append(f"open questions naming it: {_places(report.questions)}")
    return lines


def render(result: Result) -> list[str]:
    """The result as lines of text."""
    lines: list[str] = []
    for report in result.addresses:
        title = f"{report.address:#010x}" + (f" {report.name}" if report.name else "")
        if report.entry is not None:
            title += f" (function, {report.size} bytes)"
        lines.append(title)
        lines.extend(f"  {line}" for line in address_lines(report))
    for file_report in result.files:
        file = file_report.file
        lines.append(file.path)
        if file.start is not None and file.end is not None:
            lines.append(
                f"  range: {file.start:#010x}-{file.end:#010x}, {_count(file.functions, 'function')} (inferred)"
            )
        else:
            lines.append("  no code attributed: no code loads its path string")
        claim = file_report.claim
        lines.append(
            f"  owning page: {claim.page} (lists {claim.via} {claim.what})" if claim else "  owning page: none"
        )
    for word in result.words:
        if word.term is not None:
            lines.append(f"{word.word}: the glossary term '{word.term.term}'")
            lines.append(f"  page: {word.term.page}")
            lines.extend(f"  function: {a:#010x} {name}".rstrip() for a, name in word.names)
            if word.term.also:
                lines.append("  also called: " + ", ".join(word.term.also))
        else:
            lines.append(f"{word.word}: not in the glossary; headings that hold it")
            lines.extend(f"  {place.text()}" for place in word.headings[:MATCHES])
    if result.similar:
        lines.append(f"names that contain '{result.query}':")
        lines.extend(f"  {a:#010x} {name}" for a, name in result.similar)
    return lines


def _place_json(place: Place) -> dict[str, Any]:
    """A place as JSON data."""
    return {"page": place.page, "line": place.line, "anchor": place.anchor}


def _claim_json(claim: Claim | None) -> dict[str, str] | None:
    """A claim as JSON data."""
    return {"page": claim.page, "via": claim.via, "what": claim.what} if claim else None


def _address_json(report: AddressReport) -> dict[str, Any]:
    """An address report as JSON data."""
    record = report.record
    return {
        "address": f"{report.address:#010x}",
        "function": f"{report.entry:#010x}" if report.entry is not None else None,
        "name": report.name,
        "size": report.size,
        "listed": report.listed,
        "offset": report.offset,
        "section": report.section,
        "code": report.code,
        "source_file": report.source_file,
        "owner": _claim_json(report.claim),
        "database": (
            {
                "system": record[0],
                "level": record[1].level,
                "page": record[1].page,
                "role": record[1].role,
                "coney": record[1].coney,
                "status": record[1].status,
            }
            if record
            else None
        ),
        "orig": list(report.orig),
        "cited": [_place_json(p) for p in report.cited],
        "questions": [_place_json(p) for p in report.questions],
    }


def to_json(result: Result) -> dict[str, Any]:
    """The result as JSON data."""
    return {
        "query": result.query,
        "addresses": [_address_json(r) for r in result.addresses],
        "files": [
            {
                "path": f.file.path,
                "start": f"{f.file.start:#010x}" if f.file.start is not None else None,
                "end": f"{f.file.end:#010x}" if f.file.end is not None else None,
                "functions": f.file.functions,
                "owner": _claim_json(f.claim),
            }
            for f in result.files
        ],
        "words": [
            {
                "word": w.word,
                "term": w.term.term if w.term else None,
                "page": w.term.page if w.term else None,
                "functions": [{"address": f"{a:#010x}", "name": n} for a, n in w.names],
                "also": list(w.term.also) if w.term else [],
                "headings": [_place_json(p) for p in w.headings],
            }
            for w in result.words
        ],
        "similar": [{"address": f"{a:#010x}", "name": n} for a, n in result.similar],
    }


def study_lines(root: Path, address: int) -> list[str]:
    """The lines `original study` prints under its own: where the function is described. A lookup that cannot run
    (a malformed data file) says so in one line instead of failing the study."""
    try:
        report = Index.load(root).address(address)
    except ConfigError as error:
        return [f"research lookup: not available ({str(error).splitlines()[0]})"]
    return ["research (coney-tools research where):", *(f"  {line}" for line in address_lines(report))]

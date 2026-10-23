# SPDX-License-Identifier: GPL-3.0-or-later
"""Who owns an address: the one research page that describes it.

Every research page of a kind that owns code says what it owns in its front matter: source files (as
`research/source-files.toml` names them), address ranges and single functions
(docs/guides/writing-docs.md#research-pages). An address has one owner: the most specific entry wins, a single function
before a range, the narrowest range or file after that. `Owners` answers the question for the tools that need it:
`research where`, the find pages, the checks of a function entry and of a page's `owns`.

Research: docs/guides/research-where.md
"""

from __future__ import annotations

import difflib
from collections.abc import Iterable
from dataclasses import dataclass, field
from functools import cached_property
from pathlib import Path

from coney_tools.docs_page import Finding
from coney_tools.research_functions import Entry
from coney_tools.research_pages import Corpus, PageFacts, key_line
from coney_tools.research_subject import Subject
from coney_tools.source_files import FileRange

RESEARCH_DIR = Path("docs/research")


@dataclass(frozen=True)
class Claim:
    """What gives a page an address: it lists the function (`function`), a range holding it (`range`) or the source
    file whose range holds it (`file`); `what` is the entry as the page's front matter writes it."""

    page: str  # docs/research/...
    via: str  # function | range | file
    what: str


@dataclass(frozen=True)
class Owners:
    """The owners of the addresses: the pages' `owns` (the corpus) and the file ranges that resolve `owns.files`."""

    corpus: Corpus = field(default_factory=Corpus)
    files: tuple[FileRange, ...] = ()

    @cached_property
    def _functions(self) -> dict[int, Claim]:
        """The single functions pages list, by address (the first page in path order when two list one)."""
        found: dict[int, Claim] = {}
        for path, facts in sorted(self.corpus.pages.items()):
            if facts.front.owns is not None:
                for address in facts.front.owns.functions:
                    found.setdefault(address, Claim(path, "function", f"{address:#010x}"))
        return found

    @cached_property
    def _spans(self) -> list[tuple[int, int, Claim]]:
        """Every range and file claim as (start, end, claim), in page path order."""
        by_name = {r.path.casefold(): r for r in self.files}
        spans: list[tuple[int, int, Claim]] = []
        for path, facts in sorted(self.corpus.pages.items()):
            owns = facts.front.owns
            if owns is None:
                continue
            for start, end in owns.ranges:
                spans.append((start, end, Claim(path, "range", f"{start:#010x}-{end:#010x}")))
            for name in owns.files:
                known = by_name.get(name.casefold())
                if known is not None:
                    spans.append((known.start, known.end, Claim(path, "file", name)))
        return spans

    def claim(self, address: int) -> Claim | None:
        """The claim that makes a page the owner of `address`, or None when no page owns it."""
        listed = self._functions.get(address)
        if listed is not None:
            return listed
        best: tuple[int, Claim] | None = None
        for start, end, claim in self._spans:
            if start <= address < end and (best is None or end - start < best[0]):
                best = (end - start, claim)
        return best[1] if best else None

    def file_owner(self, file: FileRange) -> Claim | None:
        """The claim that makes a page the owner of the whole of `file`: the narrowest range or source file claim whose
        span holds all of the file's range (a single function owns a part of it, not the file)."""
        best: tuple[int, Claim] | None = None
        for start, end, claim in self._spans:
            if start <= file.start and file.end <= end and (best is None or end - start < best[0]):
                best = (end - start, claim)
        return best[1] if best else None

    def owner(self, address: int) -> str:
        """The page (`docs/research/...`) that owns `address`, or '' for none. The most specific entry wins
        (writing-docs.md, One owner per address): a single function before any range, then the narrowest range or
        source file."""
        found = self.claim(address)
        return found.page if found else ""

    def problem(self, entry: Entry) -> str:
        """Why `entry`'s page is not the owner of its address, or '' when it is (or the page says nothing yet)."""
        page = f"{RESEARCH_DIR.as_posix()}/{entry.page.partition('#')[0]}"
        owner = self.owner(entry.address)
        if owner and owner != page:
            return f"{owner} owns it, not {page}: point the entry at the page that owns it (one owner per address)"
        facts = self.corpus.pages.get(page)
        if not owner and facts is not None and facts.front.present:
            return f"{page} does not own it: add it to the page's owns, or point the entry at the page that does"
        return ""


def check_function_entries(subject: Subject) -> Iterable[Finding]:
    """Rule `function-entries` of `docs check`: each entry of the function database that points at this page is for
    an address the page owns, so a page's author sees what render --check would say about it."""
    entries = subject.env.functions.get(subject.path, ())
    if not entries:
        return
    pages = {**subject.env.corpus.pages, subject.path: PageFacts(subject.path, subject.front)}
    owners = Owners(Corpus(pages), subject.env.source_files)
    line = key_line(subject.page, "owns") if subject.front.owns is not None else 1
    for entry in entries:
        if problem := owners.problem(entry):
            yield Finding("function-entries", line, f"function {entry.address:#010x} {entry.name}: {problem}")


def check_owned_files(subject: Subject) -> Iterable[Finding]:
    """Rule `owns` of `docs check`, for the files: each `owns.files` entry is a source file with an address range in
    `research/source-files.toml`, so the page owns something. Not checked while the checkout has no such data."""
    owns = subject.front.owns
    ranges = subject.env.source_files
    if owns is None or not owns.files or not ranges:
        return
    known = {r.path.casefold(): r.path for r in ranges}
    line = key_line(subject.page, "owns")
    for name in owns.files:
        if name.casefold() in known:
            continue
        near = difflib.get_close_matches(name, [r.path for r in ranges], n=1, cutoff=0.6)
        hint = f" (did you mean {near[0]}?)" if near else ""
        yield Finding(
            "owns",
            line,
            f"file '{name}' is no source file with an address range in research/source-files.toml{hint}",
        )

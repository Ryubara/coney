# SPDX-License-Identifier: GPL-3.0-or-later
"""The front matter of a research page and the corpus of all of them: what a page says about itself and its neighbours.

Every research page starts with YAML front matter (docs/guides/writing-docs.md#research-pages):

    ---
    kind: topic              # system | topic | format | mission | feature | survey
    system: combat           # system and topic pages
    owns:
      files: ["Human/cnsplayertag.cpp"]
      ranges: ["0x00284920-0x00288000"]    # [start, end)
      functions: ["0x00225830"]
    ---

`owns` says which addresses the page describes; no two pages own the same one. A page may take a function out of a
range another page owns (the most specific entry wins), so a range inside another range is fine and two ranges that
cross or equal each other are not.

The *corpus* is what the checks of one page need to know about the others: their front matter (for the owners and
the systems) and their longer paragraphs (for copies).
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

import yaml

from coney_tools.conventions import Research
from coney_tools.docs_page import Finding, Page, kind_of, read_page
from coney_tools.research_catalog import Catalog
from coney_tools.source_files import FileRange

_ADDRESS8 = re.compile(r"0x[0-9a-fA-F]{8}")
_RANGE = re.compile(r"^(0x[0-9a-fA-F]{8})-(0x[0-9a-fA-F]{8})$")


@dataclass(frozen=True)
class Owns:
    """What a page owns: source files (as the source map names them), address ranges `[start, end)` and single
    functions."""

    files: tuple[str, ...] = ()
    ranges: tuple[tuple[int, int], ...] = ()
    functions: tuple[int, ...] = ()

    @property
    def empty(self) -> bool:
        """Whether the page owns nothing."""
        return not (self.files or self.ranges or self.functions)


@dataclass(frozen=True)
class FrontMatter:
    """The front matter of a page, as far as it is valid. `present` is false when the page has none."""

    present: bool = False
    kind: str | None = None
    system: str | None = None
    owns: Owns | None = None


@dataclass(frozen=True)
class PageFacts:
    """What the corpus keeps of one page."""

    path: str
    front: FrontMatter
    #: The paragraphs long enough to be copies, whitespace normalised, to the line each starts on.
    paragraphs: Mapping[str, int] = field(default_factory=dict)


def key_line(page: Page, key: str) -> int:
    """The line of front matter key `key` (1 for a page without it)."""
    pattern = re.compile(rf"^\s*{re.escape(key)}\s*:")
    for index, text in enumerate(page.front_text.splitlines()):
        if pattern.match(text):
            return index + 2
    return 1


def read_front_matter(
    page: Page, research: Research, catalog: Catalog | None = None
) -> tuple[FrontMatter, list[Finding]]:
    """The valid part of `page`'s front matter and a finding (rule `front-matter` or `owns`) for each fault."""
    if not page.front_matter:
        return FrontMatter(), [
            Finding(
                "front-matter",
                1,
                "the page has no front matter: start it with --- kind: ... --- (writing-docs.md#research-pages)",
            )
        ]
    found: list[Finding] = []
    try:
        data = yaml.safe_load(page.front_text)
    except yaml.YAMLError:
        return FrontMatter(True), [Finding("front-matter", 1, "the front matter is not valid YAML")]
    if not isinstance(data, dict):
        return FrontMatter(True), [Finding("front-matter", 1, "the front matter must be a mapping of keys")]
    for key in data:
        if key not in research.front_matter:
            found.append(
                Finding(
                    "front-matter",
                    key_line(page, str(key)),
                    f"unknown front matter key '{key}' (known: {', '.join(research.front_matter)})",
                )
            )
    kind = data.get("kind")
    if not isinstance(kind, str) or kind not in research.kinds:
        found.append(
            Finding(
                "front-matter",
                key_line(page, "kind"),
                f"'kind' must be one of {', '.join(research.kinds)}" + ("" if kind is None else f" (not '{kind}')"),
            )
        )
        kind = None
    system = data.get("system")
    if system is not None and (not isinstance(system, str) or not system.strip()):
        found.append(Finding("front-matter", key_line(page, "system"), "'system' must be the system's name"))
        system = None
    spec = research.kinds.get(kind) if kind else None
    if spec is not None:
        if spec.system == "required" and system is None:
            found.append(Finding("front-matter", key_line(page, "kind"), f"a {kind} page names its 'system'"))
        elif spec.system == "none" and system is not None:
            found.append(
                Finding("front-matter", key_line(page, "system"), f"a {kind} page belongs to no system: drop 'system'")
            )
    owns, owns_findings = _read_owns(page, data.get("owns"), research, catalog)
    found.extend(owns_findings)
    if spec is not None:
        line = key_line(page, "owns")
        if spec.owns == "none" and "owns" in data:
            found.append(Finding("owns", line, f"a {kind} page owns no addresses: drop 'owns'"))
        elif spec.owns == "required" and (owns is None or owns.empty) and not owns_findings:
            found.append(Finding("owns", key_line(page, "kind"), f"a {kind} page owns files, ranges or functions"))
    return FrontMatter(True, kind, system if isinstance(system, str) else None, owns), found


def _read_owns(
    page: Page, value: object, research: Research, catalog: Catalog | None
) -> tuple[Owns | None, list[Finding]]:
    """The `owns` mapping of the front matter, with a finding for each entry that cannot be read or does not resolve."""
    if value is None:
        return None, []
    line = key_line(page, "owns")
    if not isinstance(value, dict):
        return None, [Finding("owns", line, "'owns' must be a mapping of files, ranges and functions")]
    found: list[Finding] = []
    files: list[str] = []
    ranges: list[tuple[int, int]] = []
    functions: list[int] = []
    for key, entries in value.items():
        if key not in research.owns_keys:
            found.append(Finding("owns", line, f"unknown 'owns' key '{key}' (known: {', '.join(research.owns_keys)})"))
            continue
        if not isinstance(entries, list) or not all(isinstance(entry, str) and entry.strip() for entry in entries):
            found.append(Finding("owns", line, f"'owns.{key}' must be a list of strings"))
            continue
        for entry in entries:
            if key == "files":
                files.append(entry)
            elif key == "ranges":
                parsed = _range(entry)
                if parsed is None:
                    found.append(
                        Finding("owns", line, f"range '{entry}' is not `0x<start>-0x<end>` (eight digits each)")
                    )
                elif parsed[0] >= parsed[1]:
                    found.append(Finding("owns", line, f"range '{entry}' ends before it starts"))
                elif catalog is not None and catalog.image is not None and not _inside(parsed, catalog.image):
                    found.append(Finding("owns", line, f"range '{entry}' is not inside the executable's image"))
                else:
                    ranges.append(parsed)
            else:
                if not re.fullmatch(_ADDRESS8, entry):
                    found.append(Finding("owns", line, f"function '{entry}' is not `0x` and eight digits"))
                    continue
                address = int(entry, 16)
                if catalog is not None and catalog.can_resolve and catalog.entry(address) is None:
                    found.append(Finding("owns", line, f"{entry} is no function entry in the Ghidra listing"))
                else:
                    functions.append(address)
    return Owns(tuple(files), tuple(ranges), tuple(functions)), found


def _range(text: str) -> tuple[int, int] | None:
    """The `[start, end)` of a range written `0x<start>-0x<end>`, or None."""
    match = _RANGE.match(text.strip())
    return (int(match.group(1), 16), int(match.group(2), 16)) if match else None


def _inside(span: tuple[int, int], image: tuple[int, int]) -> bool:
    """Whether `[start, end)` lies inside the image `[first, after)`."""
    return image[0] <= span[0] and span[1] <= image[1]


def paragraph_key(text: str) -> str:
    """A paragraph as copies are compared: its list marker dropped and its whitespace collapsed."""
    return re.sub(r"\s+", " ", re.sub(r"^(?:[-*+]|\d+[.)])\s+", "", text.strip()))


def facts_of(path: str, text: str, research: Research) -> PageFacts:
    """The `PageFacts` of the page at `path` with content `text` (its front matter read without complaint)."""
    page = read_page(text)
    front, _ = read_front_matter(page, research)
    paragraphs: dict[str, int] = {}
    for block in page.blocks:
        if block.kind == "text":
            key = paragraph_key(block.text)
            if len(key) >= research.copy_min_chars:
                paragraphs.setdefault(key, block.line)
    return PageFacts(path, front, paragraphs)


@dataclass(frozen=True)
class Corpus:
    """The research pages of a checkout, as far as one page's check needs to know them."""

    pages: Mapping[str, PageFacts] = field(default_factory=dict)

    def others(self, path: str) -> Iterable[PageFacts]:
        """Every page but `path`, in path order."""
        return (facts for name, facts in sorted(self.pages.items()) if name != path)

    @classmethod
    def from_texts(cls, texts: Mapping[str, str], research: Research) -> Corpus:
        """The corpus of pages given as path to text."""
        return cls({path: facts_of(path, text, research) for path, text in texts.items()})

    @classmethod
    def from_root(cls, root: Path, research: Research) -> Corpus:
        """The corpus of the research pages under `root` (generated and landing pages are not research pages)."""
        texts: dict[str, str] = {}
        folder = root / "docs" / "research"
        for item in sorted(folder.rglob("*.md")) if folder.is_dir() else []:
            name = item.relative_to(root).as_posix()
            if kind_of(name) == "research":
                texts[name] = item.read_text(encoding="utf-8", errors="replace")
        return cls.from_texts(texts, research)


def _crosses(first: tuple[int, int], second: tuple[int, int]) -> bool:
    """Whether two ranges are the same or cross: neither holds the other, yet they share addresses."""
    (start, end), (other_start, other_end) = first, second
    if (start, end) == (other_start, other_end):
        return True
    inside = (start >= other_start and end <= other_end) or (other_start >= start and other_end <= end)
    return start < other_end and other_start < end and not inside


def _file_spans(owns: Owns, files: Sequence[FileRange]) -> list[tuple[str, tuple[int, int]]]:
    """The files `owns` lists as (name, [start, end)), for the ones the source files data gives a range."""
    by_name = {known.path.casefold(): known for known in files}
    found = []
    for name in owns.files:
        known = by_name.get(name.casefold())
        if known is not None:
            found.append((name, (known.start, known.end)))
    return found


def owner_conflicts(
    path: str, owns: Owns, corpus: Corpus, files: Sequence[FileRange] = ()
) -> Iterable[tuple[str, str]]:
    """Each entry `owns` shares with another page of the corpus, as (what, the other page).

    The same file, the same function or the same range is owned twice; two ranges that cross are too. A file is its
    range in `files` (the source files data), so a file against a range, or against another page's file under another
    spelling, is held to the same rule. A range inside another range is a page taking part of what another owns, which
    the most specific entry settles.
    """
    mine_files = _file_spans(owns, files)
    for other in corpus.others(path):
        theirs = other.front.owns
        if theirs is None:
            continue
        their_files = _file_spans(theirs, files)
        mine_names = {name.casefold() for name in owns.files}
        for name in theirs.files:
            if name.casefold() in mine_names:
                yield f"file {name}", other.path
        for address in sorted(set(owns.functions) & set(theirs.functions)):
            yield f"function 0x{address:08x}", other.path
        for start, end in owns.ranges:
            for other_start, other_end in theirs.ranges:
                if _crosses((start, end), (other_start, other_end)):
                    yield f"range 0x{start:08x}-0x{end:08x}", other.path
            for name, span in their_files:
                if _crosses((start, end), span):
                    yield f"range 0x{start:08x}-0x{end:08x} (the range of {name})", other.path
        for name, span in mine_files:
            for other_start, other_end in theirs.ranges:
                if _crosses(span, (other_start, other_end)):
                    yield f"file {name} (range 0x{other_start:08x}-0x{other_end:08x})", other.path

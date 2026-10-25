# SPDX-License-Identifier: GPL-3.0-or-later
"""What every rule of the research page standard is given: the page under check (`Subject`) and what it is checked
against (`ResearchEnv`), and the patterns the rules share.

Split from `research_check` so that the evidence rule (`research_evidence`) and the rules the later parts of the rework
add (`research_check.EXTRA_RULES`) import the subject without importing each other.
"""

from __future__ import annotations

import re
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass, field

from coney_tools.conventions import Research
from coney_tools.docs_page import Finding, Page, heading_name
from coney_tools.orig_scan import NOT_XBOX
from coney_tools.research_catalog import Catalog
from coney_tools.research_functions import Entry
from coney_tools.research_legacy import Legacy
from coney_tools.research_pages import Corpus, FrontMatter
from coney_tools.source_files import FileRange

#: An address in the executable, written as the conventions say: `0x` and eight hex digits (`0x00149160`), within the
#: EE's 32 MB of main memory where the executable and its data live (`0x00100000`-`0x01ffffff`). A field offset, a mask
#: or a flag value is written shorter; a name hash, a disc offset or a full-width mask (`0x80000000`) falls outside.
#: An address after the word Xbox is the Xbox port's (XBOX_ADDRESS), never this one (NOT_XBOX).
ADDRESS = re.compile(r"(?<![\w])" + NOT_XBOX + r"0x0(?:0[1-9a-fA-F]|1[0-9a-fA-F])[0-9a-fA-F]{5}\b")
#: An address in the Xbox port's `default.xbe` (base `0x00010000`), written `Xbox 0x000bd3c0` or ``Xbox `0x000bd3c0` ``
#: (docs/guides/research-evidence.md#xbox-addresses): graded like ADDRESS, never looked up in `SLUS_212.15`. The two
#: older Xbox pages write `XBE 0x...`, which reads the same.
XBOX_ADDRESS = re.compile(r"\b(?:[Xx]box|XBE) `?(?P<address>0x[0-9a-fA-F]{8})\b")
#: A script locator (`level99.lua:f31`: function prototype 31 of the chunk), which mission pages cite instead of an
#: address.
LOCATOR = re.compile(r"\b[\w-]+\.lua:f\d+\b")
#: A date, and an issue or pull request number: history, which lives in git and the pull requests (the `history` rule of
#: the research pages and of the engine pages).
DATE = re.compile(r"\b(?:19|20)\d\d-\d\d-\d\d\b")
ISSUE = re.compile(r"(?<![\w&/#])#\d+\b|\b(?:issue|pull request|PR)s?\s+\d+\b")
#: Inline code, and the target of a Markdown link, which the history rule does not read.
CODE_SPAN = re.compile(r"`[^`\n]*`")
LINK_TARGET = re.compile(r"\]\([^)\n]*\)")
VERIFIED_LINE = re.compile(r"^[ \t>*_]*verified against\b[ \t*_]*:?[ \t*_]*(?P<rest>.*)$", re.IGNORECASE)


@dataclass(frozen=True)
class ResearchEnv:
    """What a page is checked against besides its own text."""

    research: Research
    catalog: Catalog = field(default_factory=Catalog)
    corpus: Corpus = field(default_factory=Corpus)
    legacy: Legacy = field(default_factory=Legacy)
    #: The function database's entries by the page they point at (`docs/research/...`), and the source map's file
    #: ranges, which resolve an `owns.files` entry to addresses (research_functions_check).
    functions: Mapping[str, tuple[Entry, ...]] = field(default_factory=dict)
    source_files: tuple[FileRange, ...] = ()


@dataclass(frozen=True)
class Subject:
    """The page under check, with what every rule needs of it."""

    path: str
    page: Page
    kind: str
    front: FrontMatter
    env: ResearchEnv
    legacy_page: bool
    #: For each heading, the canonical name of the level-2 section it sits in ('' above the first, or unknown name).
    sections: list[str]


Rule = Callable[[Subject], Iterable[Finding]]
"""A rule of the standard: it takes the subject of the check and yields its findings. The later parts of the rework
add theirs to `research_check.EXTRA_RULES`."""


def section_names(page: Page, research: Research, kind: str) -> list[str]:
    """For each heading, the level-2 section it sits in: the kind's name for it, the heading's own text when the kind
    has no such section, '' above the first level-2 heading."""
    names: list[str] = []
    current = ""
    for heading in page.headings:
        if heading.level == 1:
            current = ""
        elif heading.level == 2:
            current = research.canonical(kind, heading.text) or heading_name(heading.text)
        names.append(current)
    return names

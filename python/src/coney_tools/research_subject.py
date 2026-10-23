# SPDX-License-Identifier: GPL-3.0-or-later
"""What every rule of the research page standard is given: the page under check (`Subject`) and what it is checked
against (`ResearchEnv`), and the patterns the rules share.

Split from `research_check` so that the evidence rule (`research_evidence`) and the rules the later parts of the rework
add (`research_check.EXTRA_RULES`) import the subject without importing each other.
"""

from __future__ import annotations

import re
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

from coney_tools.conventions import Research
from coney_tools.docs_page import Finding, Page, heading_name
from coney_tools.research_catalog import Catalog
from coney_tools.research_legacy import Legacy
from coney_tools.research_pages import Corpus, FrontMatter

#: An address in the executable, written as the conventions say: `0x` and eight hex digits (`0x00149160`), within the
#: EE's 32 MB of main memory where the executable and its data live (`0x00100000`-`0x01ffffff`). A field offset, a mask
#: or a flag value is written shorter; a name hash, a disc offset or a full-width mask (`0x80000000`) falls outside.
ADDRESS = re.compile(r"(?<![\w])0x0(?:0[1-9a-fA-F]|1[0-9a-fA-F])[0-9a-fA-F]{5}\b")
#: A script locator (`level99.lua:f31`: function prototype 31 of the chunk), which mission pages cite instead of an
#: address.
LOCATOR = re.compile(r"\b[\w-]+\.lua:f\d+\b")
VERIFIED_LINE = re.compile(r"^[ \t>*_]*verified against\b[ \t*_]*:?[ \t*_]*(?P<rest>.*)$", re.IGNORECASE)


@dataclass(frozen=True)
class ResearchEnv:
    """What a page is checked against besides its own text."""

    research: Research
    catalog: Catalog = field(default_factory=Catalog)
    corpus: Corpus = field(default_factory=Corpus)
    legacy: Legacy = field(default_factory=Legacy)


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

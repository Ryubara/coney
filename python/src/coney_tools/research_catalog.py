# SPDX-License-Identifier: GPL-3.0-or-later
"""What the research page rules check a page against: the executable's functions and sections, and the run records.

Everything here is reference data the repository already commits (addresses, sizes, our own names; docs/guides/
research-workflow.md#understood), never the game itself:

* `docs/progress/ghidra-functions.tsv`: every function the shared Ghidra project knows, with its size and our name;
* `research/elf-sections.toml`: the sections of `SLUS_212.15` (name, start, end), from its section headers;
* `research/runs/<id>.toml`: the run records a `confirmed (runtime)` claim cites (written by the tool that ran them).

An address *resolves* when it is a function's entry, lies inside a function, or lies in a data section of the
executable. A repository without the listing (a checkout that never exported it) resolves everything: there is
nothing to hold an address to.
"""

from __future__ import annotations

import bisect
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.conventions import Research
from coney_tools.progress import ProgressError
from coney_tools.progress_research import ListedFunction, load_listing


@dataclass(frozen=True)
class Section:
    """A section of the executable: its name and the addresses it covers, `start` up to but not including `end`.
    `code` marks the one whose addresses must lie inside a function (`.text`)."""

    name: str
    start: int
    end: int
    code: bool = False


@dataclass(frozen=True)
class Catalog:
    """The functions, sections and run records an address or a run id is checked against."""

    functions: tuple[ListedFunction, ...] = ()
    sections: tuple[Section, ...] = ()
    #: The ids of the run records in `research/runs/`; None when the repository has no such folder yet.
    runs: frozenset[str] | None = None
    _starts: list[int] = field(init=False, repr=False, compare=False, default_factory=list)

    def __post_init__(self) -> None:
        """Keep the function starts for the binary searches."""
        object.__setattr__(self, "_starts", [function.address for function in self.functions])

    def entry(self, address: int) -> ListedFunction | None:
        """The function that starts at `address`, if there is one."""
        index = bisect.bisect_left(self._starts, address)
        if index < len(self.functions) and self.functions[index].address == address:
            return self.functions[index]
        return None

    def containing(self, address: int) -> ListedFunction | None:
        """The function whose body holds `address` (its entry included)."""
        index = bisect.bisect_right(self._starts, address) - 1
        if index >= 0:
            function = self.functions[index]
            if function.address <= address < function.address + max(function.size, 1):
                return function
        return None

    def section_of(self, address: int) -> Section | None:
        """The section that holds `address`."""
        for section in self.sections:
            if section.start <= address < section.end:
                return section
        return None

    @property
    def image(self) -> tuple[int, int] | None:
        """The first address of the executable's image and the first after it; None without sections."""
        if not self.sections:
            return None
        return min(s.start for s in self.sections), max(s.end for s in self.sections)

    @property
    def can_resolve(self) -> bool:
        """Whether there is a function listing to hold addresses to."""
        return bool(self.functions)

    def resolves(self, address: int) -> bool:
        """Whether `address` is a function entry, inside a function, or in a data section of the executable."""
        if not self.can_resolve:
            return True
        if self.containing(address) is not None:
            return True
        section = self.section_of(address)
        return section is not None and not section.code

    def in_image(self, address: int) -> bool:
        """Whether `address` lies in the executable's image (any section, a function or not)."""
        image = self.image
        return self.section_of(address) is not None if image else self.containing(address) is not None

    @property
    def has_records(self) -> bool:
        """Whether the repository has a folder of run records at all (none until the first record is committed)."""
        return self.runs is not None

    def has_run(self, run: str) -> bool:
        """Whether run record `run` exists: never while there is no folder, because nothing can back an id then."""
        return self.runs is not None and run in self.runs


def parse_sections(text: str, where: str = "research/elf-sections.toml") -> tuple[Section, ...]:
    """The sections of a `[[section]]` TOML file: `name`, `start`, `end` and, for `.text`, `code = true`."""
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise ConfigError(f"{where}: not valid TOML ({error})") from error
    sections = []
    for index, item in enumerate(data.get("section", [])):
        try:
            sections.append(Section(str(item["name"]), int(item["start"]), int(item["end"]), bool(item.get("code"))))
        except (KeyError, TypeError, ValueError) as error:
            raise ConfigError(f"{where}: section {index + 1} needs a name, a start and an end") from error
        if sections[-1].start >= sections[-1].end:
            raise ConfigError(f"{where}: section {sections[-1].name} ends before it starts")
    return tuple(sorted(sections, key=lambda section: section.start))


def load_catalog(root: Path, research: Research) -> Catalog:
    """The catalog of the checkout at `root`: whatever of the three data files exists."""
    try:
        listing = load_listing(root / research.files.ghidra_functions)
    except ProgressError as error:
        raise ConfigError(str(error)) from error
    sections_file = root / research.files.elf_sections
    sections: tuple[Section, ...] = ()
    if sections_file.is_file():
        sections = parse_sections(sections_file.read_text(encoding="utf-8"), research.files.elf_sections)
    runs_folder = root / research.files.runs
    runs = frozenset(path.stem for path in runs_folder.glob("*.toml")) if runs_folder.is_dir() else None
    return Catalog(tuple(listing or ()), sections, runs)

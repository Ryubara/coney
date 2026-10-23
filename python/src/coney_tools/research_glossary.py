# SPDX-License-Identifier: GPL-3.0-or-later
"""The glossary: the words a player uses, each linked to the research page and the functions that describe it.

`research/glossary.yaml` is a list of entries:

    - term: mount
      page: combat/grabs.md#mount
      functions: [0x0026f008, 0x0027ec20]
      also: [pin, ground and pound]

A player says "sprint", "stamina" or "mount"; the original's code calls it something else. The glossary says which page
describes the word and where its logic lives, so `research where sprint` and `research/find/words.md` answer the
question the page's headings cannot. The task-leads index (task_leads_index.py) reads the same file.

Research: docs/guides/research-where.md
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

from coney_tools.config import ConfigError
from coney_tools.research_functions import PAGE

_KEYS = frozenset({"term", "page", "functions", "also"})


class GlossaryError(ConfigError):
    """The glossary is malformed; the message names the file and every problem found in it."""


@dataclass(frozen=True)
class Term:
    """One word: the page and section that describe it, the functions where its logic lives, and what else the player
    calls it."""

    term: str
    page: str  # `<path below docs/research>.md` with an optional `#section`
    functions: tuple[int, ...] = ()
    also: tuple[str, ...] = ()

    @property
    def words(self) -> tuple[str, ...]:
        """The term and the other words for it."""
        return (self.term, *self.also)


@dataclass(frozen=True)
class Glossary:
    """The terms of the file, in the file's order."""

    terms: tuple[Term, ...] = ()

    def find(self, word: str) -> Term | None:
        """The term that `word` names (or is another name for), ignoring case."""
        folded = word.casefold()
        for term in self.terms:
            if folded in (w.casefold() for w in term.words):
                return term
        return None


def _address(value: object) -> int | None:
    """An address written as an integer (`0x0026f008` in YAML) or as the text `0x0026f008`, or None."""
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if 0 <= value <= 0xFFFFFFFF else None
    if isinstance(value, str) and len(value) == 10 and value.startswith("0x"):
        try:
            return int(value, 16)
        except ValueError:
            return None
    return None


def _term(item: object, number: int, where: str, problems: list[str]) -> Term | None:
    """One list entry as a Term, or None with its problems noted."""
    if not isinstance(item, dict):
        problems.append(f"{where}: entry {number} is not a mapping")
        return None
    start = len(problems)
    label = f"{where}: entry {number}" + (f" ({item['term']})" if isinstance(item.get("term"), str) else "")
    for key in sorted(set(item) - _KEYS, key=str):
        problems.append(f"{label}: unknown key {key!r} (keys: {', '.join(sorted(_KEYS))})")
    term = item.get("term")
    if not isinstance(term, str) or not term.strip():
        problems.append(f"{label}: term must be the word, as the player says it")
        term = ""
    page = item.get("page")
    if not isinstance(page, str) or not PAGE.match(page):
        problems.append(f"{label}: page must be `<file>.md` or `<file>.md#section` below docs/research, got {page!r}")
    raw = item.get("functions", [])
    addresses: list[int] = []
    if not isinstance(raw, list):
        problems.append(f"{label}: functions must be a list of addresses")
    else:
        for value in raw:
            address = _address(value)
            if address is None:
                problems.append(f"{label}: {value!r} is not an address (0x and eight hex digits)")
            else:
                addresses.append(address)
    also = item.get("also", [])
    if not isinstance(also, list) or not all(isinstance(word, str) and word.strip() for word in also):
        problems.append(f"{label}: also must be a list of words")
        also = []
    if len(problems) != start:
        return None
    return Term(term, str(page), tuple(addresses), tuple(str(word) for word in also))


def parse(text: str, where: str = "research/glossary.yaml") -> Glossary:
    """The glossary file `text` as terms. Raises GlossaryError listing every problem."""
    try:
        data: Any = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise GlossaryError(f"{where}: not valid YAML ({' '.join(str(error).split())})") from error
    if data is None:
        return Glossary()
    if not isinstance(data, list):
        raise GlossaryError(f"{where}: must be a list of entries, each with a term and a page")
    problems: list[str] = []
    terms = [t for i, item in enumerate(data, start=1) if (t := _term(item, i, where, problems)) is not None]
    seen: dict[str, str] = {}
    for term in terms:
        for word in term.words:
            first = seen.setdefault(word.casefold(), term.term)
            if first != term.term:
                problems.append(f"{where}: '{word}' is a name of both '{first}' and '{term.term}'")
    if problems:
        raise GlossaryError("\n".join(problems))
    return Glossary(tuple(terms))


def load(root: Path, path: Path) -> Glossary:
    """The glossary of the checkout at `root`; empty when the file is missing. Raises GlossaryError."""
    full = root / path
    if not full.is_file():
        return Glossary()
    try:
        return parse(full.read_bytes().decode("utf-8"), path.as_posix())
    except (OSError, UnicodeDecodeError) as error:
        raise GlossaryError(f"{path.as_posix()}: cannot be read ({error})") from error


def check(
    glossary: Glossary,
    page_problem: Callable[[str], str],
    is_function: Callable[[int], bool] | None,
    where: str = "research/glossary.yaml",
) -> list[str]:
    """What is wrong with the terms against the checkout: a page or section that does not exist (`page_problem` says
    why, '' for none) and an address that is no function of the Ghidra listing (`is_function`; None without one)."""
    found: list[str] = []
    for term in glossary.terms:
        if problem := page_problem(term.page):
            found.append(f"{where}: {term.term}: {problem}")
        if is_function is not None:
            found.extend(
                f"{where}: {term.term}: {address:#010x} is no function entry in the Ghidra listing"
                for address in term.functions
                if not is_function(address)
            )
    return found

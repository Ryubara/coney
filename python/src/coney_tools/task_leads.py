# SPDX-License-Identifier: GPL-3.0-or-later
"""The leads search: which entries of the index an issue and the coordinator's terms point at, and why.

Two kinds of match, each with its reason:

* **Named in the issue.** The addresses, code-like names, repository paths, research links and `#N` references written
  in the issue are matched exactly against the entries' `exact` tokens (a path by prefix). They are always listed, ahead
  of the ranked ones and outside the per-source limit: an issue that names `0x00322b78` must show where it lives.
* **Ranked.** Each term of `--terms` is split into name parts. A part found in few entries is rare and ranks above a
  common one; a part in many entries (`get`, `object`) is not a match on its own. An entry's score adds the
  rarity of the parts it holds, with a bonus when it holds all the parts of a term. Per source, the best
  `tasks.leads.max_per_source` are listed.

A term that matches nothing is reported, for the task file's "Searched, not related" line.
"""

from __future__ import annotations

import math
import re
from collections import Counter
from dataclasses import dataclass, field

from coney_tools.conventions import LeadsRules
from coney_tools.task_leads_index import SOURCES, Entry, split_name

_ADDRESS = re.compile(r"\b0x[0-9a-fA-F]{8}\b")
_IDENTIFIER = re.compile(r"^~?[A-Za-z_][A-Za-z0-9_]*(?:::~?[A-Za-z_][A-Za-z0-9_]*)*$")
_PATH = re.compile(r"\b((?:src|tests|docs|research|python|assets|cmake|\.github)/[A-Za-z0-9_./#-]*[A-Za-z0-9_/#])")
_PR_NUMBER = re.compile(r"(?<![\w&/])#(\d+)\b")


@dataclass(frozen=True)
class Match:
    """An entry the search found and the reason it did."""

    entry: Entry
    #: `named` (written in the issue), `rare` or `ordinary`.
    tier: str
    score: float
    reason: str


@dataclass
class LeadsResult:
    """What a search found: the matches by source, in display order, and the terms nothing matched."""

    named: list[Match] = field(default_factory=list)
    ranked: list[Match] = field(default_factory=list)
    unmatched: list[str] = field(default_factory=list)

    def matches(self) -> list[Match]:
        """Named matches first, then the ranked ones."""
        return [*self.named, *self.ranked]


def is_code_like(name: str) -> bool:
    """Whether a backticked word reads as an identifier: it has an underscore, a `::`, a digit or an inner capital."""
    if not _IDENTIFIER.match(name) or len(name) < 3:
        return False
    return "_" in name or "::" in name or any(c.isdigit() for c in name) or any(c.isupper() for c in name[1:])


def code_spans(text: str) -> list[str]:
    """The code spans of a text: on each line, the pieces between backtick pairs (none if the count is odd)."""
    spans: list[str] = []
    for line in text.splitlines():
        pieces = line.split("`")
        if len(pieces) % 2 == 1:
            spans.extend(piece for piece in pieces[1::2] if 2 <= len(piece) <= 80)
    return spans


def identifiers_in(text: str) -> tuple[list[str], list[str], list[str]]:
    """The exact names an issue writes: `(addresses, names, paths)`; paths keep a `#anchor`; `#N` go with the names.

    Addresses are lower case. Names are the code-like words in backticks. Paths are repository paths. References such as
    `#317` are returned with the names, as `#317`.
    """
    addresses = list(dict.fromkeys(a.lower() for a in _ADDRESS.findall(text)))
    names = [name for name in code_spans(text) if is_code_like(name)]
    names += [f"#{number}" for number in _PR_NUMBER.findall(text)]
    paths = [path.rstrip("./") if not path.endswith("/") else path for path in _PATH.findall(text)]
    return addresses, list(dict.fromkeys(names)), list(dict.fromkeys(paths))


class Searcher:
    """A search over a fixed list of entries: the part counts are computed once."""

    def __init__(self, entries: tuple[Entry, ...], rules: LeadsRules) -> None:
        self.entries = entries
        self.rules = rules
        counts: Counter[str] = Counter()
        for entry in entries:
            counts.update(set(entry.parts))
        self._frequency = counts
        self._total = max(1, len(entries))

    def frequency(self, part: str) -> int:
        """How many entries hold the name part."""
        return self._frequency.get(part, 0)

    def _weight(self, part: str) -> float:
        """The rarity weight of a part: higher for a part found in fewer entries."""
        return math.log((self._total + 1) / (self.frequency(part) + 1))

    def search(self, terms: list[str], issue_text: str = "") -> LeadsResult:
        """Search for the terms and for the identifiers written in `issue_text`."""
        result = LeadsResult()
        result.named = self._named(issue_text)
        taken = {(match.entry.source, match.entry.where) for match in result.named}
        per_source: dict[str, list[Match]] = {source: [] for source in SOURCES}
        found_terms: set[str] = set()
        for term in terms:
            matches = self._ranked(term)
            if matches:
                found_terms.add(term)
            for match in matches:
                key = (match.entry.source, match.entry.where)
                if key not in taken:
                    per_source[match.entry.source].append(match)
        chosen: list[Match] = []
        for source in SOURCES:
            best: dict[str, Match] = {}
            for match in per_source[source]:
                kept = best.get(match.entry.where)
                if kept is None or (match.tier, -match.score) < (kept.tier, -kept.score):
                    best[match.entry.where] = match
            ordered = sorted(best.values(), key=lambda m: (m.tier != "rare", -m.score, m.entry.title, m.entry.where))
            chosen.extend(ordered[: self.rules.max_per_source])
        result.ranked = sorted(chosen, key=lambda m: (m.tier != "rare", -m.score, m.entry.source, m.entry.title))
        result.unmatched = [term for term in terms if term not in found_terms]
        return result

    def _named(self, text: str) -> list[Match]:
        """Entries an identifier written in the issue names exactly."""
        addresses, names, paths = identifiers_in(text)
        wanted = {
            token: f"{kind} {token}" for kind, tokens in (("address", addresses), ("name", names)) for token in tokens
        }
        found: list[Match] = []
        folder_hits: Counter[str] = Counter()
        cap = 2 * self.rules.max_per_source
        for entry in self.entries:
            reason = ""
            for token in entry.exact:
                if token in wanted:
                    reason = f"named in the issue: {wanted[token]}"
                    break
                if token in paths:
                    reason = f"named in the issue: path {token}"
                    break
                folder = next((p for p in paths if p.endswith("/") and token.startswith(p)), None)
                if folder is not None and folder_hits[folder] < cap:
                    folder_hits[folder] += 1
                    reason = f"named in the issue: folder {folder} (first {cap} entries)"
                    break
            if reason:
                found.append(Match(entry, "named", 1000.0, reason))
        found.sort(key=lambda m: (SOURCES.index(m.entry.source), m.entry.title, m.entry.where))
        return found

    def _ranked(self, term: str) -> list[Match]:
        """Entries a term's name parts match, with the score and the reason of each."""
        parts = list(dict.fromkeys(split_name(term)))
        if not parts:
            return []
        usable = [p for p in parts if self.frequency(p) < self.rules.common_min_entries] or parts
        exact = term.strip()
        found: list[Match] = []
        for entry in self.entries:
            held = set(entry.parts)
            hits = [part for part in usable if part in held]
            is_exact = exact in entry.exact
            if not hits and not is_exact:
                continue
            score = sum(self._weight(part) for part in hits)
            if len(usable) > 1 and len(hits) == len(usable):
                score += 2.0
            if is_exact:
                score += 5.0
            rare = [part for part in hits if self.frequency(part) <= self.rules.rare_max_entries]
            tier = "rare" if rare or is_exact else "ordinary"
            if is_exact:
                reason = f'"{term}" is written exactly in {entry.where}'
            else:
                shown = ", ".join(f'"{part}" ({self.frequency(part)} entries)' for part in hits)
                reason = f'term "{term}" matches the name part {shown}' + (" (rare)" if rare else "")
            found.append(Match(entry, tier, score, reason))
        return found

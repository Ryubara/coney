# SPDX-License-Identifier: GPL-3.0-or-later
"""The prepared index behind `task create --terms` and `task leads`: where in the repository a name or an address lives.

The sources are committed files plus one read of GitHub, so the index holds names, addresses, headings and paths only,
never game data (`LEGAL.md`):

* `research`: the headings (with their anchors) of `docs/research/*.md`, with the addresses and the code-span names
  cited under each heading;
* `functions`: the entries with `coney` in the function database (`research/functions/`), the original functions Coney
  reimplements;
* `ghidra`: `docs/progress/ghidra-functions.tsv`, the committed list of the functions the shared Ghidra project names;
* `orig`: the `@orig` tags of `src/` (`orig_index`), with the Coney file and line;
* `pr`: the merged pull requests (title and changed files), read with `gh`;
* `glossary`: `research/glossary.yaml` (player words linked to code by studies), when it exists.

An `Entry` carries the lower-case `parts` of its names (what ranking compares), its `exact` tokens (addresses, names,
paths: what an identifier written in an issue must equal) and the text to show. The index is cached as JSON in the
scratch folder and rebuilt when it is old, when `HEAD` moved or on request (docs/guides/issues.md#task-files).
"""

from __future__ import annotations

import json
import re
from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

import yaml

from coney_tools import research_functions
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import iso_time, parse_time
from coney_tools.orig_index import build_index, page_sections

#: Bumped when the cached shape changes; an older cache is rebuilt.
INDEX_VERSION = 1
SOURCES = ("research", "functions", "ghidra", "orig", "pr", "glossary")

_ADDRESS = re.compile(r"\b0x[0-9a-fA-F]{8}\b")
_CODE_NAME = re.compile(r"`(~?[A-Za-z_][A-Za-z0-9_]*(?:::~?[A-Za-z_][A-Za-z0-9_]*)*)`")
_DEFAULT_NAME = re.compile(r"^(?:thunk_)?(?:FUN|SUB|LAB)_[0-9a-fA-F]{8}$")
_LOWER_UPPER = re.compile(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])")
_NON_ALNUM = re.compile(r"[^A-Za-z0-9]+")
#: The most code-span names kept per research heading, so one long table does not dominate the index.
MAX_NAMES_PER_HEADING = 80


def split_name(name: str) -> tuple[str, ...]:
    """The lower-case parts of a name: `Goal_ThrowObject` is `goal throw object`, `GetEnemyWeaponsHeld` is
    `get enemy weapon held`, `Foo::Bar2` is `foo bar 2`. A trailing plural `s` is dropped from words of 4+ letters."""
    parts: list[str] = []
    for chunk in _NON_ALNUM.split(name):
        for word in _LOWER_UPPER.sub(" ", chunk).split():
            for piece in re.findall(r"[A-Za-z]+|[0-9]+", word):
                lowered = piece.lower()
                if len(lowered) > 3 and lowered.endswith("s") and not lowered.endswith("ss"):
                    lowered = lowered[:-1]
                parts.append(lowered)
    return tuple(parts)


@dataclass(frozen=True)
class Entry:
    """One thing the index knows: where it is, what it is called and what an issue could name it by."""

    source: str
    #: What is shown: the heading, the function name, the pull request title.
    title: str
    #: Where it is: a page and anchor, `src/ai/x.cpp:42`, an address, `PR #123`.
    where: str
    #: The lower-case parts of its names, for ranking.
    parts: tuple[str, ...]
    #: Addresses (lower case), names and paths an issue can write exactly.
    exact: tuple[str, ...] = ()
    #: One line of extra context for the reason.
    note: str = ""

    def as_json(self) -> dict[str, Any]:
        """The entry as JSON data."""
        return {
            "source": self.source,
            "title": self.title,
            "where": self.where,
            "parts": list(self.parts),
            "exact": list(self.exact),
            "note": self.note,
        }

    @staticmethod
    def from_json(data: dict[str, Any]) -> Entry:
        """The entry from `as_json` data."""
        return Entry(
            source=str(data["source"]),
            title=str(data["title"]),
            where=str(data["where"]),
            parts=tuple(str(item) for item in data["parts"]),
            exact=tuple(str(item) for item in data.get("exact", ())),
            note=str(data.get("note", "")),
        )


@dataclass(frozen=True)
class MergedPr:
    """A merged pull request as the index needs it."""

    number: int
    title: str
    files: tuple[str, ...]


def _lower_address(token: str) -> str:
    """An address in the index's form: lower case."""
    return token.lower()


def research_entries(root: Path) -> list[Entry]:
    """An entry per heading of every research page, holding the addresses and names cited under it."""
    entries: list[Entry] = []
    folder = root / "docs" / "research"
    for page in sorted(folder.glob("*.md")):
        text = page.read_text(encoding="utf-8", errors="replace")
        relative = page.relative_to(root).as_posix()
        first = next((line[2:].strip() for line in text.splitlines() if line.startswith("# ")), page.stem)
        entries.append(Entry("research", first, relative, split_name(first), (relative,), "research page"))
        explicit = set(re.findall(r"\{#([A-Za-z0-9_-]+)\}", text))
        grouped: dict[tuple[str, str], list[str]] = {}
        for anchor, (heading, body) in page_sections(text).items():
            if anchor:
                grouped.setdefault((heading, body), []).append(anchor)
        for (heading, body), anchors in grouped.items():
            if heading == first:
                continue  # the page's own title: the page entry stands for it
            anchors.sort(key=lambda name: (name not in explicit, len(name), name))
            names = list(dict.fromkeys(_CODE_NAME.findall(body)))[:MAX_NAMES_PER_HEADING]
            addresses = tuple(dict.fromkeys(_lower_address(a) for a in _ADDRESS.findall(body)))
            parts = tuple(dict.fromkeys((*split_name(heading), *(p for name in names for p in split_name(name)))))
            wheres = tuple(f"{relative}#{anchor}" for anchor in anchors)
            entries.append(Entry("research", heading, wheres[0], parts, (*wheres, *addresses, *names)))
    return entries


def function_entries(root: Path) -> list[Entry]:
    """The functions Coney reimplements: the entries with `coney` in the function database."""
    entries: list[Entry] = []
    for system in research_functions.load_all(root):
        for function in system.entries:
            if function.coney:
                address = f"0x{function.address:08x}"
                entries.append(
                    Entry(
                        "functions",
                        function.name,
                        address,
                        split_name(function.name),
                        (address, function.name),
                        f"reimplemented ({function.coney.partition(':')[0]})",
                    )
                )
    return entries


def ghidra_entries(root: Path) -> list[Entry]:
    """The named functions of the committed Ghidra list (`docs/progress/ghidra-functions.tsv`)."""
    path = root / "docs" / "progress" / "ghidra-functions.tsv"
    if not path.is_file():
        return []
    entries: list[Entry] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) < 3 or not fields[2] or _DEFAULT_NAME.match(fields[2]):
            continue
        address = _lower_address(fields[0])
        entries.append(
            Entry("ghidra", fields[2], address, split_name(fields[2]), (address, fields[2]), f"{fields[1]} bytes")
        )
    return entries


def orig_entries(root: Path, conventions: Conventions) -> list[Entry]:
    """The `@orig` tags of `src/`: the original's name and address next to the Coney file and line."""
    index = build_index(root, conventions.paths.gameplay)
    entries: list[Entry] = []
    for row in index.rows:
        address = f"0x{row.address:08x}"
        parts = tuple(dict.fromkeys((*split_name(row.name), *split_name(row.coney))))
        entries.append(
            Entry(
                "orig",
                row.name,
                row.where,
                parts,
                (address, row.name, row.path, row.coney),
                f"@orig {address}, {row.coney}",
            )
        )
    return entries


def glossary_entries(root: Path) -> list[Entry]:
    """Player words linked to code (`research/glossary.yaml`); a missing file is no entries."""
    path = root / "research" / "glossary.yaml"
    if not path.is_file():
        return []
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    pairs: list[tuple[str, Any]] = []
    if isinstance(data, dict):
        pairs = [(str(key), value) for key, value in data.items()]
    elif isinstance(data, list):
        pairs = [(str(item.get("term", "")), item) for item in data if isinstance(item, dict)]
    entries: list[Entry] = []
    for word, value in pairs:
        if not word:
            continue
        text = value if isinstance(value, str) else json.dumps(value, default=str)
        names = tuple(dict.fromkeys(_CODE_NAME.findall(text) + _ADDRESS.findall(text)))
        parts = tuple(dict.fromkeys((*split_name(word), *(p for name in names for p in split_name(name)))))
        entries.append(Entry("glossary", word, "research/glossary.yaml", parts, (word, *names), text[:100]))
    return entries


def pr_entries(prs: list[MergedPr]) -> list[Entry]:
    """The merged pull requests: their titles and the files they changed."""
    return [
        Entry(
            "pr",
            pr.title,
            f"PR #{pr.number}",
            split_name(pr.title),
            (f"#{pr.number}", *pr.files),
            f"{len(pr.files)} files",
        )
        for pr in prs
    ]


def read_merged_prs(run: Callable[[list[str]], str], limit: int) -> list[MergedPr]:
    """The last `limit` merged pull requests through `gh`; OSError or ValueError when gh fails or answers badly."""
    out = run(["pr", "list", "--state", "merged", "--limit", str(limit), "--json", "number,title,files"])
    return [
        MergedPr(int(item["number"]), str(item["title"]), tuple(str(f["path"]) for f in item.get("files", [])))
        for item in json.loads(out)
    ]


def build_entries(root: Path, conventions: Conventions, prs: list[MergedPr] | None) -> list[Entry]:
    """Every entry of every source; `prs` None leaves the pull requests out."""
    entries = [
        *research_entries(root),
        *function_entries(root),
        *ghidra_entries(root),
        *orig_entries(root, conventions),
        *glossary_entries(root),
    ]
    if prs is not None:
        entries.extend(pr_entries(prs))
    return entries


# --- the cache ------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class CachedIndex:
    """The index as saved: its entries, when it was built, the commit it was built at and what could not be read."""

    entries: tuple[Entry, ...]
    built_at: datetime
    head: str
    notices: tuple[str, ...]


def save_index(path: Path, index: CachedIndex) -> None:
    """Write the cache file."""
    data = {
        "version": INDEX_VERSION,
        "built_at": iso_time(index.built_at),
        "head": index.head,
        "notices": list(index.notices),
        "entries": [entry.as_json() for entry in index.entries],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")


def load_index(path: Path) -> CachedIndex | None:
    """The cache file, or None when it is missing, damaged or of another version."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("version") != INDEX_VERSION:
            return None
        return CachedIndex(
            tuple(Entry.from_json(item) for item in data["entries"]),
            parse_time(data["built_at"]),
            str(data["head"]),
            tuple(str(item) for item in data.get("notices", ())),
        )
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return None


def is_fresh(index: CachedIndex, head: str, now: datetime, max_age_hours: int) -> bool:
    """Whether a cached index may be used: built at this `head` and not older than `max_age_hours`."""
    return index.head == head and now - index.built_at < timedelta(hours=max_age_hours)


def prepare_index(
    root: Path,
    conventions: Conventions,
    cache: Path,
    head: str,
    now: datetime,
    run: Callable[[list[str]], str] | None,
    refresh: bool = False,
) -> CachedIndex:
    """The index to search: the cached one when fresh, else built now and saved.

    `run` is gh for the merged pull requests; None, or a gh failure, leaves them out with a notice (the rest of the
    index still works).
    """
    cached = None if refresh else load_index(cache)
    if cached is not None and is_fresh(cached, head, now, conventions.tasks.leads.index_max_age_hours):
        return cached
    notices: list[str] = []
    prs: list[MergedPr] | None = None
    if run is not None:
        try:
            prs = read_merged_prs(run, conventions.tasks.leads.merged_prs)
        except (OSError, ValueError, KeyError, TypeError) as error:
            notices.append(f"the merged pull requests were not read ({error})")
    else:
        notices.append("the merged pull requests were not read (no gh)")
    index = CachedIndex(tuple(build_entries(root, conventions, prs)), now, head, tuple(notices))
    save_index(cache, index)
    return index

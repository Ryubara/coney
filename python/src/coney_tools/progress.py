# SPDX-License-Identifier: GPL-3.0-or-later
"""The progress tracker's data: what share of the original game Coney reimplements, and how much is researched.

Four committed inputs feed it, none of them game data:

* `docs/progress/totals.toml`: the denominators, taken by hand from the source map
  (docs/research/source-map.md): `.text`, the coverage categories, each subsystem's address ranges and the
  middleware, which Coney replaces rather than reimplements;
* the function database, `research/functions/<system>.toml` (coney_tools.research_functions): the numerator is every
  entry that has a `coney` field, the place in `src/` where the port lives; an entry's subsystem is the one whose range
  holds its address and its size is the function's size in the Ghidra listing;
* the status table of `docs/roadmap.md`, parsed rather than copied;
* `docs/progress/ghidra-functions.tsv` and the research pages, for the sizes and for the "Understood" measure
  (coney_tools.progress_research).

It also checks that the entries with `coney` agree with the `@orig` tags in `src/` (docs/guides/conventions.md), so an
engineer cannot add a tag without recording the port, or the other way round.

The retired list `docs/progress/functions.toml` is read only by the one-off importer and by the history of days before
the move (coney_tools.progress_import).
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from itertools import pairwise
from pathlib import Path
from typing import TYPE_CHECKING, Any

from coney_tools import research_functions
from coney_tools.config import ConfigError

if TYPE_CHECKING:
    from coney_tools.progress_research import Understanding

DATA_DIR = Path("docs/progress")
TOTALS_FILE = DATA_DIR / "totals.toml"
#: The retired list of reimplemented functions; only the importer and the history of earlier days read it.
LEGACY_FUNCTIONS_FILE = DATA_DIR / "functions.toml"
#: The committed page; the docs build fills its marker with the generated tables (progress_render.PAGE_MARKER).
PAGE_FILE = DATA_DIR / "index.md"
#: The committed history page, whose marker the build fills with the chart and the table of days.
HISTORY_PAGE_FILE = DATA_DIR / "history.md"
#: The site's home page, whose marker the build fills with the compact picture of functions.
HOME_PAGE_FILE = Path("docs/index.md")
ROADMAP_FILE = Path("docs/roadmap.md")
SOURCE_DIR = Path("src")

#: The subsystem of game code that lies in no subsystem range of totals.toml.
UNATTRIBUTED = "unattributed"
#: Subsystems whose code the source map has not tied to a file or directory: not "researched" however it is read.
UNPLACED = frozenset({UNATTRIBUTED, "link-once"})
COVERAGE_KINDS = ("researched", "game", "middleware")
_SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".inl"}
#: `@orig 0x<address> <OriginalName> (<File>.cpp)`; the file part is optional here, the conventions check it.
_ORIG_TAG = re.compile(r"@orig\s+0x(?P<address>[0-9a-fA-F]{8})\b\s*(?P<name>.*?)\s*(?:\([^()]*\))?\s*$")
_MILESTONE_ROW = re.compile(r"^\|\s*\[(?P<name>[^\]]+)\]\(#(?P<anchor>[^)]+)\)\s*\|\s*(?P<status>[^|]+?)\s*\|\s*$")


class ProgressError(ConfigError):
    """A progress data file is missing or malformed; the message names the file and the problem."""


@dataclass(frozen=True)
class Range:
    """Addresses [start, end) of `.text`."""

    start: int
    end: int

    @property
    def size(self) -> int:
        """Bytes in the range."""
        return self.end - self.start

    def __contains__(self, address: object) -> bool:
        return isinstance(address, int) and self.start <= address < self.end


@dataclass(frozen=True)
class Coverage:
    """One category of the source map's coverage table."""

    key: str
    label: str
    kind: str  # one of COVERAGE_KINDS
    bytes: int


@dataclass(frozen=True)
class Subsystem:
    """A top-level source directory of the original and the `.text` ranges the source map gives it."""

    name: str
    ranges: tuple[Range, ...]

    @property
    def bytes(self) -> int:
        """Bytes of `.text` in this subsystem's ranges."""
        return sum(r.size for r in self.ranges)


@dataclass(frozen=True)
class Middleware:
    """A library the original links in and Coney replaces with something else."""

    name: str
    bytes: int
    replaced_by: str
    ranges: tuple[Range, ...]


@dataclass(frozen=True)
class Totals:
    """The parsed totals.toml."""

    text: Range
    coverage: tuple[Coverage, ...]
    subsystems: tuple[Subsystem, ...]
    middleware: tuple[Middleware, ...]

    @property
    def game_bytes(self) -> int:
        """The game's own code: every coverage category that is not middleware."""
        return sum(c.bytes for c in self.coverage if c.kind != "middleware")

    @property
    def researched_bytes(self) -> int:
        """Game code the source map ties to at least a file or a directory."""
        return sum(c.bytes for c in self.coverage if c.kind == "researched")

    @property
    def unattributed_bytes(self) -> int:
        """Game code in no subsystem range."""
        return self.game_bytes - sum(s.bytes for s in self.subsystems)

    def subsystem_of(self, address: int) -> str | None:
        """The subsystem whose range holds `address`; UNATTRIBUTED for other game code; None outside game code."""
        if address not in self.text or any(address in r for m in self.middleware for r in m.ranges):
            return None
        for subsystem in self.subsystems:
            if any(address in r for r in subsystem.ranges):
                return subsystem.name
        return UNATTRIBUTED


@dataclass(frozen=True)
class Function:
    """An original function Coney reimplements: an entry of the function database that has a `coney` field."""

    address: int
    name: str  # the name in the shared Ghidra project
    subsystem: str  # the totals.toml subsystem whose range holds the address; "" for an address outside game code
    size: int | None  # the Ghidra listing's size; None while the listing has no function at the address
    coney: str = ""  # where the port lives: `src/core/combat/grab.cpp:Grab::start`
    system: str = ""  # the database file that lists it
    status: str = (
        ""  # how far the port is checked against the original: `legacy` or `compared` (research_functions.STATUSES)
    )


@dataclass(frozen=True)
class OrigTag:
    """An `@orig` tag found in Coney's source."""

    address: int
    name: str
    where: str  # "src/fileio/wad_index.h:42"


@dataclass(frozen=True)
class Mismatch:
    """An `@orig` tag that spells a function's name differently from the database, which has Ghidra's."""

    tag: OrigTag
    name: str  # the database's

    @property
    def text(self) -> str:
        """`src/x.h:3: @orig 0x... names 'Old', the database 'New'`."""
        return f"{self.tag.where}: @orig {self.tag.address:#010x} names {self.tag.name!r}, the database {self.name!r}"


@dataclass(frozen=True)
class Milestone:
    """A row of the roadmap's status table."""

    name: str
    anchor: str
    status: str


@dataclass
class Progress:
    """Everything the tracker shows, computed from the inputs."""

    totals: Totals
    functions: list[Function]
    milestones: list[Milestone]
    problems: list[str] = field(default_factory=list)
    understanding: Understanding | None = None  # None until the checkout has a Ghidra listing
    mismatches: list[Mismatch] = field(default_factory=list)  # tags spelling a name differently; `new_mismatches`

    @property
    def no_size_reason(self) -> str:
        """Why a reimplemented function can have no size: the listing is missing, or it lacks a function there."""
        if self.understanding is None:
            return (
                "there is no docs/progress/ghidra-functions.tsv, which gives the sizes "
                "(`coney-tools ghidra export-names`)"
            )
        return "the Ghidra listing has no function at their address"

    @property
    def warnings(self) -> list[str]:
        """The mismatches as messages."""
        return [m.text for m in self.mismatches]

    @property
    def game_functions(self) -> int | None:
        """Functions of the game's own code in the Ghidra listing, the whole every share counts; None without one."""
        return None if self.understanding is None else self.understanding.functions

    @property
    def reimplemented_functions(self) -> int:
        """Functions of the game's own code that Coney reimplements.

        Counted in the listing, so a port tagged at an address the listing has no function at is not one (it is
        `unlisted_functions`): the number is then the one the picture counts. Without a listing it is every recorded
        port, which no share can use yet.
        """
        return len(self.listed())

    @property
    def unlisted_functions(self) -> int:
        """Ports recorded in the database at an address the Ghidra listing has no game function at."""
        return len(self.functions) - self.reimplemented_functions

    @property
    def uncounted_functions(self) -> int:
        """Ports no share counts: every recorded port without a listing, else those the listing has no function for."""
        return len(self.functions) if self.understanding is None else self.unlisted_functions

    @property
    def researched_functions(self) -> int | None:
        """Game functions inside a source directory's range of the source map; None without a listing."""
        return None if self.understanding is None else self.understanding.placed_functions

    @property
    def compared_functions(self) -> int:
        """How many reimplemented functions are compared side by side with the original (`status = "compared"`)."""
        return sum(1 for f in self.listed() if f.status == "compared")

    def listed(self) -> list[Function]:
        """The functions `reimplemented_functions` counts: all of them without a listing, else those it has."""
        if self.understanding is None:
            return list(self.functions)
        known = {s.function.address for s in self.understanding.statuses}
        return [f for f in self.functions if f.address in known]

    def subsystem_rows(self) -> list[tuple[str, int, int]]:
        """(name, game functions, reimplemented functions) per subsystem, then unattributed.

        The total counts the listing's functions in the subsystem's range (0 for a subsystem with none, or without a
        listing), so the rows add up to `game_functions`.
        """
        totals: dict[str, int] = {}
        owner: dict[int, str] = {}
        if self.understanding is not None:
            for status in self.understanding.statuses:
                totals[status.subsystem] = totals.get(status.subsystem, 0) + 1
                owner[status.function.address] = status.subsystem
        done: dict[str, int] = {}
        for f in self.listed():
            name = owner.get(f.address, f.subsystem)
            done[name] = done.get(name, 0) + 1
        names = [s.name for s in self.totals.subsystems] + [UNATTRIBUTED]
        return [(name, totals.get(name, 0), done.get(name, 0)) for name in names]

    def to_json(self) -> dict[str, Any]:
        """The summary as plain data, for `coney-tools progress summary --json`: every share counts functions."""
        game = self.game_functions
        return {
            "reimplemented": _share(self.reimplemented_functions, game),
            "researched": None if game is None else _share(self.researched_functions or 0, game),
            "compared": _share(self.compared_functions, self.reimplemented_functions),
            "unlisted_functions": self.unlisted_functions,
            "functions_without_size": sum(1 for f in self.functions if f.size is None),
            "subsystems": [{"name": name} | _share(done, total) for name, total, done in self.subsystem_rows()],
            "coverage": [
                {"key": c.key, "label": c.label, "kind": c.kind, "bytes": c.bytes} for c in self.totals.coverage
            ],
            "middleware": [
                {"name": m.name, "bytes": m.bytes, "replaced_by": m.replaced_by} for m in self.totals.middleware
            ],
            "milestones": [{"name": m.name, "anchor": m.anchor, "status": m.status} for m in self.milestones],
            "understood": self._understood_json(),
            "problems": self.problems,
        }

    def _understood_json(self) -> dict[str, Any] | None:
        """The "Understood" measure as plain data, or None without a listing."""
        u = self.understanding
        if u is None:
            return None
        order = [s.name for s in self.totals.subsystems]
        return _share(u.understood_functions, u.functions) | {
            "subsystems": [
                {"name": r.name} | _share(r.understood_functions, r.functions) for r in u.by_subsystem(order)
            ],
        }


def _share(part: int, whole: int | None) -> dict[str, Any]:
    """`part` of `whole` functions as the functions/total/percent fields of the JSON summary (`whole` None: unknown)."""
    if whole is None:
        return {"functions": part, "total": None, "percent": None}
    return {"functions": part, "total": whole, "percent": round(100.0 * part / whole, 3) if whole else 0.0}


# --- reading the inputs ---------------------------------------------------------------------------------------------


def _read_toml(path: Path) -> dict[str, Any]:
    """Parse a TOML file, turning every failure into a ProgressError that names it."""
    try:
        with path.open("rb") as handle:
            return tomllib.load(handle)
    except FileNotFoundError as error:
        raise ProgressError(f"{path}: missing") from error
    except (tomllib.TOMLDecodeError, UnicodeDecodeError, OSError) as error:
        raise ProgressError(f"{path}: {error}") from error


def _int(path: Path, where: str, value: object) -> int:
    """`value` if it is a non-negative integer (not a bool); otherwise a ProgressError naming `where`."""
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ProgressError(f"{path}: {where} must be a non-negative integer, got {value!r}")
    return value


def _str(path: Path, where: str, value: object) -> str:
    """`value` if it is a non-empty string; otherwise a ProgressError naming `where`."""
    if not isinstance(value, str) or not value:
        raise ProgressError(f"{path}: {where} must be a non-empty string, got {value!r}")
    return value


def _ranges(path: Path, where: str, value: object) -> tuple[Range, ...]:
    """A list of `[start, end]` pairs as Ranges; each must be non-empty."""
    if not isinstance(value, list):
        raise ProgressError(f"{path}: {where}.ranges must be a list of [start, end] pairs")
    result = []
    for pair in value:
        if not isinstance(pair, list) or len(pair) != 2:
            raise ProgressError(f"{path}: {where}.ranges holds {pair!r}, not a [start, end] pair")
        start, end = _int(path, where, pair[0]), _int(path, where, pair[1])
        if end <= start:
            raise ProgressError(f"{path}: {where} has the empty or reversed range [{start:#010x}, {end:#010x}]")
        result.append(Range(start, end))
    return tuple(result)


def _tables(path: Path, data: dict[str, Any], key: str) -> list[dict[str, Any]]:
    """The array of tables `[[key]]` of `data`; none is an empty list."""
    value = data.get(key, [])
    if not isinstance(value, list) or not all(isinstance(item, dict) for item in value):
        raise ProgressError(f"{path}: {key} must be an array of tables ([[{key}]])")
    return value


def load_totals(path: Path) -> Totals:
    """Read totals.toml. Raises ProgressError for a missing file, a bad value or overlapping ranges."""
    data = _read_toml(path)
    text = data.get("text")
    if not isinstance(text, dict):
        raise ProgressError(f"{path}: no [text] table")
    text_range = _ranges(path, "text", [[text.get("start"), text.get("end")]])[0]

    coverage = []
    for i, item in enumerate(_tables(path, data, "coverage")):
        where = f"coverage {i + 1}"
        kind = _str(path, f"{where}.kind", item.get("kind"))
        if kind not in COVERAGE_KINDS:
            raise ProgressError(f"{path}: {where}.kind is {kind!r}; expected one of {', '.join(COVERAGE_KINDS)}")
        coverage.append(
            Coverage(
                _str(path, f"{where}.key", item.get("key")),
                _str(path, f"{where}.label", item.get("label")),
                kind,
                _int(path, f"{where}.bytes", item.get("bytes")),
            )
        )
    subsystems = []
    for item in _tables(path, data, "subsystem"):
        name = _str(path, "subsystem.name", item.get("name"))
        subsystems.append(Subsystem(name, _ranges(path, f"subsystem {name}", item.get("ranges"))))
    middleware = []
    for item in _tables(path, data, "middleware"):
        name = _str(path, "middleware.name", item.get("name"))
        middleware.append(
            Middleware(
                name,
                _int(path, f"middleware {name}.bytes", item.get("bytes")),
                _str(path, f"middleware {name}.replaced_by", item.get("replaced_by")),
                _ranges(path, f"middleware {name}", item.get("ranges")),
            )
        )

    # Cross-checks: unique names, no two ranges overlapping, every range inside .text, and no more subsystem bytes
    # than game code.
    names = [s.name for s in subsystems]
    if UNATTRIBUTED in names or len(set(names)) != len(names):
        raise ProgressError(f"{path}: subsystem names must be unique and not {UNATTRIBUTED!r}")
    labelled = [(s.name, r) for s in subsystems for r in s.ranges] + [(m.name, r) for m in middleware for r in m.ranges]
    labelled.sort(key=lambda item: item[1].start)
    for (name, r), (next_name, next_r) in pairwise(labelled):
        if next_r.start < r.end:
            raise ProgressError(f"{path}: the ranges of {name} and {next_name} overlap at {next_r.start:#010x}")
    for name, r in labelled:
        if r.start < text_range.start or r.end > text_range.end:
            raise ProgressError(f"{path}: a range of {name} leaves .text")
    totals = Totals(text_range, tuple(coverage), tuple(subsystems), tuple(middleware))
    if totals.unattributed_bytes < 0:
        raise ProgressError(f"{path}: the subsystem ranges add up to more than the game code in the coverage table")
    return totals


def load_database(root: Path) -> list[research_functions.SystemFunctions]:
    """The function database under `root`. Raises ProgressError when a file of it is malformed."""
    try:
        return research_functions.load_all(root)
    except research_functions.ResearchFunctionsError as error:
        raise ProgressError(str(error)) from error


def reimplemented(
    systems: list[research_functions.SystemFunctions], totals: Totals, sizes: dict[int, int] | None = None
) -> list[Function]:
    """The functions Coney reimplements: the entries of `systems` that have `coney`, in address order.

    `sizes` maps an address to the size of the function there in the Ghidra listing (None without a listing; an
    address the listing has no function at has none either, and the function then counts but adds no bytes).
    """
    functions = [
        Function(
            entry.address,
            entry.name,
            totals.subsystem_of(entry.address) or "",
            (sizes or {}).get(entry.address),
            entry.coney,
            system.system,
            entry.status,
        )
        for system in systems
        for entry in system.entries
        if entry.coney
    ]
    return sorted(functions, key=lambda f: f.address)


def tags_in(path: str, text: str) -> list[OrigTag]:
    """The `@orig` tags in the C++ file `path` whose text is `text`."""
    tags = []
    for number, line in enumerate(text.splitlines(), start=1):
        match = _ORIG_TAG.search(line)
        if match:
            tags.append(OrigTag(int(match["address"], 16), match["name"], f"{path}:{number}"))
    return tags


def scan_orig_tags(root: Path, source_dir: Path = SOURCE_DIR) -> list[OrigTag]:
    """Find every `@orig` tag in the C++ sources under `root / source_dir`."""
    tags: list[OrigTag] = []
    base = root / source_dir
    if not base.is_dir():
        return tags
    for path in sorted(p for p in base.rglob("*") if p.suffix in _SOURCE_SUFFIXES and p.is_file()):
        text = path.read_text(encoding="utf-8", errors="replace")
        tags += tags_in(path.relative_to(root).as_posix(), text)
    return tags


def base_tag_keys(root: Path, base: str) -> set[tuple[int, str]]:
    """The (address, name) of every `@orig` tag as it was where HEAD left `base`.

    The files the branch changed are read at that commit (a renamed file under its old path), the rest as they are.
    Raises ConfigError when git cannot say (no such base, no repository).
    """
    # Imported here: these modules build on this one.
    from coney_tools import local_gate

    changed = {p for p in local_gate.changed_files(root, base) if p.startswith(f"{SOURCE_DIR.as_posix()}/")}
    since = local_gate.merge_base(root, base)
    renamed = local_gate.renamed_files(root, base)
    keys = {(t.address, t.name) for t in scan_orig_tags(root) if t.where.rsplit(":", 1)[0] not in changed}
    for path in changed:
        text = local_gate.file_at(root, since, renamed.get(path, path))
        if text is not None:
            keys |= {(t.address, t.name) for t in tags_in(path, text)}
    return keys


def parse_milestones(roadmap: str) -> list[Milestone]:
    """The roadmap's status table: every row of the form `| [Name](#anchor) | status |`, in order."""
    milestones = []
    for line in roadmap.splitlines():
        match = _MILESTONE_ROW.match(line.strip())
        if match:
            milestones.append(Milestone(match["name"], match["anchor"], match["status"]))
    return milestones


# --- checks -----------------------------------------------------------------------------------------------------------


def _database(function: Function) -> str:
    """How a problem names the database entry of `function`."""
    path = f"{research_functions.FUNCTIONS_DIR.as_posix()}/{function.system}.toml"
    return f"{path}: {function.address:#010x} {function.name}"


def check(
    totals: Totals, functions: list[Function], tags: list[OrigTag], unported: dict[int, str] | None = None
) -> list[str]:
    """Problems with the reimplemented functions, alone and against the `@orig` tags; an empty list means consistent.

    `unported` maps the address of every database entry without `coney` to its system, to tell a tag whose entry lacks
    the field from a tag with no entry at all.
    """
    problems = []
    listed = {f.address: f for f in functions}
    for f in functions:
        if not f.subsystem:
            problems.append(
                f"{_database(f)}: not game code (outside .text or inside middleware, which is not reimplemented)"
            )

    # Against the source: every tag recorded in the database, and every recorded port tagged in its file.
    tagged: dict[int, list[OrigTag]] = {}
    for tag in tags:
        tagged.setdefault(tag.address, []).append(tag)
    for address, here in sorted(tagged.items()):
        tag = here[0]
        if address in listed:
            continue
        if unported is not None and address in unported:
            problems.append(
                f"{tag.where}: @orig {address:#010x} {tag.name}: {research_functions.FUNCTIONS_DIR.as_posix()}/"
                f"{unported[address]}.toml lists it without `coney`, the place of the port"
            )
        else:
            problems.append(
                f"{tag.where}: @orig {address:#010x} {tag.name} has no entry in the function database "
                f"({research_functions.FUNCTIONS_DIR.as_posix()}/<system>.toml)"
            )
    for f in functions:
        found = tagged.get(f.address)
        if not found:
            problems.append(f"{_database(f)}: no @orig tag in src/ reimplements it")
            continue
        port = research_functions.CONEY.match(f.coney)
        files = {t.where.rsplit(":", 1)[0] for t in found}
        if port and port["file"] not in files:
            problems.append(
                f"{_database(f)}: coney names {port['file']}, but its @orig tag is in {', '.join(sorted(files))}"
            )
    return problems


def name_mismatches(functions: list[Function], tags: list[OrigTag]) -> list[Mismatch]:
    """The tags that spell a function's name differently from the database, which has Ghidra's name.

    The tag's name is free text copied when the function was ported; Ghidra's name is the one to use. `progress check`
    fails on the ones a branch adds (`new_mismatches`) and warns about the rest.
    """
    listed = {f.address: f for f in functions}
    return [
        Mismatch(tag, listed[tag.address].name)
        for tag in tags
        if tag.address in listed and tag.name and tag.name != listed[tag.address].name
    ]


def new_mismatches(mismatches: list[Mismatch], base_tags: set[tuple[int, str]]) -> list[Mismatch]:
    """The mismatches of tags that did not exist, under that address and name, where the branch left its base."""
    return [m for m in mismatches if (m.tag.address, m.tag.name) not in base_tags]


def load(root: Path, functions: list[Function] | None = None) -> Progress:
    """Read every input under the checkout `root` and check them against each other.

    `functions` replaces the database as the list of reimplemented functions, for a checkout from before the database
    held them (the history of earlier days); nothing is then checked against the `@orig` tags.
    """
    # Imported here: progress_research builds on this module's Totals.
    from coney_tools import progress_research

    totals = load_totals(root / TOTALS_FILE)
    listing = progress_research.load_listing(root / progress_research.LISTING_FILE)
    try:
        roadmap = (root / ROADMAP_FILE).read_text(encoding="utf-8")
    except OSError as error:
        raise ProgressError(f"{root / ROADMAP_FILE}: cannot be read ({error})") from error
    milestones = parse_milestones(roadmap)
    if not milestones:
        raise ProgressError(f"{ROADMAP_FILE}: no status table (rows like `| [Name](#anchor) | status |`)")
    problems: list[str] = []
    mismatches: list[Mismatch] = []
    if functions is None:
        systems = load_database(root)
        sizes = {f.address: f.size for f in listing} if listing is not None else None
        functions = reimplemented(systems, totals, sizes)
        tags = scan_orig_tags(root)
        unported = {e.address: s.system for s in systems for e in s.entries if not e.coney}
        problems = check(totals, functions, tags, unported)
        mismatches = name_mismatches(functions, tags)
    return Progress(totals, functions, milestones, problems, progress_research.load(root, totals, listing), mismatches)

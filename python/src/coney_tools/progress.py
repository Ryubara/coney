# SPDX-License-Identifier: GPL-3.0-or-later
"""The progress tracker's data: what share of the original game Coney reimplements, and how much is researched.

Three committed inputs feed it, none of them game data:

* `docs/progress/totals.toml`: the denominators, taken by hand from the source map
  (docs/research/source-map.md): `.text`, the coverage categories, each subsystem's address ranges and the
  middleware, which Coney replaces rather than reimplements;
* `docs/progress/functions.toml`: the numerator, one entry per original function Coney reimplements;
* the status table of `docs/roadmap.md`, parsed rather than copied;
* `docs/progress/ghidra-functions.tsv` and the research pages, for the "Understood" measure
  (coney_tools.progress_research).

It also checks that functions.toml agrees with the `@orig` tags in `src/` (docs/guides/conventions.md), so an
implementer cannot add a tag without counting it, or the other way round.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass, field
from itertools import pairwise
from pathlib import Path
from typing import TYPE_CHECKING, Any

from coney_tools.config import ConfigError

if TYPE_CHECKING:
    from coney_tools.progress_research import Understanding

DATA_DIR = Path("docs/progress")
TOTALS_FILE = DATA_DIR / "totals.toml"
FUNCTIONS_FILE = DATA_DIR / "functions.toml"
PAGE_FILE = DATA_DIR / "index.md"
README_FILE = Path("README.md")
ROADMAP_FILE = Path("docs/roadmap.md")
SOURCE_DIR = Path("src")

#: The subsystem of game code that lies in no subsystem range of totals.toml.
UNATTRIBUTED = "unattributed"
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
    """An original function Coney reimplements (one [[function]] of functions.toml)."""

    address: int
    name: str
    subsystem: str
    size: int | None  # None until someone fills it in


@dataclass(frozen=True)
class OrigTag:
    """An `@orig` tag found in Coney's source."""

    address: int
    name: str
    where: str  # "src/fileio/wad_index.h:42"


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

    @property
    def reimplemented_bytes(self) -> int:
        """Bytes of the original's code that Coney reimplements (functions with a known size)."""
        return sum(f.size or 0 for f in self.functions)

    def subsystem_rows(self) -> list[tuple[str, int, int, int]]:
        """(name, total bytes, reimplemented bytes, reimplemented functions) per subsystem, then unattributed."""
        rows = [(s.name, s.bytes) for s in self.totals.subsystems] + [(UNATTRIBUTED, self.totals.unattributed_bytes)]
        result = []
        for name, total in rows:
            mine = [f for f in self.functions if f.subsystem == name]
            result.append((name, total, sum(f.size or 0 for f in mine), len(mine)))
        return result

    def to_json(self) -> dict[str, Any]:
        """The summary as plain data, for `coney-tools progress show --json`."""
        game = self.totals.game_bytes
        return {
            "reimplemented": _share(self.reimplemented_bytes, game) | {"functions": len(self.functions)},
            "researched": _share(self.totals.researched_bytes, game),
            "functions_without_size": sum(1 for f in self.functions if f.size is None),
            "subsystems": [
                {"name": name, "functions": count} | _share(done, total)
                for name, total, done, count in self.subsystem_rows()
            ],
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
        return _share(u.understood_bytes, u.bytes) | {
            "functions": u.understood_functions,
            "total_functions": u.functions,
            "subsystems": [
                {"name": r.name, "functions": r.understood_functions, "total_functions": r.functions}
                | _share(r.understood_bytes, r.bytes)
                for r in u.by_subsystem(order)
            ],
        }


def _share(part: int, whole: int) -> dict[str, Any]:
    """`part` of `whole` as the bytes/total/percent fields of the JSON summary."""
    return {"bytes": part, "total": whole, "percent": round(100.0 * part / whole, 3) if whole else 0.0}


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


def load_functions(path: Path) -> list[Function]:
    """Read functions.toml (an empty file is no functions). Raises ProgressError for a malformed entry."""
    functions = []
    for i, item in enumerate(_tables(path, _read_toml(path), "function")):
        where = f"function {i + 1}"
        unknown = sorted(set(item) - {"address", "name", "subsystem", "size"})
        if unknown:
            raise ProgressError(f"{path}: {where} has unknown key(s) {', '.join(unknown)}")
        size = item.get("size")
        functions.append(
            Function(
                _int(path, f"{where}.address", item.get("address")),
                _str(path, f"{where}.name", item.get("name")),
                _str(path, f"{where}.subsystem", item.get("subsystem")),
                None if size is None else _int(path, f"{where}.size", size),
            )
        )
    return functions


def scan_orig_tags(root: Path, source_dir: Path = SOURCE_DIR) -> list[OrigTag]:
    """Find every `@orig` tag in the C++ sources under `root / source_dir`."""
    tags: list[OrigTag] = []
    base = root / source_dir
    if not base.is_dir():
        return tags
    for path in sorted(p for p in base.rglob("*") if p.suffix in _SOURCE_SUFFIXES and p.is_file()):
        text = path.read_text(encoding="utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), start=1):
            match = _ORIG_TAG.search(line)
            if match:
                where = f"{path.relative_to(root).as_posix()}:{number}"
                tags.append(OrigTag(int(match["address"], 16), match["name"], where))
    return tags


def parse_milestones(roadmap: str) -> list[Milestone]:
    """The roadmap's status table: every row of the form `| [Name](#anchor) | status |`, in order."""
    milestones = []
    for line in roadmap.splitlines():
        match = _MILESTONE_ROW.match(line.strip())
        if match:
            milestones.append(Milestone(match["name"], match["anchor"], match["status"]))
    return milestones


# --- checks -----------------------------------------------------------------------------------------------------------


def check(totals: Totals, functions: list[Function], tags: list[OrigTag]) -> list[str]:
    """Problems with functions.toml, alone and against the `@orig` tags; an empty list means consistent."""
    # functions.toml on its own: no duplicates, each in the subsystem the source map gives, sizes that fit.
    problems = []
    seen: dict[int, Function] = {}
    for f in functions:
        label = f"functions.toml: {f.address:#010x} {f.name}"
        expected = totals.subsystem_of(f.address)
        if f.address in seen:
            problems.append(f"{label}: listed twice")
        seen[f.address] = f
        if expected is None:
            problems.append(f"{label}: not game code (outside .text or inside middleware, which is not reimplemented)")
        elif f.subsystem != expected:
            problems.append(f"{label}: subsystem is {f.subsystem!r}; the source map puts it in {expected!r}")
        if f.size is not None and (f.size == 0 or f.size % 4):
            problems.append(f"{label}: size {f.size} is not a positive multiple of 4 (one MIPS instruction)")
    ordered = sorted((f for f in functions if f.size), key=lambda f: f.address)
    for f, after in pairwise(ordered):
        if f.address + (f.size or 0) > after.address:
            problems.append(f"functions.toml: {f.address:#010x} {f.name}: its size runs into {after.address:#010x}")

    # Against the source: every tag listed under the same name, and every listed function tagged.
    tagged: dict[int, OrigTag] = {}
    for tag in tags:
        tagged.setdefault(tag.address, tag)
        listed = seen.get(tag.address)
        if listed is None:
            problems.append(f"{tag.where}: @orig {tag.address:#010x} {tag.name} has no entry in functions.toml")
        elif tag.name and tag.name != listed.name:
            problems.append(
                f"{tag.where}: @orig {tag.address:#010x} names {tag.name!r}, functions.toml {listed.name!r}"
            )
    for f in functions:
        if f.address not in tagged:
            problems.append(f"functions.toml: {f.address:#010x} {f.name}: no @orig tag in src/ reimplements it")
    return problems


def load(root: Path) -> Progress:
    """Read every input under the checkout `root` and check them against each other."""
    totals = load_totals(root / TOTALS_FILE)
    functions = load_functions(root / FUNCTIONS_FILE)
    try:
        roadmap = (root / ROADMAP_FILE).read_text(encoding="utf-8")
    except OSError as error:
        raise ProgressError(f"{root / ROADMAP_FILE}: cannot be read ({error})") from error
    milestones = parse_milestones(roadmap)
    if not milestones:
        raise ProgressError(f"{ROADMAP_FILE}: no status table (rows like `| [Name](#anchor) | status |`)")
    problems = check(totals, functions, scan_orig_tags(root))
    # Imported here: progress_research builds on this module's Totals.
    from coney_tools import progress_research

    return Progress(totals, functions, milestones, problems, progress_research.load(root, totals))

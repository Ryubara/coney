# SPDX-License-Identifier: GPL-3.0-or-later
"""The original's source files and the addresses the source map gives each, as data (`research/source-files.toml`).

`SLUS_212.15` has no symbols, but its `.rodata` holds the paths of the source files it was built from, and the source
map (docs/research/source-map.md) works out which stretch of `.text` came from which file. Its per-file tables are this
file's rows: the file, its attributed range, how many functions lie in it, the static-initialiser stub that probably
ends its translation unit, and the anchors (functions that load the file's path string) with the allocation tags each
passes. Tools read the data directly: the owner lookup of the research pages (`research where`, research_owners.py),
the progress picture (progress_grid.py) and the check of the page `owns` (research_owners.py, research_check.py). The
docs build fills the file tables of the source map page from it (research_find.py), so the page cannot drift.

The ranges are inferred, like the directory ranges of `docs/progress/totals.toml`, which stays hand-kept because its
subsystem ranges are widened by stretches placed by position and its coverage bytes come from the whole map, neither of
which a file range gives. `check_totals` holds the two together: a file lies in its directory's range, or in a
middleware range, and no subsystem range takes in a file that is not its own.

This module is the data model, the schema and the one canonical text; it depends on nothing of the tracker but the
totals file it checks against.

Research: docs/research/source-map.md
"""

from __future__ import annotations

import bisect
import tomllib
from dataclasses import dataclass, field
from itertools import pairwise
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError
from coney_tools.progress import Totals
from coney_tools.research_functions import quote

#: Where the data lives; `research.files.source_files` of `.github/conventions.yml` names the same path (a test holds
#: the two equal).
SOURCE_FILES = Path("research/source-files.toml")
_MAX_ADDRESS = 0xFFFFFFFF
_FILE_KEYS = frozenset(
    {
        "directory",
        "path",
        "start",
        "end",
        "path_string",
        "functions",
        "stub",
        "anchors",
        "more_anchors",
        "note",
    }
)
_ANCHOR_KEYS = frozenset({"address", "tags"})
_DEFAULT_HEADER = (
    "# The source files of the executable and the addresses the source map gives each, one [[file]] each, in the\n"
    "# source map's order (by directory, then by name). Fields: docs/guides/research-source-files.md."
)


class SourceFilesError(ConfigError):
    """The source files data is malformed; the message names the file and every problem found in it."""


@dataclass(frozen=True)
class FileRange:
    """A source file of the original and the `[start, end)` addresses the source map attributes to it."""

    path: str
    start: int
    end: int


@dataclass(frozen=True)
class Anchor:
    """A function that loads the file's path string (so it was compiled from that file), and the allocation tags or
    other strings it passes, mostly class names."""

    address: int
    tags: tuple[str, ...] = ()


@dataclass(frozen=True)
class SourceFile:
    """One source file of the original, or a unit with no path string that the map places in a directory."""

    #: The source directory whose table the file is in (`Device/ps2`, `GUI/RumbleModeGUI`).
    directory: str
    #: The file's path as the executable names it below `c:/Warriors/Source/`, directory first.
    path: str
    #: The attributed range `[start, end)`; both None for a file whose path string no code loads.
    start: int | None = None
    end: int | None = None
    #: Functions the map attributes to the file (0 without a range).
    functions: int = 0
    #: The static-initialiser stub that probably ends the file's unit.
    stub: int | None = None
    anchors: tuple[Anchor, ...] = field(default_factory=tuple)
    #: How many anchors there are beyond the ones listed.
    more_anchors: int = 0
    #: Where the path string sits in `.rodata`, for a file with no range.
    path_string: int | None = None
    #: What the other fields do not say.
    note: str = ""

    @property
    def name(self) -> str:
        """The file's path below its directory (`deep/Three.cpp`)."""
        return self.path[len(self.directory) + 1 :]

    @property
    def has_range(self) -> bool:
        """Whether the map attributes code to the file."""
        return self.start is not None and self.end is not None

    def range(self) -> FileRange | None:
        """The file and its attributed range, or None when it has none."""
        if self.start is None or self.end is None:
            return None
        return FileRange(self.path, self.start, self.end)


@dataclass(frozen=True)
class SourceFiles:
    """The data file: its leading comment lines and its files in file order."""

    header: str
    files: tuple[SourceFile, ...]

    def ranges(self) -> tuple[FileRange, ...]:
        """The files that have a range, in address order."""
        found = [r for f in self.files if (r := f.range()) is not None]
        return tuple(sorted(found, key=lambda r: r.start))


# --- reading ------------------------------------------------------------------------------------------------------


def _header_of(text: str) -> str:
    """The comment lines at the top of a file."""
    lines = []
    for line in text.splitlines():
        if not line.startswith("#"):
            break
        lines.append(line.rstrip())
    return "\n".join(lines)


def _is_int(value: object) -> bool:
    """True for an integer that is not a boolean."""
    return isinstance(value, int) and not isinstance(value, bool)


def _address(item: dict[str, Any], key: str, label: str, problems: list[str]) -> int | None:
    """The optional address `key` of `item`, or None (with a problem noted when it is not an address)."""
    if key not in item:
        return None
    value = item[key]
    if not _is_int(value) or not 0 <= value <= _MAX_ADDRESS:
        problems.append(f"{label}: {key} must be an address (an integer up to 0xffffffff), got {value!r}")
        return None
    return int(value)


def _count(value: object, key: str, label: str, problems: list[str]) -> int:
    """The count `value` of `key` (0 or more), or 0 with a problem noted when it is not one."""
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        problems.append(f"{label}: {key} must be a count (a whole number, 0 or more), got {value!r}")
        return 0
    return value


def _anchors(item: object, label: str, problems: list[str]) -> tuple[Anchor, ...]:
    """The `anchors` array of a file."""
    if not isinstance(item, list):
        problems.append(f"{label}: anchors must be an array of {{address, tags}} tables")
        return ()
    found: list[Anchor] = []
    for index, raw in enumerate(item, start=1):
        where = f"{label} anchor {index}"
        if not isinstance(raw, dict):
            problems.append(f"{where}: must be a table with address and tags")
            continue
        for key in sorted(set(raw) - _ANCHOR_KEYS):
            problems.append(f"{where}: unknown key {key!r} (keys: {', '.join(sorted(_ANCHOR_KEYS))})")
        address = _address(raw, "address", where, problems)
        tags = raw.get("tags", [])
        if not isinstance(tags, list) or not all(isinstance(tag, str) and tag for tag in tags):
            problems.append(f"{where}: tags must be a list of strings")
            tags = []
        if address is None:
            if "address" not in raw:
                problems.append(f"{where}: address is missing")
            continue
        found.append(Anchor(address, tuple(tags)))
    return tuple(found)


def _file(item: object, number: int, where: str, problems: list[str]) -> SourceFile | None:
    """One `[[file]]` table as a SourceFile, or None with its problems noted."""
    if not isinstance(item, dict):
        problems.append(f"{where}: file {number} is not a table")
        return None
    start_count = len(problems)
    path = item.get("path")
    label = f"{where}: file {number}" + (f" ({path})" if isinstance(path, str) else "")
    for key in sorted(set(item) - _FILE_KEYS):
        problems.append(f"{label}: unknown key {key!r} (keys: {', '.join(sorted(_FILE_KEYS))})")
    directory = item.get("directory")
    if not isinstance(directory, str) or not directory.strip():
        problems.append(f"{label}: directory must name the source directory of the file's table")
        directory = ""
    if not isinstance(path, str) or not path.strip():
        problems.append(f"{label}: path must be the file's path, directory first")
        path = ""
    elif directory and not path.startswith(directory + "/"):
        problems.append(f"{label}: path must start with its directory {directory!r} and a slash")
    start = _address(item, "start", label, problems)
    end = _address(item, "end", label, problems)
    if (start is None) != (end is None):
        problems.append(f"{label}: start and end go together: a file has an address range or none")
    elif start is not None and end is not None and end <= start:
        problems.append(f"{label}: the range {start:#010x}-{end:#010x} ends before it starts")
    path_string = _address(item, "path_string", label, problems)
    functions = _count(item.get("functions"), "functions", label, problems)
    stub = _address(item, "stub", label, problems)
    if stub is not None and start is not None and stub < start:
        problems.append(f"{label}: stub {stub:#010x} lies before the file's range")
    more = _count(item.get("more_anchors", 0), "more_anchors", label, problems)
    note = item.get("note", "")
    if not isinstance(note, str) or "\n" in note:
        problems.append(f"{label}: note must be one line of text")
        note = ""
    anchors = _anchors(item.get("anchors", []), label, problems)
    if start is not None and end is not None:
        if functions == 0:
            problems.append(f"{label}: it has an address range, so at least one function")
        for anchor in anchors:
            if not start <= anchor.address < end:
                problems.append(f"{label}: anchor {anchor.address:#010x} lies outside the file's range")
    else:
        if path_string is None:
            problems.append(f"{label}: a file with no address range gives path_string, where its path string is")
        if functions or anchors or more or stub is not None:
            problems.append(f"{label}: it has no address range, so no functions, anchors or stub")
    if len(problems) != start_count:
        return None
    return SourceFile(
        str(directory),
        str(path),
        start,
        end,
        functions,
        stub,
        anchors,
        more,
        path_string,
        note,
    )


def parse(text: str, path: Path | str = SOURCE_FILES) -> SourceFiles:
    """The data file `text` (read from `path`) as files. Raises SourceFilesError listing every problem."""
    where = Path(path).as_posix()
    problems: list[str] = []
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise SourceFilesError(f"{where}: not valid TOML ({error})") from error
    for key in sorted(set(data) - {"file"}):
        problems.append(f"{where}: unknown key {key!r} (the file holds only [[file]] tables)")
    raw = data.get("file", [])
    if not isinstance(raw, list):
        raise SourceFilesError(f"{where}: file must be an array of tables ([[file]])")
    files = [f for i, item in enumerate(raw, start=1) if (f := _file(item, i, where, problems)) is not None]
    seen: set[str] = set()
    for file in files:
        if file.path.casefold() in seen:
            problems.append(f"{where}: {file.path} is listed twice")
        seen.add(file.path.casefold())
    ranged = sorted((f for f in files if f.start is not None and f.end is not None), key=lambda f: f.start or 0)
    for before, after in pairwise(ranged):
        if after.start is not None and before.end is not None and after.start < before.end:
            problems.append(
                f"{where}: {before.path} and {after.path} overlap at {after.start:#010x}: a file's code is its own"
            )
    if problems:
        raise SourceFilesError("\n".join(problems))
    return SourceFiles(_header_of(text), tuple(files))


def load(root: Path, path: Path = SOURCE_FILES) -> SourceFiles:
    """The data of the checkout at `root`; no files when the file is missing. Raises SourceFilesError."""
    full = root / path
    if not full.is_file():
        return SourceFiles("", ())
    try:
        text = full.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise SourceFilesError(f"{path.as_posix()}: cannot be read ({error})") from error
    return parse(text, path)


def load_ranges(root: Path, path: Path = SOURCE_FILES) -> tuple[FileRange, ...]:
    """The file ranges of the checkout at `root`, in address order (none when it has no data file)."""
    return load(root, path).ranges()


def file_of(ranges: list[FileRange] | tuple[FileRange, ...], starts: list[int], address: int) -> str:
    """The source file whose range holds `address`, or an empty string. `starts` are the ranges' starts, in order."""
    i = bisect.bisect_right(starts, address) - 1
    if i >= 0 and address < ranges[i].end:
        return ranges[i].path
    return ""


# --- checking against totals.toml ---------------------------------------------------------------------------------


def check_totals(files: SourceFiles, totals: Totals) -> list[str]:
    """Where the file data and `totals.toml` disagree, in both directions that the data can hold.

    Files into ranges: a file's range lies inside a range of its directory's subsystem or inside a middleware range. A
    directory is a subsystem of its own name (`Device/ps2`) or of its first component (`GUI/RumbleModeGUI` is `GUI`).

    Ranges onto files: a subsystem range takes in no byte of a file that is not its own (another directory's, or one
    no subsystem owns such as Lua), and a range of a subsystem that has files holds at least one of them. That is
    as tight as it can be: the ranges also cover the stretches the source map places by position and calls, and
    a subsystem such as Maths has no file with an address, so a range cannot be held to its files' first and last
    byte, and one that grew into a gap or a position stretch is not caught.

    `progress.load_totals` already refuses two overlapping ranges, so a range that takes in a file of another subsystem
    can only reach this check together with that file lying outside its own range (the first direction fails too); the
    "too wide" message then names the range that grew. The new failure the second direction alone can raise is a range
    that holds none of its subsystem's files.

    The totals file is hand-kept; this is what stops it and the file data drifting apart.
    """
    subsystems = {s.name: s for s in totals.subsystems}
    # Each file with an address, as (file, range, the subsystem its directory belongs to).
    placed = []
    for file in files.files:
        if file.start is not None and file.end is not None:
            owner = subsystems.get(file.directory) or subsystems.get(file.directory.partition("/")[0])
            placed.append((file, (file.start, file.end), owner))

    found: list[str] = []
    # Files into ranges.
    for file, (start, end), owner in placed:
        inside = owner is not None and any(r.start <= start and end <= r.end for r in owner.ranges)
        library = any(r.start <= start and end <= r.end for m in totals.middleware for r in m.ranges)
        if inside or library:
            continue
        where = f"{start:#010x}-{end:#010x}"
        if owner is None:
            found.append(
                f"{file.path} ({where}): directory {file.directory} is no subsystem of totals.toml, "
                "and the range is in no middleware range"
            )
        else:
            found.append(f"{file.path} ({where}) lies outside the range of {owner.name} in totals.toml")
    # Ranges onto files.
    for subsystem in totals.subsystems:
        own = [(start, end) for _, (start, end), owner in placed if owner is subsystem]
        for span in subsystem.ranges:
            where = f"{span.start:#010x}-{span.end:#010x}"
            for file, (start, end), owner in placed:
                if owner is subsystem or start >= span.end or end <= span.start:
                    continue
                whose = f"belongs to {owner.name}" if owner else "belongs to no subsystem"
                found.append(
                    f"{subsystem.name} in totals.toml is too wide: its range {where} takes in "
                    f"{file.path} ({start:#010x}-{end:#010x}), which {whose}"
                )
            if own and not any(span.start <= start and end <= span.end for start, end in own):
                found.append(f"{subsystem.name} in totals.toml: its range {where} holds none of its files")
    return found


# --- writing ------------------------------------------------------------------------------------------------------


def _file_text(file: SourceFile) -> str:
    """One file as the canonical `[[file]]` block."""
    lines = ["[[file]]", f"directory = {quote(file.directory)}", f"path = {quote(file.path)}"]
    if file.start is not None and file.end is not None:
        lines += [f"start = 0x{file.start:08x}", f"end = 0x{file.end:08x}"]
    if file.path_string is not None:
        lines.append(f"path_string = 0x{file.path_string:08x}")
    lines.append(f"functions = {file.functions}")
    if file.stub is not None:
        lines.append(f"stub = 0x{file.stub:08x}")
    if file.anchors:
        lines.append("anchors = [")
        for anchor in file.anchors:
            tags = ", ".join(quote(tag) for tag in anchor.tags)
            lines.append(f"  {{address = 0x{anchor.address:08x}, tags = [{tags}]}},")
        lines.append("]")
    if file.more_anchors:
        lines.append(f"more_anchors = {file.more_anchors}")
    if file.note:
        lines.append(f"note = {quote(file.note)}")
    return "\n".join(lines)


def dumps(data: SourceFiles) -> str:
    """The canonical text of the data file: its header, then its files in the order held, one blank line apart."""
    blocks = [data.header or _DEFAULT_HEADER, *(_file_text(f) for f in data.files)]
    return "\n\n".join(blocks) + "\n"

# SPDX-License-Identifier: GPL-3.0-or-later
"""The PCSX2 state library: save states of the original that every engineer shares, so nobody drives the game to the
same place twice (docs/guides/pcsx2-states.md).

A save state is game memory (LEGAL.md), so the library is a folder beside the repository, never in it:
`pcsx2_states_dir` in `coney.local.toml`, else `state-library` in the workspace's `emulators/` folder. It holds one
`<id>.p2s` file per state, made read-only, and one `catalog.json` describing them:

```json
{ "format": 1, "states": [ { "id": "pedestrian-grab", "level": "level99", "checkpoint": 3, "description": "...",
  "tags": ["grab"], "position": [1.0, 2.0, 0.25], "heading_deg": 90.0, "pcsx2": "v2.9.94", "patches": [],
  "author": "maintainer", "made": "2026-10-05", "added": "2026-10-21", "sha256": "...", "size": 14866749,
  "made_by": "the maintainer's quick-save slot 6" } ] }
```

Names, our own descriptions, numbers and hashes only: no state data is ever written into the catalog beyond the
player's position, heading, level and checkpoint, which the `where` and `position` recipes read from the state's own
memory (`read_scene`).

A state is never changed: `Library.add` copies a file in and makes it read-only, `Library.verify` hashes the files
again, and `states use` hands out a copy (`pcsx2_states_cli`). The catalog is rewritten through a lock file and a
temporary file, so two agents adding at once cannot lose each other's entry.

Research: docs/guides/pcsx2-states.md
"""

from __future__ import annotations

import contextlib
import json
import math
import os
import re
import stat
import time
import uuid
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import asdict, dataclass, field, fields, replace
from pathlib import Path
from typing import Any

from coney_tools import hooks as hook_tools
from coney_tools import recipes, recipes_run
from coney_tools.config import (
    ConfigError,
    find_repo_root,
    load_config,
    local_config_file,
    main_checkout,
    workspace_folder,
)
from coney_tools.game_memory import ExpressionError
from coney_tools.pcsx2_copies import copy_checked, sha256
from coney_tools.pcsx2_state import EE_MEMORY_SIZE, Patch
from coney_tools.pine import Memory, PineError, Read, Write
from coney_tools.wad import refuse_inside_repo

CATALOG_FILE = "catalog.json"
LOCK_FILE = "catalog.lock"
CATALOG_FORMAT = 1
#: The folder of the workspace's `emulators/` that holds the library when `pcsx2_states_dir` names none.
DEFAULT_FOLDER = "state-library"
#: What a level or checkpoint the tool could not read is called; the catalog says so rather than guessing.
UNKNOWN = "unknown"
#: A state's id: a short slug, also its file name.
SLUG = recipes.STATE_ID
#: A level's name as the game's level table spells it (`level99`).
LEVEL_NAME = re.compile(r"^[A-Za-z0-9_]{1,15}$")
#: The seconds a lock file may be held before it is taken for a dead run's.
LOCK_STALE_SECONDS = 300.0


class LibraryError(ConfigError):
    """A problem with the library or the catalog; the message names the state or file."""


def library_dir(start: Path | None = None) -> Path:
    """The state library's folder: `pcsx2_states_dir` of `coney.local.toml` (this checkout's, else the main
    checkout's), else `state-library` in the workspace's `emulators/` folder beside the main checkout. Like the
    claims folder it ignores `scratch_dir`, so every worktree sees one library."""
    root = find_repo_root(start or Path.cwd())
    main = main_checkout(root)
    paths = load_config(root).paths if local_config_file(root) else {}
    return paths.get("pcsx2_states_dir") or workspace_folder(main, "emulators") / DEFAULT_FOLDER


def check_slug(name: str) -> str:
    """`name` when it is a valid state id, else LibraryError saying what an id is."""
    if not SLUG.match(name):
        raise LibraryError(
            f"{name!r}: a state id is a short slug of lower-case letters, digits and '-' (at most 48 characters, "
            "starting with a letter or digit)"
        )
    return name


@dataclass(frozen=True)
class Entry:
    """One state of the catalog."""

    id: str
    #: The level's name (`level99`) or UNKNOWN.
    level: str
    #: The checkpoint the game held (the number `SetCheckPoint` takes), or None when unknown.
    checkpoint: int | None
    #: What the scene shows and what it is set up for ("facing a pedestrian, ready to grab").
    description: str
    tags: tuple[str, ...] = ()
    #: The player's x, y, z in metres, or None when unknown.
    position: tuple[float, float, float] | None = None
    #: The player's heading in degrees (0 faces +y, anticlockwise), or None when unknown.
    heading_deg: float | None = None
    #: The PCSX2 version that saved the file (`v2.9.94`), or UNKNOWN.
    pcsx2: str = UNKNOWN
    #: The patches and hooks of research/traces/patches/ the state's memory holds.
    patches: tuple[str, ...] = ()
    author: str = ""
    #: The date the state was made (`YYYY-MM-DD`) and the date it was added to the library.
    made: str = ""
    added: str = ""
    sha256: str = ""
    size: int = 0
    #: The recipe or steps that produced it.
    made_by: str = ""

    def to_json(self) -> dict[str, Any]:
        """The entry as the catalog stores it."""
        data = asdict(self)
        for key in ("tags", "patches"):
            data[key] = list(data[key])
        data["position"] = list(self.position) if self.position is not None else None
        return data

    @classmethod
    def from_json(cls, raw: object, where: str) -> Entry:
        """The entry a catalog table describes. Raises LibraryError for a missing, unknown or mistyped field."""
        if not isinstance(raw, dict):
            raise LibraryError(f"{where}: an entry is an object")
        names = {f.name for f in fields(cls)}
        unknown = set(raw) - names
        missing = {"id", "level", "description", "sha256", "size"} - set(raw)
        if unknown or missing:
            wrong = (
                f"unknown field(s) {', '.join(sorted(unknown))}" if unknown else f"missing {', '.join(sorted(missing))}"
            )
            raise LibraryError(f"{where}: {wrong}")
        try:
            position = raw.get("position")
            entry = cls(
                id=str(raw["id"]),
                level=str(raw["level"]),
                checkpoint=None if raw.get("checkpoint") is None else int(raw["checkpoint"]),
                description=str(raw["description"]),
                tags=_strings(raw.get("tags", []), "tags"),
                position=None if position is None else (float(position[0]), float(position[1]), float(position[2])),
                heading_deg=None if raw.get("heading_deg") is None else float(raw["heading_deg"]),
                pcsx2=str(raw.get("pcsx2", UNKNOWN)),
                patches=_strings(raw.get("patches", []), "patches"),
                author=str(raw.get("author", "")),
                made=str(raw.get("made", "")),
                added=str(raw.get("added", "")),
                sha256=str(raw["sha256"]),
                size=int(raw["size"]),
                made_by=str(raw.get("made_by", "")),
            )
        except (TypeError, ValueError, IndexError) as error:
            raise LibraryError(f"{where}: a field has the wrong type ({error})") from error
        check_slug(entry.id)
        return entry


def _strings(value: object, what: str) -> tuple[str, ...]:
    """`value`, a list of strings, as a tuple; ValueError for anything else (a bare string would split into letters)."""
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"{what} is a list of strings")
    return tuple(value)


class Library:
    """The state library in `path`: its catalog and the read-only `<id>.p2s` files beside it."""

    def __init__(self, path: Path) -> None:
        """The library in the folder `path` (made when the first state is added)."""
        self.path = path

    @property
    def catalog_path(self) -> Path:
        """The catalog file."""
        return self.path / CATALOG_FILE

    def state_path(self, state_id: str) -> Path:
        """Where the file of state `state_id` is."""
        return self.path / f"{check_slug(state_id)}.p2s"

    def entries(self) -> list[Entry]:
        """Every entry in catalog order; none when the library does not exist yet. Raises LibraryError for a catalog
        that cannot be read or lists an id twice."""
        file = self.catalog_path
        if not file.is_file():
            return []
        try:
            data = json.loads(file.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise LibraryError(f"{file}: {error}") from error
        listed = data.get("states") if isinstance(data, dict) else None
        if not isinstance(data, dict) or data.get("format") != CATALOG_FORMAT or not isinstance(listed, list):
            raise LibraryError(f"{file}: not a state catalog of format {CATALOG_FORMAT}")
        entries = [Entry.from_json(raw, f"{file}: states[{k}]") for k, raw in enumerate(listed)]
        seen: set[str] = set()
        for entry in entries:
            if entry.id in seen:
                raise LibraryError(f"{file}: id {entry.id} appears twice")
            seen.add(entry.id)
        return entries

    def get(self, state_id: str) -> Entry:
        """The entry `state_id`, or LibraryError listing the ids there are."""
        for entry in self.entries():
            if entry.id == state_id:
                return entry
        known = ", ".join(e.id for e in self.entries()) or "none yet"
        raise LibraryError(f"no state {state_id!r} in {self.path}; there are: {known}")

    def find(self, level: str | None = None, tag: str | None = None) -> list[Entry]:
        """The entries of one level (case-insensitive) and/or carrying one tag, in catalog order."""
        return [
            e
            for e in self.entries()
            if (level is None or e.level.lower() == level.lower()) and (tag is None or tag.lower() in e.tags)
        ]

    @contextlib.contextmanager
    def locked(self, timeout: float = 20.0) -> Iterator[None]:
        """Hold the catalog's lock file while the block runs (a lock older than LOCK_STALE_SECONDS belongs to a run
        that died and is taken over). LibraryError when it stays held for `timeout` seconds."""
        try:
            self.path.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            raise LibraryError(f"{self.path}: {error}") from error
        lock = self.path / LOCK_FILE
        deadline = time.monotonic() + timeout
        # A token in the lock file says whose it is, so a run never removes the lock another run took over.
        token = uuid.uuid4().hex
        while True:
            try:
                descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except FileExistsError:
                with contextlib.suppress(OSError):
                    if time.time() - lock.stat().st_mtime > LOCK_STALE_SECONDS:
                        lock.unlink()
                        continue
                if time.monotonic() > deadline:
                    raise LibraryError(f"{lock}: held by another run for {timeout:.0f} s; try again") from None
                time.sleep(0.05)
            except OSError as error:
                raise LibraryError(f"{lock}: {error}") from error
            else:
                with os.fdopen(descriptor, "w", encoding="ascii") as handle:
                    handle.write(token)
                break
        try:
            yield
        finally:
            with contextlib.suppress(OSError):
                if lock.read_text(encoding="ascii") == token:
                    lock.unlink()

    def _write_catalog(self, entries: Sequence[Entry]) -> None:
        """Write the catalog through a temporary file, so a failed write leaves the old catalog."""
        text = json.dumps({"format": CATALOG_FORMAT, "states": [e.to_json() for e in entries]}, indent=2) + "\n"
        partial = self.catalog_path.with_name(CATALOG_FILE + ".part")
        partial.write_text(text, encoding="utf-8")
        partial.replace(self.catalog_path)

    def add(self, entry: Entry, source: Path) -> Entry:
        """Copy the file `source` into the library as `entry.id` and add the entry (its hash and size filled in from
        the copy), returning it. The copy is made read-only; `source` is only read. Raises LibraryError when the id or
        its file is already there, or when the library would be inside the repository (game memory never goes there)."""
        refuse_inside_repo(self.path)
        target = self.state_path(entry.id)
        try:
            return self._add(entry, source, target)
        except OSError as error:
            raise LibraryError(f"{self.path}: {error}") from error

    def _add(self, entry: Entry, source: Path, target: Path) -> Entry:
        """`add` once the library is known to be outside the repository."""
        with self.locked():
            entries = self.entries()
            if any(e.id == entry.id for e in entries):
                raise LibraryError(f"state {entry.id} is already in the library; ids are never reused (pick another)")
            if target.exists():
                raise LibraryError(f"{target} exists but is not in the catalog; `states verify` lists such files")
            digest = copy_checked(source, target, force=False)
            added = replace(entry, sha256=digest, size=target.stat().st_size)
            try:
                self._write_catalog([*entries, added])
            except OSError:
                target.unlink(missing_ok=True)
                raise
            target.chmod(stat.S_IREAD | stat.S_IRGRP | stat.S_IROTH)
        return added

    def verify(self, ids: Sequence[str] = ()) -> Report:
        """Hash the files of `ids` (every entry when none) against the catalog, and look for files the catalog does
        not list."""
        report = Report()
        entries = self.entries()
        wanted = set(ids)
        unknown = wanted - {e.id for e in entries}
        if unknown:
            raise LibraryError(f"no state {', '.join(sorted(unknown))} in the catalog")
        for entry in entries:
            if wanted and entry.id not in wanted:
                continue
            report.checked += 1
            path = self.state_path(entry.id)
            if not path.is_file():
                report.problems.append(f"{entry.id}: {path.name} is missing")
                continue
            if path.stat().st_size != entry.size:
                report.problems.append(f"{entry.id}: {path.stat().st_size} bytes, the catalog says {entry.size}")
            elif sha256(path) != entry.sha256:
                report.problems.append(f"{entry.id}: the file's SHA-256 differs from the catalog's")
            if path.stat().st_mode & stat.S_IWUSR:
                report.notes.append(f"{entry.id}: the file is writable (a library file is read-only)")
        if not wanted and self.path.is_dir():
            listed = {f"{e.id}.p2s" for e in entries}
            report.problems += [
                f"{p.name}: in the library folder but not in the catalog"
                for p in sorted(self.path.glob("*.p2s"))
                if p.name not in listed
            ]
        return report


@dataclass
class Report:
    """What `Library.verify` found: the states checked, what is wrong and what is only worth a note."""

    checked: int = 0
    problems: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)


# -- reading a state's own memory


class StateMemory:
    """A read-only `Memory` over a state's EE RAM, so the recipes that read the game (`where`, `position`) read a state
    file exactly as they read a running copy over PINE."""

    def __init__(self, ram: bytes) -> None:
        """The memory of `ram`, the state's `eeMemory.bin`."""
        self.ram = ram

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """The reads' values, unsigned. A write is refused (a state is only read); an address outside the RAM is a
        PineError, as a read PCSX2 refuses is."""
        if writes:
            raise PineError("a saved state is only read")
        values = []
        for read in reads:
            if not 0 <= read.address <= len(self.ram) - read.size or read.address >= EE_MEMORY_SIZE:
                raise PineError(f"{read.address:#010x} is outside the state's RAM")
            values.append(int.from_bytes(self.ram[read.address : read.address + read.size], "little"))
        return values


@dataclass(frozen=True)
class Scene:
    """What the recipes read of a game: the level, checkpoint and the player's place; None for what they could not."""

    level: str | None = None
    checkpoint: int | None = None
    position: tuple[float, float, float] | None = None
    heading_deg: float | None = None
    #: Why a reading is missing, one line each.
    notes: tuple[str, ...] = ()


def _run_recipe(memory: Memory, book: recipes.RecipeBook, name: str) -> dict[str, Any]:
    """What the recipe `name` shows over `memory`, with its own output dropped."""
    recipe = book.recipes[name]
    runner = recipes_run.Runner(memory, out=lambda _line: None)
    return runner.run(recipe, recipes_run.bind_args(recipe, {}))


def read_scene(memory: Memory, book: recipes.RecipeBook) -> Scene:
    """The level, checkpoint, and the player's position and heading in the game behind `memory` (a running copy over
    PINE or a `StateMemory`), by running the `where` and `position` recipes. A reading the game cannot give (no level
    loaded, no player) is left out and said in `notes`."""
    notes: list[str] = []
    level: str | None = None
    checkpoint: int | None = None
    position: tuple[float, float, float] | None = None
    heading: float | None = None
    try:
        shown = _run_recipe(memory, book, "where")
        name = str(shown.get("level", ""))
        if LEVEL_NAME.match(name):
            level, checkpoint = name, int(shown["checkpoint"])
        else:
            notes.append("no level is loaded (the level name does not read)")
    except (recipes.RecipeError, ExpressionError, PineError, KeyError) as error:
        notes.append(f"level and checkpoint: {error}")
    try:
        shown = _run_recipe(memory, book, "position")
        position = (float(shown["x"]), float(shown["y"]), float(shown["z"]))
        heading = float(shown["heading_deg"])
        if not all(math.isfinite(v) for v in (*position, heading)):
            position, heading = None, None
            notes.append("position and heading: the memory holds no player (the numbers are not finite)")
    except (recipes.RecipeError, ExpressionError, PineError, KeyError) as error:
        notes.append(f"position and heading: {error}")
    return Scene(level, checkpoint, position, heading, tuple(notes))


def detect_patches(
    memory: Memory, patches: Mapping[str, Patch], hooks: Mapping[str, hook_tools.Hook]
) -> tuple[str, ...]:
    """The names of the patches and hooks the state behind `memory` holds: a patch when every one of its edits' words
    is there, a hook when its address holds a jump into the hook caves (`recipes_run.check_patches`' tests). A patch
    that a later one overwrote in part is not listed."""
    found = []
    for name, patch in patches.items():
        if any(any(edit.data) for edit in patch.edits) and _holds(memory, patch):
            found.append(name)
    for name in hooks:
        try:
            recipes_run.check_patches(memory, [name], {}, hooks)
        except (recipes.RecipeError, PineError):
            continue
        found.append(name)
    return tuple(found)


def _holds(memory: Memory, patch: Patch) -> bool:
    """Whether every edit of `patch` is in the memory byte for byte."""
    try:
        for edit in patch.edits:
            # Whole words where the edit is made of them (the state's RAM answers any size, a PINE read only these).
            size = 4 if len(edit.data) % 4 == 0 else 1
            wanted = [int.from_bytes(edit.data[k : k + size], "little") for k in range(0, len(edit.data), size)]
            held = memory.batch([Read(edit.address + size * k, size) for k in range(len(wanted))])
            if held != wanted:
                return False
    except PineError:
        return False
    return True

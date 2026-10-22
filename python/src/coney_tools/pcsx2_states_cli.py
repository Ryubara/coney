# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pcsx2 states list | show | add | use | verify | render`: the shared library of PCSX2 save states.

The library (pcsx2_states.py) is a folder beside the repository with one read-only `<id>.p2s` per state and a catalog
describing them. `add` copies a state in and fills in its level, checkpoint, position and heading by running the
`where` and `position` recipes over the state's own memory, or over a running copy's PINE port with `--copy`. `use`
hands out a copy of a state to a copy you hold a claim on, so the library file is never played on or changed.

Research: docs/guides/pcsx2-states.md
"""

from __future__ import annotations

import datetime
import json
import math
import re
import textwrap
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from coney_tools import hooks as hook_tools
from coney_tools import pcsx2_claims as claims
from coney_tools import pcsx2_cli, pcsx2_state, recipes
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.pcsx2_copies import copy_checked, sha256
from coney_tools.pcsx2_states import (
    LEVEL_NAME,
    UNKNOWN,
    Entry,
    Library,
    LibraryError,
    Scene,
    StateMemory,
    check_slug,
    detect_patches,
    library_dir,
    read_scene,
)
from coney_tools.pine import PineClient, PineError
from coney_tools.wad import refuse_inside_repo

#: The folder of a PCSX2 copy that holds the files `states use` hands out (outside `sstates/`, where the quick-save
#: slots live).
USE_FOLDER = "library-states"
#: The page whose marked block `render` rewrites.
PAGE = Path("docs/guides/pcsx2-states.md")
BEGIN = "<!-- states:begin -->"
END = "<!-- states:end -->"
_TAG = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")
_DESCRIPTION_WIDTH = 78


def add_commands(commands: Any) -> None:
    """Register `pcsx2 states ...` under the pcsx2 group's subcommands."""
    states = commands.add_parser("states", help="the shared library of PCSX2 save states: list, add, use, verify")
    sub = states.add_subparsers(dest="states_command", required=True)
    list_ = sub.add_parser("list", help="list the states, filtered by level or tag")
    list_.add_argument("--level", help="only this level (level99)")
    list_.add_argument("--tag", help="only states with this tag")
    list_.add_argument("--json", action="store_true", help="print the entries as JSON")
    show = sub.add_parser("show", help="print one state's whole catalog entry")
    show.add_argument("id")
    show.add_argument("--json", action="store_true", help="print the entry as JSON")
    add = sub.add_parser("add", help="copy a state file into the library and describe it")
    add.add_argument("file", help="a .p2s file, or slot:N for quick-save slot N (only read)")
    add.add_argument("--id", required=True, help="the state's id: a short slug (pedestrian-grab)")
    add.add_argument("--desc", required=True, help="what the scene shows and what it is set up for")
    add.add_argument("--level", help="the level (level99) or unknown; default: read from the state")
    add.add_argument("--checkpoint", help="the checkpoint number or unknown; default: read from the state")
    add.add_argument("--tag", action="append", default=[], metavar="TAG", help="a tag (repeatable): grab, fence, ...")
    add.add_argument("--made-by", default="", help="the recipe or steps that produced it")
    add.add_argument("--author", help="who made it (default: --agent)")
    add.add_argument("--made", metavar="DATE", help="when it was made, YYYY-MM-DD (default: the file's own date)")
    add.add_argument("--pcsx2", help="the PCSX2 version that saved it (default: the file's own)")
    add.add_argument("--position", metavar="X,Y,Z", help="the player's position in metres (default: read)")
    add.add_argument("--heading", type=float, metavar="DEG", help="the player's heading in degrees (default: read)")
    add.add_argument("--copy", help="read level, checkpoint, position and heading over PINE from this running copy")
    add.add_argument("--agent", help="your id (must hold the claim on --copy)")
    add.add_argument("--no-read", action="store_true", help="read nothing from the state: give everything by flags")
    add.add_argument("--pcsx2-dir", type=Path, help="the portable PCSX2 folder for slot:N; default: pcsx2_dir")
    use = sub.add_parser("use", help="hand out a copy of a state to a PCSX2 copy you hold a claim on")
    use.add_argument("id")
    use.add_argument("--copy", required=True, help="the claimed copy (pcsx2, pcsx2-b, ...)")
    use.add_argument("--agent", required=True, help="your id (must hold the claim)")
    use.add_argument("--patch", nargs="+", default=[], metavar="NAME", help="patches of research/traces/patches/")
    use.add_argument("--launch", action="store_true", help="start PCSX2 on the copy of the state")
    verify = sub.add_parser("verify", help="hash the library's files against the catalog")
    verify.add_argument("ids", nargs="*", metavar="ID", help="only these (default: every state)")
    render = sub.add_parser("render", help="write the library's list into docs/guides/pcsx2-states.md")
    render.add_argument("--out", type=Path, help="another page with the marked block (default: the guide)")


def run_command(args: Any) -> int:
    """Dispatch a `pcsx2 states` command."""
    library = Library(library_dir())
    command = args.states_command
    if command == "list":
        return run_list(library, args.level, args.tag, args.json)
    if command == "show":
        return run_show(library, args.id, args.json)
    if command == "add":
        return run_add(library, args)
    if command == "use":
        return run_use(library, args.id, args.copy, args.agent, args.patch, args.launch)
    if command == "verify":
        return run_verify(library, args.ids)
    return run_render(library, args.out)


# -- list, show


def _checkpoint(entry: Entry) -> str:
    """The checkpoint as a table cell."""
    return "?" if entry.checkpoint is None else str(entry.checkpoint)


def run_list(library: Library, level: str | None, tag: str | None, as_json: bool) -> int:
    """`states list`: the entries of one level and/or tag, one line each (or as JSON)."""
    entries = library.find(level, tag)
    if as_json:
        print(json.dumps([e.to_json() for e in entries], indent=2))
        return 0
    if not entries:
        print(f"no states in {library.path}" + (" for that level or tag" if level or tag else ""))
        return 0
    width = max(len(e.id) for e in entries)
    for e in entries:
        text = textwrap.shorten(e.description, _DESCRIPTION_WIDTH, placeholder="...")
        print(f"{e.id:<{width}}  {e.level:<8}  cp {_checkpoint(e):<3}  {text}")
    return 0


def entry_text(entry: Entry, library: Library) -> list[str]:
    """`states show`'s lines for one entry."""
    where = "unknown"
    if entry.position is not None:
        x, y, z = entry.position
        heading = "?" if entry.heading_deg is None else f"{entry.heading_deg:.1f} degrees"
        where = f"x {x:.3f}, y {y:.3f}, z {z:.3f}; heading {heading}"
    rows = [
        ("id", entry.id),
        ("level", f"{entry.level}, checkpoint {_checkpoint(entry)}"),
        ("scene", entry.description),
        ("tags", ", ".join(entry.tags) or "none"),
        ("player", where),
        ("PCSX2", f"saved by {entry.pcsx2}"),
        ("patches", ", ".join(entry.patches) or "none"),
        ("made", f"{entry.made or '?'} by {entry.author or '?'}; added {entry.added or '?'}"),
        ("made by", entry.made_by or "not recorded"),
        ("file", f"{library.state_path(entry.id)} ({entry.size:,} bytes)"),
        ("sha256", entry.sha256),
    ]
    return [f"{label:<8}{value}" for label, value in rows]


def run_show(library: Library, state_id: str, as_json: bool) -> int:
    """`states show`: one entry in full."""
    entry = library.get(state_id)
    print(json.dumps(entry.to_json(), indent=2) if as_json else "\n".join(entry_text(entry, library)))
    return 0


# -- add


def _book() -> recipes.RecipeBook:
    """The recipes of this checkout (`where` and `position` are the ones read)."""
    try:
        return recipes.load(find_repo_root(Path.cwd()))
    except recipes.RecipeError as error:
        raise ConfigError(str(error)) from error


def _parse_position(text: str) -> tuple[float, float, float]:
    """`X,Y,Z` as three numbers. Raises ConfigError otherwise."""
    try:
        x, y, z = (float(part) for part in text.split(","))
    except ValueError as error:
        raise ConfigError(f"--position {text!r}: write X,Y,Z in metres, such as 1.5,-20,0.25") from error
    if not all(math.isfinite(v) for v in (x, y, z)):
        raise ConfigError(f"--position {text!r}: the numbers must be finite")
    return x, y, z


def _parse_checkpoint(text: str) -> int | None:
    """A checkpoint given on the command line: a number, or unknown (None)."""
    if text.lower() == UNKNOWN:
        return None
    try:
        return int(text)
    except ValueError as error:
        raise ConfigError(f"--checkpoint {text!r}: a number, or {UNKNOWN}") from error


def _tags(names: Sequence[str]) -> tuple[str, ...]:
    """The tags, lower case and unique, each a short slug. Raises LibraryError for another spelling."""
    out: list[str] = []
    for name in names:
        tag = name.strip().lower()
        if not _TAG.match(tag):
            raise LibraryError(f"tag {name!r}: lower-case letters, digits and '-' only (at most 32 characters)")
        if tag not in out:
            out.append(tag)
    return tuple(out)


def _live_scene(copy: str, agent: str | None) -> Scene:
    """The scene of the game running in the claimed copy `copy`, over its PINE port."""
    if not agent:
        raise ConfigError("--copy needs --agent (the copy is read only under your claim)")
    registry = claims.default_registry()
    registry.require_claim(agent, copy)
    held = registry.copy(copy)
    if held.port is None:
        raise ConfigError(f"{copy}: PINE is off in its ini (`coney-tools pcsx2 setup-copies`)")
    try:
        with PineClient(held.port) as pine:
            return read_scene(pine, _book())
    except PineError as error:
        raise ConfigError(f"{copy}: {error}") from error


def _source(file: str, pcsx2_dir: Path | None) -> Path:
    """The state file to add: a path, or `slot:N` (only read)."""
    if file.startswith("slot:") and not file[5:].isdigit():
        raise ConfigError(f"{file}: write slot:N with the number of a quick-save slot, such as slot:3")
    pcsx2 = pcsx2_dir or pcsx2_cli._config_paths().get("pcsx2_dir")
    return pcsx2_cli._source_state(file, pcsx2)


def run_add(library: Library, args: Any) -> int:
    """`states add`: copy a state into the library and describe it. The level, checkpoint, position and heading come
    from the flags, else from the running copy (`--copy`), else from the state's own memory; the PCSX2 version, the
    date and the patches come from the file. Raises ConfigError when the level cannot be told."""
    check_slug(args.id)
    author = args.author or args.agent
    if not author:
        raise ConfigError("say who made the state: --author NAME (or --agent ID)")
    source = _source(args.file, args.pcsx2_dir)
    tags = _tags(args.tag)
    found_patches: tuple[str, ...] = ()
    scene, how = Scene(), "given by flags"
    try:
        if args.copy:
            scene, how = _live_scene(args.copy, args.agent), f"read over PINE from {args.copy}"
        if not args.no_read:
            memory = StateMemory(pcsx2_state.read_ee_memory(source))
            if not args.copy:
                scene, how = read_scene(memory, _book()), "read from the state's own memory"
            found_patches = _patches_in(memory)
    except pcsx2_state.StateError as error:
        raise ConfigError(str(error)) from error
    level = args.level or scene.level
    if args.level and args.level != UNKNOWN and not LEVEL_NAME.match(args.level):
        raise ConfigError(f"--level {args.level!r}: a level's name as the game spells it (level99), or {UNKNOWN}")
    if args.made:
        try:
            datetime.date.fromisoformat(args.made)
        except ValueError as error:
            raise ConfigError(f"--made {args.made!r}: write a date as YYYY-MM-DD") from error
    if level is None:
        raise ConfigError(
            "the level could not be read"
            + (f" ({'; '.join(scene.notes)})" if scene.notes else "")
            + f"; pass --level NAME (or --level {UNKNOWN})"
        )
    checkpoint = _parse_checkpoint(args.checkpoint) if args.checkpoint else scene.checkpoint
    position = _parse_position(args.position) if args.position else scene.position
    if position is not None:
        # Millimetres: the game's floats carry more digits than a position means, and the catalog is read by people.
        position = (round(position[0], 3), round(position[1], 3), round(position[2], 3))
    heading = args.heading if args.heading is not None else scene.heading_deg
    entry = Entry(
        id=args.id,
        level=level,
        checkpoint=checkpoint,
        description=" ".join(args.desc.split()),
        tags=tags,
        position=position,
        heading_deg=None if heading is None else round(heading, 3),
        pcsx2=args.pcsx2 or pcsx2_state.read_version(source) or UNKNOWN,
        patches=found_patches,
        author=author,
        made=args.made or pcsx2_state.saved_on(source) or datetime.date.today().isoformat(),
        added=datetime.date.today().isoformat(),
        made_by=" ".join(args.made_by.split()),
    )
    added = library.add(entry, source)
    for line in entry_text(added, library):
        print(line)
    print(f"({how})")
    for note in scene.notes:
        print(f"note: {note}")
    if args.no_read:
        print("note: --no-read: the patches the state holds were not looked for, so 'patches none' means unchecked")
    return 0


def _patches_in(memory: StateMemory) -> tuple[str, ...]:
    """The patches and hooks of research/traces/patches/ that the state's memory holds."""
    root = find_repo_root(Path.cwd())
    try:
        known = pcsx2_state.load_patches(root / recipes.PATCHES_DIR)
        hooks = hook_tools.load_hooks(root / recipes.PATCHES_DIR)
    except (pcsx2_state.StateError, hook_tools.HookError) as error:
        raise ConfigError(str(error)) from error
    return detect_patches(memory, known, hooks)


# -- use


def run_use(library: Library, state_id: str, copy: str, agent: str, patch_names: list[str], launch: bool) -> int:
    """`states use`: write a copy of the state into the claimed copy's `library-states/` folder (with the named patches
    applied) and print its path; the library file is only read, and checked against the catalog first. With `launch`
    it also starts PCSX2 on the copy."""
    registry = claims.default_registry()
    claim = registry.require_claim(agent, copy)
    entry = library.get(state_id)
    source = library.state_path(state_id)
    if not source.is_file() or source.stat().st_size != entry.size or sha256(source) != entry.sha256:
        raise LibraryError(f"{state_id}: its file does not match the catalog; run `coney-tools pcsx2 states verify`")
    target = Path(claim.path) / USE_FOLDER / f"{state_id}.p2s"
    refuse_inside_repo(target)
    try:
        if patch_names:
            patches, _ = pcsx2_cli._patches(patch_names)
            count = pcsx2_state.prepare(source, target, patches, [Path(claim.path) / "sstates"])
            print(f"{target}: {count} edit(s) from {', '.join(patch_names)}")
        else:
            copy_checked(source, target, force=True)
            print(target)
    except pcsx2_state.StateError as error:
        raise ConfigError(str(error)) from error
    if entry.patches:
        print(f"note: the state already holds {', '.join(entry.patches)}; do not apply them again")
    print(f"saved by PCSX2 {entry.pcsx2}; the file is yours to play on, the library's is untouched")
    if launch:
        return pcsx2_cli.run_launch(target, Path(claim.path), None, None, agent)
    print(f"start it with `coney-tools pcsx2 launch --agent {agent} {target}`")
    return 0


# -- verify, render


def run_verify(library: Library, ids: Sequence[str]) -> int:
    """`states verify`: hash the files against the catalog; 1 when one is missing, changed or unlisted."""
    report = library.verify(ids)
    for problem in report.problems:
        print(f"PROBLEM {problem}")
    for note in report.notes:
        print(f"note {note}")
    print(f"{report.checked} state(s) checked in {library.path}: {len(report.problems)} problem(s)")
    return 1 if report.problems else 0


def render_block(entries: Sequence[Entry]) -> str:
    """The Markdown table of the library's states: names and our own descriptions, no state data."""
    lines = ["| Id | Level | Checkpoint | Shows, and set up for | Tags |", "| --- | --- | --- | --- | --- |"]
    for e in entries:
        text = e.description.replace("|", "\\|")
        lines.append(f"| `{e.id}` | {e.level} | {_checkpoint(e)} | {text} | {', '.join(e.tags)} |")
    return "\n".join(lines)


def run_render(library: Library, out: Path | None) -> int:
    """`states render`: rewrite the block between the page's markers with the library's list."""
    page = out or find_repo_root(Path.cwd()) / PAGE
    entries = library.entries()
    if not entries:
        raise ConfigError(f"{library.path}: no states to list")
    text = page.read_bytes().decode("utf-8").replace("\r\n", "\n")
    start, end = text.find(BEGIN), text.find(END)
    if start < 0 or end < start:
        raise ConfigError(f"{page}: needs {BEGIN} and {END} around the list")
    block = f"{BEGIN}\n\n{render_block(entries)}\n\n"
    page.write_bytes((text[:start] + block + text[end:]).encode("utf-8"))
    print(f"wrote {len(entries)} state(s) into {page}")
    return 0

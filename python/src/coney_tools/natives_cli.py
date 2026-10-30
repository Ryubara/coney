# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools bindings ...` commands: render, sync-coney and render-cpp (all with --check), stats, and
scan-usage and scan-helpers (read the disc)."""

from __future__ import annotations

import collections
from collections.abc import Callable
from pathlib import Path

from coney_tools import lua4, natives, natives_cpp, natives_render, script_helpers, script_helpers_render
from coney_tools.config import ConfigError, find_repo_root


def _load_checked(root: Path) -> natives.Masterlist | None:
    """The masterlist, or None after printing its problems."""
    masterlist = natives.load(root)
    if masterlist.problems:
        for problem in masterlist.problems:
            print(f"problem: {problem}")
        print(f"bindings: {len(masterlist.problems)} problem(s) in {natives.DATA_DIR.as_posix()}/")
        return None
    return masterlist


def run_render(check_only: bool) -> int:
    """Check the YAML and write the pages; with `check_only`, write nothing and return 1 when a page is stale.

    The page of `global.lua`'s helpers is written beside them from research/script-helpers.yaml when the checkout has
    that file. A page in docs/references/bindings/ that the renderer would not write (a category that no longer exists)
    also counts as stale, and is deleted when not checking.
    """
    root = find_repo_root(Path.cwd())
    masterlist = _load_checked(root)
    if masterlist is None:
        return 1
    helpers = script_helpers.load(root)
    if helpers is not None and helpers.problems:
        for problem in helpers.problems:
            print(f"problem: {problem}")
        print(f"bindings: {len(helpers.problems)} problem(s) in {script_helpers.DATA_FILE.as_posix()}")
        return 1
    pages = natives_render.render(masterlist)
    if helpers is not None:
        pages[script_helpers.PAGE] = script_helpers_render.page(helpers, masterlist)
    folder = root / natives.PAGES_DIR
    stale: list[str] = []
    for name, text in pages.items():
        path = folder / name
        try:
            current = path.read_bytes().decode("utf-8").replace("\r\n", "\n") if path.exists() else None
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{path}: cannot be read ({error})") from error
        if current == text:
            continue
        stale.append(name)
        if not check_only:
            folder.mkdir(parents=True, exist_ok=True)
            path.write_bytes(text.encode("utf-8"))
    extra = sorted(p.name for p in folder.glob("*.md") if p.name not in pages) if folder.exists() else []
    for name in extra:
        stale.append(name)
        if not check_only:
            (folder / name).unlink()
    if check_only and stale:
        print(f"bindings: stale: {', '.join(stale)}; run `uv run --project python coney-tools bindings render`")
        return 1
    print(f"bindings: {'updated ' + ', '.join(stale) if stale else 'up to date'} ({len(masterlist.bindings)} bindings)")
    return 0


def run_cpp(check_only: bool) -> int:
    """Write the C++ signature table (src/debug/native_signatures.cpp); with `check_only`, write nothing and return 1
    when it is stale."""
    root = find_repo_root(Path.cwd())
    masterlist = _load_checked(root)
    if masterlist is None:
        return 1
    text = natives_cpp.render_cpp(masterlist)
    path = root / natives_cpp.CPP_FILE
    try:
        current = path.read_bytes().decode("utf-8").replace("\r\n", "\n") if path.exists() else None
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    stale = current != text
    if check_only and stale:
        print(
            f"bindings: {natives_cpp.CPP_FILE.as_posix()} is stale;"
            " run `uv run --project python coney-tools bindings render-cpp`"
        )
        return 1
    if stale:
        path.write_bytes(text.encode("utf-8"))
    state = "updated" if stale else "up to date"
    print(f"bindings: {natives_cpp.CPP_FILE.as_posix()} {state} ({len(masterlist.bindings)} bindings)")
    return 0


def run_stats() -> int:
    """Print the counts by category, evidence level, detail and usage; 1 when the YAML has problems."""
    masterlist = _load_checked(find_repo_root(Path.cwd()))
    if masterlist is None:
        return 1
    bindings = masterlist.bindings
    print(f"bindings: {len(bindings)} ({sum(1 + len(b.overloads) for b in bindings)} registrations)")
    for title, counter in (
        ("category", collections.Counter(b.category for b in bindings)),
        ("evidence", collections.Counter(b.evidence for b in bindings)),
        ("detail", collections.Counter(b.depth for b in bindings)),
        ("coney", collections.Counter(b.coney for b in bindings)),
    ):
        print(f"{title}: " + ", ".join(f"{key} {count}" for key, count in counter.most_common()))
    used = [b for b in bindings if b.usage and b.usage.calls]
    boot = [b for b in bindings if b.usage and b.usage.boot]
    mission = [b for b in bindings if b.usage and b.usage.mission1]
    print(f"used by scripts: {len(used)}; boot to menu: {len(boot)}; mission 1: {len(mission)}")
    return 0


def run_coney(check_only: bool) -> int:
    """Set each entry's `coney` key from Coney's binding table; with `check_only`, return 1 when one differs.

    A name in the table that the masterlist lacks is a problem too (a typo, or a binding to add to the YAML).
    """
    root = find_repo_root(Path.cwd())
    masterlist = _load_checked(root)
    if masterlist is None:
        return 1
    table = natives.load_coney_table(root)
    known = {b.name for b in masterlist.bindings}
    unknown = sorted(set(table) - known)
    if unknown:
        print(f"bindings: in {natives.CONEY_TABLE.as_posix()} but not in the masterlist: {', '.join(unknown)}")
        return 1
    stale: list[str] = []
    for path in sorted((root / natives.DATA_DIR).glob("*.yaml")):
        try:
            text = path.read_bytes().decode("utf-8").replace("\r\n", "\n")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{path}: cannot be read ({error})") from error
        updated = natives.set_coney_statuses(text, table)
        if updated == text:
            continue
        stale.append(path.name)
        if not check_only:
            path.write_bytes(updated.encode("utf-8"))
    if check_only and stale:
        print(
            f"bindings: coney status stale in {', '.join(stale)}; run"
            " `uv run --project python coney-tools bindings sync-coney`"
        )
        return 1
    counts = collections.Counter(table.values())
    summary = ", ".join(f"{status} {counts[status]}" for status in ("implemented", "partial", "not implemented"))
    print(f"bindings: {'updated ' + ', '.join(stale) if stale else 'up to date'} (table: {summary})")
    return 0


def run_mission1(disc_arg: str | None, check_only: bool) -> int:
    """Set each entry's `usage.mission1` from the first mission's scripts on the player's disc; with `check_only`,
    write nothing and return 1 when one differs. Prints the coverage counts either way."""
    from coney_tools import natives_mission, refs_extract  # the disc readers are only needed here
    from coney_tools.wad_cli import open_disc

    root = find_repo_root(Path.cwd())
    masterlist = _load_checked(root)
    if masterlist is None:
        return 1
    disc = refs_extract.DiscFacts(open_disc(disc_arg))
    chunks: dict[str, bytes] = {}
    for name in (*natives_mission.MISSION1_SCRIPTS, natives_mission.HELPERS_SCRIPT):
        entry = disc.by_name(name)
        if entry is None:
            raise ConfigError(f"{name} is not on this disc")
        chunks[name] = disc.read(entry)
    scripts, helpers = natives_mission.scripts_from(chunks)
    names = natives_mission.reached_names(scripts, helpers)
    used = natives_mission.mission_bindings(names, (b.name for b in masterlist.bindings))
    stale = _rewrite_data(root, lambda text: natives_mission.set_mission1(text, used), check_only)
    rows = [b for b in masterlist.bindings if b.name in used]
    traced = sum(1 for b in rows if b.depth == "thorough")
    implemented = collections.Counter(b.coney for b in rows)
    print(
        f"mission 1: {len(rows)} bindings; traced {traced}; Coney: implemented {implemented['implemented']},"
        f" partial {implemented['partial']}, not implemented {implemented['not implemented']}"
    )
    if check_only and stale:
        print(
            f"bindings: mission1 stale in {', '.join(stale)}; "
            "run `uv run --project python coney-tools bindings scan-usage --first-mission`"
        )
        return 1
    state = f"updated {', '.join(stale)}; run `bindings render`" if stale else "mission1 up to date"
    print(f"bindings: {state}")
    return 0


def _rewrite_data(root: Path, change: Callable[[str], str], check_only: bool) -> list[str]:
    """Apply `change` to the text of every data file; returns the names of the files it changed, writing them unless
    `check_only`."""
    stale: list[str] = []
    for path in sorted((root / natives.DATA_DIR).glob("*.yaml")):
        try:
            text = path.read_bytes().decode("utf-8").replace("\r\n", "\n")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{path}: cannot be read ({error})") from error
        updated = change(text)
        if updated == text:
            continue
        stale.append(path.name)
        if not check_only:
            path.write_bytes(updated.encode("utf-8"))
    return stale


def run_missions(disc_arg: str | None, check_only: bool) -> int:
    """Set each entry's `usage.levels` from the scripts of the story's later levels on the player's disc; with
    `check_only`, write nothing and return 1 when one differs. Prints each level's counts either way: the bindings it
    can call, those no earlier level of the story calls (level99 included), and how many of those are traced."""
    from coney_tools import natives_mission, refs_extract  # the disc readers are only needed here
    from coney_tools.refs_cli import known_names
    from coney_tools.wad_cli import open_disc

    root = find_repo_root(Path.cwd())
    masterlist = _load_checked(root)
    if masterlist is None:
        return 1
    script_names = [name for name in known_names(root, None) if name.endswith(".lua")]
    disc = refs_extract.DiscFacts(open_disc(disc_arg))

    def chunk(name: str) -> lua4.Proto:
        # One compiled script from the disc, by WAD name.
        entry = disc.by_name(name)
        if entry is None:
            raise ConfigError(f"{name} is not on this disc")
        return lua4.parse_chunk(disc.read(entry))

    helpers = chunk(natives_mission.HELPERS_SCRIPT)
    by_name = {b.name: b for b in masterlist.bindings}
    seen = {b.name for b in masterlist.bindings if b.usage and b.usage.mission1}
    levels_of: dict[str, list[int]] = collections.defaultdict(list)
    for level, label in natives.STORY_LEVELS:
        files = natives_mission.level_scripts(level, script_names)
        if not files:
            raise ConfigError(f"no scripts of level{level} in the WAD names list")
        names = natives_mission.reached_names([chunk(name) for name in files], helpers)
        used = natives_mission.mission_bindings(names, by_name)
        for name in used:
            levels_of[name].append(level)
        new = used - seen
        seen |= used
        traced = sum(1 for name in new if by_name[name].depth == "thorough")
        print(f"level{level} ({label}, {len(files)} scripts): {len(used)} bindings; new {len(new)}, traced {traced}")
    stale = _rewrite_data(root, lambda text: natives_mission.set_levels(text, levels_of), check_only)
    if check_only and stale:
        print(
            f"bindings: levels stale in {', '.join(stale)}; "
            "run `uv run --project python coney-tools bindings scan-usage --story`"
        )
        return 1
    state = f"updated {', '.join(stale)}; run `bindings render`" if stale else "levels up to date"
    print(f"bindings: {state}")
    return 0


def run_helpers(disc_arg: str | None, chunks_dir: Path | None, check_only: bool) -> int:
    """Survey `global.lua` and the other compiled scripts (from the player's disc, or a folder of chunks) and write
    the counts into research/script-helpers.yaml; with `check_only`, write nothing and return 1 when it would change.

    A helper or global the file lacks gets an entry to fill in, and one the disc lacks is reported; either makes the
    command return 1, since `bindings render` refuses the file until it is put right.
    """
    from coney_tools import lua_decompile  # the chunk readers are only needed here

    root = find_repo_root(Path.cwd())
    masterlist = _load_checked(root)
    if masterlist is None:
        return 1
    path = root / script_helpers.DATA_FILE
    try:
        text = path.read_bytes().decode("utf-8").replace("\r\n", "\n") if path.exists() else ""
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    chunks = (
        lua_decompile.chunks_from_folder(chunks_dir)
        if chunks_dir is not None
        else lua_decompile.chunks_from_disc(root, disc_arg)
    )
    by_name = dict(chunks)
    if script_helpers.HELPERS_SCRIPT not in by_name:
        raise ConfigError(f"{script_helpers.HELPERS_SCRIPT} is not among the {len(chunks)} chunks")
    names = frozenset(b.name for b in masterlist.bindings)
    result = script_helpers.survey(by_name[script_helpers.HELPERS_SCRIPT], chunks, names)
    updated, added, gone = script_helpers.apply_survey(text, result)
    print(
        f"{script_helpers.HELPERS_SCRIPT}: {len(result.helpers)} helpers, {len(result.globals)} globals read by other"
        f" scripts, from {len(chunks)} chunks"
    )
    if added:
        print(f"bindings: new in {script_helpers.HELPERS_SCRIPT}, to describe: {', '.join(added)}")
    if gone:
        print(f"bindings: no longer in {script_helpers.HELPERS_SCRIPT}, to remove: {', '.join(gone)}")
    stale = updated != text
    if check_only:
        if stale:
            print(
                f"bindings: {script_helpers.DATA_FILE.as_posix()} is stale; run"
                " `uv run --project python coney-tools bindings scan-helpers`"
            )
        return 1 if stale or gone else 0
    if stale:
        path.write_bytes(updated.encode("utf-8"))
    print(
        f"bindings: {'updated ' + script_helpers.DATA_FILE.as_posix() if stale else 'helpers up to date'}"
        + ("; run `bindings render`" if stale else "")
    )
    return 1 if added or gone else 0

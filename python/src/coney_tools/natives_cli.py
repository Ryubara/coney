# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools natives ...` commands: render, coney and cpp (all with --check), stats, and mission1 (reads the
disc)."""

from __future__ import annotations

import collections
from pathlib import Path

from coney_tools import natives, natives_cpp, natives_render
from coney_tools.config import ConfigError, find_repo_root


def _load_checked(root: Path) -> natives.Masterlist | None:
    """The masterlist, or None after printing its problems."""
    masterlist = natives.load(root)
    if masterlist.problems:
        for problem in masterlist.problems:
            print(f"problem: {problem}")
        print(f"natives: {len(masterlist.problems)} problem(s) in {natives.DATA_DIR.as_posix()}/")
        return None
    return masterlist


def run_render(check_only: bool) -> int:
    """Check the YAML and write the pages; with `check_only`, write nothing and return 1 when a page is stale.

    A page in docs/references/bindings/ that the renderer would not write (a category that no longer exists) also
    counts as stale, and is deleted when not checking.
    """
    root = find_repo_root(Path.cwd())
    masterlist = _load_checked(root)
    if masterlist is None:
        return 1
    pages = natives_render.render(masterlist)
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
        print(f"natives: stale: {', '.join(stale)}; run `uv run --project python coney-tools natives render`")
        return 1
    print(f"natives: {'updated ' + ', '.join(stale) if stale else 'up to date'} ({len(masterlist.bindings)} bindings)")
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
            f"natives: {natives_cpp.CPP_FILE.as_posix()} is stale;"
            " run `uv run --project python coney-tools natives cpp`"
        )
        return 1
    if stale:
        path.write_bytes(text.encode("utf-8"))
    state = "updated" if stale else "up to date"
    print(f"natives: {natives_cpp.CPP_FILE.as_posix()} {state} ({len(masterlist.bindings)} bindings)")
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
        print(f"natives: in {natives.CONEY_TABLE.as_posix()} but not in the masterlist: {', '.join(unknown)}")
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
            f"natives: coney status stale in {', '.join(stale)}; run"
            " `uv run --project python coney-tools natives coney`"
        )
        return 1
    counts = collections.Counter(table.values())
    summary = ", ".join(f"{status} {counts[status]}" for status in ("implemented", "partial", "not implemented"))
    print(f"natives: {'updated ' + ', '.join(stale) if stale else 'up to date'} (table: {summary})")
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
    stale: list[str] = []
    for path in sorted((root / natives.DATA_DIR).glob("*.yaml")):
        try:
            text = path.read_bytes().decode("utf-8").replace("\r\n", "\n")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{path}: cannot be read ({error})") from error
        updated = natives_mission.set_mission1(text, used)
        if updated == text:
            continue
        stale.append(path.name)
        if not check_only:
            path.write_bytes(updated.encode("utf-8"))
    rows = [b for b in masterlist.bindings if b.name in used]
    traced = sum(1 for b in rows if b.depth == "thorough")
    implemented = collections.Counter(b.coney for b in rows)
    print(
        f"mission 1: {len(rows)} bindings; traced {traced}; Coney: implemented {implemented['implemented']},"
        f" partial {implemented['partial']}, not implemented {implemented['not implemented']}"
    )
    if check_only and stale:
        print(
            f"natives: mission1 stale in {', '.join(stale)}; run `uv run --project python coney-tools natives mission1`"
        )
        return 1
    state = f"updated {', '.join(stale)}; run `natives render`" if stale else "mission1 up to date"
    print(f"natives: {state}")
    return 0

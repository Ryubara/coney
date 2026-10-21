# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pcsx2 setup-copies` and `pcsx2 recordings push | pull`: the portable PCSX2 copies agents claim.

Copy i (`pcsx2`, `pcsx2-b`, `pcsx2-c`, ...) runs the PCSX2-MCP fork's build that `pcsx2_mcp_dir` names, with its own
PINE port (`pcsx2_pine_base` + i) and debug server port (`pcsx2_debug_base` + i), so several agents each drive their
own (docs/guides/research-workflow.md#several-at-once). `setup-copies` copies only the build's program files over a
copy and edits its ini in place: a copy's settings, BIOS, memory cards, save states and recordings stay as they are.
A copy whose PCSX2 runs is refused (its program files are in use, and its ports would change under its user).

Input recordings (`.p2m2`, with their start savestate `<file>_SaveState.p2s`) are made and played inside a copy's
`recordings/` folder, the only place the debug server reads them from; `recordings push | pull` moves them between
there and the shared scratch folder `recordings/`, checking every copy's SHA-256.
"""

from __future__ import annotations

import hashlib
import re
import shutil
import string
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from coney_tools import pcsx2_claims, pcsx2_proc
from coney_tools.config import (
    INT_KEYS,
    ConfigError,
    find_repo_root,
    load_config,
    local_config_file,
    main_checkout,
    workspace_folder,
)
from coney_tools.debug_server import savestate_of
from coney_tools.pcsx2_cli import set_ini_value

#: Folders and files of a copy that hold its own data, never replaced from the build: settings, BIOS, memory cards,
#: states, recordings, captures, caches and the rest PCSX2 writes beside itself in a portable install.
USER_DATA: frozenset[str] = frozenset(
    {
        "inis",
        "bios",
        "memcards",
        "sstates",
        "recordings",
        "snaps",
        "snapshots",
        "videos",
        "logs",
        "cache",
        "cheats",
        "covers",
        "gamesettings",
        "inputprofiles",
        "patches",
        "textures",
        "portable.txt",
    }
)
#: Entries of a fork release folder that are not PCSX2's: the MCP server runs from the release, never a copy.
NOT_PROGRAM: frozenset[str] = frozenset({"pcsx2-mcp-server"})
#: Loose program files a copy may hold from an older PCSX2 that the build no longer has (and that would still load
#: or, for updater.exe, offer a stock PCSX2 update): removed when the build lacks them.
STALE_SUFFIXES: tuple[str, ...] = (".dll", ".exe")

#: A recording's path below `recordings/`: names of letters, digits, '.', '_' and '-' (the debug server's rule).
_RECORDING_NAME = re.compile(r"^[A-Za-z0-9._-]+$")


def copy_name(index: int) -> str:
    """The folder name of copy `index`: `pcsx2`, then `pcsx2-b`, `pcsx2-c`, ..."""
    if not 0 <= index < len(string.ascii_lowercase):
        raise ConfigError(f"copy {index}: at most {len(string.ascii_lowercase)} copies (pcsx2 to pcsx2-z)")
    return "pcsx2" if index == 0 else f"pcsx2-{string.ascii_lowercase[index]}"


def copy_settings(index: int, pine_base: int, debug_base: int) -> list[tuple[str, str, str]]:
    """The ini settings (section, key, value) copy `index` gets: PINE and the debug server each on its own port,
    and no SDL gamepad source (the user's pad must never drive an agent's game)."""
    return [
        ("EmuCore", "EnablePINE", "true"),
        ("EmuCore", "PINESlot", str(pine_base + index)),
        ("EmuCore", "EnableDebugServer", "true"),
        ("EmuCore", "DebugServerPort", str(debug_base + index)),
        ("InputSources", "SDL", "false"),
    ]


def program_entries(build: Path) -> list[Path]:
    """The top-level files and folders of a fork build that are PCSX2's program. Raises ConfigError when `build`
    holds no PCSX2 (a server-only folder, or a stock install's data)."""
    if not (build / "pcsx2-qt.exe").is_file() and not any(build.glob("*.AppImage")):
        raise ConfigError(
            f"pcsx2_mcp_dir: {build} holds no PCSX2 build (pcsx2-qt.exe); name a release folder "
            "(tools/pcsx2-mcp/release/PCSX2-MCP-coney-v2.9.129.N-win64), docs/guides/workspace.md#pcsx2-copies"
        )
    return sorted(entry for entry in build.iterdir() if entry.name not in USER_DATA | NOT_PROGRAM)


def _same_file(source: Path, target: Path) -> bool:
    """Whether `target` already is a copy of `source`: the same size and the very same modification time, which
    copy2 carries over (to the nanosecond PCSX2's NTFS keeps), so a rerun copies nothing."""
    if not target.is_file():
        return False
    a, b = source.stat(), target.stat()
    return a.st_size == b.st_size and a.st_mtime_ns == b.st_mtime_ns


def _sync_tree(source: Path, target: Path, log: Callable[[str], None], dry_run: bool) -> int:
    """Make the folder `target` hold exactly `source`'s files; returns how many files were copied or removed."""
    changed = 0
    wanted = {path.relative_to(source) for path in source.rglob("*")}
    if target.is_dir():
        for path in sorted(target.rglob("*"), reverse=True):
            if path.relative_to(target) not in wanted:
                changed += 1
                if dry_run:
                    continue
                if path.is_dir() and not path.is_symlink():
                    shutil.rmtree(path)
                else:
                    path.unlink()
    for relative in sorted(wanted):
        src, dst = source / relative, target / relative
        if src.is_dir():
            if not dry_run:
                dst.mkdir(parents=True, exist_ok=True)
        elif not _same_file(src, dst):
            changed += 1
            if not dry_run:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
    if changed:
        log(f"    {'would update' if dry_run else 'updated'} {target.name}/ ({changed} file(s))")
    return changed


@dataclass(frozen=True)
class CopyPlan:
    """One copy `setup-copies` makes or updates."""

    index: int
    name: str
    path: Path
    settings: list[tuple[str, str, str]]


def plan_copies(root: Path, count: int, pine_base: int, debug_base: int) -> list[CopyPlan]:
    """The copies 0 .. count-1 under `root`, with their settings."""
    if count < 1:
        raise ConfigError("--count must be at least 1")
    return [
        CopyPlan(i, copy_name(i), root / copy_name(i), copy_settings(i, pine_base, debug_base)) for i in range(count)
    ]


def _running_in(folder: Path, processes: dict[int, Path]) -> list[int]:
    """The PCSX2 processes started from `folder`."""
    resolved = folder.resolve()
    return sorted(pid for pid, image in processes.items() if image.parent.resolve() == resolved)


def _seed_new_copy(plan: CopyPlan, template: Path | None, log: Callable[[str], None], dry_run: bool) -> None:
    """Give a new copy its settings and BIOS folder from an existing copy (`template`), or a minimal ini that skips
    PCSX2's first-run wizard when there is none (the user then adds a BIOS dumped from their own console)."""
    ini = plan.path / "inis" / "PCSX2.ini"
    source = template / "inis" / "PCSX2.ini" if template is not None else None
    log(f"    {'would seed' if dry_run else 'seeded'} inis/PCSX2.ini from {source or 'a minimal ini'}")
    if dry_run:
        return
    ini.parent.mkdir(parents=True, exist_ok=True)
    if source is not None and source.is_file():
        shutil.copy2(source, ini)
    else:
        ini.write_text("[UI]\nSetupWizardIncomplete = false\n", encoding="utf-8")
    if template is not None and (template / "bios").is_dir() and not (plan.path / "bios").exists():
        shutil.copytree(template / "bios", plan.path / "bios")
        log(f"    copied bios/ from {template.name}")


def setup_copy(
    plan: CopyPlan,
    build: list[Path],
    template: Path | None,
    log: Callable[[str], None],
    dry_run: bool,
) -> None:
    """Make or update one copy: the build's program files over it, stale program files gone, portable.txt, and its
    ini's ports and gamepad setting. The copy's own data (USER_DATA) is never touched but for those ini keys."""
    new = not plan.path.exists()
    log(f"  {plan.name}: {'create' if new else 'update'} {plan.path}" + (" (dry run)" if dry_run else ""))
    if not dry_run:
        plan.path.mkdir(parents=True, exist_ok=True)
    names = {entry.name for entry in build}
    copied = 0
    for entry in build:
        target = plan.path / entry.name
        if entry.is_dir():
            if target.exists() and not target.is_dir():
                raise ConfigError(f"{target}: a file where the build has a folder; move it away first")
            copied += _sync_tree(entry, target, log, dry_run)
        elif not _same_file(entry, target):
            copied += 1
            if not dry_run:
                shutil.copy2(entry, target)
    log(f"    {'would copy' if dry_run else 'copied'} {copied} program file(s) in all")
    if plan.path.is_dir():
        for stale in sorted(plan.path.iterdir()):
            if stale.is_file() and stale.suffix.lower() in STALE_SUFFIXES and stale.name not in names:
                log(f"    {'would remove' if dry_run else 'removed'} {stale.name} (not in the build)")
                if not dry_run:
                    stale.unlink()
    if not dry_run:
        # PCSX2 keeps its settings, cards and states beside itself only when this file exists.
        (plan.path / "portable.txt").touch()
    ini = plan.path / "inis" / "PCSX2.ini"
    if not ini.is_file():
        _seed_new_copy(plan, template, log, dry_run)
    if dry_run and not ini.is_file():
        log("    would set " + ", ".join(f"{key} = {value}" for _, key, value in plan.settings))
        return
    text = ini.read_text(encoding="utf-8")
    changed = text
    for section, key, value in plan.settings:
        changed = set_ini_value(changed, section, key, value)
    if changed != text:
        log(f"    {'would set' if dry_run else 'set'} " + ", ".join(f"{k} = {v}" for _, k, v in plan.settings))
        if not dry_run:
            ini.write_text(changed, encoding="utf-8")


def setup_copies(
    root: Path,
    build_dir: Path,
    plans: list[CopyPlan],
    processes: dict[int, Path],
    log: Callable[[str], None] = print,
    dry_run: bool = False,
) -> list[str]:
    """Set up every planned copy under `root` from the fork build in `build_dir`; returns the names of the copies
    refused because their PCSX2 runs (`processes`: pid to executable of every PCSX2)."""
    build = program_entries(build_dir)
    log(f"PCSX2 build: {build_dir}" + (" (dry run: nothing is changed)" if dry_run else ""))
    existing = [plan.path for plan in plans if (plan.path / "inis" / "PCSX2.ini").is_file()]
    template = existing[0] if existing else None
    refused = []
    for plan in plans:
        pids = _running_in(plan.path, processes) if plan.path.exists() else []
        if pids:
            log(f"  {plan.name}: refused, its PCSX2 runs (pid {', '.join(map(str, pids))}); close it and run again")
            refused.append(plan.name)
            continue
        setup_copy(plan, build, template, log, dry_run)
    return refused


def _config() -> tuple[dict[str, Path | None], dict[str, int], Path]:
    """coney.local.toml's paths and numbers (the defaults when there is no file) and the main checkout."""
    root = find_repo_root(Path.cwd())
    main = main_checkout(root)
    if local_config_file(root) is None:
        return {}, dict(INT_KEYS), main
    config = load_config(root)
    return config.paths, config.numbers, main


def run_setup_copies(count: int | None, dry_run: bool) -> int:
    """`pcsx2 setup-copies`: make or update the copies; 1 when one was refused because it runs."""
    paths, numbers, main = _config()
    build = paths.get("pcsx2_mcp_dir")
    if build is None or not build.is_dir():
        raise ConfigError(
            "pcsx2_mcp_dir is not set in coney.local.toml (or does not exist): name the fork's release folder "
            "(docs/guides/workspace.md#pcsx2-copies)"
        )
    root = paths.get("pcsx2_root") or pcsx2_claims.default_pcsx2_root(main)
    plans = plan_copies(
        root,
        count if count is not None else numbers["pcsx2_copies"],
        numbers["pcsx2_pine_base"],
        numbers["pcsx2_debug_base"],
    )
    refused = setup_copies(root, build, plans, pcsx2_proc.pcsx2_processes(), dry_run=dry_run)
    return 1 if refused else 0


# -- recordings


def recording_path(name: str) -> Path:
    """`name` (a recording's path below `recordings/`, `.p2m2` added when missing) as a relative path. Raises
    ConfigError for a path the debug server would refuse."""
    parts = name.replace("\\", "/").split("/")
    if not 1 <= len(parts) <= 8 or any(p in ("", ".", "..") or not _RECORDING_NAME.match(p) for p in parts):
        raise ConfigError(
            f"{name!r}: a recording is named by up to 8 names of letters, digits, '.', '_' and '-' split by '/' "
            "(below recordings/), never '.' or '..'"
        )
    relative = Path(*parts)
    return relative if relative.suffix == ".p2m2" else relative.with_name(relative.name + ".p2m2")


def sha256(path: Path) -> str:
    """The SHA-256 of a file, read in pieces (a savestate is tens of megabytes)."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def copy_checked(source: Path, target: Path, force: bool) -> str:
    """Copy `source` to `target` and check the copy's SHA-256 against the source's; returns it. A different file
    already at `target` is refused unless `force`; the same one is left. Raises ConfigError on a mismatch (the
    copy is deleted)."""
    expected = sha256(source)
    if target.is_file() and not force:
        if sha256(target) == expected:
            return expected
        raise ConfigError(f"{target} exists and differs from {source}; pass --force to replace it")
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    shutil.copyfile(source, partial)
    actual = sha256(partial)
    if actual != expected:
        partial.unlink(missing_ok=True)
        raise ConfigError(f"{target}: the copy's SHA-256 {actual} differs from {source}'s {expected}; nothing kept")
    partial.replace(target)
    return expected


def transfer_recording(source_dir: Path, target_dir: Path, relative: Path, force: bool) -> list[tuple[Path, str]]:
    """Copy the recording `relative` and its start savestate (when there is one) from `source_dir` to `target_dir`;
    returns each file written with its SHA-256. Raises ConfigError when the recording is missing."""
    recording = source_dir / relative
    if not recording.is_file():
        raise ConfigError(f"{recording}: no such recording")
    files = [recording] + ([savestate_of(recording)] if savestate_of(recording).is_file() else [])
    written = []
    for path in files:
        target = target_dir / path.relative_to(source_dir)
        written.append((target, copy_checked(path, target, force)))
    return written


def scratch_recordings() -> Path:
    """The shared recordings folder: `recordings/` in the scratch folder (`scratch_dir`, else the main checkout's
    `../../scratch`)."""
    paths, _, main = _config()
    scratch = paths.get("scratch_dir") or workspace_folder(main, "scratch")
    return scratch / "recordings"


def run_recordings(direction: str, agent: str, copy: str, name: str, force: bool) -> int:
    """`pcsx2 recordings push | pull`: copy a recording between the scratch folder's `recordings/` and the claimed
    copy's `recordings/` (push: to the copy; pull: from it)."""
    registry = pcsx2_claims.default_registry()
    claim = registry.require_claim(agent, copy)
    relative = recording_path(name)
    shared, own = scratch_recordings(), Path(claim.path) / "recordings"
    source, target = (shared, own) if direction == "push" else (own, shared)
    for path, digest in transfer_recording(source, target, relative, force):
        print(f"{path}: sha256 {digest}")
    if direction == "push":
        print(f"play it with pcsx2_input_recording play, path recordings/{relative.as_posix()}")
    return 0

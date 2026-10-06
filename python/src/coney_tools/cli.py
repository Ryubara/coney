# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools <group> <command>` command line."""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Sequence
from importlib.metadata import version
from pathlib import Path
from typing import Any

from coney_tools import natives_cli, pcsx2_cli, progress_cli, refs_cli, trace_cli, wad_cli, xbox_cli
from coney_tools.config import PATH_KEYS, ConfigError, find_repo_root, load_config
from coney_tools.repo_checks import check_pointer_files, check_title, first_line, load_title_rules


def _config_show() -> int:
    """Print every path key with whether it exists."""
    config = load_config(find_repo_root(Path.cwd()))
    for key in PATH_KEYS:
        path = config.paths[key]
        if path is None:
            print(f"{key} = unset")
        else:
            print(f"{key} = {path} ({'found' if path.exists() else 'missing'})")
    return 0


def _repo_check() -> int:
    """Run the repository checks; 1 when any fails."""
    problems = check_pointer_files(find_repo_root(Path.cwd()))
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print("pointer files: ok")
    return 0


def _repo_check_title(file: str) -> int:
    """Check the title in `file` (a commit message or a pull request title; `-` reads stdin); 1 when refused."""
    rules = load_title_rules(find_repo_root(Path.cwd()))
    if file == "-":
        message = sys.stdin.read()
    else:
        try:
            message = Path(file).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            print(f"coney-tools: {file}: cannot be read ({error})", file=sys.stderr)
            return 2
    title = first_line(message)
    problems = check_title(title, rules)
    if problems:
        print(f"title refused: {title!r}: {'; '.join(problems)} (see AGENTS.md, Commits and GitHub)")
        return 1
    print("title: ok")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    """Build the parser for every group and command; each group's commands are registered by its own helper."""
    parser = argparse.ArgumentParser(prog="coney-tools", description="Coney's own automation.")
    parser.add_argument("--version", action="version", version=f"coney-tools {version('coney-tools')}")
    groups = parser.add_subparsers(dest="group", required=True)
    config = groups.add_parser("config", help="local configuration (coney.local.toml)")
    config.add_subparsers(dest="command", required=True).add_parser("show", help="show the configured paths")
    repo = groups.add_parser("repo", help="repository checks")
    repo_commands = repo.add_subparsers(dest="command", required=True)
    repo_commands.add_parser("check", help="check the agent pointer files")
    check_title_parser = repo_commands.add_parser(
        "check-title", help="check a commit or pull request title against the commit title rules"
    )
    check_title_parser.add_argument("file", help="a file whose first line (not blank, not #) is the title; - for stdin")
    _add_wad_commands(groups)
    _add_xbox_commands(groups)
    _add_progress_commands(groups)
    _add_natives_commands(groups)
    _add_refs_commands(groups)
    _add_pcsx2_commands(groups)
    _add_trace_commands(groups)
    return parser


def _add_wad_commands(groups: Any) -> None:
    """Register `coney-tools wad ...`."""
    disc_help = "a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    names_help = "a names file, one name per line (made by `wad names`)"
    wad = groups.add_parser("wad", help="read the game's WARRIORS.DIR / WARRIORS.WAD archive")
    commands = wad.add_subparsers(dest="command", required=True)
    info = commands.add_parser("info", help="entry count, sizes and a breakdown by first four bytes")
    info.add_argument("disc", nargs="?", help=disc_help)
    info.add_argument("--names", type=Path, help=names_help)
    listing = commands.add_parser("list", help="one line per entry: index, offset, size, hash, name")
    listing.add_argument("disc", nargs="?", help=disc_help)
    listing.add_argument("--names", type=Path, help=names_help)
    extract = commands.add_parser("extract", help="write entries to OUT_DIR (outside the repository)")
    extract.add_argument("paths", nargs="+", metavar="[DISC] OUT_DIR", help=f"[DISC] ({disc_help}) and OUT_DIR")
    extract.add_argument("--names", type=Path, help=names_help)
    extract.add_argument("--only", nargs="+", metavar="HASH_OR_NAME", help="extract just these entries")
    names = commands.add_parser("names", help="recover names by hashing strings found on the disc")
    names.add_argument("paths", nargs="+", metavar="[DISC] OUT_FILE", help=f"[DISC] ({disc_help}) and OUT_FILE")
    scene_check = commands.add_parser("scenes", help="parse every scene record of scene_list.cnk; counts and hashes")
    scene_check.add_argument("disc", nargs="?", help=disc_help)


def _add_xbox_commands(groups: Any) -> None:
    """Register `coney-tools xbox ...`."""
    disc_help = "the Xbox disc: a full image, an XISO or an extracted folder"
    names_help = "a names file, one name per line (made by `xbox names`, or a PS2 names file)"
    ps2_help = "also compare with the PS2 disc (a folder or .iso, as for the wad commands)"
    group = groups.add_parser("xbox", help="read the Xbox disc's XBoxWad.idx archive (an optional asset source)")
    commands = group.add_subparsers(dest="command", required=True)
    files = commands.add_parser("files", help="every file on the disc with its size")
    files.add_argument("disc", help=disc_help)
    info = commands.add_parser("info", help="entries and bytes by volume and by kind")
    info.add_argument("disc", help=disc_help)
    info.add_argument("--names", type=Path, help=names_help)
    listing = commands.add_parser("list", help="one line per entry: index, volume, offset, size, hash, name")
    listing.add_argument("disc", help=disc_help)
    listing.add_argument("--names", type=Path, help=names_help)
    names = commands.add_parser("names", help="match candidate names against the index; write the matches")
    names.add_argument("disc", help=disc_help)
    names.add_argument("out_file", type=Path, help="where to write the names (outside the repository)")
    names.add_argument("--candidates", type=Path, nargs="+", metavar="FILE", help="names files to try")
    extract = commands.add_parser("extract", help="write entries to OUT_DIR (outside the repository)")
    extract.add_argument("disc", help=disc_help)
    extract.add_argument("out_dir", type=Path, help="the folder to write to")
    extract.add_argument("--names", type=Path, help=names_help)
    extract.add_argument("--only", nargs="+", metavar="HASH_OR_NAME", help="extract just these entries")
    resources = commands.add_parser("resources", help="the resource index: counts and graphics-chunk kinds")
    resources.add_argument("disc", help=disc_help)
    resources.add_argument("--ps2", metavar="PS2_DISC", help=ps2_help)
    textures = commands.add_parser("textures", help="texture chunk counts by format, mip count and size")
    textures.add_argument("disc", help=disc_help)
    textures.add_argument("--ps2", metavar="PS2_DISC", help=ps2_help)


def _add_progress_commands(groups: Any) -> None:
    """Register `coney-tools progress ...`."""
    tracker = groups.add_parser("progress", help="the progress tracker shown in README.md and docs/progress/")
    commands = tracker.add_subparsers(dest="command", required=True)
    show = commands.add_parser("show", help="print how much is reimplemented and researched")
    show.add_argument("--json", action="store_true", help="print everything as JSON")
    update = commands.add_parser("update", help="regenerate the progress blocks of README.md and the docs page")
    update.add_argument("--check", action="store_true", help="change nothing; exit 1 when a block is stale")
    sizes = commands.add_parser("sizes", help="check the listed functions' sizes against your own disc")
    sizes.add_argument(
        "disc", nargs="?", help="a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    )
    sizes.add_argument("--fill", action="store_true", help="write the estimated size of entries that have none")


def _add_natives_commands(groups: Any) -> None:
    """Register `coney-tools natives ...`."""
    group = groups.add_parser("natives", help="the script-binding masterlist (research/bindings/)")
    commands = group.add_subparsers(dest="command", required=True)
    render = commands.add_parser("render", help="check the YAML and regenerate docs/references/bindings/")
    render.add_argument("--check", action="store_true", help="change nothing; exit 1 when a page is stale")
    coney = commands.add_parser("coney", help="set each entry's coney status from src/scripting/script_bindings.cpp")
    coney.add_argument("--check", action="store_true", help="change nothing; exit 1 when a status is stale")
    cpp = commands.add_parser("cpp", help="write the debug menus' C++ signature table, src/debug/native_signatures.cpp")
    cpp.add_argument("--check", action="store_true", help="change nothing; exit 1 when the table is stale")
    commands.add_parser("stats", help="print the counts by category, evidence level and usage")
    mission = commands.add_parser("mission1", help="set usage.mission1 from the first mission's scripts on your disc")
    mission.add_argument(
        "disc", nargs="?", help="a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    )
    mission.add_argument("--check", action="store_true", help="change nothing; exit 1 when a marker is stale")


def _add_refs_commands(groups: Any) -> None:
    """Register `coney-tools refs ...`."""
    group = groups.add_parser("refs", help="the game reference lists (research/references/, docs/references/)")
    commands = group.add_subparsers(dest="command", required=True)
    render = commands.add_parser("render", help="check the lists and write the pages of docs/references/")
    render.add_argument("--check", action="store_true", help="change nothing; exit 1 when a page is stale")
    extract = commands.add_parser("extract", help="refresh the lists from your own disc, keeping hand-written fields")
    extract.add_argument(
        "disc", nargs="?", help="a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    )
    extract.add_argument("--only", nargs="+", choices=refs_cli.topic_keys(), metavar="LIST", help="these lists only")
    extract.add_argument("--names", type=Path, help="extra WAD names, one per line (the last word of each line)")
    compress = commands.add_parser("compress-images", help="rewrite the thumbnails as 256-colour PNGs, in place")
    compress.add_argument("folder", nargs="?", type=Path, help="default: docs/references/images/")


def _add_pcsx2_flags(command: Any) -> None:
    """The folder flags every PCSX2 command takes; each defaults to coney.local.toml."""
    command.add_argument("--pcsx2-dir", type=Path, help="the portable PCSX2 folder; default: pcsx2_dir")
    command.add_argument("--iso", type=Path, help="the disc image (or a folder with one); default: game_dir")
    command.add_argument("--scratch", type=Path, help="where state copies and the disc link go; default: scratch_dir")


def _add_pcsx2_commands(groups: Any) -> None:
    """Register `coney-tools pcsx2 ...`."""
    group = groups.add_parser("pcsx2", help="drive the original in PCSX2 over PINE: patched states, recording")
    commands = group.add_subparsers(dest="command", required=True)
    prepare = commands.add_parser("prepare-state", help="copy a save state with patches applied to its EE memory")
    prepare.add_argument("source", help="a .p2s file, or slot:N for quick-save slot N (only read)")
    prepare.add_argument("out", type=Path, help="the patched copy (outside the repository and sstates/)")
    prepare.add_argument(
        "--patch", nargs="+", default=[], metavar="NAME", help="patches of research/traces/patches.toml"
    )
    prepare.add_argument("--pcsx2-dir", type=Path, help="the portable PCSX2 folder; default: pcsx2_dir")
    repack = commands.add_parser("repack-state", help="copy a save state with plain deflate in place of zstd")
    repack.add_argument("source", help="a .p2s file, or slot:N for quick-save slot N (only read)")
    repack.add_argument("out", type=Path, help="the repacked copy (outside the repository and sstates/)")
    repack.add_argument("--pcsx2-dir", type=Path, help="the portable PCSX2 folder; default: pcsx2_dir")
    launch = commands.add_parser("launch", help="start PCSX2 on a state file and wait until its game runs")
    launch.add_argument("state", type=Path, help="a .p2s file (a patched copy)")
    _add_pcsx2_flags(launch)
    record = commands.add_parser("record", help="play a scenario on the original and write its per-update trace")
    record.add_argument("scenario", type=Path, help="a scenario TOML (research/traces/scenarios/)")
    record.add_argument("--out", type=Path, required=True, help="the trace CSV (outside the repository)")
    record.add_argument("--state", help="the state to copy instead of the scenario's slot: a .p2s file or slot:N")
    record.add_argument("--attach", action="store_true", help="record a PCSX2 already running a patched state")
    record.add_argument("--keep-open", action="store_true", help="leave PCSX2 running afterwards")
    _add_pcsx2_flags(record)


def _add_trace_commands(groups: Any) -> None:
    """Register `coney-tools trace ...`."""
    group = groups.add_parser("trace", help="per-update traces: run a scenario on Coney, compare with the original")
    commands = group.add_subparsers(dest="command", required=True)
    coney = commands.add_parser("coney", help="play a scenario on Coney headless and write its --trace")
    coney.add_argument("scenario", type=Path, help="a scenario TOML (research/traces/scenarios/)")
    coney.add_argument("--out", type=Path, required=True, help="the trace CSV (outside the repository)")
    coney.add_argument("--coney", type=Path, help="Coney's executable; default: build/dev/src/platform/coney")
    coney.add_argument("--disc", help="the disc for Coney; default: game_dir")
    diff = commands.add_parser("diff", help="compare the original's trace with Coney's; exit 1 outside tolerance")
    diff.add_argument("original", type=Path, help="the original's trace (pcsx2 record)")
    diff.add_argument("coney", type=Path, help="Coney's trace (coney --trace, or trace coney)")
    diff.add_argument("--scenario", type=Path, help="take the columns, tolerances, start and frame from a scenario")
    diff.add_argument("--columns", nargs="+", metavar="COLUMN", help="compare these (default: every shared one)")
    diff.add_argument("--tolerance", nargs="+", default=[], metavar="COLUMN=VALUE", help="per column; *=VALUE for all")
    diff.add_argument("--from", dest="start", type=int, help="the first step (default 1, or the input's first)")
    diff.add_argument("--to", dest="end", type=int, help="the last step (default: the last both have)")
    diff.add_argument("--shift", type=int, help="compare original step s with Coney step s + SHIFT (default 0)")
    diff.add_argument("--start-frame", action="store_true", help="compare in each player's frame at the first step")
    diff.add_argument("--context", type=int, default=3, help="rows shown each side of a divergence (default 3)")


def _run_progress(args: argparse.Namespace) -> int:
    """Dispatch a `progress` command."""
    if args.command == "show":
        return progress_cli.run_show(args.json)
    if args.command == "update":
        return progress_cli.run_update(args.check)
    return progress_cli.run_sizes(args.disc, args.fill)


def _run_wad(args: argparse.Namespace) -> int:
    """Dispatch a `wad` command."""
    if args.command == "info":
        return wad_cli.run_info(args.disc, args.names)
    if args.command == "list":
        return wad_cli.run_list(args.disc, args.names)
    if args.command == "extract":
        return wad_cli.run_extract(args.paths, args.names, args.only)
    if args.command == "scenes":
        return wad_cli.run_scenes(args.disc)
    return wad_cli.run_names(args.paths)


def _run_xbox(args: argparse.Namespace) -> int:
    """Dispatch an `xbox` command."""
    if args.command == "files":
        return xbox_cli.run_files(args.disc)
    if args.command == "info":
        return xbox_cli.run_info(args.disc, args.names)
    if args.command == "list":
        return xbox_cli.run_list(args.disc, args.names)
    if args.command == "names":
        return xbox_cli.run_names(args.disc, args.out_file, args.candidates)
    if args.command == "extract":
        return xbox_cli.run_extract(args.disc, args.out_dir, args.names, args.only)
    if args.command == "resources":
        return xbox_cli.run_resources(args.disc, args.ps2)
    return xbox_cli.run_textures(args.disc, args.ps2)


def _run_pcsx2(args: argparse.Namespace) -> int:
    """Dispatch a `pcsx2` command."""
    if args.command == "prepare-state":
        return pcsx2_cli.run_prepare_state(args.source, args.out, args.patch, args.pcsx2_dir)
    if args.command == "repack-state":
        return pcsx2_cli.run_repack_state(args.source, args.out, args.pcsx2_dir)
    if args.command == "launch":
        return pcsx2_cli.run_launch(args.state, args.pcsx2_dir, args.iso, args.scratch)
    flags = (args.pcsx2_dir, args.iso, args.scratch)
    return pcsx2_cli.run_record(args.scenario, args.out, args.state, args.attach, args.keep_open, flags)


def _run(args: argparse.Namespace) -> int:
    """Dispatch to the chosen command and return its exit status."""
    if args.group == "wad":
        return _run_wad(args)
    if args.group == "xbox":
        return _run_xbox(args)
    if args.group == "progress":
        return _run_progress(args)
    if args.group == "natives":
        if args.command == "render":
            return natives_cli.run_render(args.check)
        if args.command == "cpp":
            return natives_cli.run_cpp(args.check)
        if args.command == "mission1":
            return natives_cli.run_mission1(args.disc, args.check)
        return natives_cli.run_coney(args.check) if args.command == "coney" else natives_cli.run_stats()
    if args.group == "refs":
        if args.command == "render":
            return refs_cli.run_render(args.check)
        if args.command == "compress-images":
            return refs_cli.run_compress_images(args.folder)
        return refs_cli.run_extract(args.disc, args.only, args.names)
    if args.group == "pcsx2":
        return _run_pcsx2(args)
    if args.group == "trace":
        if args.command == "coney":
            return trace_cli.run_coney(args.scenario, args.out, args.coney, args.disc)
        options = (args.start, args.end, args.shift, args.start_frame, args.context)
        return trace_cli.run_diff(args.original, args.coney, args.scenario, args.columns, args.tolerance, options)
    if args.group == "config":
        return _config_show()
    if args.command == "check-title":
        return _repo_check_title(args.file)
    return _repo_check()


def main(argv: Sequence[str] | None = None) -> int:
    """Run the command line; returns 0 on success, 1 when a check fails, 2 on a usage or configuration error."""
    args = _build_parser().parse_args(argv)
    try:
        status = _run(args)
        # Flush here, inside the handler below: a closed pipe often only shows when buffered output is written.
        sys.stdout.flush()
        return status
    except ConfigError as error:
        print(f"coney-tools: {error}", file=sys.stderr)
        return 2
    except (BrokenPipeError, wad_cli.OutputClosedError):
        # The reader went away (`wad list | head`): stop quietly, as Unix tools do. Point stdout at devnull so the
        # interpreter's final flush does not fail again on exit.
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        return 0


if __name__ == "__main__":
    sys.exit(main())

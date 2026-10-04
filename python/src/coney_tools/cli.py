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

from coney_tools import progress_cli, wad_cli
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
    _add_progress_commands(groups)
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


def _run_progress(args: argparse.Namespace) -> int:
    if args.command == "show":
        return progress_cli.run_show(args.json)
    if args.command == "update":
        return progress_cli.run_update(args.check)
    return progress_cli.run_sizes(args.disc, args.fill)


def _run_wad(args: argparse.Namespace) -> int:
    if args.command == "info":
        return wad_cli.run_info(args.disc, args.names)
    if args.command == "list":
        return wad_cli.run_list(args.disc, args.names)
    if args.command == "extract":
        return wad_cli.run_extract(args.paths, args.names, args.only)
    return wad_cli.run_names(args.paths)


def _run(args: argparse.Namespace) -> int:
    """Dispatch to the chosen command and return its exit status."""
    if args.group == "wad":
        return _run_wad(args)
    if args.group == "progress":
        return _run_progress(args)
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

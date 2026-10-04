# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools <group> <command>` command line."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from importlib.metadata import version
from pathlib import Path

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
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """Run the command line; returns 0 on success, 1 when a check fails, 2 on a usage or configuration error."""
    args = _build_parser().parse_args(argv)
    try:
        if args.group == "config":
            return _config_show()
        if args.command == "check-title":
            return _repo_check_title(args.file)
        return _repo_check()
    except ConfigError as error:
        print(f"coney-tools: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())

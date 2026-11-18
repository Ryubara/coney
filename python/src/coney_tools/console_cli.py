# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools console ...` commands: `render-cpp` (the game library's table of player commands) and `render`
(the console commands reference), both from the console's catalogs and both with `--check`, which CI runs
(docs/guides/console.md#catalog)."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from coney_tools import console_catalog, console_cpp, console_render
from coney_tools.config import ConfigError, find_repo_root


def add_commands(groups: Any) -> None:
    """Register `coney-tools console ...`."""
    group = groups.add_parser("console", help="the Coney console's command catalogs (src/console/commands.toml)")
    commands = group.add_subparsers(dest="command", required=True)
    cpp = commands.add_parser(
        "render-cpp", help="write the game library's table of player commands, src/console/commands_table.cpp"
    )
    cpp.add_argument("--check", action="store_true", help="change nothing; exit 1 when the table is stale")
    render = commands.add_parser("render", help="write the console commands reference, docs/guides/console-commands.md")
    render.add_argument(
        "--check", action="store_true", help="change nothing; exit 1 when the page is stale or a command has no example"
    )


def run(args: Any) -> int:
    """Run the `console` command `args` names."""
    if args.command == "render-cpp":
        return run_cpp(args.check)
    return run_render(args.check)


def _load(root: Path) -> list[console_catalog.Catalog] | None:
    """The catalogs, or None after printing their problems."""
    catalogs = console_catalog.load(root)
    problems = [problem for catalog in catalogs for problem in catalog.problems]
    if problems:
        for problem in problems:
            print(f"problem: {problem}")
        print(f"console: {len(problems)} problem(s) in the catalogs")
        return None
    return catalogs


def _write(root: Path, path: Path, text: str, check_only: bool, command: str) -> int:
    """Compare `text` with the file at `path`; write it unless checking. 1 when it is stale and only checked."""
    full = root / path
    try:
        current = full.read_bytes().decode("utf-8").replace("\r\n", "\n") if full.exists() else None
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{full}: cannot be read ({error})") from error
    stale = current != text
    if check_only and stale:
        print(f"console: {path.as_posix()} is stale; run `uv run --project python coney-tools console {command}`")
        return 1
    if stale:
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_bytes(text.encode("utf-8"))
    print(f"console: {path.as_posix()} {'updated' if stale else 'up to date'}")
    return 0


def run_cpp(check_only: bool) -> int:
    """Write src/console/commands_table.cpp from the player catalog; with `check_only`, write nothing and return 1
    when it is stale."""
    root = find_repo_root(Path.cwd())
    catalogs = _load(root)
    if catalogs is None:
        return 1
    return _write(root, console_cpp.CPP_FILE, console_cpp.render_cpp(catalogs[0]), check_only, "render-cpp")


def run_render(check_only: bool) -> int:
    """Write the console commands reference; with `check_only`, write nothing and return 1 when it is stale. A command
    without an example is a problem of the catalog, so both fail on it."""
    root = find_repo_root(Path.cwd())
    catalogs = _load(root)
    if catalogs is None:
        return 1
    return _write(root, console_render.PAGE, console_render.page(catalogs), check_only, "render")

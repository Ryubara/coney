# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools ghidra ...` commands: `setup` (a contributor's own named project) and `import-names`.

Paths come from flags, then coney.local.toml (`ghidra_install`, `ghidra_projects`, `jdk_home`, `game_dir`), then the
workspace defaults of coney.local.example.toml. See docs/guides/ghidra.md#own-project.
"""

from __future__ import annotations

import argparse
import os
import shutil
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

from coney_tools import ghidra_names, ghidra_setup, workspace_fetch
from coney_tools.build_cli import scratch_root
from coney_tools.config import ConfigError, find_repo_root, load_config, local_config_file, workspace_folder
from coney_tools.disc import NTSC_U_SHA1, Disc
from coney_tools.ghidra_headless import PROGRAM, PROJECT_NAME, GhidraProject, has_program, run_logged
from coney_tools.progress_research import LISTING_FILE, ListedFunction, load_listing
from coney_tools.wad_cli import open_disc
from coney_tools.workspace_checks import find_jdk, run_command

#: The ghidra-mcp server's address (docs/guides/ghidra.md: port 8090 is this project's server).
SERVER_URL = "http://127.0.0.1:8090"


def add_parser(groups: Any) -> None:
    """Register `coney-tools ghidra setup` and `ghidra import-names`."""
    group = groups.add_parser("ghidra", help="your own Ghidra project of SLUS_212.15 (docs/guides/ghidra.md)")
    commands = group.add_subparsers(dest="ghidra_command", required=True)
    setup = commands.add_parser(
        "setup", help="fetch Ghidra, compile the EE language, import SLUS_212.15 from your disc, apply our names"
    )
    setup.add_argument("--disc", help="a mounted disc, folder or .iso (default: game_dir)")
    setup.add_argument("--no-names", action="store_true", help="stop after the import; do not apply the names")
    setup.add_argument(
        "--any-release", action="store_true", help="import an executable that is not the NTSC-U release anyway"
    )
    import_names = commands.add_parser(
        "import-names", help="apply docs/progress/ghidra-functions.tsv's names to your project by address"
    )
    import_names.add_argument("--program", default=PROGRAM, help=f"the program in the project (default {PROGRAM})")
    import_names.add_argument("--listing", type=Path, help=f"the names listing (default {LISTING_FILE.as_posix()})")
    for command in (setup, import_names):
        command.add_argument(
            "--project-dir", type=Path, help="the folder holding the project (default: ghidra_projects)"
        )
        command.add_argument(
            "--project-name", default=PROJECT_NAME, help=f"the project's name (default {PROJECT_NAME})"
        )
        command.add_argument("--overwrite", action="store_true", help="also replace names you gave by hand")
        command.add_argument("--dry-run", action="store_true", help="print what would happen; save nothing")


def _server_up() -> bool:
    """True when a ghidra-mcp server answers on this project's port."""
    try:
        with urllib.request.urlopen(f"{SERVER_URL}/check_connection", timeout=2) as response:
            return bool(response.status == 200)
    except (urllib.error.URLError, OSError):
        return False


def _setup_paths(root: Path, args: argparse.Namespace) -> ghidra_setup.Setup:
    """The folders and programs for `root`, from the flags, coney.local.toml and the workspace defaults."""
    paths = load_config(root).paths if local_config_file(root) else {}
    platform = workspace_fetch.platform_name()
    install = paths.get("ghidra_install") or workspace_folder(
        root, f"tools/ghidra_{workspace_fetch.GHIDRA_VERSION}_PUBLIC"
    )
    projects = args.project_dir or paths.get("ghidra_projects") or workspace_folder(root, "ghidra")
    jdk = find_jdk(platform, shutil.which, run_command, os.environ, paths.get("jdk_home"))
    scratch = scratch_root(root)

    def fetch(dest: Path, cache: Path) -> None:
        workspace_fetch.fetch_tool("ghidra", dest, platform, cache, workspace_fetch.download_url)

    return ghidra_setup.Setup(
        install=install,
        jdk=jdk,
        project=GhidraProject(projects.resolve(), args.project_name),
        work=scratch / "ghidra",
        downloads=scratch / "downloads",
        platform=platform,
        runner=run_logged,
        fetch=fetch,
        server_up=_server_up,
    )


def _listing(root: Path, given: Path | None) -> list[ListedFunction]:
    """The names listing, from `given` or the checkout (a malformed one raises ProgressError, a ConfigError)."""
    path = given or root / LISTING_FILE
    functions = load_listing(path)
    if functions is None:
        raise ConfigError(f"{path}: not found")
    return functions


def run(args: argparse.Namespace) -> int:
    """Dispatch `ghidra setup` or `ghidra import-names`."""
    root = find_repo_root(Path.cwd())
    setup = _setup_paths(root, args)
    listing = _listing(root, getattr(args, "listing", None))
    if args.ghidra_command == "setup":
        disc: Disc | None = None
        if not has_program(setup.project, PROGRAM):
            disc = open_disc(args.disc)
        options = ghidra_setup.Options(NTSC_U_SHA1, not args.no_names, args.any_release, args.dry_run, args.overwrite)
        return ghidra_setup.run_setup(setup, disc, listing, options)
    if setup.jdk is None:
        raise ConfigError("no JDK 21 found: install one and set jdk_home in coney.local.toml")
    if not has_program(setup.project, args.program):
        raise ConfigError(f"{setup.project.gpr} holds no {args.program}: run `coney-tools ghidra setup` first")
    if setup.project.lock.exists() and _server_up():
        raise ConfigError(f"{setup.project.gpr} is open in a running ghidra-mcp server; stop the server first")
    job = ghidra_names.ImportJob(
        setup.project, args.program, setup.install, setup.jdk, setup.work, args.dry_run, args.overwrite
    )
    for line in ghidra_names.format_summary(ghidra_names.import_names(job, listing), args.dry_run):
        print(line)
    return 0

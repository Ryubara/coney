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

from coney_tools import (
    audio_cli,
    check_cli,
    clips,
    mcp_serve,
    mission_cli,
    missions_render,
    movies_cli,
    natives_cli,
    pcsx2_claims_cli,
    pcsx2_cli,
    pcsx2_clip,
    progress_cli,
    refs_cli,
    stale_processes,
    trace_cli,
    wad_cli,
    workspace,
    workspace_clean,
    workspace_fetch,
    xbox_cli,
)
from coney_tools.audit_cli import add_audit_parser, run_audit
from coney_tools.config import PATH_KEYS, ConfigError, find_repo_root, load_config
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.conventions_render import run_render
from coney_tools.hygiene_cli import add_hygiene_parser, run_hygiene
from coney_tools.pr_health import add_pr_health_parser, run_pr_health
from coney_tools.repo_checks import check_body, check_docs, check_pointer_files, check_title, first_line
from coney_tools.repo_labels import apply_labels, gh_script


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


def _conventions() -> Conventions:
    """The conventions of the checkout the command runs in (`.github/conventions.yml`); ConfigError when unusable."""
    return load_conventions(find_repo_root(Path.cwd()))


def _repo_check() -> int:
    """Run the repository checks; 1 when any fails."""
    root = find_repo_root(Path.cwd())
    # Loading the conventions validates the file itself, so a bad edit or merge of it fails here, in CI.
    conventions = load_conventions(root)
    problems = check_pointer_files(root, conventions.paths.pointer_files)
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print("conventions: ok")
    print("pointer files: ok")
    return 0


def _repo_labels(apply: bool, repo: str | None) -> int:
    """Print the `gh` commands that create the `labels` of the conventions, or run them with `apply`; 1 on failure."""
    labels = _conventions().labels.list
    if not apply:
        print("\n".join(gh_script(labels, repo)))
        return 0
    failed = apply_labels(labels, repo)
    for name in failed:
        print(f"label failed: {name}", file=sys.stderr)
    print(f"labels: {len(labels) - len(failed)} of {len(labels)} synced")
    return 1 if failed else 0


def _repo_check_title(file: str) -> int:
    """Check the title in `file` (a commit message or a pull request title; `-` reads stdin); 1 when refused."""
    rules = _conventions().commits.title
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


def _repo_check_docs(body_file: str | None) -> int:
    """Check the changed paths on stdin (one per line) against the pull request body in `body_file`; 1 when refused."""
    changed = [line.strip().replace("\\", "/") for line in sys.stdin.read().splitlines() if line.strip()]
    body = ""
    if body_file is not None:
        try:
            body = Path(body_file).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            print(f"coney-tools: {body_file}: cannot be read ({error})", file=sys.stderr)
            return 2
    conventions = _conventions()
    problems = check_docs(changed, body, conventions.paths, conventions.pull_requests.docs_none)
    if problems:
        print(f"docs refused: {'; '.join(problems)} (see docs/guides/research-workflow.md)")
        return 1
    print("docs: ok")
    return 0


def _repo_check_body(body_file: str | None) -> int:
    """Check the pull request description in `body_file` (`-` or none reads stdin); 1 when refused."""
    rules = _conventions().pull_requests
    if body_file is None or body_file == "-":
        body = sys.stdin.read()
    else:
        try:
            body = Path(body_file).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            print(f"coney-tools: {body_file}: cannot be read ({error})", file=sys.stderr)
            return 2
    problems = check_body(body, rules)
    if problems:
        print(f"body refused: {'; '.join(problems)} (see .github/pull_request_template.md)")
        return 1
    print("body: ok")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    """Build the parser for every group and command; each group's commands are registered by its own helper."""
    parser = argparse.ArgumentParser(prog="coney-tools", description="Coney's own automation.")
    parser.add_argument("--version", action="version", version=f"coney-tools {version('coney-tools')}")
    groups = parser.add_subparsers(dest="group", required=True)
    config = groups.add_parser("config", help="local configuration (coney.local.toml)")
    config.add_subparsers(dest="command", required=True).add_parser("show", help="show the configured paths")
    group = groups.add_parser("workspace", help="the local workspace beside the checkout (docs/guides/workspace.md)")
    workspace_commands = group.add_subparsers(dest="command", required=True)
    init = workspace_commands.add_parser(
        "init", help="create the workspace folders, clone, write coney.local.toml and check the prerequisites"
    )
    init.add_argument("--workspace", type=Path, help="the workspace folder (default: found from the checkout)")
    init.add_argument("--repo-url", help="the repository to clone when not run inside a checkout")
    init.add_argument(
        "--fetch",
        action="append",
        default=[],
        choices=[*workspace_fetch.TOOLS, "all"],
        help="also download a research tool at its pinned version (repeatable; never a BIOS or game files)",
    )
    init.add_argument("--dry-run", action="store_true", help="print the steps without doing any")
    clean = workspace_commands.add_parser(
        "clean",
        help="remove merged worktrees and old build folders, stop stale waits and orphaned test runs "
        "(a dry run unless --apply)",
    )
    clean.add_argument("--workspace", type=Path, help="the workspace folder (default: found from the checkout)")
    clean.add_argument("--apply", action="store_true", help="act; without it, only report what would be done")
    clean.add_argument(
        "--build-hours",
        type=float,
        default=workspace_clean.DEFAULT_BUILD_HOURS,
        help="delete build/ folders of worktrees untouched this many hours (default: %(default)g)",
    )
    clean.add_argument(
        "--worktree-hours",
        type=float,
        default=workspace_clean.DEFAULT_WORKTREE_HOURS,
        help="remove merged worktrees only when untouched this many hours (default: %(default)g)",
    )
    clean.add_argument("--fetch", action="store_true", help="git fetch origin main first, so merges are current")
    clean.add_argument(
        "--shell-hours",
        type=float,
        default=stale_processes.DEFAULT_SHELL_HOURS,
        help="a shell waiting in a loop is stale only when older than this many hours (default: %(default)g)",
    )
    clean.add_argument(
        "--log-idle-hours",
        type=float,
        default=stale_processes.DEFAULT_IDLE_HOURS,
        help="... and every file its loop reads is unchanged this many hours (default: %(default)g)",
    )
    disk = workspace_commands.add_parser(
        "disk", help="print the free space of the workspace drive; exit 1 below --min-gb"
    )
    disk.add_argument("--workspace", type=Path, help="the workspace folder (default: found from the checkout)")
    disk.add_argument(
        "--min-gb",
        type=float,
        default=None,
        help="the free space in GB below which the exit code is 1 (default: workspace.disk_min_free_gb)",
    )
    mcp = groups.add_parser("mcp", help="the debugging servers that .mcp.json registers for agents")
    serve = mcp.add_subparsers(dest="command", required=True).add_parser(
        "serve", help="run a server found through coney.local.toml, over stdin and stdout"
    )
    serve.add_argument("name", choices=mcp_serve.SERVERS)
    repo = groups.add_parser("repo", help="repository checks")
    repo_commands = repo.add_subparsers(dest="command", required=True)
    repo_commands.add_parser("check", help="check .github/conventions.yml and the agent pointer files")
    check_title_parser = repo_commands.add_parser(
        "check-title", help="check a commit or pull request title against the commit title rules"
    )
    check_title_parser.add_argument("file", help="a file whose first line (not blank, not #) is the title; - for stdin")
    check_docs_parser = repo_commands.add_parser(
        "check-docs", help="check that a pull request which changes code also changes the docs (changed paths on stdin)"
    )
    check_docs_parser.add_argument("--body", help="a file holding the pull request description (for `Docs: none`)")
    check_body_parser = repo_commands.add_parser(
        "check-body", help="check a pull request description against the template: sections, summary, AI use"
    )
    check_body_parser.add_argument(
        "--body", help="a file holding the pull request description; - or omitted reads stdin"
    )
    render_parser = repo_commands.add_parser(
        "render",
        help="rewrite the generated blocks of AGENTS.md, CONTRIBUTING.md and the guides from .github/conventions.yml",
    )
    render_parser.add_argument(
        "--check", action="store_true", help="change nothing; exit 1 when a block is out of date"
    )
    add_audit_parser(repo_commands)
    add_pr_health_parser(repo_commands)
    add_hygiene_parser(repo_commands)
    _add_check_commands(groups)
    labels_parser = repo_commands.add_parser(
        "labels",
        help="print the gh commands for the issue labels of .github/conventions.yml, or sync them with --apply",
    )
    labels_parser.add_argument("--apply", action="store_true", help="create or update the labels through gh now")
    labels_parser.add_argument("--repo", help="OWNER/NAME to sync; default: the repository gh finds from the checkout")
    _add_wad_commands(groups)
    _add_extract_command(groups)
    _add_audio_commands(groups)
    _add_movies_commands(groups)
    _add_xbox_commands(groups)
    _add_progress_commands(groups)
    _add_natives_commands(groups)
    _add_refs_commands(groups)
    _add_missions_commands(groups)
    _add_pcsx2_commands(groups)
    _add_clips_commands(groups)
    _add_trace_commands(groups)
    return parser


def _add_check_commands(groups: Any) -> None:
    """Register `coney-tools check ...`."""
    group = groups.add_parser("check", help="the local gate: what CI would fail on, for the files a change touched")
    commands = group.add_subparsers(dest="command", required=True)
    changed = commands.add_parser(
        "changed",
        help="clang-tidy (with GCC/Clang warnings), pre-commit, docs, Python and repo checks on what changed",
    )
    changed.add_argument("--base", default="origin/main", help="the branch the change is measured from")
    changed.add_argument("--build-dir", default="build/dev", help="the build folder holding compile_commands.json")
    changed.add_argument("--only", action="append", choices=check_cli.STEPS, help="run only this step (repeatable)")
    changed.add_argument("--skip", action="append", choices=check_cli.STEPS, help="leave this step out (repeatable)")
    changed.add_argument(
        "--jobs",
        type=int,
        default=max(1, (os.cpu_count() or 2) // 2),
        help="clang-tidy processes at once (default: half the cores)",
    )
    changed.add_argument("--clang-tidy", help="the clang-tidy to use; default: clang-tidy-19, Visual Studio's, PATH")


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


def _add_extract_command(groups: Any) -> None:
    """Register `coney-tools extract [DISC] OUT_DIR`."""
    from coney_tools.extract_types import TYPES  # the type list only; the extractor itself loads NumPy

    disc_help = "a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    extract = groups.add_parser(
        "extract", help="extract every asset of your disc to open formats in OUT_DIR (outside the repository)"
    )
    extract.add_argument("paths", nargs="+", metavar="[DISC] OUT_DIR", help=f"[DISC] ({disc_help}) and OUT_DIR")
    extract.add_argument(
        "--only", nargs="+", metavar="TYPE", choices=list(TYPES), help=f"just these types: {', '.join(TYPES)}"
    )
    extract.add_argument("--verify", action="store_true", help="compare the file counts with the expected ones")
    extract.add_argument("--jobs", type=int, metavar="N", help="worker processes for decoding audio (default: up to 4)")
    extract.add_argument(
        "--xbox",
        type=Path,
        metavar="XBOX_DISC",
        help="your Xbox disc (image, XISO or folder): its larger textures and HD movies replace the PS2 ones",
    )
    extract.add_argument(
        "--xbox-index",
        type=Path,
        metavar="FILE",
        help="the Xbox resource index (`xbox index`): read when it fits the disc, else built and written there",
    )
    extract.add_argument(
        "--xbox-texture-map",
        type=Path,
        metavar="FILE",
        help="also write the Xbox texture map there: which Xbox texture replaced which PS2 one (needs textures)",
    )


def _add_audio_commands(groups: Any) -> None:
    """Register `coney-tools audio ...`."""
    disc_help = "a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    group = groups.add_parser(
        "audio", help="the game's sound data: the sound and music lists, banks, BFW.SND, MUSIC.SND"
    )
    commands = group.add_subparsers(dest="command", required=True)
    info = commands.add_parser("info", help="counts of sounds, banks and music tracks, with hashes of the tables")
    info.add_argument("disc", nargs="?", help=disc_help)
    listing = commands.add_parser("list", help="one line per sound, music track or bank sound")
    listing.add_argument("what", choices=["sounds", "music", "banks"], help="which list")
    listing.add_argument("disc", nargs="?", help=disc_help)
    decode = commands.add_parser("decode", help="decode one sound or music track to a WAV file outside the repository")
    decode.add_argument(
        "paths",
        nargs="+",
        metavar="[DISC] NAME OUT",
        help=f"[DISC] ({disc_help}), a sound or track name or 0x hash, and the WAV file",
    )
    decode.add_argument("--bank", help="decode from this bank instead of where the sound list says")


def _add_movies_commands(groups: Any) -> None:
    """Register `coney-tools movies ...`."""
    group = groups.add_parser("movies", help="the game's Bink movies (PSS/*.BIK): header values")
    commands = group.add_subparsers(dest="command", required=True)
    listing = commands.add_parser("list", help="one line per movie: size, Bink revision, size, frames, rate, audio")
    listing.add_argument("disc", nargs="?", help="a folder (mounted disc) or .iso image; default: game_dir")


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
    index = commands.add_parser("index", help="write the resource index (xbox-resources.bin) to OUT_FILE")
    index.add_argument("disc", help=disc_help)
    index.add_argument("out_file", type=Path, help="where to write it (outside the repository)")
    texture_map = commands.add_parser(
        "texture-map", help="match every PS2 texture and write the texture map (xbox-textures.bin) to OUT_FILE"
    )
    texture_map.add_argument("ps2_disc", metavar="PS2_DISC", help="the PS2 disc (a folder or .iso)")
    texture_map.add_argument("disc", help=disc_help)
    texture_map.add_argument("out_file", type=Path, help="where to write it (outside the repository)")
    texture_map.add_argument("--xbox-index", type=Path, metavar="FILE", help="the resource index, as for extract")
    textures = commands.add_parser("textures", help="texture chunk counts by format, mip count and size")
    textures.add_argument("disc", help=disc_help)
    textures.add_argument("--ps2", metavar="PS2_DISC", help=ps2_help)


def _add_progress_commands(groups: Any) -> None:
    """Register `coney-tools progress ...`."""
    tracker = groups.add_parser("progress", help="the progress tracker (docs/progress/, built into the docs site)")
    commands = tracker.add_subparsers(dest="command", required=True)
    show = commands.add_parser("show", help="print how much is reimplemented and researched")
    show.add_argument("--json", action="store_true", help="print everything as JSON")
    commands.add_parser("check", help="check the tracker's inputs (functions.toml, @orig tags, ranges, roadmap)")
    render = commands.add_parser("render", help="write the page and badge JSON the docs build publishes, to a folder")
    render.add_argument("out_dir", type=Path, help="a folder to write progress/index.md and progress/badges/ to")
    sizes = commands.add_parser("sizes", help="check the listed functions' sizes against your own disc")
    sizes.add_argument(
        "disc", nargs="?", help="a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    )
    sizes.add_argument("--fill", action="store_true", help="write the estimated size of entries that have none")
    ghidra = commands.add_parser(
        "ghidra", help="re-export docs/progress/ghidra-functions.tsv from a running ghidra-mcp (read only)"
    )
    ghidra.add_argument("--url", default=None, help="the ghidra-mcp server (default http://127.0.0.1:8090)")
    ghidra.add_argument("--check", action="store_true", help="change nothing; exit 1 when the listing is stale")
    backlog = commands.add_parser("backlog", help="write one Markdown file per subsystem of not-yet-understood code")
    backlog.add_argument("out_dir", type=Path, help="a folder outside the repository")


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
    missions = commands.add_parser(
        "missions", help="set usage.levels from the scripts of the story's later levels on your disc"
    )
    missions.add_argument(
        "disc", nargs="?", help="a folder (mounted disc) or .iso image; default: game_dir in coney.local.toml"
    )
    missions.add_argument("--check", action="store_true", help="change nothing; exit 1 when a list is stale")


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


def _add_missions_commands(groups: Any) -> None:
    """Register `coney-tools missions ...`."""
    group = groups.add_parser("missions", help="the mission status list (research/missions.yaml, docs/missions/)")
    commands = group.add_subparsers(dest="command", required=True)
    render = commands.add_parser("render", help="check the list and write the pages of docs/missions/")
    render.add_argument("--check", action="store_true", help="change nothing; exit 1 when a page is stale")


def _add_pcsx2_flags(command: Any) -> None:
    """The folder flags every PCSX2 command takes; each defaults to coney.local.toml."""
    command.add_argument("--pcsx2-dir", type=Path, help="the portable PCSX2 folder; default: pcsx2_dir")
    command.add_argument("--iso", type=Path, help="the disc image (or a folder with one); default: game_dir")
    command.add_argument("--scratch", type=Path, help="where state copies and the disc link go; default: scratch_dir")


def _add_claim_commands(commands: Any) -> None:
    """Register the claim commands of `coney-tools pcsx2`: claim, release, status, keys, screenshot."""
    claim = commands.add_parser("claim", help="claim a free PCSX2 copy for an agent (every PCSX2 use starts here)")
    claim.add_argument("--agent", required=True, help="your id: letters, digits, '.', '_' and '-'")
    claim.add_argument("--copy", help="a copy by name (pcsx2, pcsx2-b, ...); default: the first free one")
    claim.add_argument("--json", action="store_true", help="print the claim as one JSON line")
    claim.add_argument("--max-age-hours", type=float, help="a claim with no process is stale after this (default 4)")
    release = commands.add_parser("release", help="close the copy's PCSX2 and drop the claim")
    release.add_argument("--agent", required=True, help="your id")
    release.add_argument("--copy", help="one copy; default: all of the agent's")
    release.add_argument("--force", action="store_true", help="release another agent's claim")
    status = commands.add_parser("status", help="who holds each PCSX2 copy and whether its PCSX2 runs")
    status.add_argument("--json", action="store_true", help="print JSON instead of a table")
    status.add_argument("--max-age-hours", type=float, help="a claim with no process is stale after this (default 4)")
    keys = commands.add_parser("keys", help="post keys to a claimed copy's window by handle (never takes focus)")
    keys.add_argument("--agent", required=True, help="your id (must hold the claim)")
    keys.add_argument("--copy", required=True, help="the copy")
    keys.add_argument("keys", nargs="+", metavar="KEY", help="a key (space, return, up, f4, w, ...); W+K presses both")
    keys.add_argument("--hold-ms", type=int, default=300, help="how long each key stays down (default 300)")
    keys.add_argument("--gap-ms", type=int, default=100, help="pause after each key (default 100)")
    shot = commands.add_parser("screenshot", help="write a copy's window as a PNG, read by handle (no focus change)")
    shot.add_argument("--copy", required=True, help="the copy")
    shot.add_argument("--out", type=Path, required=True, help="the PNG (outside the repository)")


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
    launch = commands.add_parser(
        "launch", help="start PCSX2 (never taking the focus) on a state file and wait until its game runs"
    )
    launch.add_argument(
        "state", type=Path, nargs="?", help="a .p2s file (a patched copy); omitted: boot the disc and wait for PINE"
    )
    launch.add_argument("--agent", help="your id; PCSX2 starts under your claim (made if you hold none)")
    _add_pcsx2_flags(launch)
    record = commands.add_parser("record", help="play a scenario on the original and write its per-update trace")
    record.add_argument("scenario", type=Path, help="a scenario TOML (research/traces/scenarios/)")
    record.add_argument("--out", type=Path, required=True, help="the trace CSV (outside the repository)")
    record.add_argument("--state", help="the state to copy instead of the scenario's slot: a .p2s file or slot:N")
    record.add_argument("--attach", action="store_true", help="record a PCSX2 already running a patched state")
    record.add_argument("--keep-open", action="store_true", help="leave PCSX2 running afterwards")
    record.add_argument("--agent", help="your id; runs under your claim (made, and released at the end, if none)")
    record.add_argument(
        "--let",
        action="append",
        default=[],
        metavar="NAME=EXPR",
        help="give or replace a name of the scenario's let (a random stream's offset per match); repeatable",
    )
    clip = commands.add_parser(
        "clip",
        help="record a short clip of the original with PCSX2's own capture of its game output (never the screen)",
    )
    clip.add_argument("state", type=Path, help="the .p2s state to start from (a copy outside sstates/)")
    clip.add_argument("--out", type=Path, required=True, help="the clip: <scratch>/<task>/clips/<name>.mp4")
    clip.add_argument("--seconds", type=int, default=10, help="how long, 1 to the cap of 10 (default 10)")
    clip.add_argument("--agent", help="your id; runs under your claim (made, and released at the end, if none)")
    _add_pcsx2_flags(clip)
    _add_claim_commands(commands)
    _add_pcsx2_flags(record)


def _add_clips_commands(groups: Any) -> None:
    """Register `coney-tools clips ...`."""
    group = groups.add_parser("clips", help="in-game video clips for issues and pull requests: check, publish, clean")
    commands = group.add_subparsers(dest="command", required=True)
    folder = commands.add_parser("folder", help="make and print a task's clips folder (<scratch>/<task>/clips)")
    folder.add_argument("task", help="the task's scratch folder name (your branch or track)")
    check = commands.add_parser("check", help="check clips as publish does (place, length, metadata), upload nothing")
    check.add_argument("clips", nargs="+", type=Path, metavar="CLIP")
    publish = commands.add_parser(
        "publish", help="upload clips with gh --attach to a pull request or issue, then delete them"
    )
    where = publish.add_mutually_exclusive_group(required=True)
    where.add_argument("--pr", type=int, help="the pull request's number")
    where.add_argument("--issue", type=int, help="the issue's number")
    publish.add_argument("--edit", action="store_true", help="put them into the description instead of a comment")
    text = publish.add_mutually_exclusive_group()
    text.add_argument("--body", help="the comment's (or description's) text")
    text.add_argument("--body-file", type=Path, help="a file with the text; ./clips/<name>.mp4 references are replaced")
    publish.add_argument("clips", nargs="+", type=Path, metavar="CLIP")
    clean = commands.add_parser("clean", help="delete clips left in the scratch clips folders past the age limit")
    clean.add_argument("--older-than", type=float, metavar="HOURS", help="default: workspace.clips.keep_hours (24)")
    clean.add_argument("--dry-run", action="store_true", help="list what would go, delete nothing")


def _run_clips(args: argparse.Namespace) -> int:
    """Dispatch a `clips` command."""
    if args.command == "folder":
        return clips.run_folder(args.task)
    if args.command == "check":
        return clips.run_check(args.clips)
    if args.command == "publish":
        target = clips.Target("pr" if args.pr is not None else "issue", args.pr or args.issue, args.edit)
        return clips.run_publish(target, args.body, args.body_file, args.clips)
    return clips.run_clean(args.older_than, args.dry_run)


def _gang_list(text: str) -> list[int]:
    """`--original-gangs 5,3`: gang ids separated by commas."""
    try:
        return [int(part) for part in text.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"{text!r}: expected gang ids separated by commas") from error


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
    fight = commands.add_parser(
        "fight-report", help="behaviour statistics of an all-humans trace; compare the original's runs with Coney's"
    )
    fight.add_argument("traces", nargs="*", type=Path, help="all-humans traces (coney --trace-humans) to report")
    fight.add_argument("--original", nargs="+", type=Path, default=[], metavar="TRACE", help="the original's runs")
    fight.add_argument("--coney", nargs="+", type=Path, default=[], metavar="TRACE", help="Coney's runs")
    fight.add_argument("--hz", type=int, default=30, help="updates a second in the traces (default 30)")
    fight.add_argument("--exclude", nargs="+", type=int, default=[], metavar="HUMAN", help="leave these humans out")
    fight.add_argument("--engage-distance", type=float, default=3.0, help="metres to the target that count as engaged")
    fight.add_argument("--from", dest="start_from", type=int, default=1, help="skip the steps before this one")
    fight.add_argument("--humans", action="store_true", help="also print the metrics of each human")
    fight.add_argument(
        "--margin", type=float, default=0.0, help="widen the original's spread by this share of its width"
    )
    fight.add_argument("--original-gangs", type=_gang_list, help="the original's gang ids in the order g0,g1,...")
    fight.add_argument("--coney-gangs", type=_gang_list, help="Coney's gang ids in the order g0,g1,...")
    mission = commands.add_parser("mission", help="play a mission scenario's course on one game; write its event log")
    mission.add_argument("scenario", type=Path, help="a mission scenario TOML (research/traces/missions/)")
    mission.add_argument("--side", choices=("coney", "original"), required=True, help="the game to play it on")
    mission.add_argument("--out", type=Path, required=True, help="the event log CSV (outside the repository)")
    mission.add_argument("--coney", type=Path, help="Coney's executable; default: build/dev/src/platform/coney")
    mission.add_argument("--disc", help="the disc for Coney; default: game_dir")
    mission.add_argument("--state", help="the original: another state file (or slot:N) to start from")
    mission.add_argument("--agent", help="the original: your id; runs under your PCSX2 claim")
    mission.add_argument("--updates", type=int, help="play at most this many updates (default: the scenario's)")
    _add_pcsx2_flags(mission)
    events = commands.add_parser("events-diff", help="compare two event logs in order; exit 1 when Coney lacks some")
    events.add_argument("original", type=Path, help="the original's event log (trace mission --side original)")
    events.add_argument("coney", type=Path, help="Coney's event log (coney --event-log, or trace mission)")
    events.add_argument("--rules", type=Path, help="the rules file; default: research/traces/events.toml")
    events.add_argument("--kinds", nargs="+", metavar="KIND", help="compare these kinds (default: the rules' list)")
    events.add_argument("--window", type=int, help="the timing window in updates (default: the rules')")
    events.add_argument("--limit", type=int, default=40, help="lines shown per list (default 40)")
    events.add_argument("--labels", type=Path, help="a JSON object of label to text, naming hashed texts")
    events.add_argument(
        "--from", dest="first", default="", metavar="EVENT", help="start both logs at their first 'kind name' EVENT"
    )


def _run_progress(args: argparse.Namespace) -> int:
    """Dispatch a `progress` command."""
    if args.command == "show":
        return progress_cli.run_show(args.json)
    if args.command == "check":
        return progress_cli.run_check()
    if args.command == "render":
        return progress_cli.run_render(args.out_dir)
    if args.command == "ghidra":
        return progress_cli.run_ghidra(args.url, args.check)
    if args.command == "backlog":
        return progress_cli.run_backlog(args.out_dir)
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
    if args.command == "index":
        return xbox_cli.run_index(args.disc, args.out_file)
    if args.command == "texture-map":
        return xbox_cli.run_texture_map(args.ps2_disc, args.disc, args.out_file, args.xbox_index)
    return xbox_cli.run_textures(args.disc, args.ps2)


def _run_audio(args: argparse.Namespace) -> int:
    """Dispatch an `audio` command."""
    if args.command == "info":
        return audio_cli.run_info(args.disc)
    if args.command == "list":
        return audio_cli.run_list(args.disc, args.what)
    if len(args.paths) not in (2, 3):
        raise ConfigError("expected [DISC] NAME OUT")
    disc = args.paths[0] if len(args.paths) == 3 else None
    return audio_cli.run_decode(disc, args.paths[-2], Path(args.paths[-1]), args.bank)


def _run_pcsx2(args: argparse.Namespace) -> int:
    """Dispatch a `pcsx2` command."""
    if args.command == "prepare-state":
        return pcsx2_cli.run_prepare_state(args.source, args.out, args.patch, args.pcsx2_dir)
    if args.command == "repack-state":
        return pcsx2_cli.run_repack_state(args.source, args.out, args.pcsx2_dir)
    if args.command == "launch":
        return pcsx2_cli.run_launch(args.state, args.pcsx2_dir, args.iso, args.scratch, args.agent)
    if args.command == "claim":
        return pcsx2_claims_cli.run_claim(args.agent, args.copy, args.json, args.max_age_hours)
    if args.command == "release":
        return pcsx2_claims_cli.run_release(args.agent, args.copy, args.force)
    if args.command == "status":
        return pcsx2_claims_cli.run_status(args.json, args.max_age_hours)
    if args.command == "keys":
        return pcsx2_claims_cli.run_keys(args.agent, args.copy, args.keys, args.hold_ms, args.gap_ms)
    if args.command == "screenshot":
        return pcsx2_claims_cli.run_screenshot(args.copy, args.out)
    if args.command == "clip":
        return pcsx2_clip.run_clip(
            args.state, args.seconds, args.out, (args.pcsx2_dir, args.iso, args.scratch), args.agent
        )
    flags = (args.pcsx2_dir, args.iso, args.scratch)
    return pcsx2_cli.run_record(
        args.scenario, args.out, args.state, args.attach, args.keep_open, flags, args.agent, args.let
    )


def _run(args: argparse.Namespace) -> int:
    """Dispatch to the chosen command and return its exit status."""
    if args.group == "check":
        return check_cli.run_changed(args.base, args.build_dir, args.only, args.skip, args.jobs, args.clang_tidy)
    if args.group == "wad":
        return _run_wad(args)
    if args.group == "xbox":
        return _run_xbox(args)
    if args.group == "audio":
        return _run_audio(args)
    if args.group == "extract":
        from coney_tools import extract  # loads NumPy, which the other commands do not need

        disc_arg, out_dir = wad_cli.split_disc_and_target(args.paths, "OUT_DIR")
        return extract.run(
            disc_arg, out_dir, args.only, args.verify, args.jobs, args.xbox, args.xbox_index, args.xbox_texture_map
        )
    if args.group == "movies":
        return movies_cli.run_list(args.disc)
    if args.group == "progress":
        return _run_progress(args)
    if args.group == "natives":
        if args.command == "render":
            return natives_cli.run_render(args.check)
        if args.command == "cpp":
            return natives_cli.run_cpp(args.check)
        if args.command == "mission1":
            return natives_cli.run_mission1(args.disc, args.check)
        if args.command == "missions":
            return natives_cli.run_missions(args.disc, args.check)
        return natives_cli.run_coney(args.check) if args.command == "coney" else natives_cli.run_stats()
    if args.group == "refs":
        if args.command == "render":
            return refs_cli.run_render(args.check)
        if args.command == "compress-images":
            return refs_cli.run_compress_images(args.folder)
        return refs_cli.run_extract(args.disc, args.only, args.names)
    if args.group == "missions":
        return missions_render.run_render(args.check)
    if args.group == "pcsx2":
        return _run_pcsx2(args)
    if args.group == "clips":
        return _run_clips(args)
    if args.group == "trace":
        if args.command == "coney":
            return trace_cli.run_coney(args.scenario, args.out, args.coney, args.disc)
        if args.command == "fight-report":
            report_options = (
                (args.hz, frozenset(args.exclude), args.engage_distance, args.start_from),
                args.humans,
                args.margin,
            )
            return trace_cli.run_fight_report(
                args.traces, args.original, args.coney, report_options, (args.original_gangs, args.coney_gangs)
            )
        if args.command == "mission":
            if args.side == "coney":
                return mission_cli.run_mission_coney(args.scenario, args.out, args.coney, args.disc, args.updates)
            flags = (args.pcsx2_dir, args.iso, args.scratch)
            return mission_cli.run_mission_original(
                args.scenario, args.out, args.state, flags, args.agent, args.updates
            )
        if args.command == "events-diff":
            return mission_cli.run_events_diff(
                args.original, args.coney, args.rules, args.kinds, args.window, args.limit, args.labels, args.first
            )
        options = (args.start, args.end, args.shift, args.start_frame, args.context)
        return trace_cli.run_diff(args.original, args.coney, args.scenario, args.columns, args.tolerance, options)
    if args.group == "config":
        return _config_show()
    if args.group == "workspace" and args.command == "clean":
        return workspace_clean.run_clean(
            Path.cwd(),
            args.workspace,
            args.apply,
            args.build_hours,
            args.worktree_hours,
            args.fetch,
            shell_hours=args.shell_hours,
            idle_hours=args.log_idle_hours,
        )
    if args.group == "workspace" and args.command == "disk":
        return workspace_clean.run_disk(Path.cwd(), args.workspace, args.min_gb)
    if args.group == "workspace":
        return workspace.run_init(Path.cwd(), args.workspace, args.repo_url, args.fetch, args.dry_run)
    if args.group == "mcp":
        return mcp_serve.run_serve(args.name)
    if args.command == "audit":
        return run_audit(args)
    if args.command == "pr-health":
        return run_pr_health(args)
    if args.command == "hygiene":
        return run_hygiene(args)
    if args.command == "render":
        return run_render(find_repo_root(Path.cwd()), args.check)
    if args.command == "labels":
        return _repo_labels(args.apply, args.repo)
    if args.command == "check-title":
        return _repo_check_title(args.file)
    if args.command == "check-docs":
        return _repo_check_docs(args.body)
    if args.command == "check-body":
        return _repo_check_body(args.body)
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

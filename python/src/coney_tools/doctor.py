# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools doctor`: check a contributor's machine and disc, and say how to fix what is missing.

One command for a person setting up (docs/guides/building.md#doctor): the build tools the engine needs (Git, the
compiler, CMake and Ninja, uv), the ones that make it faster or check it (ccache, clang-tidy 19), the configuration
file, the disc against the NTSC-U release the research refers to, the research tools (a JDK 21, Ghidra and its
project, Node.js), the ghidra-mcp port and the free disk space. Every check only reads: it runs version options,
hashes the disc's small files and never installs or changes anything. It prints counts, sizes and hashes only
(LEGAL.md). `which`, `run` and the disc are passed in, so the tests stand in for the machine.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import socket
import urllib.error
import urllib.request
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools import local_gate, pcsx2_claims, workspace, workspace_fetch
from coney_tools.build_cli import scratch_root
from coney_tools.config import (
    ConfigError,
    find_repo_root,
    load_config,
    local_config_file,
    main_checkout,
    workspace_folder,
    workspace_root,
)
from coney_tools.disc import NTSC_U_SHA1, Disc
from coney_tools.ghidra_headless import LANGUAGES, PROGRAM, PROJECT_NAME, GhidraProject, has_program
from coney_tools.workspace_checks import GCC_MIN, Check, Run, Which, check_prerequisites, parse_version, run_command

#: The free space a `dev` build with its tests needs: 6.5 GB measured on 2026-10-21 (Windows, MSVC, a full build/dev).
BUILD_GB = 6.5
#: Below this much free space a build is likely to fail part way: the build plus a margin for the logs and caches.
MIN_FREE_GB = 10.0
#: The ghidra-mcp server's port (docs/guides/ghidra.md).
GHIDRA_MCP_PORT = 8090


@dataclass(frozen=True)
class Reference:
    """A file of the NTSC-U disc as the project knows it: its size, and its SHA-1 when hashing it is cheap."""

    size: int
    #: None for the 1.4 GB archive, which is compared by size only (hashing it takes minutes).
    sha1: str | None


#: The NTSC-U release's files that Coney reads, measured from the maintainers' disc: sizes and hashes only.
NTSC_U: dict[str, Reference] = {
    PROGRAM: Reference(4_903_976, NTSC_U_SHA1),
    "WARRIORS.DIR": Reference(128_428, "0170cf199ed10947f8d20ecd40a5f4b0bd7877f7"),
    "WARRIORS.WAD": Reference(1_495_371_776, None),
}


def _sha1(disc: Disc, name: str) -> str:
    """The SHA-1 of the disc file `name`, read in pieces."""
    digest = hashlib.sha1()
    with disc.open(name) as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def disc_checks(disc: Disc | None, reference: Mapping[str, Reference] = NTSC_U) -> list[Check]:
    """One check per file of `reference` on `disc`: present, the right size and, where known, the right SHA-1.

    A missing file is a problem (Coney cannot run from that disc); a file of another size or hash is a note, since
    another release may run but is untested and its addresses differ from the research's.
    """
    if disc is None:
        hint = "pass --disc (a mounted disc, a folder or an .iso of your own disc) or set game_dir in coney.local.toml"
        return [Check("Disc", False, "none given (needed only to run the game and the disc tests)", hint, False)]
    checks = [Check("Disc", True, str(disc.path), "", False)]
    for name, known in reference.items():
        if not disc.has(name):
            checks.append(Check(name, False, "missing", "is this The Warriors' PS2 disc?", True))
            continue
        size = disc.size(name)
        sha1 = _sha1(disc, name) if known.sha1 is not None else None
        detail = f"{size:,} bytes" + (f", SHA-1 {sha1}" if sha1 else "")
        same = size == known.size and (known.sha1 is None or sha1 == known.sha1)
        if not same:
            detail += " (not the NTSC-U release: other releases are untested and their addresses differ)"
        checks.append(Check(name, same, detail, "Coney is checked against the NTSC-U release (SLUS-21215)", False))
    return checks


def uv_check(which: Which) -> Check:
    """uv, which runs every `coney-tools` command (and so this one; a missing one means it ran another way)."""
    found = which("uv")
    return Check("uv", found is not None, found or "not found", "install uv: docs.astral.sh/uv", True)


def clang_tidy_check() -> Check:
    """clang-tidy 19, which `coney-tools check changed` runs on C++ changes."""
    hint = (
        "Windows: the Visual Studio component C++ Clang tools for Windows; Linux: clang-tidy-19; "
        "macOS: brew install llvm@19"
    )
    try:
        path, major = local_gate.find_clang_tidy()
    except ConfigError:
        return Check("clang-tidy 19", False, "not found (needed only to check C++ changes)", hint, False)
    ok = major == local_gate.TIDY_MAJOR
    version = f"version {major}" if major is not None else "version unknown"
    return Check("clang-tidy 19", ok, f"{path} ({version})", hint, False)


def default_compiler_check(platform: str, which: Which, run: Run, env: Mapping[str, str]) -> Check | None:
    """On Linux, a note when the default `g++` is older than 13 while a newer one is installed and CXX is not set.

    CMake configures with `cc` and `c++` unless CC and CXX say otherwise, so on Ubuntu 22.04 installing g++-13 is not
    enough. None when there is nothing to say.
    """
    if platform != "linux" or env.get("CXX"):
        return None
    default = which("g++")
    output = run([default, "--version"]) if default else None
    version = parse_version(output) if output else None
    if version is None or version >= GCC_MIN:
        return None
    for major in (15, 14, 13):
        if which(f"g++-{major}"):
            hint = f"export CC=gcc-{major} CXX=g++-{major} before the first configure (or delete build/ and configure)"
            return Check("Default compiler", False, f"g++ is {'.'.join(map(str, version))}", hint, False)
    return None


def config_check(root: Path) -> Check:
    """Whether a coney.local.toml applies; the tools work without one, with the workspace defaults
    (`config.workspace_root`: beside the main checkout in a workspace, `<clone>-workspace` beside a plain clone)."""
    path = local_config_file(root)
    hint = "optional: only to change a default (docs/guides/workspace.md#local-config)"
    detail = str(path) if path else f"none (defaults are used, in {workspace_root(root)})"
    return Check("coney.local.toml", path is not None, detail, hint, False)


def ghidra_checks(install: Path, project: GhidraProject) -> list[Check]:
    """Ghidra, its compiled EE language and a project holding the executable: optional, all made by ghidra setup."""
    hint = "uv run --project python coney-tools ghidra setup (docs/guides/ghidra.md#own-project)"
    there = (install / "support").is_dir()
    checks = [
        Check(
            f"Ghidra {workspace_fetch.GHIDRA_VERSION}",
            there,
            str(install) if there else f"not in {install}",
            hint,
            False,
        )
    ]
    sla = install / LANGUAGES / "r5900.sla"
    checks.append(
        Check("EE language (r5900.sla)", sla.is_file(), "compiled" if sla.is_file() else "not compiled", hint, False)
    )
    imported = has_program(project, PROGRAM)
    detail = f"{PROGRAM} in {project.gpr}" if imported else f"no {PROGRAM} in {project.gpr}"
    checks.append(Check("Ghidra project", imported, detail, hint, False))
    return checks


def pcsx2_check(platform: str, build: Path | None) -> Check:
    """The PCSX2-MCP fork's build that the PCSX2 copies run (`pcsx2_mcp_dir`): its PCSX2 and its MCP server."""
    if platform == "windows":
        hint = (
            "download the fork's release: uv run --project python coney-tools workspace init "
            "--fetch pcsx2-mcp-release, "
            "then coney-tools pcsx2 setup-copies (docs/guides/workspace.md#pcsx2-copies)"
        )
    else:
        hint = "the PCSX2-MCP fork's build is Windows only today (docs/guides/workspace.md#pcsx2-copies)"
    if build is None:
        return Check("PCSX2-MCP build", False, "pcsx2_mcp_dir is not set", hint, False)
    program = (build / "pcsx2-qt.exe").is_file() or any(build.glob("*.AppImage"))
    ok = program and (build / "pcsx2-mcp-server").is_dir()
    detail = str(build) if ok else f"{build} holds no PCSX2 build with pcsx2-mcp-server/"
    return Check("PCSX2-MCP build", ok, detail, hint, False)


def bios_check(copy: Path) -> Check:
    """Whether the first PCSX2 copy has a BIOS; it is the user's own, which Coney never downloads (LEGAL.md)."""
    folder = copy / "bios"
    count = sum(1 for entry in folder.iterdir() if entry.is_file()) if folder.is_dir() else 0
    hint = f"dump the BIOS from your own console into {folder} (Coney never downloads one)"
    return Check("PCSX2 BIOS", count > 0, f"{count} file(s) in {folder}", hint, False)


def is_listening(port: int) -> bool:
    """True when something accepts connections on 127.0.0.1:`port`."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            return True
    except OSError:
        return False


def port_check(port: int, listening: Callable[[int], bool], answers: Callable[[], bool]) -> Check:
    """The ghidra-mcp port: free, or held by ghidra-mcp, is fine; held by another program is a note."""
    name = f"Port {port} (ghidra-mcp)"
    hint = "start the server on another port: tools/start-ghidra-mcp.ps1 -Port <n> (and set GHIDRA_MCP_URL)"
    if not listening(port):
        return Check(name, True, "free", hint, False)
    if answers():
        return Check(name, True, "ghidra-mcp answers", hint, False)
    return Check(name, False, "taken by another program", hint, False)


def disk_check(free: int) -> Check:
    """The free space where the build folder goes."""
    gb = free / (1 << 30)
    hint = f"free some space: a dev build with its tests takes about {BUILD_GB} GB"
    return Check("Free space", gb >= MIN_FREE_GB, f"{gb:.1f} GB", hint, True)


def report(sections: Sequence[tuple[str, Sequence[Check]]]) -> int:
    """Print every section's checks and a last line counting problems and notes; 1 when there is a problem."""
    problems = notes = 0
    for title, checks in sections:
        print(title)
        for check in checks:
            status = "ok" if check.ok else "problem" if check.required else "note"
            problems += status == "problem"
            notes += status == "note"
            line = f"  {status:<9}{check.name}: {check.detail}"
            print(line if check.ok or not check.hint else f"{line} -> {check.hint}")
    if problems == 0:
        print(f"No problems: the engine can be built here. {notes} note{'s' * (notes != 1)}.")
        return 0
    print(f"{problems} problem{'s' * (problems != 1)}, {notes} note{'s' * (notes != 1)}.")
    return 1


def free_space(folder: Path) -> int:
    """The free bytes on the drive holding `folder` (or its nearest existing parent)."""
    while not folder.exists() and folder.parent != folder:
        folder = folder.parent
    return shutil.disk_usage(folder).free


def _open_disc(given: str | None, game_dir: Path | None) -> tuple[Disc | None, Check | None]:
    """The disc named on the command line, else game_dir; a check instead when it cannot be opened."""
    path = Path(given) if given is not None else game_dir
    if path is None:
        return None, None
    try:
        return Disc(path), None
    except ConfigError as error:
        return None, Check("Disc", False, str(error), "pass --disc with a mounted disc, a folder or an .iso", True)


def _ghidra_server_answers() -> bool:
    """True when ghidra-mcp answers on its port."""
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{GHIDRA_MCP_PORT}/check_connection", timeout=2) as response:
            return bool(response.status == 200)
    except (urllib.error.URLError, OSError):
        return False


def run(cwd: Path, disc_arg: str | None) -> int:
    """Run every check for the checkout around `cwd` and print them; 1 when something the engine needs is missing."""
    root = find_repo_root(cwd)
    paths = load_config(root).paths if local_config_file(root) else {}
    name = workspace_fetch.platform_name()
    env = os.environ
    found = check_prerequisites(name, shutil.which, run_command, env, paths.get("jdk_home"))
    build = [check for check in found if check.required]
    build.append(uv_check(shutil.which))
    note = default_compiler_check(name, shutil.which, run_command, env)
    build += [note] if note is not None else []
    build += [workspace.ccache_check(root), clang_tidy_check()]

    disc, problem = _open_disc(disc_arg, paths.get("game_dir"))
    discs = [problem] if problem is not None else disc_checks(disc)

    ghidra = f"tools/ghidra_{workspace_fetch.GHIDRA_VERSION}_PUBLIC"
    install = paths.get("ghidra_install") or workspace_folder(root, ghidra)
    projects = paths.get("ghidra_projects") or workspace_folder(root, "ghidra")
    research = [check for check in found if not check.required]
    research += ghidra_checks(install, GhidraProject(projects, PROJECT_NAME))
    research.append(port_check(GHIDRA_MCP_PORT, is_listening, _ghidra_server_answers))
    research.append(pcsx2_check(name, paths.get("pcsx2_mcp_dir")))
    # The first copy, where `pcsx2 setup-copies` makes it (pcsx2_dir names it outright when set).
    copies = paths.get("pcsx2_root") or pcsx2_claims.default_pcsx2_root(main_checkout(root))
    research.append(bios_check(paths.get("pcsx2_dir") or copies / "pcsx2"))

    print(f"coney-tools doctor: {root} ({name})")
    sections = [
        ("Build tools", build),
        ("Configuration", [config_check(root), Check("Scratch folder", True, str(scratch_root(root)), "", False)]),
        ("Disc", discs),
        ("Research tools (optional: docs/guides/ghidra.md, docs/guides/workspace.md#pcsx2-copies)", research),
        ("Disk", [disk_check(free_space(root))]),
    ]
    return report(sections)

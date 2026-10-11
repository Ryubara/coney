# SPDX-License-Identifier: GPL-3.0-or-later
"""The pieces of `coney-tools check changed`: which files a change touched, which of them CI's clang-tidy job would
check, and how to run clang-tidy the way CI's Clang build would report it.

Everything here is a function of the repository and its arguments; `check_cli` runs the steps and prints them. The
file selection mirrors `.github/scripts/tidy-files.sh` (a test checks that the two agree), and the warning flags
mirror `cmake/coney_target.cmake`, so a local pass and CI's pass disagree only where CI uses another compiler.
"""

from __future__ import annotations

import json
import os
import re
import shlex
import shutil
import subprocess
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

from coney_tools.config import ConfigError

#: The clang-tidy major version CI runs (`clang-tidy-19` in .github/workflows/build.yml).
TIDY_MAJOR = 19
#: Paths whose change can alter any file's tidy result; CI then checks everything (tidy-files.sh prints ALL). This is
#: the script's own pattern, character for character; a test compares them.
TIDY_ALL_PATTERN = (
    r"^(\.clang-tidy|CMakeLists\.txt|CMakePresets\.json|cmake/|\.github/workflows/build\.yml|"
    r"\.github/scripts/tidy-files\.sh)"
)
#: The warnings the GCC and Clang builds enable (cmake/coney_target.cmake); CI adds -Werror to them. MSVC's /W4 is
#: not the same set: GCC and Clang flag, for one, a designated initializer that leaves a field out.
WARNING_FLAGS: tuple[str, ...] = ("-Wall", "-Wextra", "-Wpedantic")
#: Pathspecs that find the C++ files of Coney itself, as tidy-files.sh passes them to `git grep`.
SOURCE_PATHSPECS: tuple[str, ...] = ("src/*.cpp", "src/*.h", "tests/*.cpp", "tests/*.h")
#: Changes under these paths need the documentation build.
DOCS_PATHS: tuple[str, ...] = ("docs/", "research/", "mkdocs.yml", "mkdocs_hooks.py", "requirements-docs.txt")
#: Lines of tool output kept per failing step; the rest is a rerun away.
MAX_OUTPUT_LINES = 40
#: Lines clang-tidy prints about its own work, not about the code.
_TIDY_NOISE = re.compile(
    r"^(\d+ warnings? generated\.|Suppressed \d+ warnings.*|Use -header-filter=.*|Use -system-headers.*|"
    r"Enabled checks:.*)$"
)


@dataclass(frozen=True)
class StepResult:
    """The outcome of one check: its name, whether it passed (or was skipped), and a line for the summary."""

    name: str
    ok: bool
    detail: str
    seconds: float = 0.0
    skipped: bool = False
    output: tuple[str, ...] = ()


def run_command(command: Sequence[str], cwd: Path) -> tuple[int, str]:
    """Run a command, returning its exit code and its stdout and stderr together; a missing program is code 127."""
    try:
        done = subprocess.run(
            list(command), cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False
        )
    except OSError as error:
        return 127, f"{command[0]}: {error.strerror or error}"
    return done.returncode, done.stdout + done.stderr


def _git(root: Path, *args: str) -> str:
    """Run `git -C root args` and return its stdout; a failure is a ConfigError carrying git's message."""
    code, output = run_command(["git", "-C", str(root), *args], root)
    if code != 0:
        raise ConfigError(f"git {' '.join(args)}: {output.strip() or 'failed'}")
    return output


def merge_base(root: Path, base: str) -> str:
    """The commit where HEAD left `base`, the same point `git diff base...HEAD` measures from."""
    return _git(root, "merge-base", base, "HEAD").strip()


def changed_files(root: Path, base: str) -> list[str]:
    """Every file added, copied, modified or renamed since HEAD left `base`, committed or not, untracked included.

    Paths are relative to `root` with forward slashes, sorted; deleted files are left out, as in tidy-files.sh.
    """
    since = merge_base(root, base)
    tracked = _git(root, "diff", "-z", "--name-only", "--diff-filter=ACMR", since).split("\0")
    untracked = _git(root, "ls-files", "-z", "--others", "--exclude-standard").split("\0")
    names = {name for name in (*tracked, *untracked) if name and (root / name).is_file()}
    return sorted(names)


def needs_full_tidy_pass(changed: Iterable[str]) -> bool:
    """True when CI's clang-tidy would check every file for this change (build setup or tidy setup changed)."""
    pattern = re.compile(TIDY_ALL_PATTERN)
    return any(pattern.match(name) for name in changed)


def _includers(root: Path, header: str) -> list[str]:
    """The C++ files of the checkout that write `#include "<header without its src/ or tests/>"`."""
    include_path = header.split("/", 1)[1]
    code, output = run_command(
        [
            "git",
            "-C",
            str(root),
            "grep",
            "-l",
            "-F",
            "--untracked",
            "-e",
            f'#include "{include_path}"',
            "--",
            *SOURCE_PATHSPECS,
        ],
        root,
    )
    # `git grep` exits 1 when nothing matches, which is an answer, not a failure.
    if code not in (0, 1):
        raise ConfigError(f"git grep for includers of {header}: {output.strip()}")
    return [line for line in output.splitlines() if line]


def tidy_sources(root: Path, changed: Iterable[str]) -> list[str]:
    """The `.cpp` files clang-tidy must check for a change, by the rules of .github/scripts/tidy-files.sh.

    A changed `.cpp` under src/ or tests/ is checked itself; a changed header there is checked through every `.cpp`
    that includes it, directly or through other headers. The caller decides what a full-pass trigger means
    (`needs_full_tidy_pass`): this always returns the files the change names.
    """
    sources: set[str] = set()
    queue: list[str] = []
    for name in changed:
        if name.startswith(("src/", "tests/")):
            if name.endswith(".cpp"):
                sources.add(name)
            elif name.endswith(".h"):
                queue.append(name)
    seen: set[str] = set()
    while queue:
        header = queue.pop(0)
        if header in seen:
            continue
        seen.add(header)
        for includer in _includers(root, header):
            if includer.endswith(".cpp"):
                sources.add(includer)
            elif includer.endswith(".h"):
                queue.append(includer)
    return sorted(name for name in sources if (root / name).is_file())


# --- clang-tidy ----------------------------------------------------------------------------------------------------


def _tidy_major(tool: Path) -> int | None:
    """The major version a clang-tidy binary reports, or None when it will not say."""
    code, output = run_command([str(tool), "--version"], tool.parent)
    found = re.search(r"LLVM version (\d+)", output)
    return int(found.group(1)) if code == 0 and found else None


def _visual_studio_tidy_candidates() -> list[Path]:
    """The clang-tidy.exe of every Visual Studio install that has the LLVM component, newest first (Windows only)."""
    program_files = os.environ.get("PROGRAMFILES(X86)")
    if not program_files:
        return []
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return []
    code, output = run_command([str(vswhere), "-products", "*", "-property", "installationPath"], vswhere.parent)
    if code != 0:
        return []
    found = [
        Path(line.strip()) / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-tidy.exe" for line in output.splitlines()
    ]
    return [path for path in reversed(found) if path.is_file()]


def find_clang_tidy(explicit: str | None = None) -> tuple[Path, int | None]:
    """Find a clang-tidy and its major version: `explicit`, else `clang-tidy-19`, Visual Studio's, then `clang-tidy`.

    CI runs version 19, so a binary of that version wins over a first match of another one. Raises ConfigError when
    there is none, with the way to get one.
    """
    if explicit is not None:
        path = Path(explicit)
        if not path.is_file():
            raise ConfigError(f"--clang-tidy {explicit}: no such file")
        return path, _tidy_major(path)
    candidates: list[Path] = []
    for name in (f"clang-tidy-{TIDY_MAJOR}", "clang-tidy"):
        located = shutil.which(name)
        if located is not None:
            candidates.append(Path(located))
    candidates[1:1] = _visual_studio_tidy_candidates()
    seen: set[Path] = set()
    first: tuple[Path, int | None] | None = None
    for path in candidates:
        if path in seen:
            continue
        seen.add(path)
        major = _tidy_major(path)
        if major == TIDY_MAJOR:
            return path, major
        if first is None:
            first = (path, major)
    if first is None:
        raise ConfigError(
            f"no clang-tidy found: put clang-tidy-{TIDY_MAJOR} on PATH, or on Windows install the Visual Studio "
            "'C++ Clang tools' component (docs/guides/building.md#clang-tidy)"
        )
    return first


def load_tidy_checks(root: Path) -> str:
    """The `Checks` list of the repository's .clang-tidy as one comma-separated string without whitespace."""
    path = root / ".clang-tidy"
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ConfigError(f".clang-tidy: cannot be read ({error})") from error
    checks = data.get("Checks") if isinstance(data, dict) else None
    if not isinstance(checks, str) or not checks.strip():
        raise ConfigError(".clang-tidy: no 'Checks' list")
    return re.sub(r"\s+", "", checks)


def _entry_command(entry: dict[str, Any]) -> list[str]:
    """The compiler command line of a compile database entry, split into arguments."""
    if isinstance(entry.get("arguments"), list):
        return [str(part) for part in entry["arguments"]]
    return shlex.split(str(entry.get("command", "")), posix=os.name != "nt")


def uses_clang_cl_driver(entry: dict[str, Any]) -> bool:
    """True when the entry compiles with cl.exe or clang-cl, so clang-tidy reads its flags in clang-cl's syntax."""
    command = _entry_command(entry)
    if not command:
        return False
    program = re.split(r"[\\/]", command[0].strip('"'))[-1].lower()
    return program in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")


def warning_args(clang_cl: bool) -> list[str]:
    """clang-tidy `--extra-arg`s that turn on the warnings the GCC and Clang builds enable.

    With a cl.exe compile database clang-tidy runs in clang-cl mode, where a bare `-Wall` means `/Wall`
    (-Weverything) and floods the output; `/clang:` hands the flag to Clang unchanged.
    """
    prefix = "/clang:" if clang_cl else ""
    return [f"--extra-arg={prefix}{flag}" for flag in WARNING_FLAGS]


def comparable(path: str | Path) -> str:
    """A path in the form two spellings of the same file share: resolved, with the platform's case rules."""
    return os.path.normcase(os.path.realpath(path))


def load_compile_db(build_dir: Path) -> dict[str, dict[str, Any]]:
    """The build directory's compile_commands.json keyed by each source file's comparable path."""
    path = build_dir / "compile_commands.json"
    if not path.is_file():
        raise ConfigError(
            f"{path}: no compile database; configure once with `cmake --preset dev` (configuring is enough, no "
            "build) or pass --build-dir"
        )
    try:
        entries = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    database: dict[str, dict[str, Any]] = {}
    for entry in entries:
        source = Path(str(entry["file"]))
        if not source.is_absolute():
            source = Path(str(entry["directory"])) / source
        database[comparable(source)] = entry
    return database


def missing_from_database(root: Path, files: Iterable[str], database: dict[str, dict[str, Any]]) -> list[str]:
    """The files the compile database has no entry for (a new file before a re-configure, or another checkout's)."""
    return [name for name in files if comparable(root / name) not in database]


def without_missing_response_files(database: dict[str, dict[str, Any]], directory: Path) -> Path:
    """Write a copy of the database into `directory` with each unreadable `@file` argument removed; return the folder.

    CMake lists the module map of every source as `@...modmap`, a file only a build writes. clang-tidy cannot read
    one that is not there, and it carries nothing a check needs, so a configure without a build still works.
    """
    cleaned: list[dict[str, Any]] = []
    for entry in database.values():
        base = Path(str(entry["directory"]))

        def unreadable(token: str, base: Path = base) -> bool:
            """True for an `@file` argument whose file is not there."""
            return token.startswith("@") and not (base / token[1:].strip('"')).is_file()

        changed = dict(entry)
        if isinstance(entry.get("arguments"), list):
            changed["arguments"] = [part for part in entry["arguments"] if not unreadable(str(part))]
        else:
            # The command line stays a string, quoting and all: the tool splits it by the platform's own rules,
            # which differ from shlex's (a Windows `-DNAME=\"value\"`).
            changed["command"] = re.sub(
                r"(?<=\s)@\S+",
                lambda found: "" if unreadable(found.group(0)) else found.group(0),
                str(entry.get("command", "")),
            )
        cleaned.append(changed)
    (directory / "compile_commands.json").write_text(json.dumps(cleaned), encoding="utf-8")
    return directory


def tidy_output(raw: str) -> list[str]:
    """clang-tidy's output without the lines about its own work (counts of suppressed warnings, hints)."""
    return [line for line in raw.splitlines() if line.strip() and not _TIDY_NOISE.match(line.strip())]


def clean_tidy_command(tool: Path, database_dir: Path, checks: str, extra: Sequence[str], source: str) -> list[str]:
    """The clang-tidy command for one source: the repository's checks plus the compiler's own warnings.

    `clang-diagnostic-*` is what lets compiler warnings through clang-tidy's filter (`-*` in .clang-tidy drops
    them); the config's `WarningsAsErrors: '*'` then makes them errors, like CI's -Werror.
    """
    return [str(tool), "-p", str(database_dir), "--quiet", f"--checks={checks},clang-diagnostic-*", *extra, source]

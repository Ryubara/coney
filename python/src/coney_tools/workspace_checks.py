# SPDX-License-Identifier: GPL-3.0-or-later
"""The prerequisite checks of `coney-tools workspace init`: what a contributor needs installed, and what is missing.

The versions are the ones docs/guides/building.md requires (compilers, CMake) and the research tools need (a JDK 21
for Ghidra, Node.js 18 for the PCSX2 server). Every check only reads: it runs a program's version option and never
installs anything. `which` and `run` are passed in so the tests can stand in for the machine's programs.
"""

from __future__ import annotations

import re
import subprocess
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

#: Finds a program on PATH, like shutil.which.
Which = Callable[[str], str | None]
#: Runs a command and returns its stdout and stderr together, or None when it cannot run.
Run = Callable[[Sequence[str]], str | None]

_VERSION = re.compile(r"\d+(?:\.\d+)+")
_JAVA_VERSION = re.compile(r'version "(\d+)(?:\.(\d+))?')
_JAVA_RUNTIME = re.compile(r"^(?:openjdk|java) (\d+)", re.MULTILINE)

#: Required compiler versions (docs/guides/building.md#what-you-need).
GCC_MIN = (13,)
CLANG_MIN = (19,)
APPLE_CLANG_MIN = (16,)
VISUAL_STUDIO_MIN = (17, 8)
CMAKE_MIN = (3, 28)
NODE_MIN = (18,)
JDK_MAJOR = 21


@dataclass(frozen=True)
class Check:
    """One prerequisite: whether it is there, what was found and what to do when it is not."""

    name: str
    ok: bool
    #: What was found (a version, a path) or why it does not count.
    detail: str
    #: What to do when it is missing.
    hint: str
    #: False for what only the research tools need: the engine builds without it.
    required: bool
    #: The folder found, for checks that find one (the JDK's home).
    path: Path | None = None


def run_command(command: Sequence[str]) -> str | None:
    """Run `command` and return its stdout and stderr together; None when it is not there or does not finish."""
    try:
        done = subprocess.run(list(command), capture_output=True, text=True, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout + done.stderr


def parse_version(text: str) -> tuple[int, ...] | None:
    """The first dotted number in `text` (`cmake version 3.28.3` -> (3, 28, 3)); None when there is none."""
    match = _VERSION.search(text)
    return tuple(int(part) for part in match.group(0).split(".")) if match else None


def java_major(text: str) -> int | None:
    """The major version in `java -version` output: `"21.0.4"` -> 21, and the old `"1.8.0"` numbering -> 8."""
    match = _JAVA_VERSION.search(text)
    if match:
        first = int(match.group(1))
        return int(match.group(2) or 0) if first == 1 else first
    match = _JAVA_RUNTIME.search(text)
    return int(match.group(1)) if match else None


def _shown(version: tuple[int, ...]) -> str:
    """A version tuple as text."""
    return ".".join(str(part) for part in version)


def _versioned(which: Which, run: Run, names: Sequence[str], minimum: tuple[int, ...]) -> tuple[str, str] | None:
    """The first of `names` on PATH whose `--version` is at least `minimum`, as (name, version)."""
    for name in names:
        program = which(name)
        output = run([program, "--version"]) if program else None
        version = parse_version(output) if output else None
        if version is not None and version >= minimum:
            return name, _shown(version)
    return None


def _visual_studio(run: Run, env: Mapping[str, str]) -> str | None:
    """The installed Visual Studio version with the C++ tools, found by vswhere; None when there is none."""
    root = env.get("ProgramFiles(x86)") or "C:/Program Files (x86)"
    vswhere = Path(root) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    output = run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationVersion",
        ]
    )
    version = parse_version(output) if output else None
    return _shown(version) if version is not None and version >= VISUAL_STUDIO_MIN else None


def _compiler(platform: str, which: Which, run: Run, env: Mapping[str, str]) -> Check:
    """The C++ compiler building.md asks for on `platform`."""
    name = "C++ compiler"
    if platform == "windows":
        version = _visual_studio(run, env)
        hint = "install Visual Studio 2022 17.8+ or its Build Tools with Desktop development with C++"
        return Check(name, version is not None, f"Visual Studio {version}" if version else "not found", hint, True)
    if platform == "macos":
        found = _versioned(which, run, ["clang++"], APPLE_CLANG_MIN)
        hint = "install Xcode 16 or newer (or xcode-select --install)"
    else:
        found = _versioned(which, run, ["g++-13", "g++-14", "g++-15", "g++"], GCC_MIN) or _versioned(
            which, run, ["clang++-19", "clang++-20", "clang++-21", "clang++"], CLANG_MIN
        )
        hint = "install g++-13 or clang-19 (docs/guides/building.md#linux)"
    detail = (
        f"{found[0]} {found[1]}" if found else "no GCC 13+ or Clang 19+ found" if platform == "linux" else "not found"
    )
    return Check(name, found is not None, detail, hint, True)


def _cmake_and_ninja(platform: str, which: Which, run: Run, env: Mapping[str, str]) -> list[Check]:
    """CMake 3.28+ and Ninja. On Windows, Visual Studio brings both, on PATH only in its developer shell."""
    hint_cmake = "install CMake 3.28 or newer (docs/guides/building.md#what-you-need)"
    hint_ninja = "install Ninja (docs/guides/building.md#what-you-need)"
    cmake = which("cmake")
    output = run([cmake, "--version"]) if cmake else None
    version = parse_version(output) if output else None
    ninja = which("ninja")
    if platform == "windows" and (version is None or ninja is None) and _visual_studio(run, env):
        bundled = "bundled with Visual Studio (on PATH after vcvars64.bat)"
        return [Check("CMake 3.28+", True, bundled, hint_cmake, True), Check("Ninja", True, bundled, hint_ninja, True)]
    if version is None:
        cmake_check = Check("CMake 3.28+", False, "not found", hint_cmake, True)
    else:
        cmake_check = Check("CMake 3.28+", version >= CMAKE_MIN, _shown(version), hint_cmake, True)
    return [cmake_check, Check("Ninja", ninja is not None, ninja or "not found", hint_ninja, True)]


def find_jdk(platform: str, which: Which, run: Run, env: Mapping[str, str], configured: Path | None) -> Path | None:
    """The home of a JDK 21 or newer: `jdk_home` from coney.local.toml, then JAVA_HOME, then `java` on PATH."""
    executable = "java.exe" if platform == "windows" else "java"
    homes: list[Path] = []
    if configured is not None:
        homes.append(configured)
    if env.get("JAVA_HOME"):
        homes.append(Path(env["JAVA_HOME"]))
    on_path = which("java")
    if on_path:
        # <home>/bin/java; a symlink such as /usr/bin/java resolves to the real install.
        homes.append(Path(on_path).resolve().parent.parent)
    for home in homes:
        output = run([str(home / "bin" / executable), "-version"])
        major = java_major(output) if output else None
        if major is not None and major >= JDK_MAJOR:
            return home
    return None


def check_prerequisites(
    platform: str, which: Which, run: Run, env: Mapping[str, str], jdk_home: Path | None
) -> list[Check]:
    """Every prerequisite for `platform` ("windows", "linux" or "macos"), in the order they are printed.

    `jdk_home` is the configured JDK, if coney.local.toml names one.
    """
    git = which("git")
    checks = [Check("Git", git is not None, git or "not found", "install Git", True)]
    checks.append(_compiler(platform, which, run, env))
    checks.extend(_cmake_and_ninja(platform, which, run, env))

    jdk = find_jdk(platform, which, run, env, jdk_home)
    checks.append(
        Check(
            "JDK 21",
            jdk is not None,
            str(jdk) if jdk else "not found",
            "install a JDK 21 (for Ghidra) and set jdk_home in coney.local.toml",
            False,
            jdk,
        )
    )
    node = _versioned(which, run, ["node"], NODE_MIN)
    checks.append(
        Check(
            "Node.js 18+",
            node is not None,
            node[1] if node else "not found",
            "install Node.js 18 or newer (for the PCSX2 agent server)",
            False,
        )
    )
    return checks

# SPDX-License-Identifier: GPL-3.0-or-later
"""Running Ghidra without its window, for `coney-tools ghidra setup` and `ghidra import-names`.

Ghidra's own `analyzeHeadless` imports, analyses and runs scripts on a project with no window and no server, which
is all a contributor's first project needs (docs/guides/ghidra.md#own-project). This module finds Ghidra's launchers,
gives them the JDK 21 that coney.local.toml names, runs them with their output in a log file and reads a project's
file index, so a step that is already done is skipped without starting Java.
"""

from __future__ import annotations

import os
import subprocess
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.process_lifetime import CREATE_NO_WINDOW

#: The language every PS2 program is imported with (the Emotion Engine extension's R5900).
LANGUAGE = "r5900:LE:32:default"
#: The NTSC-U executable every address in the project refers to.
PROGRAM = "SLUS_212.15"
#: The project name the ghidra-mcp server opens (docs/guides/ghidra.md#pieces).
PROJECT_NAME = "warriors"
#: The extension's folder below the Ghidra install, and its language folder there.
EXTENSION = Path("Ghidra") / "Extensions" / "ghidra-emotionengine-reloaded"
LANGUAGES = EXTENSION / "data" / "languages"

#: Runs `command` with `env` in `cwd`, its output going to the file `log`; returns the exit code. Passed in, so the
#: tests stand in for Ghidra.
Runner = Callable[[Sequence[str], Mapping[str, str], Path, Path], int]


@dataclass(frozen=True)
class GhidraProject:
    """A Ghidra project: `<folder>/<name>.gpr` with its `<name>.rep` folder beside it."""

    folder: Path
    name: str

    @property
    def gpr(self) -> Path:
        """The project file."""
        return self.folder / f"{self.name}.gpr"

    @property
    def lock(self) -> Path:
        """The lock file Ghidra keeps while the project is open (a forced stop leaves it behind)."""
        return self.folder / f"{self.name}.lock"

    @property
    def exists(self) -> bool:
        """True when the project file is there."""
        return self.gpr.is_file()


def programs(project: GhidraProject) -> set[str]:
    """Every file in `project`, as its project path (`/SLUS_212.15`); empty when the project or its index is missing.

    Read from the index Ghidra keeps of the project's files, `<name>.rep/idata/~index.dat`: a line `/folder` opens a
    folder and each indented `id:name:file-id` line below it is one file.
    """
    index = project.folder / f"{project.name}.rep" / "idata" / "~index.dat"
    try:
        lines = index.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return set()
    found: set[str] = set()
    folder: str | None = None
    for line in lines:
        if line.startswith("/"):
            folder = line.rstrip("/")
        elif line.startswith("  ") and folder is not None:
            parts = line.strip().split(":")
            if len(parts) >= 3:
                found.add(f"{folder}/{parts[1]}")
    return found


def has_program(project: GhidraProject, name: str) -> bool:
    """True when `project` holds the program `name` in its root folder."""
    return f"/{name}" in programs(project)


def _launcher(install: Path, name: str, platform: str) -> Path:
    """Ghidra's `support/<name>` launcher (`.bat` on Windows); ConfigError when the install has none."""
    path = install / "support" / (f"{name}.bat" if platform == "windows" else name)
    if not path.is_file():
        raise ConfigError(
            f"{path} not found: is {install} a Ghidra 12.1.4 install? (`coney-tools ghidra setup` fetches one)"
        )
    return path


def analyze_headless(install: Path, platform: str) -> Path:
    """The `analyzeHeadless` launcher of the Ghidra at `install`."""
    return _launcher(install, "analyzeHeadless", platform)


def sleigh(install: Path, platform: str) -> Path:
    """The `sleigh` language compiler of the Ghidra at `install`."""
    return _launcher(install, "sleigh", platform)


def java_env(jdk: Path, base: Mapping[str, str]) -> dict[str, str]:
    """`base` with `JAVA_HOME` set to `jdk` and its `bin` first on `PATH`.

    Ghidra's launchers take the first JDK they find, and a system `java` is often older than the 21 Ghidra 12 needs.
    """
    env = dict(base)
    env["JAVA_HOME"] = str(jdk)
    env["PATH"] = os.pathsep.join(part for part in (str(jdk / "bin"), base.get("PATH", "")) if part)
    return env


def run_logged(command: Sequence[str], env: Mapping[str, str], log: Path, cwd: Path) -> int:
    """Run `command` in `cwd` with its output written to `log`; returns its exit code.

    On Windows the child gets no console window of its own, so a run from a background task never takes the focus.
    """
    log.parent.mkdir(parents=True, exist_ok=True)
    flags = CREATE_NO_WINDOW if os.name == "nt" else 0
    with log.open("w", encoding="utf-8", errors="replace") as out:
        try:
            done = subprocess.run(
                list(command),
                env=dict(env),
                cwd=cwd,
                stdout=out,
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL,
                creationflags=flags,
                check=False,
            )
        except OSError as error:
            raise ConfigError(f"{command[0]}: cannot be run ({error})") from error
    return done.returncode


def log_tail(log: Path, lines: int = 15) -> str:
    """The last `lines` lines of `log`, or "" when it cannot be read."""
    try:
        text = log.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    return "\n".join(text.splitlines()[-lines:])


def failed(what: str, code: int, log: Path) -> ConfigError:
    """The error for a Ghidra step that exited with `code`: names its log and quotes its end."""
    tail = log_tail(log)
    return ConfigError(f"{what} failed (exit code {code}); its log is {log}" + (f":\n{tail}" if tail else ""))

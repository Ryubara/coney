# SPDX-License-Identifier: GPL-3.0-or-later
"""The pieces of `coney-tools ci run`: which files a change touched, and how the gate runs a program.

Everything here is a function of the repository and its arguments; `check_cli` runs the steps and prints them. There is
no clang-tidy here: CI's per-pull-request job tidies the lines a pull request changes (`ci_tidy`).
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError

#: Lines of tool output kept per failing step; the rest is a rerun away.
MAX_OUTPUT_LINES = 40


@dataclass(frozen=True)
class StepResult:
    """The outcome of one check: its name, whether it passed (or was skipped), and a line for the summary."""

    name: str
    ok: bool
    detail: str
    seconds: float = 0.0
    skipped: bool = False
    output: tuple[str, ...] = ()


def low_priority(command: Sequence[str]) -> tuple[list[str], dict[str, object]]:
    """`command` and the `subprocess` arguments that run it below normal priority, as `coney-tools build` runs its own:
    the gate's pre-commit and pytest runs share the machine with other agents' builds and must not take
    their cores (or the keyboard from the person at it). Windows: BELOW_NORMAL_PRIORITY_CLASS with no console window,
    the flags BuildRun uses. Elsewhere `nice -n 5` in front of the command when `nice` exists: never a `preexec_fn`,
    which is unsafe in threads."""
    if sys.platform == "win32":
        return list(command), {"creationflags": 0x00004000 | 0x08000000}  # BELOW_NORMAL_PRIORITY_CLASS | NO_WINDOW
    nice = shutil.which("nice")
    return ([nice, "-n", "5", *command] if nice else list(command)), {}


def run_command(command: Sequence[str], cwd: Path) -> tuple[int, str]:
    """Run a command below normal priority (`low_priority`), returning its exit code and its stdout and stderr
    together; a missing program is code 127."""
    lowered, options = low_priority(command)
    try:
        done = subprocess.run(  # type: ignore[call-overload]
            lowered,
            cwd=cwd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
            **options,
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


def file_at(root: Path, commit: str, path: str) -> str | None:
    """The text of `path` at `commit`, or None when the file did not exist there."""
    done = subprocess.run(
        ["git", "-C", str(root), "show", f"{commit}:{path}"], capture_output=True, encoding="utf-8", errors="replace"
    )
    return done.stdout if done.returncode == 0 else None


def merge_base(root: Path, base: str) -> str:
    """The commit where HEAD left `base`, the same point `git diff base...HEAD` measures from."""
    return _git(root, "merge-base", base, "HEAD").strip()


def changed_files(root: Path, base: str) -> list[str]:
    """Every file added, copied, modified or renamed since HEAD left `base`, committed or not, untracked included.

    Paths are relative to `root` with forward slashes, sorted; deleted files are left out.
    """
    since = merge_base(root, base)
    tracked = _git(root, "diff", "-z", "--name-only", "--diff-filter=ACMR", since).split("\0")
    untracked = _git(root, "ls-files", "-z", "--others", "--exclude-standard").split("\0")
    names = {name for name in (*tracked, *untracked) if name and (root / name).is_file()}
    return sorted(names)


def renamed_files(root: Path, base: str, similarity: int | None = None) -> dict[str, str]:
    """Each file renamed since HEAD left `base` (staged or committed, as git detects renames) -> its old path.

    `similarity` is git's rename score in percent (`-M<n>%`); None leaves git's own default, 50."""
    since = merge_base(root, base)
    detect = "-M" if similarity is None else f"-M{similarity}%"
    fields = _git(root, "diff", "-z", "--name-status", detect, "--diff-filter=R", since).split("\0")
    renamed: dict[str, str] = {}
    # -z gives `R<score>`, the old path and the new path as three fields.
    for i in range(0, len(fields) - 2, 3):
        if fields[i].startswith("R"):
            renamed[fields[i + 2]] = fields[i + 1]
    return renamed


def removed_files(root: Path, base: str, similarity: int | None = None) -> set[str]:
    """Each path that stopped being a file at its name since HEAD left `base`: deleted, or the old path of a rename.

    `similarity` is as for `renamed_files`."""
    since = merge_base(root, base)
    detect = "-M" if similarity is None else f"-M{similarity}%"
    fields = _git(root, "diff", "-z", "--name-status", detect, "--diff-filter=DR", since).split("\0")
    removed: set[str] = set()
    i = 0
    # -z gives `D`, the path; or `R<score>`, the old path and the new path. Anything else (a git warning) is skipped.
    while i + 1 < len(fields):
        if fields[i].startswith("R") and i + 2 < len(fields):
            removed.add(fields[i + 1])
            i += 3
        elif fields[i] == "D":
            removed.add(fields[i + 1])
            i += 2
        else:
            i += 1
    return removed

# SPDX-License-Identifier: GPL-3.0-or-later
"""The one place that decides which `bash` a test runs a shell script with.

`shutil.which("bash")` is wrong on a Windows machine with the Windows Subsystem for Linux: it can find
System32's `bash.exe`, a Linux where the checkout is `/mnt/c/...` and `uv` does not exist, so a script
fails or, behind a trailing `exit 0`, silently prints nothing. A test that needs bash calls `git_bash()` (or marks
itself `needs_bash`) and never `shutil.which("bash")`.
"""

import os
import shutil
from pathlib import Path

import pytest

#: Whether the tests run on Windows (a module value so a test can pretend, since `os.name` cannot be patched).
ON_WINDOWS = os.name == "nt"


def git_bash() -> str | None:
    """The bash a test should run scripts with, or None when there is none to trust.

    On Windows it is the one that ships with Git for Windows, found beside `git` (never the one `PATH` happens to list
    first, which may be WSL's); elsewhere it is the `bash` on `PATH`.
    """
    if not ON_WINDOWS:
        return shutil.which("bash")
    git = shutil.which("git")
    for parent in Path(git).parents if git else []:
        for candidate in (parent / "bin" / "bash.exe", parent / "usr" / "bin" / "bash.exe"):
            if candidate.is_file():
                return str(candidate)
    return None


def no_bash_reason() -> str:
    """Why `git_bash()` found nothing, for a skip message that says what to install."""
    if ON_WINDOWS:
        return "no Git for Windows bash beside git (the bash.exe in System32 is WSL's, not a stand-in)"
    return "no bash on PATH"


#: Skips a test that runs a shell script when `git_bash()` finds no bash.
needs_bash = pytest.mark.skipif(git_bash() is None, reason=no_bash_reason())

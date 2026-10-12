# SPDX-License-Identifier: GPL-3.0-or-later
"""The issue labels: `labels` of `.github/conventions.yml` is the source; `coney-tools repo labels` syncs them.

Reading and validating the labels is `coney_tools.conventions`; this module turns them into `gh` commands.
"""

from __future__ import annotations

import shlex
import subprocess
from collections.abc import Callable, Sequence

from coney_tools.conventions import Label


def gh_arguments(label: Label, repo: str | None = None) -> list[str]:
    """The `gh` command that creates `label`, or updates it when it exists (`--force`), as an argument list."""
    arguments = ["gh", "label", "create", label.name, "--color", label.color, "--description", label.description]
    arguments.append("--force")
    if repo is not None:
        arguments += ["--repo", repo]
    return arguments


def gh_script(labels: Sequence[Label], repo: str | None = None) -> list[str]:
    """One shell-quoted `gh label create` line per label, for a person to read or paste."""
    return [shlex.join(gh_arguments(label, repo)) for label in labels]


def apply_labels(
    labels: Sequence[Label],
    repo: str | None = None,
    run: Callable[[list[str]], int] | None = None,
) -> list[str]:
    """Create or update every label through `gh`; returns the names that failed (empty when all synced).

    `run` takes an argument list and returns an exit code; it defaults to running the command and is replaced in
    tests so nothing touches GitHub.
    """
    runner = run if run is not None else _run_gh
    return [label.name for label in labels if runner(gh_arguments(label, repo)) != 0]


def _run_gh(arguments: list[str]) -> int:
    """Run one `gh` command, showing its output; 127 when `gh` is not installed."""
    try:
        return subprocess.run(arguments, check=False).returncode
    except FileNotFoundError:
        return 127

# SPDX-License-Identifier: GPL-3.0-or-later
"""The issue labels: `.github/labels.yml` is the source of truth, `coney-tools repo labels` prints or syncs them."""

from __future__ import annotations

import re
import shlex
import subprocess
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

import yaml

from coney_tools.config import ConfigError

#: Where the labels are defined, relative to the repository root.
LABELS_FILE = ".github/labels.yml"
#: GitHub refuses a label description longer than this.
DESCRIPTION_MAX = 100
#: A label colour as GitHub takes it: six hex digits, no `#`.
_COLOUR = re.compile(r"^[0-9a-fA-F]{6}$")


class LabelsError(ConfigError):
    """The labels file is missing or does not hold a usable label list."""


@dataclass(frozen=True)
class Label:
    """One issue label: its name, its colour (six hex digits) and its description."""

    name: str
    color: str
    description: str


def load_labels(root: Path) -> list[Label]:
    """Read and validate `.github/labels.yml` under `root`; raises LabelsError naming the first problem."""
    path = root / LABELS_FILE
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, yaml.YAMLError) as error:
        raise LabelsError(f"{LABELS_FILE}: cannot be read ({error})") from error
    entries = data.get("labels") if isinstance(data, dict) else None
    if not isinstance(entries, list) or not entries:
        raise LabelsError(f"{LABELS_FILE}: expected a non-empty `labels:` list")
    labels: list[Label] = []
    seen: set[str] = set()
    for index, entry in enumerate(entries, start=1):
        label = _parse_label(entry, index)
        if label.name.lower() in seen:
            raise LabelsError(f"{LABELS_FILE}: label {label.name!r} appears twice (GitHub ignores case)")
        seen.add(label.name.lower())
        labels.append(label)
    return labels


def _parse_label(entry: object, index: int) -> Label:
    """Turn one list entry into a Label, checking each field; `index` (1-based) names the entry in errors."""
    if not isinstance(entry, dict):
        raise LabelsError(f"{LABELS_FILE}: entry {index} is not a mapping")
    fields = {}
    for key in ("name", "color", "description"):
        value = entry.get(key)
        # A colour such as 000000 or 123456 is read by YAML as a number; give it back its leading zeros.
        if key == "color" and isinstance(value, int):
            value = f"{value:06d}"
        if not isinstance(value, str) or not value.strip():
            raise LabelsError(f"{LABELS_FILE}: entry {index} needs a text `{key}`")
        fields[key] = value.strip()
    if not _COLOUR.match(fields["color"]):
        raise LabelsError(f"{LABELS_FILE}: {fields['name']!r}: color must be six hex digits without '#'")
    if len(fields["description"]) > DESCRIPTION_MAX:
        raise LabelsError(f"{LABELS_FILE}: {fields['name']!r}: description is over {DESCRIPTION_MAX} characters")
    return Label(fields["name"], fields["color"].lower(), fields["description"])


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

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.repo_labels: the label file, the printed `gh` commands and the sync, with no GitHub access."""

import shlex
from pathlib import Path
from typing import Any

import pytest
import yaml

from coney_tools.cli import main
from coney_tools.config import find_repo_root
from coney_tools.repo_labels import (
    DESCRIPTION_MAX,
    LABELS_FILE,
    Label,
    LabelsError,
    apply_labels,
    gh_arguments,
    gh_script,
    load_labels,
)

ROOT = find_repo_root(Path(__file__).resolve())


def write(root: Path, text: str) -> Path:
    """Write `text` as the labels file under `root` and return `root`."""
    path = root / LABELS_FILE
    path.parent.mkdir(parents=True)
    path.write_text(text, encoding="utf-8")
    return root


def test_load_reads_every_field(tmp_path: Path) -> None:
    root = write(tmp_path, "labels:\n  - {name: bug, color: D73A4A, description: Broken}\n")
    assert load_labels(root) == [Label("bug", "d73a4a", "Broken")]


def test_all_digit_colour_is_kept(tmp_path: Path) -> None:
    # YAML reads 000000 as the number 0; the loader gives it back its zeros.
    root = write(tmp_path, "labels:\n  - {name: black, color: 000000, description: Dark}\n")
    assert load_labels(root)[0].color == "000000"


@pytest.mark.parametrize(
    "entry",
    [
        "{name: a, color: zzzzzz, description: d}",
        "{name: a, color: '#d73a4a', description: d}",
        "{name: a, color: d73a4a}",
        "{color: d73a4a, description: d}",
        "{name: a, color: d73a4a, description: " + "x" * (DESCRIPTION_MAX + 1) + "}",
        "just text",
    ],
)
def test_bad_entry_is_refused(tmp_path: Path, entry: str) -> None:
    with pytest.raises(LabelsError):
        load_labels(write(tmp_path, f"labels:\n  - {entry}\n"))


def test_duplicate_name_ignoring_case_is_refused(tmp_path: Path) -> None:
    text = "labels:\n  - {name: Bug, color: d73a4a, description: d}\n  - {name: bug, color: d73a4a, description: d}\n"
    with pytest.raises(LabelsError, match="twice"):
        load_labels(write(tmp_path, text))


def test_missing_or_empty_file_is_refused(tmp_path: Path) -> None:
    with pytest.raises(LabelsError, match="cannot be read"):
        load_labels(tmp_path)
    with pytest.raises(LabelsError, match="non-empty"):
        load_labels(write(tmp_path, "labels: []\n"))


def test_gh_arguments_force_and_repo() -> None:
    label = Label("good first issue", "7057ff", "Good for newcomers")
    arguments = gh_arguments(label)
    assert arguments[:5] == ["gh", "label", "create", "good first issue", "--color"]
    assert arguments[-1] == "--force"
    assert gh_arguments(label, "owner/name")[-2:] == ["--repo", "owner/name"]


def test_script_lines_round_trip_through_a_shell() -> None:
    label = Label("A-ai", "c5def5", "Area: AI humans, pathing")
    (line,) = gh_script([label])
    assert shlex.split(line) == gh_arguments(label)


def test_apply_runs_each_and_reports_failures() -> None:
    labels = [Label("a", "000000", "x"), Label("b", "000000", "y"), Label("c", "000000", "z")]
    seen: list[list[str]] = []

    def run(arguments: list[str]) -> int:
        seen.append(arguments)
        return 1 if arguments[3] == "b" else 0

    assert apply_labels(labels, None, run) == ["b"]
    assert [arguments[3] for arguments in seen] == ["a", "b", "c"]


def test_repo_labels_command_prints_and_never_applies(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["repo", "labels"]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines and all(line.startswith("gh label create ") and line.endswith("--force") for line in lines)


# --- the real file and the forms that use it ---------------------------------------------------------------------


def test_the_real_labels_load() -> None:
    names = {label.name for label in load_labels(ROOT)}
    assert {"bug", "task", "parity", "S-needs-triage", "P-high", "A-combat", "good first issue"} <= names


def _form(name: str) -> dict[str, Any]:
    """Read an issue form from .github/ISSUE_TEMPLATE."""
    form: dict[str, Any] = yaml.safe_load((ROOT / ".github" / "ISSUE_TEMPLATE" / name).read_text(encoding="utf-8"))
    return form


@pytest.mark.parametrize("form", ["bug.yml", "task.yml"])
def test_form_labels_exist(form: str) -> None:
    names = {label.name for label in load_labels(ROOT)}
    assert set(_form(form)["labels"]) <= names


def test_task_form_areas_match_the_area_labels() -> None:
    areas = {label.name for label in load_labels(ROOT) if label.name.startswith("A-")}
    (dropdown,) = [item for item in _form("task.yml")["body"] if item.get("id") == "area"]
    assert set(dropdown["attributes"]["options"]) == areas


def test_blank_issues_are_off() -> None:
    assert _form("config.yml")["blank_issues_enabled"] is False

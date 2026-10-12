# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.repo_labels: the printed `gh` commands and the sync, with no GitHub access."""

import shlex

import pytest
from conventions_support import REPO

from coney_tools.cli import main
from coney_tools.conventions import Label, load_conventions
from coney_tools.repo_labels import apply_labels, gh_arguments, gh_script


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


def test_repo_labels_command_prints_every_label_of_the_conventions(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["repo", "labels"]) == 0
    printed = [shlex.split(line)[3] for line in capsys.readouterr().out.splitlines()]
    assert printed == [label.name for label in load_conventions(REPO).labels.list]

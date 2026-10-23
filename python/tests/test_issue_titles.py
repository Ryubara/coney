# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools issue titles` (issue_titles, issue_cli.run_titles): made-up issues, no network."""

import argparse
import json
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from conventions_support import REPO

from coney_tools import issue_cli, issue_titles
from coney_tools.repo_checks import check_title

RULES = CONVENTIONS.commits.title


def propose(title: str, *labels: str) -> issue_titles.Retitle:
    """The proposal for a title that must be wrong."""
    found = issue_titles.propose_title(1, title, labels, CONVENTIONS)
    assert found is not None
    return found


def test_a_title_that_passes_needs_no_proposal() -> None:
    assert issue_titles.propose_title(1, "gameplay: Fix the fight goal", ["bug"], CONVENTIONS) is None


@pytest.mark.parametrize(
    ("title", "labels", "proposed", "confidence"),
    [
        ("Bug: Fix the camera clipping", ("bug", "A-camera"), "gameplay: Fix the camera clipping", "auto"),
        ("Task: Adds the throw order", ("task", "A-ai"), "gameplay: Add the throw order", "auto"),
        ("Task: Update the pause menu", ("task", "A-hud"), "gameplay: Update the pause menu", "auto"),
        ("Task: Port the audio mixer", ("task", "A-audio"), "audio: Add the audio mixer", "review"),
        (
            "Bug: Camera clips through a wall",
            ("bug", "A-camera"),
            "gameplay: Fix camera clips through a wall",
            "review",
        ),
        ("Task: Reimplement NPC_Update", ("task", "A-ai"), "gameplay: Add NPC_Update", "review"),
        ("Task: Window mode on Linux", ("task", "A-platform"), "platform: Add window mode on Linux", "review"),
        ("Bug: Crash on start", ("bug",), "gameplay: Fix crash on start", "review"),
        ("Task: Guide for new people", ("docs",), "docs: Add guide for new people", "review"),
        ("tools: add a check", ("task", "A-tools"), "tools: Add a check", "auto"),
        ("Build everything", ("task", "A-tools"), "tools: Add everything", "review"),
        ("Fix", ("bug", "A-ai"), "gameplay: Fix fix", "review"),
    ],
)
def test_proposals(title: str, labels: tuple[str, ...], proposed: str, confidence: str) -> None:
    found = propose(title, *labels)
    assert (found.proposed, found.confidence) == (proposed, confidence)


def test_every_proposal_passes_the_rule() -> None:
    samples = [
        ("Bug: x" * 30, ("bug", "A-ai")),
        ("Task: Fix the thing.", ("task", "A-core")),
        ("[Bug] crash", ("bug",)),
        ("Task - cleanup of the pathing tables", ("task", "A-ai")),
        ("Parity: Camera", ("parity", "A-camera")),
    ]
    for title, labels in samples:
        found = issue_titles.propose_title(1, title, labels, CONVENTIONS)
        if found is not None:
            assert check_title(found.proposed, RULES) == [], found


def test_a_long_title_is_cut_at_a_word_and_marked_for_review() -> None:
    found = propose("Task: " + "Fix the very long title of an issue that keeps going and going " * 3, "task", "A-ai")
    assert len(found.proposed) <= RULES.max_length and found.confidence == "review" and "cut to 72" in found.why
    assert not found.proposed.endswith(" ")


def test_a_right_area_with_a_wrong_verb_keeps_the_area() -> None:
    found = propose("graphics: fix the water", "A-ai")
    assert found.proposed == "graphics: Fix the water"


def test_the_hygiene_report_issue_gets_the_title_the_sweep_looks_for() -> None:
    found = propose("Repo hygiene", "task")
    assert found.proposed == CONVENTIONS.hygiene.report_issue.title and found.confidence == "auto"
    assert check_title(found.proposed, RULES) == []


def test_the_command_quotes_the_title() -> None:
    retitle = issue_titles.Retitle(7, "old", 'tools: Fix a "quoted" \\ name', "auto", "")
    assert retitle.command() == 'gh issue edit 7 --title "tools: Fix a \\"quoted\\" \\\\ name"'


def test_the_list_reads_gh_json_and_sorts_by_number() -> None:
    issues = [
        {"number": 9, "title": "Bug: b", "labels": [{"name": "bug"}]},
        {"number": 3, "title": "gameplay: Fix a", "labels": []},
        {"number": 5, "title": "Task: Add c", "labels": [{"name": "A-ai"}]},
    ]
    assert [r.number for r in issue_titles.list_retitles(issues, CONVENTIONS)] == [5, 9]


# --- the command ----------------------------------------------------------------------------------------------------


ISSUES = json.dumps(
    [
        {"number": 5, "title": "Task: Add the throw order", "labels": [{"name": "A-ai"}]},
        {"number": 6, "title": "Bug: Camera clips", "labels": [{"name": "bug"}, {"name": "A-camera"}]},
        {"number": 7, "title": "gameplay: Fix the fight goal", "labels": []},
    ]
)


class Gh:
    """A fake gh that lists the issues and records the renames."""

    def __init__(self, fail: set[int] | None = None) -> None:
        self.calls: list[list[str]] = []
        self.fail = fail or set()

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if arguments[:2] == ["issue", "list"]:
            return ISSUES
        if arguments[:2] == ["issue", "edit"] and int(arguments[2]) in self.fail:
            raise OSError("gh issue edit failed")
        return ""

    def edits(self) -> list[list[str]]:
        """The rename calls."""
        return [call for call in self.calls if call[:2] == ["issue", "edit"]]


def parse(*words: str) -> argparse.Namespace:
    """Parse an `issue titles` command line."""
    group = argparse.ArgumentParser().add_subparsers(dest="group")
    issue_cli.add_issue_parser(group)
    return group.choices["issue"].parse_args(["titles", *words])


@pytest.fixture(autouse=True)
def in_repo(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(REPO)


def test_the_list_prints_the_proposals_and_the_commands_and_changes_nothing(
    capsys: pytest.CaptureFixture[str],
) -> None:
    gh = Gh()
    assert issue_cli.run_titles(parse(), gh) == 1
    out = capsys.readouterr().out
    assert "2 of 3 open issue(s) break the title rule" in out
    assert 'gh issue edit 5 --title "gameplay: Add the throw order"' in out
    assert 'gh issue edit 6 --title "gameplay: Fix camera clips"' in out
    assert gh.edits() == []


def test_no_bad_title_is_exit_0(capsys: pytest.CaptureFixture[str]) -> None:
    class Clean(Gh):
        def __call__(self, arguments: list[str]) -> str:
            return json.dumps([{"number": 7, "title": "gameplay: Fix the fight goal", "labels": []}])

    assert issue_cli.run_titles(parse(), Clean()) == 0
    assert "0 of 1" in capsys.readouterr().out


def test_json_output(capsys: pytest.CaptureFixture[str]) -> None:
    issue_cli.run_titles(parse("--json"), Gh())
    rows = json.loads(capsys.readouterr().out)
    assert [row["number"] for row in rows] == [5, 6] and rows[0]["confidence"] == "auto"


def test_apply_renames_the_sure_ones_and_include_review_the_rest(capsys: pytest.CaptureFixture[str]) -> None:
    gh = Gh()
    assert issue_cli.run_titles(parse("--apply"), gh) == 0
    assert gh.edits() == [["issue", "edit", "5", "--title", "gameplay: Add the throw order"]]
    again = Gh()
    issue_cli.run_titles(parse("--apply", "--include-review"), again)
    assert [call[2] for call in again.edits()] == ["5", "6"]
    assert "renamed #5" in capsys.readouterr().out


def test_a_failed_rename_is_reported_and_the_others_go_on(capsys: pytest.CaptureFixture[str]) -> None:
    gh = Gh(fail={5})
    assert issue_cli.run_titles(parse("--apply", "--include-review"), gh) == 1
    assert [call[2] for call in gh.edits()] == ["5", "6"]
    assert "#5 was not renamed" in capsys.readouterr().err


def test_the_command_is_registered(tmp_path: Path) -> None:
    from coney_tools.cli import main

    with pytest.raises(SystemExit) as stop:
        main(["issue", "titles", "--help"])
    assert stop.value.code == 0

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools pr open --adopt` (docs/guides/pr.md#adopt): taking over a pull request that was opened by
hand on a unit's branch, so `pr open` can update it once the PR guard refuses every hand-typed push.

The lease of `pr open`'s push is the tip its last run recorded; a pull request no run pushed has no record, and its
branch already exists, so the push is refused. `--adopt <head>` names the head the coordinator read from GitHub: it
becomes the lease once, and nothing is pushed when the branch is anywhere else.
"""

import argparse
import json
import subprocess
from pathlib import Path

import pytest
from git_support import git
from pr_open_support import Open, make_open
from pr_support import make
from test_pr_open import start_fix

from coney_tools import pr_open, pr_publish

NUMBER = 900


@pytest.fixture(autouse=True)
def _no_task_from_the_environment(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("CONEY_TASK", raising=False)


def opened_by_hand(tmp_path: Path) -> tuple[Open, str]:
    """A ready unit whose branch somebody pushed and opened as a pull request by hand; the unit and that head."""
    opened = make_open(make(tmp_path, "docs"))
    built = opened.built
    other = built.root.parent.parent / "by-hand"
    subprocess.run(["git", "clone", "-q", str(built.origin), str(other)], check=True)
    git(other, "checkout", "-q", "-b", "work")
    # the same change the unit's branch makes, in another commit (as if the branch were later rebased or amended)
    for path in git(built.root, "diff", "--name-only", f"{built.base}..HEAD").split():
        (other / path).parent.mkdir(parents=True, exist_ok=True)
        (other / path).write_bytes((built.root / path).read_bytes())
    git(other, "add", "-A")
    git(other, "commit", "-q", "-m", "an early round, pushed by hand")
    git(other, "push", "-q", "origin", "work")
    head = git(other, "rev-parse", "HEAD").strip()
    opened.gh.prs[NUMBER] = {
        "head": "work",
        "title": "docs: Add the widget",
        "body": "written by hand",
        "state": "OPEN",
        "merged": False,
        "auto": True,
        "url": f"https://github.com/acme/widgets/pull/{NUMBER}",
    }
    return opened, head


def pushes(opened: Open) -> list[list[str]]:
    """The `git push` calls the run made."""
    return [args for args in opened.git_calls if args and args[0] == "push"]


def test_without_adopt_a_pull_request_opened_by_hand_is_not_overwritten_and_the_message_says_how(
    tmp_path: Path,
) -> None:
    opened, head = opened_by_hand(tmp_path)
    assert opened.run() == 4
    assert "no earlier pr open pushed it" in opened.text() and "--adopt" in opened.text()
    assert opened.origin_tip() == head


def test_adopt_takes_the_pull_request_over_with_the_named_head_as_the_lease(tmp_path: Path) -> None:
    opened, head = opened_by_hand(tmp_path)
    assert opened.run(adopt=head) == 0, opened.text()
    assert opened.origin_tip() == opened.built.tip
    assert [args[1] for args in pushes(opened)] == [f"--force-with-lease=work:{head}"]
    assert "pr create" not in opened.gh.names()
    assert opened.gh.prs[NUMBER]["title"] != "docs: Add the widget" and opened.gh.prs[NUMBER]["auto"]
    record = json.loads((opened.built.unit.dir / pr_publish.PUBLISHED_FILE).read_text(encoding="utf-8"))
    assert record["number"] == NUMBER and record["tip"] == opened.built.tip
    assert opened.said[-1].startswith(f"updated #{NUMBER}")


def test_after_the_takeover_the_next_run_needs_no_adopt_and_a_second_adopt_is_refused(tmp_path: Path) -> None:
    opened, head = opened_by_hand(tmp_path)
    assert opened.run(adopt=head) == 0, opened.text()
    opened.said.clear()
    assert opened.run() == 0, opened.text()
    opened.said.clear()
    assert opened.run(adopt=head) == 2
    assert "pr-published.json" in opened.text() and "leave --adopt out" in opened.text()


def test_adopt_with_a_head_the_branch_is_not_at_pushes_nothing(tmp_path: Path) -> None:
    opened, head = opened_by_hand(tmp_path)
    wrong = opened.built.base
    assert opened.run(adopt=wrong) == 2
    assert f"origin/work is at {head[:12]}" in opened.text()
    assert pushes(opened) == [] and opened.gh.writes() == [] and opened.origin_tip() == head
    assert not (opened.built.unit.dir / pr_publish.PUBLISHED_FILE).exists()


@pytest.mark.parametrize("value", ["abc123", "A" * 40, "g" * 40, "HEAD", "origin/work"])
def test_adopt_takes_only_a_full_commit_id(tmp_path: Path, value: str) -> None:
    opened, head = opened_by_hand(tmp_path)
    assert opened.run(adopt=value) == 2
    assert "40-digit" in opened.text() and pushes(opened) == [] and opened.origin_tip() == head


def test_adopt_with_no_pull_request_to_take_over_is_refused(tmp_path: Path) -> None:
    opened = make_open(make(tmp_path, "docs"))
    assert opened.run(adopt="a" * 40) == 2
    assert "no pull request to take over" in opened.text() and pushes(opened) == []


def test_a_dry_run_with_adopt_says_what_it_would_take_over_and_changes_nothing(tmp_path: Path) -> None:
    opened, head = opened_by_hand(tmp_path)
    before = opened.snapshot()
    assert opened.run(adopt=head, dry_run=True) == 0, opened.text()
    assert f"would take over #{NUMBER} at {head[:12]}" in opened.text()
    assert opened.snapshot() == before and opened.gh.writes() == []


def test_the_command_line_takes_adopt() -> None:
    parser = argparse.ArgumentParser()
    pr_open.add_parser(parser.add_subparsers())
    args = parser.parse_args(["open", "t", "--adopt", "a" * 40])
    assert args.adopt == "a" * 40


def test_adopt_works_for_a_fix_task_through_its_parents_pull_request_and_record(tmp_path: Path) -> None:
    # A fix task has no `pr-published.json` of its own: the parent's folder holds it, so that is where the lease goes.
    opened, head = opened_by_hand(tmp_path)
    start_fix(opened)
    assert opened.run(task="work-fix", adopt=head) == 0, opened.text()
    assert [args[1] for args in pushes(opened)] == [f"--force-with-lease=work:{head}"]
    record = json.loads((opened.built.unit.dir / pr_publish.PUBLISHED_FILE).read_text(encoding="utf-8"))
    assert record["number"] == NUMBER and not (opened.built.unit.dir.parent / "work-fix" / "pr-published.json").exists()


def test_adopt_on_a_fix_task_is_refused_once_the_parent_was_taken_over(tmp_path: Path) -> None:
    opened, head = opened_by_hand(tmp_path)
    assert opened.run(adopt=head) == 0, opened.text()
    start_fix(opened)
    opened.said.clear()
    before = len(pushes(opened))
    assert opened.run(task="work-fix", adopt=head) == 2
    assert "pr-published.json" in opened.text() and "leave --adopt out" in opened.text()
    assert len(pushes(opened)) == before


def push_by_hand_too(opened: Open) -> str:
    """Somebody adds a commit to the pull request's branch (from elsewhere, or on GitHub), which the unit lacks."""
    other = opened.built.root.parent.parent / "by-hand"
    (other / "later.md").write_text("pushed from elsewhere\n", encoding="utf-8")
    git(other, "add", "-A")
    git(other, "commit", "-q", "-m", "a later round, pushed from elsewhere")
    git(other, "push", "-q", "origin", "work")
    return git(other, "rev-parse", "HEAD").strip()


def test_adopt_is_refused_when_the_pull_request_has_a_commit_the_unit_lacks(tmp_path: Path) -> None:
    opened, _ = opened_by_hand(tmp_path)
    later = push_by_hand_too(opened)
    assert opened.run(adopt=later) == 2
    assert "a later round, pushed from elsewhere" in opened.text() and "--adopt-rewrite" in opened.text()
    assert pushes(opened) == [] and opened.gh.writes() == [] and opened.origin_tip() == later
    assert not (opened.built.unit.dir / pr_publish.PUBLISHED_FILE).exists()


def test_adopt_rewrite_takes_it_over_and_the_push_drops_that_commit(tmp_path: Path) -> None:
    opened, _ = opened_by_hand(tmp_path)
    later = push_by_hand_too(opened)
    assert opened.run(adopt=later, adopt_rewrite=True) == 0, opened.text()
    assert opened.origin_tip() == opened.built.tip
    assert [args[1] for args in pushes(opened)] == [f"--force-with-lease=work:{later}"]


def test_adopt_rewrite_without_adopt_is_refused_and_names_the_missing_option(tmp_path: Path) -> None:
    opened, _ = opened_by_hand(tmp_path)
    assert opened.run(adopt_rewrite=True) == 2
    assert "--adopt-rewrite" in opened.text() and "--adopt" in opened.text()
    assert pushes(opened) == [] and opened.gh.writes() == []


def test_a_head_that_is_no_ancestor_but_whose_changes_the_unit_holds_is_taken_over(tmp_path: Path) -> None:
    # The remote head is another commit than the unit's (after a local rebase), yet every change of it is in the unit.
    opened, head = opened_by_hand(tmp_path)
    ancestor = subprocess.run(
        ["git", "-C", str(opened.built.root), "merge-base", "--is-ancestor", head, "HEAD"], check=False
    )
    assert ancestor.returncode != 0
    assert opened.run(adopt=head) == 0, opened.text()


def test_the_command_line_takes_adopt_rewrite() -> None:
    parser = argparse.ArgumentParser()
    pr_open.add_parser(parser.add_subparsers())
    assert parser.parse_args(["open", "t", "--adopt", "a" * 40, "--adopt-rewrite"]).adopt_rewrite is True
    assert parser.parse_args(["open", "t"]).adopt_rewrite is False

# SPDX-License-Identifier: GPL-3.0-or-later
"""Stamps are bound to the change a branch makes, not to a commit: a clean rebase keeps them, an edit makes them stale
(docs/guides/stamps.md#change)."""

from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest
from git_support import git
from stamp_support import rules

from coney_tools import pr_diff, stamps

NINE = "".join(f"line {n}\n" for n in range(1, 10))
STEPS = ("checked", "reviewed", "pr-opened")


class Clock:
    """A clock that moves one second per reading."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 23, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(seconds=1)
        return self.now


def commit(repo: Path, name: str, text: str, message: str = "change") -> None:
    """Write `name` and commit it."""
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", message)


@pytest.fixture
def stamped(tmp_path: Path) -> tuple[Path, Path]:
    """A repository on branch `work` with one change and a log whose checked, reviewed and pr-opened stamps cover the
    change; returns (repository, log folder)."""
    repo = tmp_path / "repo"
    repo.mkdir()
    git(repo, "init", "-q", "-b", "main")
    commit(repo, "src/widget.cpp", NINE, "base")
    git(repo, "checkout", "-q", "-b", "work")
    commit(repo, "src/widget.cpp", NINE.replace("line 4", "LINE 4"))
    log = tmp_path / "log"
    found = pr_diff.change_of(repo, "main", "HEAD")
    clock = Clock()
    for step in STEPS:
        stamps.append(
            log,
            rules=rules(),
            task="work",
            step=step,
            subject="",
            result="pass",
            commit=found.tip,
            covers={stamps.CHANGE: found.sha256},
            detail={},
            who={"kind": "tool", "tool": "test"},
            clock=clock,
        )
    return repo, log


def fresh(repo: Path, log: Path) -> list[str]:
    """The steps whose newest passing stamp covers the branch's change as it is now."""
    report = stamps.verify(log, rules())
    assert report.ok
    now = pr_diff.change_of(repo, "main", "HEAD").sha256
    return [step for step in STEPS if report.current(step, "", {stamps.CHANGE: now}) is not None]


def test_stamps_cover_the_change_they_were_made_for(stamped: tuple[Path, Path]) -> None:
    repo, log = stamped
    assert fresh(repo, log) == list(STEPS)


def test_a_clean_rebase_onto_a_new_main_commit_keeps_every_stamp_valid(stamped: tuple[Path, Path]) -> None:
    repo, log = stamped
    old_tip = git(repo, "rev-parse", "HEAD").strip()
    git(repo, "checkout", "-q", "main")
    commit(repo, "src/other.cpp", "new on main\n", "main moves")
    commit(repo, "src/widget.cpp", "head\n" + NINE, "main edits the same file")
    git(repo, "checkout", "-q", "work")
    git(repo, "rebase", "-q", "main")
    assert git(repo, "rev-parse", "HEAD").strip() != old_tip  # every commit id is new
    assert fresh(repo, log) == list(STEPS)  # and the stamps still verify
    assert stamps.verify(log, rules()).stamps[0].commit == old_tip  # the old commit stays in the stamp as a record


def test_changing_one_line_makes_every_stamp_stale(stamped: tuple[Path, Path]) -> None:
    repo, log = stamped
    commit(repo, "src/widget.cpp", NINE.replace("line 4", "LINE 4").replace("line 6", "LINE 6"))
    assert fresh(repo, log) == []


def test_a_stamp_goes_stale_when_the_branch_changes_back_and_forth_to_another_change(
    stamped: tuple[Path, Path],
) -> None:
    repo, log = stamped
    commit(repo, "src/widget.cpp", NINE.replace("line 4", "LINE 4").replace("line 6", "LINE 6"))
    assert fresh(repo, log) == []
    commit(repo, "src/widget.cpp", NINE.replace("line 4", "LINE 4"))
    assert fresh(repo, log) == list(STEPS)  # the same change again: the stamps cover it again

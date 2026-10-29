# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the trial rebase behind Conclave's conflict events (docs/guides/conclave.md#conflicts).

The repositories are real (`pr_support` builds a unit on a branch `work` with a bare `origin` on disk); `main` moves by
a second clone pushing to that origin. The proof is the real one too, except where a test needs a stamp to go stale
and says so. No network is used."""

import subprocess
import tempfile
from collections.abc import Sequence
from datetime import UTC, datetime
from pathlib import Path

import pytest
from git_support import git
from pr_support import Built, commit_all, make, own_identity, write

from coney_tools import conflict_work, pr_ready
from coney_tools.conflict_work import Ledger, Trial

NOW = datetime(2026, 10, 29, 12, 0, tzinfo=UTC)


def main_moves(built: Built, name: str, text: str) -> None:
    """`origin/main` gets one commit that writes `name`; the unit's checkout does not fetch it (the trial must)."""
    other = built.root.parent.parent / "other"
    if not other.exists():
        subprocess.run(["git", "clone", "-q", str(built.origin), str(other)], check=True)
        own_identity(other)
    git(other, "checkout", "-q", "main")
    git(other, "pull", "-q", "origin", "main")
    write(other, name, text)
    commit_all(other, f"main writes {name}")
    git(other, "push", "-q", "origin", "main")


def real_ready(root: Path, unit: object, text: Path, conventions: object, now: datetime) -> pr_ready.Readiness:
    """The proof `pr open` makes, as Conclave asks it."""
    return pr_ready.readiness(root, unit, text, conventions, now=now)  # type: ignore[arg-type]


def trial(built: Built, ready: conflict_work.ReadyFn = real_ready) -> Trial:
    """A fetch and a trial of the unit as it is."""
    refused = conflict_work.prepare(built.root, built.conventions)
    assert refused is None
    return conflict_work.run_trial(built.root, built.unit, built.unit.dir / "PR.md", built.conventions, NOW, ready)


def worktrees(built: Built) -> list[str]:
    """The worktrees git lists for the unit's repository."""
    return [
        line for line in git(built.root, "worktree", "list", "--porcelain").splitlines() if line.startswith("worktree")
    ]


# --- the three answers ----------------------------------------------------------------------------------------------


def test_a_branch_main_moved_past_without_a_conflict_rebases_clean_and_is_ready(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    before = built.diff
    main_moves(built, "other.txt", "main moved\n")
    found = trial(built)
    assert found.status == conflict_work.READY and found.clean and found.paths == ()
    assert found.change == before


def test_a_real_conflict_names_the_paths_git_could_not_merge(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "python/src/coney_tools/widget.py", '"""A widget that main wrote differently."""\n')
    found = trial(built)
    assert found.status == conflict_work.CONFLICT and not found.clean
    assert found.paths == ("python/src/coney_tools/widget.py",)
    assert "widget.py" in found.paths_text() and found.reason.startswith("rebase onto origin/main stops at")


def test_a_clean_rebase_whose_stamps_no_longer_cover_the_change_is_stale(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "other.txt", "main moved\n")
    stale = pr_ready.Readiness(False, "reviewed: the review covers another change; run task review", "e" * 40)
    found = trial(built, lambda *_: stale)
    assert found.status == conflict_work.STALE and found.clean
    assert found.reason.startswith("reviewed:") and found.change == "e" * 40


def test_a_branch_that_has_main_already_has_nothing_to_rebase(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    assert trial(built).status == conflict_work.CURRENT


def test_a_proof_that_cannot_be_read_is_unknown_not_stale(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "other.txt", "main moved\n")
    unknown = pr_ready.Readiness(False, "origin/HEAD is not set here", unknown=True)
    found = trial(built, lambda *_: unknown)
    assert found.status == conflict_work.UNKNOWN and "origin/HEAD" in found.reason


# --- nothing of the unit is touched ---------------------------------------------------------------------------------


def test_the_trial_leaves_the_branch_the_worktree_and_the_remote_as_they_were(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "python/src/coney_tools/widget.py", '"""Another widget."""' + chr(10))
    made: list[str] = []
    real_mkdtemp = tempfile.mkdtemp

    def mkdtemp(*args: object, **kwargs: object) -> str:
        made.append(real_mkdtemp(*args, **kwargs))  # type: ignore[call-overload]
        return made[-1]

    monkeypatch.setattr(tempfile, "mkdtemp", mkdtemp)
    tip, before = built.tip, worktrees(built)
    remote = git(built.origin, "for-each-ref", "--format=%(refname) %(objectname)")
    assert trial(built).status == conflict_work.CONFLICT
    assert built.tip == tip and git(built.root, "status", "--porcelain") == ""
    assert git(built.root, "branch", "--show-current").strip() == "work"
    assert (
        worktrees(built) == before and git(built.origin, "for-each-ref", "--format=%(refname) %(objectname)") == remote
    )
    assert made and not any(Path(folder).exists() for folder in made)


def test_a_branch_in_the_middle_of_a_rebase_is_left_alone(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "python/src/coney_tools/widget.py", '"""Another widget."""\n')
    git(built.root, "fetch", "-q", "origin", "main")
    subprocess.run(["git", "-C", str(built.root), "rebase", "origin/main"], capture_output=True, check=False)
    refused = conflict_work.prepare(built.root, built.conventions)
    assert refused is not None and refused.status == conflict_work.UNKNOWN and "in progress" in refused.reason
    git(built.root, "rebase", "--abort")


def test_a_fetch_that_fails_makes_the_trial_unknown(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    git(built.root, "remote", "set-url", "origin", str(tmp_path / "nowhere.git"))
    refused = conflict_work.prepare(built.root, built.conventions)
    assert refused is not None and refused.status == conflict_work.UNKNOWN and "fetch" in refused.reason


# --- the ledger -----------------------------------------------------------------------------------------------------


def test_a_conflict_is_first_seen_once_per_head(tmp_path: Path) -> None:
    ledger = Ledger(tmp_path / "pr-conflicts")
    first = datetime(2026, 10, 28, 8, 0, tzinfo=UTC)
    assert ledger.first_seen("812", "aaaaaaa", first) == first
    assert ledger.first_seen("812", "aaaaaaa", NOW) == first
    assert ledger.first_seen("812", "bbbbbbb", NOW) == NOW


def test_a_ledger_that_cannot_be_read_or_written_never_fails_the_read(tmp_path: Path) -> None:
    blocked = tmp_path / "pr-conflicts"
    blocked.write_text("a file where the folder should be", encoding="utf-8")
    assert Ledger(blocked).first_seen("812", "aaaaaaa", NOW) == NOW
    folder = tmp_path / "ok"
    folder.mkdir()
    (folder / "812.json").write_text("{not json", encoding="utf-8")
    assert Ledger(folder).first_seen("812", "aaaaaaa", NOW) == NOW


def test_the_trial_is_reused_until_something_it_depends_on_moves(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "other.txt", "main moved\n")
    folder = tmp_path / "pr-conflicts"
    Ledger(folder).first_seen("812", built.tip, NOW)
    calls: list[int] = []

    def counting(root: Path, unit: object, text: Path, conventions: object, now: datetime) -> pr_ready.Readiness:
        calls.append(1)
        return real_ready(root, unit, text, conventions, now)

    def ask() -> Trial:
        # one Ledger object is one read (it remembers that main was fetched), the files in `folder` outlive it
        return conflict_work.trial_of(
            built.root,
            built.unit,
            built.unit.dir / "PR.md",
            built.conventions,
            NOW,
            counting,
            Ledger(folder),
            "812",
            built.tip,
        )

    assert ask().status == conflict_work.READY and ask().status == conflict_work.READY
    assert len(calls) == 1
    # a new stamp (the engineer redid a step) is another answer
    built.stamp("finished", result="pass")
    assert ask().status == conflict_work.READY and len(calls) == 2
    # so is a main that moved on
    main_moves(built, "more.txt", "main moved again\n")
    assert ask().status == conflict_work.READY and len(calls) == 3


def test_a_conflict_that_is_over_is_forgotten(tmp_path: Path) -> None:
    ledger = Ledger(tmp_path / "pr-conflicts")
    ledger.first_seen("812", "aaaaaaa", NOW)
    ledger.first_seen("813", "bbbbbbb", NOW)
    ledger.forget_except({"813"})
    assert sorted(path.name for path in (tmp_path / "pr-conflicts").iterdir()) == ["813.json"]


def test_main_is_fetched_once_for_all_the_trials_of_a_read(tmp_path: Path) -> None:
    built = make(tmp_path / "unit", shape="tooling")
    main_moves(built, "other.txt", "main moved" + chr(10))
    ledger = Ledger(tmp_path / "pr-conflicts")
    fetches: list[str] = []

    def counting(folder: Path, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
        if args[0] == "fetch":
            fetches.append(args[-1])
        return conflict_work.real_git(folder, args)

    for key in ("812", "813"):
        ledger.first_seen(key, built.tip, NOW)
        text = built.unit.dir / "PR.md"
        conflict_work.trial_of(
            built.root, built.unit, text, built.conventions, NOW, real_ready, ledger, key, built.tip, git=counting
        )
    assert fetches == ["main"]

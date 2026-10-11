# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.workspace_clean: `workspace clean` and `workspace disk` on made-up git repositories."""

import os
import shutil
import stat
import subprocess
import time
from collections import namedtuple
from pathlib import Path

import pytest

from coney_tools import workspace_clean
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.workspace_clean import BUSY_FILE, Entry, Settings, plan, run_clean, run_disk

GIT_ENV = {
    **os.environ,
    "GIT_AUTHOR_NAME": "t",
    "GIT_AUTHOR_EMAIL": "t@example.invalid",
    "GIT_COMMITTER_NAME": "t",
    "GIT_COMMITTER_EMAIL": "t@example.invalid",
}
OLD = 100.0  # hours: older than every default threshold
Usage = namedtuple("Usage", "total used free")


def git(folder: Path, *args: str) -> str:
    """Run git in `folder` with a made-up identity; its output."""
    done = subprocess.run(["git", "-C", str(folder), *args], check=True, capture_output=True, text=True, env=GIT_ENV)
    return done.stdout.strip()


class Space:
    """A workspace: `repos/coney` cloned from a made-up origin, and a place for worktrees."""

    def __init__(self, root: Path) -> None:
        self.root = root
        origin = root / "origin.git"
        seed = root / "seed"
        seed.mkdir(parents=True)
        git(seed, "init", "-q", "-b", "main")
        (seed / ".gitignore").write_text("/build/\n/.coney-busy\n", encoding="utf-8")
        (seed / "a.txt").write_text("a\n", encoding="utf-8")
        (seed / "coney.local.example.toml").write_text("", encoding="utf-8")  # marks a Coney checkout
        git(seed, "add", "-A")
        git(seed, "commit", "-q", "-m", "first")
        subprocess.run(["git", "clone", "-q", "--bare", str(seed), str(origin)], check=True, env=GIT_ENV)
        self.checkout = root / "repos" / "coney"
        self.checkout.parent.mkdir()
        subprocess.run(["git", "clone", "-q", str(origin), str(self.checkout)], check=True, env=GIT_ENV)
        self.worktrees = root / "worktrees" / "coney"
        self.worktrees.mkdir(parents=True)

    def worktree(self, name: str, *, commits: int = 0, build: int = 0, age: float = OLD) -> Path:
        """A worktree on a new branch `name` from origin/main, with `commits` own commits and a `build/` holding
        `build` bytes, all last touched `age` hours ago."""
        path = self.worktrees / name
        git(self.checkout, "worktree", "add", "-q", str(path), "-b", name, "origin/main")
        for number in range(commits):
            (path / f"{name}-{number}.txt").write_text("x\n", encoding="utf-8")
            git(path, "add", "-A")
            git(path, "commit", "-q", "-m", f"work {number}")
        if build:
            (path / "build" / "dev").mkdir(parents=True)
            (path / "build" / "dev" / "out.bin").write_bytes(b"\0" * build)
        self.touch(path, age)
        return path

    def touch(self, path: Path, age: float) -> None:
        """Set the modification time of everything of the worktree (and git's folder for it) to `age` hours ago."""
        when = time.time() - age * 3600
        for folder, folders, files in os.walk(path):
            if ".git" in files:
                files.remove(".git")
            for name in [*files, *folders]:
                os.utime(Path(folder) / name, (when, when))
        if (path / "build").is_dir():
            os.utime(path / "build", (when, when))
        gitdir = Path(git(path, "rev-parse", "--absolute-git-dir"))
        for name in ("HEAD", "logs/HEAD"):
            os.utime(gitdir / name, (when, when))

    def settings(self, cwd: Path | None = None, heads: dict[str, list[str]] | None = None, **kwargs: float) -> Settings:
        """Settings for `plan`; `heads` makes the stand-in for GitHub (None for branches it does not list)."""
        known = heads or {}
        return Settings(
            root=self.root / "worktrees",
            checkout=self.checkout,
            cwd=cwd or self.checkout,
            now=time.time(),
            build_hours=kwargs.get("build_hours", 12.0),
            worktree_hours=kwargs.get("worktree_hours", 1.0),
            merged_heads=lambda branch: known.get(branch),
        )

    def entries(self, **kwargs: object) -> dict[str, Entry]:
        """The plan, by worktree name."""
        entries, _ = plan(self.settings(**kwargs))  # type: ignore[arg-type]
        return {entry.name: entry for entry in entries}


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path)


def tip(path: Path) -> str:
    return git(path, "rev-parse", "HEAD")


def test_merged_worktree_is_removed_with_its_branch(space: Space) -> None:
    space.worktree("done", build=5000)
    entry = space.entries()["done"]
    assert entry.remove
    assert entry.reclaim >= 5000
    assert "merged" in entry.why


def test_apply_removes_worktree_branch_and_build(space: Space) -> None:
    path = space.worktree("done", build=5000)
    assert run_clean(space.checkout, space.root, True) == 0
    assert not path.exists()
    assert "done" not in git(space.checkout, "branch", "--list", "done")
    assert space.checkout.is_dir()


def test_dry_run_changes_nothing(space: Space, capsys: pytest.CaptureFixture[str]) -> None:
    path = space.worktree("done", build=5000)
    assert run_clean(space.checkout, space.root, False) == 0
    assert (path / "build" / "dev" / "out.bin").is_file()
    assert git(space.checkout, "branch", "--list", "done")
    out = capsys.readouterr().out
    assert "would remove" in out
    assert "Would reclaim" in out


def test_uncommitted_changes_keep_the_worktree(space: Space) -> None:
    modified = space.worktree("modified")
    (modified / "a.txt").write_text("changed\n", encoding="utf-8")
    untracked = space.worktree("untracked")
    (untracked / "notes.txt").write_text("mine\n", encoding="utf-8")
    entries = space.entries()
    assert not entries["modified"].remove
    assert "uncommitted" in entries["modified"].why
    assert not entries["untracked"].remove
    assert "uncommitted" in entries["untracked"].why


def test_unmerged_commits_keep_the_worktree(space: Space) -> None:
    path = space.worktree("wip", commits=2)
    entry = space.entries()["wip"]
    assert not entry.remove
    assert "not in origin/main" in entry.why  # GitHub was not asked: the safe side
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: []) == 0
    assert path.is_dir()
    assert git(space.checkout, "branch", "--list", "wip")


def test_squash_merged_pull_request_removes_worktree_and_branch(space: Space) -> None:
    path = space.worktree("squashed", commits=2)
    entry = space.entries(heads={"squashed": [tip(path)]})["squashed"]
    assert entry.remove
    assert "pull request merged" in entry.why
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: [tip(path)]) == 0
    assert not path.exists()
    assert not git(space.checkout, "branch", "--list", "squashed")


def test_commits_after_the_merged_pull_request_keep_the_worktree(space: Space) -> None:
    path = space.worktree("later", commits=1)
    merged_head = tip(path)
    (path / "more.txt").write_text("more\n", encoding="utf-8")
    git(path, "add", "-A")
    git(path, "commit", "-q", "-m", "after the merge")
    space.touch(path, OLD)
    entry = space.entries(heads={"later": [merged_head]})["later"]
    assert not entry.remove
    assert "unmerged" in entry.why


def test_a_pull_request_with_more_commits_covers_the_branch(space: Space) -> None:
    path = space.worktree("behind", commits=1)
    older = tip(path)
    (path / "more.txt").write_text("more\n", encoding="utf-8")
    git(path, "add", "-A")
    git(path, "commit", "-q", "-m", "second")
    newer = tip(path)
    git(path, "reset", "-q", "--hard", older)
    space.touch(path, OLD)
    assert space.entries(heads={"behind": [newer]})["behind"].remove


def test_unrelated_merged_pull_request_does_not_cover_the_branch(space: Space) -> None:
    space.worktree("reused", commits=1)
    entry = space.entries(heads={"reused": ["0" * 40]})["reused"]
    assert not entry.remove


def test_busy_worktree_is_untouched(space: Space) -> None:
    path = space.worktree("working", build=5000)
    (path / BUSY_FILE).write_text("", encoding="utf-8")
    entry = space.entries()["working"]
    assert not entry.remove
    assert not entry.delete_build
    assert BUSY_FILE in entry.why
    assert run_clean(space.checkout, space.root, True) == 0
    assert (path / "build" / "dev" / "out.bin").is_file()


def test_fresh_merged_worktree_is_kept(space: Space) -> None:
    space.worktree("just-started", age=0.1)
    entry = space.entries()["just-started"]
    assert not entry.remove
    assert "used 0.1 h ago" in entry.why


def test_old_build_of_an_unmerged_worktree_is_deleted_but_the_worktree_stays(space: Space) -> None:
    path = space.worktree("wip", commits=1, build=5000)
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None) == 0
    assert path.is_dir()
    assert (path / "wip-0.txt").is_file()
    assert not (path / "build").exists()


def test_recent_build_is_kept(space: Space) -> None:
    path = space.worktree("building", commits=1, build=5000)
    recent = time.time() - 3600
    os.utime(path / "build" / "dev" / "out.bin", (recent, recent))
    os.utime(path / "build" / "dev", (recent, recent))
    entry = space.entries()["building"]
    assert not entry.delete_build
    assert "build/ used" in entry.build_why


def test_build_threshold_follows_the_option(space: Space) -> None:
    path = space.worktree("wip", commits=1, build=100, age=5)
    assert not space.entries(build_hours=12)["wip"].delete_build
    assert space.entries(build_hours=2)["wip"].delete_build
    assert path.is_dir()


def test_build_that_git_does_not_ignore_is_left_alone(space: Space) -> None:
    path = space.worktree("odd", commits=1, build=100)
    (path / ".gitignore").write_text("", encoding="utf-8")  # no longer ignores build/
    git(path, "add", "-A")
    git(path, "commit", "-q", "-m", "unignore")
    space.touch(path, OLD)
    entry = space.entries()["odd"]
    assert not entry.delete_build
    assert "not ignored" in entry.build_why


def test_main_checkout_and_current_folder_are_never_touched(space: Space) -> None:
    (space.checkout / "build").mkdir()
    (space.checkout / "build" / "x.bin").write_bytes(b"\0" * 100)
    mine = space.worktree("mine", build=100)
    entries = space.entries(cwd=mine / "src" if (mine / "src").is_dir() else mine)
    assert not entries["mine"].remove
    assert not entries["mine"].delete_build
    assert "this runs from" in entries["mine"].why
    assert "coney" not in entries
    assert run_clean(space.checkout, space.root, True) == 0
    assert (space.checkout / "build" / "x.bin").is_file()


def test_worktrees_outside_the_workspace_are_ignored(space: Space, tmp_path_factory: pytest.TempPathFactory) -> None:
    elsewhere = tmp_path_factory.mktemp("elsewhere") / "other"
    git(space.checkout, "worktree", "add", "-q", str(elsewhere), "-b", "other", "origin/main")
    assert "other" not in space.entries()
    run_clean(space.checkout, space.root, True)
    assert elsewhere.is_dir()


def test_locked_worktree_is_kept(space: Space) -> None:
    path = space.worktree("locked")
    git(space.checkout, "worktree", "lock", str(path))
    assert not space.entries()["locked"].remove


def test_detached_head_worktree_that_is_merged_is_removed(space: Space) -> None:
    path = space.worktree("loose")
    git(path, "checkout", "-q", "--detach")
    space.touch(path, OLD)
    entry = space.entries()["loose"]
    assert entry.remove
    assert entry.branch is None


def test_read_only_files_and_deep_paths_are_deleted(space: Space) -> None:
    path = space.worktree("ro", commits=1, build=10)
    deep = path / "build" / "dev" / ("d" * 60) / ("e" * 60) / ("f" * 60) / ("g" * 60) / ("h" * 60)
    deep.mkdir(parents=True)
    locked = deep / "pack.bin"
    locked.write_bytes(b"x")
    locked.chmod(stat.S_IREAD)
    space.touch(path, OLD)
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None) == 0
    assert not (path / "build").exists()


def test_failed_removal_is_reported_and_exits_1(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    space.worktree("wip", commits=1, build=100)

    def refuse(path: Path) -> None:
        raise PermissionError("in use")

    monkeypatch.setattr(workspace_clean, "remove_tree", refuse)
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None) == 1
    assert "FAILED" in capsys.readouterr().out


def test_stale_worktree_entries_are_pruned_on_apply(space: Space) -> None:
    path = space.worktree("gone")
    shutil.rmtree(path)
    entries, stale = plan(space.settings())
    assert not entries
    assert [tree.path.name for tree in stale] == ["gone"]
    run_clean(space.checkout, space.root, True)
    assert "gone" not in git(space.checkout, "worktree", "list")


def test_report_names_free_space_and_folders_that_are_not_worktrees(
    space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    space.worktree("done", build=2000)
    (space.worktrees / "stray-folder").mkdir()
    frees = iter([10 * 1024**3, 12 * 1024**3])  # before and after
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100, 50, next(frees)))
    run_clean(space.checkout, space.root, True)
    out = capsys.readouterr().out
    assert "Free space: 10.0 GB" in out
    assert "Free space: 10.0 GB -> 12.0 GB" in out
    assert "Reclaimed" in out
    assert "stray-folder" in out


def test_missing_origin_main_is_an_error(tmp_path: Path) -> None:
    checkout = tmp_path / "repos" / "coney"
    checkout.mkdir(parents=True)
    git(checkout, "init", "-q", "-b", "main")
    with pytest.raises(ConfigError, match="origin/main"):
        run_clean(checkout, tmp_path, False)


def test_disk_exit_codes(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100 * 1024**3, 0, 31 * 1024**3))
    assert run_disk(tmp_path, tmp_path, 30) == 0
    assert "31.0 GB" in capsys.readouterr().out
    assert run_disk(tmp_path, tmp_path, 32) == 1
    assert "LOW" in capsys.readouterr().out


def test_cli_commands(space: Space, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    path = space.worktree("done", build=100)
    monkeypatch.setattr(workspace_clean, "gh_merged_heads", lambda checkout: lambda branch: None)
    monkeypatch.chdir(space.checkout)
    assert main(["workspace", "clean"]) == 0
    assert path.is_dir()
    assert "dry run" in capsys.readouterr().out
    assert main(["workspace", "clean", "--apply", "--build-hours", "1", "--worktree-hours", "0.5"]) == 0
    assert not path.exists()
    assert main(["workspace", "disk", "--min-gb", "0"]) == 0
    assert main(["workspace", "disk", "--min-gb", "100000000"]) == 1

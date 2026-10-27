# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.workspace_clean: `workspace clean` and `workspace check-space` on made-up git repositories."""

import json
import os
import shutil
import stat
import subprocess
import time
from collections import namedtuple
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

from coney_tools import stale_processes, workspace_clean
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
            live=workspace_clean.live_slots(self.root, self.checkout),
        )

    def entries(self, **kwargs: object) -> dict[str, Entry]:
        """The plan, by worktree name."""
        entries, _ = plan(self.settings(**kwargs))  # type: ignore[arg-type]
        return {entry.name: entry for entry in entries}


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path)


#: The machine's processes as `workspace clean` sees them in these tests (never the real ones: --apply stops them).
FAKE_PROCESSES: list[stale_processes.Proc] = []
#: The process trees a test's `workspace clean --apply` stopped.
STOPPED: list[int] = []


@pytest.fixture(autouse=True)
def no_real_processes(monkeypatch: pytest.MonkeyPatch) -> None:
    FAKE_PROCESSES.clear()
    STOPPED.clear()
    monkeypatch.setattr(stale_processes, "list_processes", lambda: list(FAKE_PROCESSES))
    monkeypatch.setattr(stale_processes, "kill_tree", lambda pid: STOPPED.append(pid))


@pytest.fixture(autouse=True)
def roomy_drive(monkeypatch: pytest.MonkeyPatch) -> None:
    """Unless a test says otherwise there is plenty of room: the machine's real free space decides nothing."""
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(1000 * 1024**3, 0, 500 * 1024**3))


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


def test_detached_head_with_unmerged_commits_is_kept(space: Space) -> None:
    path = space.worktree("loose-work", commits=1)
    git(path, "checkout", "-q", "--detach")
    space.touch(path, OLD)
    entry = space.entries()["loose-work"]
    assert not entry.remove
    assert "detached HEAD with 1 commit" in entry.why


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
    frees = iter([10 * 1024**3, 12 * 1024**3, 12 * 1024**3])  # before, at the disk-pressure check, after
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100, 50, next(frees)))
    run_clean(space.checkout, space.root, True, min_gb=0)
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
    assert main(["workspace", "check-space", "--min-gb", "0"]) == 0
    assert main(["workspace", "check-space", "--min-gb", "100000000"]) == 1


def test_disk_minimum_defaults_to_the_conventions(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    from conventions_support import changed, write_checkout

    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100 * 1024**3, 0, 35 * 1024**3))
    checkout = tmp_path / "repos" / "coney"
    checkout.mkdir(parents=True)
    write_checkout(checkout, changed(("workspace.disk_min_free_gb", 40)))
    assert run_disk(checkout, tmp_path) == 1  # 35 GB free, the file asks for 40
    write_checkout(checkout, changed(("workspace.disk_min_free_gb", 30)))
    assert run_disk(checkout, tmp_path) == 0
    assert run_disk(checkout, tmp_path, 36) == 1  # an explicit limit wins over the file


def test_disk_without_a_conventions_file_uses_the_built_in_minimum(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100 * 1024**3, 0, 29 * 1024**3))
    assert run_disk(tmp_path, tmp_path) == 1


def test_clean_honours_the_busy_marker_of_the_conventions(space: Space) -> None:
    from conventions_support import changed, write_checkout

    write_checkout(space.checkout, changed(("workspace.busy_marker", ".agent-busy")))
    kept = space.worktree("kept")
    (kept / ".agent-busy").write_text("", encoding="utf-8")
    removed = space.worktree("removed")
    (removed / ".coney-busy").write_text("", encoding="utf-8")
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None) == 0
    assert kept.is_dir() and not removed.exists()


# --- gh_merged_heads: one call for every worktree -------------------------------------------------------------------


class FakeGh:
    """A stand-in for `gh`: records each command and answers the merged-pull-request list from a table."""

    def __init__(self, merged: dict[str, list[str]], *, code: int = 0, text: str | None = None) -> None:
        self.merged = merged
        self.code = code
        self.text = text
        self.calls: list[list[str]] = []

    def __call__(self, command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
        self.calls.append(command)
        if self.text is not None:
            body = self.text
        else:
            body = json.dumps(
                [{"headRefName": name, "headRefOid": oid} for name, oids in self.merged.items() for oid in oids]
            )
        return subprocess.CompletedProcess(command, self.code, body, "boom" if self.code else "")


def heads_for(branch: str) -> list[str]:
    """The made-up merged heads of a branch: two pull requests for every third one, one for the rest."""
    index = int(branch.rsplit("-", 1)[1])
    return [f"{index:040x}", f"{index + 1000:040x}"] if index % 3 == 0 else [f"{index:040x}"]


def test_the_merged_state_of_100_worktrees_costs_one_gh_call(tmp_path: Path) -> None:
    merged = {f"branch-{n}": heads_for(f"branch-{n}") for n in range(0, 100, 2)}  # the even ones have pull requests
    gh = FakeGh(merged)
    lookup = workspace_clean.gh_merged_heads(tmp_path, gh)
    assert gh.calls == []  # nothing is asked until a worktree needs the answer
    answers = {f"branch-{n}": lookup(f"branch-{n}") for n in range(100)}
    assert len(gh.calls) == 1
    # The answers are the per-branch lookup's (`--head <branch>`): the heads of its merged pull requests, or none.
    for name, answer in answers.items():
        assert answer is not None and sorted(answer) == sorted(merged.get(name, []))


def test_the_one_call_asks_for_the_merged_pull_requests_of_every_branch(tmp_path: Path) -> None:
    gh = FakeGh({})
    workspace_clean.gh_merged_heads(tmp_path, gh)("any")
    (command,) = gh.calls
    assert command[:3] == ["gh", "pr", "list"] and "--head" not in command
    assert command[command.index("--state") + 1] == "merged"
    assert command[command.index("--json") + 1] == "headRefName,headRefOid"
    assert int(command[command.index("--limit") + 1]) >= 1000


@pytest.mark.parametrize(
    "gh",
    [FakeGh({}, code=1), FakeGh({}, text="not json"), FakeGh({}, text='{"a": 1}'), FakeGh({}, text='[{"x": 1}]')],
    ids=["exit code", "not json", "not a list", "no head names"],
)
def test_a_gh_that_fails_keeps_every_worktree_after_one_call(tmp_path: Path, gh: FakeGh) -> None:
    lookup = workspace_clean.gh_merged_heads(tmp_path, gh)
    assert [lookup(f"branch-{n}") for n in range(20)] == [None] * 20
    assert len(gh.calls) == 1


def test_a_missing_gh_is_never_run(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shutil, "which", lambda name: None)
    gh = FakeGh({})
    monkeypatch.setattr(workspace_clean, "_run_gh", gh)
    lookup = workspace_clean.gh_merged_heads(tmp_path)
    assert lookup("branch-1") is None and lookup("branch-2") is None
    assert gh.calls == []


def test_a_gh_that_cannot_be_started_keeps_every_worktree(tmp_path: Path) -> None:
    calls: list[list[str]] = []

    def runner(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
        calls.append(command)
        raise FileNotFoundError("gh")

    lookup = workspace_clean.gh_merged_heads(tmp_path, runner)
    assert lookup("a") is None and lookup("b") is None
    assert len(calls) == 1


def test_a_full_list_cannot_say_no_for_a_branch_it_does_not_hold(tmp_path: Path) -> None:
    """When GitHub returned as many pull requests as were asked for, an older one may be missing: unknown, so kept."""
    gh = FakeGh({"new": ["a" * 40], "newer": ["b" * 40]})
    lookup = workspace_clean.gh_merged_heads(tmp_path, gh, limit=2)
    assert lookup("new") == ["a" * 40]
    assert lookup("old") is None
    assert "--limit" in gh.calls[0] and gh.calls[0][gh.calls[0].index("--limit") + 1] == "2"
    assert len(gh.calls) == 1


def test_a_clean_of_several_worktrees_asks_github_once(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    """A real clean over three worktrees with commits asks GitHub once, and a merged pull request frees its own."""
    paths = {name: space.worktree(name, commits=1) for name in ("one", "two", "three")}
    gh = FakeGh({"one": [tip(paths["one"])], "two": ["f" * 40]})
    monkeypatch.setattr(workspace_clean, "_run_gh", gh)
    monkeypatch.setattr(shutil, "which", lambda name: "gh")
    assert run_clean(space.checkout, space.root, True) == 0
    assert len(gh.calls) == 1
    assert not paths["one"].exists() and paths["two"].is_dir() and paths["three"].is_dir()


def test_worktrees_judged_side_by_side_still_cost_one_gh_call(tmp_path: Path) -> None:
    """Threads that need the answer at the same moment wait for the one call instead of making their own."""
    gh = FakeGh({"branch-1": ["a" * 40]})
    slow = gh.__call__

    def slow_run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
        time.sleep(0.2)
        return slow(command, cwd)

    lookup = workspace_clean.gh_merged_heads(tmp_path, slow_run)
    with ThreadPoolExecutor(max_workers=8) as pool:
        answers = list(pool.map(lookup, [f"branch-{n}" for n in range(16)]))
    assert answers[1] == ["a" * 40] and answers[0] == [] and len(gh.calls) == 1


def test_clean_lists_and_with_apply_stops_stale_processes(space: Space, capsys: pytest.CaptureFixture[str]) -> None:
    now = time.time()
    FAKE_PROCESSES.append(stale_processes.Proc(4242, 999_999, "ctest", now - 600, "ctest --preset dev"))
    assert run_clean(space.checkout, space.root, False, merged_heads=lambda branch: None) == 0
    assert "would stop   ctest (pid 4242): its parent (pid 999999) is gone" in capsys.readouterr().out
    assert STOPPED == []
    assert run_clean(space.checkout, space.root, True, merged_heads=lambda branch: None) == 0
    assert STOPPED == [4242]


def test_the_process_limits_reach_clean(space: Space, monkeypatch: pytest.MonkeyPatch) -> None:
    seen: list[tuple[bool, float, float]] = []

    def run(apply: bool, shell: float, idle: float) -> int:
        seen.append((apply, shell, idle))
        return 0

    monkeypatch.setattr(stale_processes, "run", run)
    monkeypatch.chdir(space.checkout)
    assert main(["workspace", "clean", "--stale-wait-hours", "5", "--log-idle-hours", "2"]) == 0
    assert seen == [(False, 5.0, 2.0)]

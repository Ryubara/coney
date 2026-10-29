# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.build_root: where a checkout's build trees live, `build/` in the checkout unless
`build_root` in coney.local.toml moves them to `<build_root>/<id>/` on another drive (docs/guides/workspace.md)."""

import json
import os
import subprocess
from pathlib import Path

import pytest

from coney_tools import build_root
from coney_tools.config import ConfigError

GIT_ENV = {
    **os.environ,
    "GIT_AUTHOR_NAME": "t",
    "GIT_AUTHOR_EMAIL": "t@example.invalid",
    "GIT_COMMITTER_NAME": "t",
    "GIT_COMMITTER_EMAIL": "t@example.invalid",
}


def git(folder: Path, *args: str) -> str:
    """Run git in `folder` with a made-up identity; its output."""
    done = subprocess.run(["git", "-C", str(folder), *args], check=True, capture_output=True, text=True, env=GIT_ENV)
    return done.stdout.strip()


class Space:
    """A workspace with a main checkout at `repos/coney` and worktrees under `worktrees/coney/`."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.checkout = root / "repos" / "coney"
        self.checkout.mkdir(parents=True)
        git(self.checkout, "init", "-q", "-b", "main")
        (self.checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
        git(self.checkout, "add", "-A")
        git(self.checkout, "commit", "-q", "-m", "first")
        self.builds = root / "elsewhere" / "coney_builds"

    def configure(self, **keys: str) -> None:
        """Write the main checkout's coney.local.toml with `keys`."""
        text = "".join(f"{key} = '{value}'\n" for key, value in keys.items())
        (self.checkout / "coney.local.toml").write_text(text, encoding="utf-8")

    def with_root(self) -> None:
        """Set `build_root` to a folder outside the workspace."""
        self.configure(build_root=self.builds.as_posix())

    def worktree(self, name: str) -> Path:
        """A worktree on a new branch `name`."""
        path = self.root / "worktrees" / "coney" / name
        git(self.checkout, "worktree", "add", "-q", str(path), "-b", name)
        return path

    def unit(self, task: str, worktree: Path) -> None:
        """The unit record `task start` writes for `task` in the workspace's scratch folder."""
        folder = self.root / "scratch" / task
        folder.mkdir(parents=True)
        record = {
            "v": 1,
            "task": task,
            "kind": "tooling",
            "issues": [1],
            "claimant": "someone",
            "branch": task,
            "worktree": worktree.as_posix(),
            "base_ref": "origin/main",
            "base_commit": "0" * 40,
            "started": "2026-10-29T00:00:00Z",
            "parent": "",
        }
        (folder / "unit.json").write_text(json.dumps(record), encoding="utf-8")


@pytest.fixture
def space(tmp_path: Path) -> Space:
    return Space(tmp_path)


# --- unset: exactly today's paths --------------------------------------------------------------------------------


def test_without_a_build_root_the_tree_is_build_in_the_checkout(space: Space) -> None:
    space.configure()
    tree = space.worktree("1-a")
    assert build_root.configured(tree) is None
    assert build_root.tree(tree) == tree / "build"
    assert build_root.tree(tree, create=True) == tree / "build"
    assert build_root.preset_folder(tree, "dev") == tree / "build" / "dev"


def test_without_a_build_root_nothing_is_written(space: Space) -> None:
    space.configure()
    tree = space.worktree("1-a")
    build_root.tree(tree, create=True)
    assert not (tree / "build").exists()
    assert build_root.read_id(tree) is None
    assert git(tree, "status", "--porcelain", "--ignored") == ""


def test_without_a_local_config_the_tree_is_build_in_the_checkout(space: Space) -> None:
    tree = space.worktree("1-a")
    assert build_root.configured(tree) is None
    assert build_root.tree(tree, create=True) == tree / "build"


def test_without_a_build_root_a_custom_folder_is_as_given(space: Space) -> None:
    space.configure()
    tree = space.worktree("1-a")
    assert build_root.custom_folder(tree, Path("build/tidy")) == (tree / "build" / "tidy").resolve()
    assert build_root.custom_folder(tree, Path("out/x")) == (tree / "out" / "x").resolve()


def test_without_a_build_root_the_folders_are_the_checkouts_build(space: Space) -> None:
    space.configure()
    tree = space.worktree("1-a")
    assert build_root.folders(tree, None) == []
    (tree / "build" / "dev").mkdir(parents=True)
    assert build_root.folders(tree, None) == [tree / "build"]


# --- set: <build_root>/<id>/ --------------------------------------------------------------------------------------


def test_a_unit_worktree_builds_under_its_task_id(space: Space) -> None:
    space.with_root()
    tree = space.worktree("1133-a-task")
    space.unit("1133-a-task", tree)
    assert build_root.tree(tree, create=True) == space.builds / "1133-a-task"
    assert build_root.preset_folder(tree, "ci", create=True) == space.builds / "1133-a-task" / "ci"
    assert (space.builds / "1133-a-task").is_dir()


def test_a_worktree_without_a_unit_gets_an_id_written_once(space: Space) -> None:
    space.with_root()
    tree = space.worktree("loose")
    first = build_root.tree(tree, create=True)
    assert first.parent == space.builds
    assert first.name.startswith("loose-")
    assert build_root.read_id(tree) == first.name
    assert build_root.tree(tree, create=True) == first


def test_the_id_stays_when_the_branch_is_renamed(space: Space) -> None:
    space.with_root()
    tree = space.worktree("1133-a-task")
    space.unit("1133-a-task", tree)
    first = build_root.tree(tree, create=True)
    git(tree, "branch", "-m", "another-name")
    assert build_root.tree(tree, create=True) == first


def test_the_id_stays_when_the_worktree_is_moved(space: Space) -> None:
    space.with_root()
    tree = space.worktree("loose")
    first = build_root.tree(tree, create=True)
    moved = space.root / "worktrees" / "coney" / "moved"
    git(space.checkout, "worktree", "move", str(tree), str(moved))
    assert build_root.tree(moved, create=True) == first
    assert build_root.owner(first) == moved.resolve()


def test_the_id_lives_outside_the_working_tree(space: Space) -> None:
    space.with_root()
    tree = space.worktree("loose")
    build_root.tree(tree, create=True)
    assert git(tree, "status", "--porcelain", "--ignored") == ""


def test_the_build_folder_names_its_worktree(space: Space) -> None:
    space.with_root()
    tree = space.worktree("loose")
    folder = build_root.tree(tree, create=True)
    assert build_root.owner(folder) == tree.resolve()
    assert build_root.owner(space.builds) is None  # no marker


def test_a_read_without_create_writes_nothing(space: Space) -> None:
    space.with_root()
    tree = space.worktree("loose")
    assert build_root.tree(tree) == tree / "build"  # no id yet: nothing was built in the root
    assert build_root.read_id(tree) is None
    assert not space.builds.exists()


def test_a_unit_id_held_by_another_worktree_is_not_shared(space: Space) -> None:
    space.with_root()
    one = space.worktree("1-a")
    space.unit("1-a", one)
    taken = build_root.tree(one, create=True)
    other = space.worktree("other")
    # A unit record that names the other worktree with the same task (a moved unit): it may not take the folder.
    (space.root / "scratch" / "1-a" / "unit.json").unlink()
    (space.root / "scratch" / "1-a").rmdir()
    space.unit("1-a", other)
    git(other, "branch", "-m", "1-a-again")
    assert build_root.tree(other, create=True) != taken


def test_a_custom_folder_under_build_moves_with_the_tree(space: Space) -> None:
    space.with_root()
    tree = space.worktree("1-a")
    space.unit("1-a", tree)
    assert build_root.custom_folder(tree, Path("build/clang"), create=True) == space.builds / "1-a" / "clang"
    assert build_root.custom_folder(tree, Path("out/x")) == (tree / "out" / "x").resolve()
    absolute = space.root / "abs"
    assert build_root.custom_folder(tree, absolute) == absolute.resolve()


def test_the_folders_are_the_old_build_and_the_one_in_the_root(space: Space) -> None:
    space.with_root()
    tree = space.worktree("1-a")
    (tree / "build" / "dev").mkdir(parents=True)
    folder = build_root.tree(tree, create=True)
    assert build_root.folders(tree, space.builds) == [tree / "build", folder]


def test_a_relative_build_root_is_relative_to_the_main_checkout(space: Space) -> None:
    space.configure(build_root="../../builds")
    tree = space.worktree("1-a")
    assert build_root.configured(tree) == (space.root / "builds").resolve()


def test_a_build_root_inside_a_checkout_is_refused(space: Space) -> None:
    space.configure(build_root="inside")
    tree = space.worktree("1-a")
    with pytest.raises(ConfigError, match="build_root"):
        build_root.configured(tree)


def test_orphans_are_marked_folders_whose_worktree_is_gone(space: Space) -> None:
    space.with_root()
    live = space.worktree("live")
    gone = space.worktree("gone")
    kept = build_root.tree(live, create=True)
    lost = build_root.tree(gone, create=True)
    git(space.checkout, "worktree", "remove", "--force", str(gone))
    (space.builds / "hand-made").mkdir()
    found = build_root.survey(space.builds, {build_root.read_id(live) or ""})
    assert [item.path for item in found.orphans] == [lost]
    assert [item.path for item in found.unmarked] == [space.builds / "hand-made"]
    assert kept not in [item.path for item in found.orphans]

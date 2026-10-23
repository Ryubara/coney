# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the unit's folders: the task name, where its stamps, studies and pairs live, and addresses."""

from pathlib import Path

import pytest
from git_support import git

from coney_tools import unit
from coney_tools.config import ConfigError


def repo(tmp_path: Path, branch: str = "fight-resume") -> Path:
    """A made-up workspace: `<ws>/repos/coney` is a checkout on `branch`."""
    root = tmp_path / "repos" / "coney"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    git(root, "init", "-q", "-b", branch)
    (root / "f.txt").write_text("x", encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    return root


def test_the_task_comes_from_the_option_then_the_environment_then_the_branch(tmp_path: Path) -> None:
    root = repo(tmp_path)
    assert unit.task_name(root, "given", {"CONEY_TASK": "env"}) == "given"
    assert unit.task_name(root, None, {"CONEY_TASK": "env"}) == "env"
    assert unit.task_name(root, None, {}) == "fight-resume"


def test_a_branch_with_a_slash_and_a_detached_head_still_name_a_folder(tmp_path: Path) -> None:
    root = repo(tmp_path, "feature/x")
    assert unit.task_name(root, None, {}) == "feature-x"
    git(root, "checkout", "-q", "--detach")
    assert unit.task_name(root, None, {}) == "coney"


@pytest.mark.parametrize("bad", ["", "..", "a/b", "a\b", ".hidden"])
def test_a_task_name_cannot_leave_the_scratch_folder(tmp_path: Path, bad: str) -> None:
    with pytest.raises(ConfigError, match="task"):
        unit.task_name(repo(tmp_path), bad, {})


def test_the_folders_of_a_unit(tmp_path: Path) -> None:
    root = repo(tmp_path)
    folder = unit.Unit.of(root, "fight-resume")
    scratch = tmp_path / "scratch" / "fight-resume"
    assert folder.dir == scratch
    assert folder.stamps == scratch / "stamps"
    assert folder.study("0x00230d00") == scratch / "study" / "0x00230d00"
    assert folder.mapping("0x00230d00") == scratch / "mapping" / "0x00230d00.toml"
    assert folder.compare("0x00230d00") == scratch / "compare" / "0x00230d00.txt"


@pytest.mark.parametrize(
    ("text", "want"),
    [("0x230d00", "0x00230d00"), ("230D00", "0x00230d00"), ("0X00230d00", "0x00230d00"), ("0x1", "0x00000001")],
)
def test_addresses_are_written_one_way(text: str, want: str) -> None:
    assert unit.address(text) == want


@pytest.mark.parametrize("bad", ["", "0x", "xyz", "0x123456789", "-1", "0x12 34"])
def test_a_bad_address_is_refused(bad: str) -> None:
    with pytest.raises(ConfigError, match="address"):
        unit.address(bad)


def test_head_and_dirty_paths(tmp_path: Path) -> None:
    root = repo(tmp_path)
    head = unit.head_commit(root)
    assert len(head) == 40
    assert unit.dirty_paths(root, ["f.txt"]) == []
    (root / "f.txt").write_text("y", encoding="utf-8")
    assert unit.dirty_paths(root, ["f.txt"]) == ["f.txt"]
    (root / "new.txt").write_text("n", encoding="utf-8")
    assert unit.dirty_paths(root, ["new.txt", "f.txt"]) == ["new.txt", "f.txt"]

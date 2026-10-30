# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `shell_support.git_bash`: which `bash` a test runs a shell script with.

On Windows with the Windows Subsystem for Linux installed, `shutil.which("bash")` can be System32's `bash.exe`,
a Linux that sees the checkout as `/mnt/c/...` and has no `uv`. The helper must never return that one: it returns the
bash that ships with Git for Windows, found beside `git`, or None.
"""

import shutil
from pathlib import Path

import pytest
import shell_support


def fake_git_for_windows(root: Path) -> Path:
    """A made-up Git for Windows folder (git in `cmd`, bash in `usr/bin`); returns the path of its `git.exe`."""
    (root / "cmd").mkdir(parents=True)
    (root / "usr" / "bin").mkdir(parents=True)
    (root / "usr" / "bin" / "bash.exe").write_text("", encoding="utf-8")
    git = root / "cmd" / "git.exe"
    git.write_text("", encoding="utf-8")
    return git


def fake_which(monkeypatch: pytest.MonkeyPatch, found: dict[str, Path]) -> None:
    """Make `shutil.which` answer from `found`, and None for any other name."""
    monkeypatch.setattr(shutil, "which", lambda name: str(found[name]) if name in found else None)


def test_on_windows_the_bash_beside_git_wins_over_the_one_found_first(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    git = fake_git_for_windows(tmp_path / "Git")
    wsl = tmp_path / "System32" / "bash.exe"
    monkeypatch.setattr(shell_support, "ON_WINDOWS", True)
    fake_which(monkeypatch, {"bash": wsl, "git": git})
    assert shell_support.git_bash() == str(tmp_path / "Git" / "usr" / "bin" / "bash.exe")


def test_on_windows_the_bin_folder_of_git_counts_too(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    root = tmp_path / "Git"
    git = fake_git_for_windows(root)
    (root / "usr" / "bin" / "bash.exe").unlink()
    (root / "bin").mkdir()
    (root / "bin" / "bash.exe").write_text("", encoding="utf-8")
    monkeypatch.setattr(shell_support, "ON_WINDOWS", True)
    fake_which(monkeypatch, {"git": git})
    assert shell_support.git_bash() == str(root / "bin" / "bash.exe")


def test_on_windows_with_only_the_wsl_bash_there_is_none(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    wsl = tmp_path / "System32" / "bash.exe"
    wsl.parent.mkdir()
    wsl.write_text("", encoding="utf-8")
    monkeypatch.setattr(shell_support, "ON_WINDOWS", True)
    fake_which(monkeypatch, {"bash": wsl})
    assert shell_support.git_bash() is None
    assert "WSL" in shell_support.no_bash_reason()


def test_on_windows_a_git_without_a_bash_beside_it_gives_none(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    git = fake_git_for_windows(tmp_path / "Git")
    (tmp_path / "Git" / "usr" / "bin" / "bash.exe").unlink()
    monkeypatch.setattr(shell_support, "ON_WINDOWS", True)
    fake_which(monkeypatch, {"bash": tmp_path / "System32" / "bash.exe", "git": git})
    assert shell_support.git_bash() is None


def test_elsewhere_the_bash_on_the_path_is_the_one(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(shell_support, "ON_WINDOWS", False)
    fake_which(monkeypatch, {"bash": tmp_path / "bin" / "bash"})
    assert shell_support.git_bash() == str(tmp_path / "bin" / "bash")
    fake_which(monkeypatch, {})
    assert shell_support.git_bash() is None
    assert "bash" in shell_support.no_bash_reason()

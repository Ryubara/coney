# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.workspace: `coney-tools workspace init` on made-up folders (no network)."""

import os
import subprocess
import tomllib
from pathlib import Path

import pytest

from coney_tools import workspace
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.workspace import WORKSPACE_DIRS, fill_config, find_layout

EXAMPLE = """# Copy to coney.local.toml.
game_dir        = "../../game"                          # your disc image
jdk_home        = ""                                    # JDK 21 home; required for Ghidra
ghidra_mcp_repo = ""                                    # a built ghidra-mcp checkout
pcsx2_mcp_dir   = ""                                    # PCSX2-MCP's Node.js server
"""

GIT_ENV = {
    **os.environ,
    "GIT_AUTHOR_NAME": "t",
    "GIT_AUTHOR_EMAIL": "t@example.invalid",
    "GIT_COMMITTER_NAME": "t",
    "GIT_COMMITTER_EMAIL": "t@example.invalid",
}


def git(folder: Path, *args: str) -> None:
    """Run git in `folder` with a made-up identity."""
    subprocess.run(["git", "-C", str(folder), *args], check=True, capture_output=True, env=GIT_ENV)


def make_upstream(folder: Path) -> Path:
    """A made-up Coney repository: a git repository holding only the example config."""
    folder.mkdir(parents=True)
    (folder / "coney.local.example.toml").write_text(EXAMPLE, encoding="utf-8")
    git(folder, "init", "-q")
    git(folder, "add", "coney.local.example.toml")
    git(folder, "commit", "-q", "-m", "x")
    return folder


@pytest.fixture(autouse=True)
def no_real_checks(monkeypatch: pytest.MonkeyPatch) -> None:
    """Skip the prerequisite checks, which would look at this machine's tools."""
    monkeypatch.setattr(workspace, "gather_checks", lambda jdk_home: [])


def test_layout_inside_the_main_checkout(tmp_path: Path) -> None:
    checkout = tmp_path / "ws" / "repos" / "coney"
    checkout.mkdir(parents=True)
    (checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
    (checkout / "src").mkdir()
    layout = find_layout(checkout / "src", None)
    assert layout.workspace == (tmp_path / "ws").resolve()
    assert layout.checkout == checkout.resolve()
    assert layout.inside


def test_layout_in_a_worktree_is_the_main_checkouts(tmp_path: Path) -> None:
    main_checkout = make_upstream(tmp_path / "ws" / "repos" / "coney")
    worktree = tmp_path / "ws" / "worktrees" / "coney" / "branch"
    git(main_checkout, "worktree", "add", "-q", "--detach", str(worktree))
    layout = find_layout(worktree, None)
    assert layout.workspace == (tmp_path / "ws").resolve()
    assert layout.checkout == main_checkout.resolve()


def test_layout_of_a_checkout_outside_repos_needs_a_workspace(tmp_path: Path) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    with pytest.raises(ConfigError, match="--workspace"):
        find_layout(tmp_path, None)


def test_layout_outside_a_checkout_is_the_current_folder(tmp_path: Path) -> None:
    layout = find_layout(tmp_path, None)
    assert layout.workspace == tmp_path.resolve()
    assert layout.checkout == tmp_path.resolve() / "repos" / "coney"
    assert not layout.inside


def test_fill_config_sets_only_empty_keys_and_keeps_comments() -> None:
    text = fill_config(EXAMPLE, {"jdk_home": "C:/jdk-21", "game_dir": "elsewhere"})
    assert "jdk_home        = 'C:/jdk-21'" in text
    assert "# JDK 21 home; required for Ghidra" in text
    # A key that already has a value keeps it.
    assert 'game_dir        = "../../game"' in text
    assert 'ghidra_mcp_repo = ""' in text


@pytest.mark.parametrize("value", ["C:/Users/O'Brien/jdk-21", r'C:\a "b"\jdk'])
def test_fill_config_writes_any_path_as_valid_toml(value: str) -> None:
    # A literal string cannot hold a ', so such a path is written as an escaped basic string.
    text = fill_config(EXAMPLE, {"jdk_home": value})
    assert tomllib.loads(text)["jdk_home"] == value


def test_init_makes_the_workspace_clones_and_writes_the_config(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    upstream = make_upstream(tmp_path / "upstream")
    ws = tmp_path / "ws"
    ws.mkdir()
    monkeypatch.chdir(ws)
    assert main(["workspace", "init", "--repo-url", str(upstream)]) == 0
    for folder in WORKSPACE_DIRS:
        assert (ws / folder).is_dir(), folder
    config = ws / "repos" / "coney" / "coney.local.toml"
    assert config.is_file()
    # The disc is the player's own: init never creates or fills anything with game data.
    assert list((ws / "game").iterdir()) == []
    capsys.readouterr()

    # Idempotent: a second run changes nothing and keeps an edited config.
    config.write_text('game_dir = "mine"\n', encoding="utf-8")
    assert main(["workspace", "init", "--repo-url", str(upstream)]) == 0
    assert config.read_text(encoding="utf-8") == 'game_dir = "mine"\n'
    out = capsys.readouterr().out
    assert "create" not in out and "clone" not in out


def test_init_inside_a_checkout_needs_no_url(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    checkout = make_upstream(tmp_path / "ws" / "repos" / "coney")
    monkeypatch.chdir(checkout)
    assert main(["workspace", "init"]) == 0
    assert (tmp_path / "ws" / "scratch").is_dir()
    assert "game_dir" in (checkout / "coney.local.toml").read_text(encoding="utf-8")


def test_dry_run_changes_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(tmp_path)
    assert main(["workspace", "init", "--repo-url", "https://example.invalid/coney.git", "--dry-run"]) == 0
    assert list(tmp_path.iterdir()) == []
    out = capsys.readouterr().out
    assert "would clone" in out and "would create" in out


def test_dry_run_lists_the_fetches(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    checkout = make_upstream(tmp_path / "ws" / "repos" / "coney")
    monkeypatch.chdir(checkout)
    assert main(["workspace", "init", "--fetch", "all", "--dry-run"]) == 0
    out = capsys.readouterr().out
    for name in ("Ghidra", "PCSX2", "ghidra-mcp", "PCSX2-MCP"):
        assert name in out
    assert not (tmp_path / "ws" / "tools").exists()


def test_outside_a_checkout_without_a_url_says_how(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(tmp_path)
    assert main(["workspace", "init"]) == 2
    assert "--repo-url" in capsys.readouterr().err


def test_a_missing_required_tool_fails_after_the_work(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    from coney_tools.workspace_checks import Check

    checkout = make_upstream(tmp_path / "ws" / "repos" / "coney")
    monkeypatch.chdir(checkout)
    monkeypatch.setattr(
        workspace, "gather_checks", lambda jdk_home: [Check("Ninja", False, "not found", "install it", True)]
    )
    assert main(["workspace", "init"]) == 1
    assert (tmp_path / "ws" / "scratch").is_dir()
    assert "install it" in capsys.readouterr().out

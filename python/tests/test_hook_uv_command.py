# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for how the hooks run `coney-tools` (`hooklib.coney_tools_command`).

The slot guard, the slot fill guard and the PR guard use it.

`uv run` syncs the project's environment before it starts the command. On Windows that sync fails while the session's
own `coney-agent.exe` runs from `python/.venv/Scripts` (the file is locked, "Access is denied"), so a hook that syncs
fails open on every call for as long as the session lives. A hook therefore runs `--no-sync` whenever the environment
already exists, and lets `uv` build it the first time (issue 1183).
"""

import subprocess
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO
from hooks_support import load_hook_module
from test_pr_guard import GUARD as PR_GUARD
from test_slot_fill_guard import GUARD as SLOT_FILL_GUARD
from test_slot_guard import GUARD as SLOT_GUARD

hooklib = load_hook_module("hooklib")


def make_environment(folder: Path) -> Path:
    """A folder `uv` would take for a built environment (it holds `pyvenv.cfg`)."""
    folder.mkdir(parents=True)
    (folder / "pyvenv.cfg").write_text("home = elsewhere\n", encoding="utf-8")
    return folder


def test_a_built_environment_is_not_synced(tmp_path: Path) -> None:
    make_environment(tmp_path / "python" / ".venv")
    command = hooklib.coney_tools_command(tmp_path, ["pr", "check-title", "-"])
    assert command[:6] == ["uv", "run", "--quiet", "--no-sync", "--project", str(tmp_path / "python")]
    assert command[6:] == ["coney-tools", "pr", "check-title", "-"]


def test_no_environment_yet_is_built_by_the_first_run(tmp_path: Path) -> None:
    command = hooklib.coney_tools_command(tmp_path, ["task", "slots"])
    assert "--no-sync" not in command
    assert command[:5] == ["uv", "run", "--quiet", "--project", str(tmp_path / "python")]


def test_an_empty_environment_folder_is_not_a_built_one(tmp_path: Path) -> None:
    (tmp_path / "python" / ".venv").mkdir(parents=True)
    assert "--no-sync" not in hooklib.coney_tools_command(tmp_path, ["task"])


def test_the_environment_the_caller_names_is_the_one_that_counts(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    make_environment(tmp_path / "python" / ".venv")
    monkeypatch.setenv("UV_PROJECT_ENVIRONMENT", str(tmp_path / "private"))
    assert "--no-sync" not in hooklib.coney_tools_command(tmp_path, ["task"])  # the named one is not built yet
    make_environment(tmp_path / "private")
    assert "--no-sync" in hooklib.coney_tools_command(tmp_path, ["task"])


def test_a_relative_environment_is_taken_from_the_project_folder(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    make_environment(tmp_path / "python" / "env")
    monkeypatch.setenv("UV_PROJECT_ENVIRONMENT", "env")
    assert "--no-sync" in hooklib.coney_tools_command(tmp_path, ["task"])


def test_the_slot_guard_runs_the_tool_without_a_sync_once_the_environment_is_built(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    make_environment(tmp_path / "python" / ".venv")
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(tmp_path))
    assert SLOT_GUARD.tool_command(["task", "slots", "--admit"])[:4] == ["uv", "run", "--quiet", "--no-sync"]


def test_the_slot_fill_guard_runs_the_tool_without_a_sync_once_the_environment_is_built(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    make_environment(tmp_path / "python" / ".venv")
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(tmp_path))
    command = SLOT_FILL_GUARD.tool_command(["task", "slots", "--fill-check"])
    assert command[:4] == ["uv", "run", "--quiet", "--no-sync"]


def test_the_pr_guard_runs_its_checks_without_a_sync_once_the_environment_is_built(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    make_environment(tmp_path / "python" / ".venv")
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(tmp_path))
    seen: dict[str, Any] = {}

    def fake_run(command: list[str], **options: Any) -> subprocess.CompletedProcess[str]:
        seen["command"] = command
        return subprocess.CompletedProcess(command, 0, "", "")

    monkeypatch.setattr("subprocess.run", fake_run)
    assert PR_GUARD.run_coney_tools(["pr", "check-title", "-"], "title\n") == (0, "")
    assert seen["command"][:4] == ["uv", "run", "--quiet", "--no-sync"]


def test_every_uv_run_of_the_hooks_that_starts_coney_tools_goes_through_the_helper() -> None:
    # A new guard that builds its own `uv run ... coney-tools` command would sync again and fail on a locked launcher.
    # The dispatcher's own `uv run --no-project python` has no environment to sync, so it is not one of them.
    offenders = [
        path.name
        for path in sorted((REPO / ".claude" / "hooks").glob("*.py"))
        if path.name != "hooklib.py" and '"uv", "run"' in path.read_text(encoding="utf-8")
    ]
    assert offenders == []

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.session_launcher, the `coney-agent` session launcher (docs/guides/agents.md#session-launcher).

Claude Code is never started: the session launcher takes a runner, and the checkouts are temporary folders.
"""

import os
import signal
import subprocess
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import session_launcher


@pytest.fixture
def checkout(tmp_path: Path) -> Path:
    """A folder with the files that make it a Coney checkout: `AGENTS.md` and three role files."""
    root = tmp_path / "coney"
    (root / ".claude" / "agents").mkdir(parents=True)
    (root / "AGENTS.md").write_text("# x\n", encoding="utf-8")
    for name in ("coordinator", "engineer", "reviewer"):
        (root / ".claude" / "agents" / f"{name}.md").write_text(f"---\nname: {name}\n---\n", encoding="utf-8")
    return root


class Recorder:
    """A runner that records the command and folder instead of starting Claude Code."""

    def __init__(self, code: int = 0) -> None:
        self.calls: list[tuple[list[str], Path]] = []
        self.code = code

    def __call__(self, command: list[str], folder: Path) -> int:
        self.calls.append((command, folder))
        return self.code


def run(argv: list[str], folder: Path, runner: Recorder) -> int:
    """Run the session launcher from `folder` with `claude` found at a made-up path."""
    return session_launcher.main(argv, start=folder, runner=runner, which=lambda name: f"/bin/{name}")


def test_the_default_role_is_the_coordinator(checkout: Path) -> None:
    runner = Recorder()
    assert run([], checkout, runner) == 0
    assert runner.calls == [(["/bin/claude", "--agent", "coordinator"], checkout)]


def test_another_role_and_options_for_claude_after_two_dashes(checkout: Path) -> None:
    runner = Recorder(3)
    assert run(["engineer", "--", "--model", "opus", "--resume"], checkout, runner) == 3
    assert runner.calls[0][0] == ["/bin/claude", "--agent", "engineer", "--model", "opus", "--resume"]


def test_it_starts_from_the_checkout_that_holds_the_current_folder(checkout: Path) -> None:
    runner = Recorder()
    inner = checkout / "src" / "core"
    inner.mkdir(parents=True)
    run([], inner, runner)
    assert runner.calls[0][1] == checkout


def test_outside_any_checkout_it_uses_its_own(tmp_path: Path) -> None:
    runner = Recorder()
    run([], tmp_path, runner)
    assert runner.calls[0][1] == REPO


def test_an_unknown_role_is_refused_with_the_list(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    runner = Recorder()
    assert run(["general-purpose"], checkout, runner) == 2
    err = capsys.readouterr().err
    assert runner.calls == [] and "coordinator, engineer, reviewer" in err


def test_the_mod_is_loaded_when_its_folder_exists(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    runner = Recorder()
    run([], checkout, runner)
    assert "--plugin-dir" not in runner.calls[0][0]
    assert "mod" in capsys.readouterr().err  # said once: the checkout has no mod folder yet
    (checkout / session_launcher.MOD_DIR).mkdir(parents=True)
    run([], checkout, runner)
    command = runner.calls[1][0]
    assert command[command.index("--plugin-dir") + 1] == str(checkout / session_launcher.MOD_DIR)


def test_the_mod_folder_can_be_named(checkout: Path, tmp_path: Path) -> None:
    runner = Recorder()
    other = tmp_path / "elsewhere"
    other.mkdir()
    run(["--mod-dir", str(other)], checkout, runner)
    command = runner.calls[0][0]
    assert command[command.index("--plugin-dir") + 1] == str(other)


def test_dry_run_prints_the_command_and_starts_nothing(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    runner = Recorder()
    assert run(["--dry-run", "reviewer"], checkout, runner) == 0
    out = capsys.readouterr().out
    assert runner.calls == [] and "claude --agent reviewer" in out and str(checkout) in out


def test_an_explicit_mod_folder_that_is_not_there_is_an_error(
    checkout: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # Only the default folder may be missing (the mod is not in every checkout); naming one is a request.
    runner = Recorder()
    assert run(["--mod-dir", "nowhere/mod"], checkout, runner) == 2
    assert runner.calls == [] and "nowhere" in capsys.readouterr().err
    assert run(["--dry-run", "--mod-dir", "nowhere/mod"], checkout, runner) == 2


def test_dry_run_prints_a_command_a_shell_can_run(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    runner = Recorder()
    folder = checkout.parent / "a folder with spaces"
    folder.mkdir()
    assert (
        run(
            ["--dry-run", "--mod-dir", str(folder), "--", "--append-system-prompt", "two words; and a $sign"],
            checkout,
            runner,
        )
        == 0
    )
    out = capsys.readouterr().out
    expected = session_launcher.quote(["/bin/claude", "--agent", "coordinator", "--plugin-dir", str(folder)])
    assert expected in out
    assert session_launcher.quote(["a b", "c;d", "$e"], posix=True) == "'a b' 'c;d' '$e'"
    assert session_launcher.quote(["a b", "c"], posix=False) == subprocess.list2cmdline(["a b", "c"])


def test_without_claude_on_the_path_it_says_so(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    runner = Recorder()
    assert session_launcher.main([], start=checkout, runner=runner, which=lambda name: None) == 2
    assert "claude" in capsys.readouterr().err and runner.calls == []


def test_on_posix_claude_takes_over_the_process_so_ctrl_c_reaches_only_it(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    # A child under Python would get Ctrl+C in Python too and die with a traceback of its own on Linux and macOS.
    seen: list[tuple[str, list[str], Path]] = []

    def fake_exec(file: str, argv: list[str]) -> None:
        seen.append((file, argv, Path.cwd()))

    monkeypatch.setattr(os, "execvp", fake_exec)
    monkeypatch.chdir(tmp_path)
    session_launcher.run_claude(["claude", "--agent", "x"], tmp_path, posix=True)
    assert seen == [("claude", ["claude", "--agent", "x"], tmp_path)]


def test_elsewhere_the_session_launcher_ignores_ctrl_c_while_claude_runs(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    during: list[object] = []

    def fake(command: list[str], cwd: Path | None = None, check: bool = False) -> subprocess.CompletedProcess[str]:
        during.append(signal.getsignal(signal.SIGINT))
        assert (command, cwd) == (["claude", "--agent", "x"], tmp_path)
        return subprocess.CompletedProcess(command, 7)

    before = signal.getsignal(signal.SIGINT)
    monkeypatch.setattr(subprocess, "run", fake)
    assert session_launcher.run_claude(["claude", "--agent", "x"], tmp_path, posix=False) == 7
    assert during == [signal.SIG_IGN] and signal.getsignal(signal.SIGINT) == before


def test_a_relative_mod_folder_is_taken_from_where_the_person_stands(checkout: Path, tmp_path: Path) -> None:
    # claude runs in the checkout, so a path relative to the caller's folder must be made absolute first.
    elsewhere = tmp_path / "work"
    (elsewhere / "mod").mkdir(parents=True)
    runner = Recorder()
    session_launcher.main(["--mod-dir", "mod"], start=elsewhere, runner=runner, which=lambda name: f"/bin/{name}")
    command = runner.calls[0][0]
    assert command[command.index("--plugin-dir") + 1] == str(elsewhere / "mod")


def test_the_wrappers_run_the_console_script() -> None:
    cmd = (REPO / "tools" / "coney-agent.cmd").read_text(encoding="utf-8")
    sh = (REPO / "tools" / "coney-agent").read_text(encoding="utf-8")
    ps1 = (REPO / "tools" / "coney-agent.ps1").read_text(encoding="utf-8")
    for wrapper in (cmd, sh, ps1):
        assert (
            "coney-agent" in wrapper and "--project" in wrapper and "docs/guides/coney-tools.md#coney-agent" in wrapper
        )
    # cmd.exe asks "Terminate batch job (Y/N)?" on Ctrl+C: said where it is read, and the PowerShell one is offered.
    assert "Terminate batch job" in cmd and "coney-agent.ps1" in cmd
    assert "exit $LASTEXITCODE" in ps1


@pytest.mark.skipif(os.name == "nt", reason="needs a symbolic link and sh")
def test_the_sh_wrapper_follows_a_symbolic_link_to_find_the_project(tmp_path: Path) -> None:
    bin_folder = tmp_path / "bin"
    bin_folder.mkdir()
    fake_uv = bin_folder / "uv"
    fake_uv.write_text('#!/bin/sh\necho "uv $*"\n', encoding="utf-8")
    fake_uv.chmod(0o755)
    link = bin_folder / "coney-agent"
    link.symlink_to(REPO / "tools" / "coney-agent")
    done = subprocess.run(
        ["sh", str(link), "engineer"],
        capture_output=True,
        text=True,
        env={**os.environ, "PATH": f"{bin_folder}{os.pathsep}{os.environ['PATH']}"},
        check=False,
    )
    assert done.returncode == 0
    assert done.stdout.strip() == f"uv run --project {REPO / 'tools'}/../python coney-agent engineer"
    assert "coney-agent" in (REPO / "python" / "pyproject.toml").read_text(encoding="utf-8")


@pytest.mark.parametrize("path", ["C:/Users/x/AppData/Roaming/npm/claude.cmd", "C:/tools/claude.BAT"])
def test_a_claude_that_is_a_batch_file_gets_a_warning_about_ctrl_c(
    checkout: Path, capsys: pytest.CaptureFixture[str], path: str
) -> None:
    # subprocess starts a .cmd through cmd.exe, which asks "Terminate batch job (Y/N)?" on Ctrl+C: the session
    # launcher cannot prevent that, so it says what to answer and what avoids it.
    runner = Recorder()
    code = session_launcher.main([], start=checkout, runner=runner, which=lambda name: path)
    assert code == 0 and runner.calls[0][0][0] == path  # the session still starts
    error = capsys.readouterr().err
    assert "Terminate batch job" in error and "answer N" in error and "claude.exe" in error


def test_the_native_claude_gets_no_warning(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    for path in ("C:/Users/x/.local/bin/claude.exe", "/usr/local/bin/claude"):

        def found(name: str, path: str = path) -> str | None:
            return path

        assert session_launcher.main([], start=checkout, runner=Recorder(), which=found) == 0
        assert "Terminate batch job" not in capsys.readouterr().err

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.mcp_serve: the debugging servers that .mcp.json starts through coney-tools."""

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from coney_tools import mcp_serve, pcsx2_claims
from coney_tools.cli import main
from coney_tools.config import PATH_KEYS, ConfigError
from coney_tools.mcp_serve import GHIDRA_MCP_URL, SERVERS, server_command

REPO = Path(__file__).resolve().parents[2]


def paths(**values: Path | None) -> dict[str, Path | None]:
    """A config's path table with every key unset except `values`."""
    table: dict[str, Path | None] = dict.fromkeys(PATH_KEYS)
    table.update(values)
    return table


def which_all(name: str) -> str | None:
    """A `which` that finds every program, at a made-up path."""
    return f"/bin/{name}"


def which_none(name: str) -> str | None:
    """A `which` that finds nothing."""
    return None


def test_ghidra_runs_the_bridge_from_the_configured_checkout(tmp_path: Path) -> None:
    command, env = server_command("ghidra-mcp", paths(ghidra_mcp_repo=tmp_path), which_all, {})
    assert command == ["/bin/uv", "run", "--frozen", "--quiet", "--directory", str(tmp_path), "bridge-mcp-ghidra",
                       "--transport", "stdio"]  # fmt: skip
    assert env["GHIDRA_MCP_URL"] == GHIDRA_MCP_URL
    # Claude Code only sees tool groups that exist when it connects.
    assert env["GHIDRA_MCP_LAZY"] == "0"


def test_ghidra_keeps_a_url_from_the_environment(tmp_path: Path) -> None:
    _, env = server_command("ghidra-mcp", paths(ghidra_mcp_repo=tmp_path), which_all, {"GHIDRA_MCP_URL": "http://h:1"})
    assert env["GHIDRA_MCP_URL"] == "http://h:1"


def test_ghidra_prefers_the_uv_that_runs_coney_tools(tmp_path: Path) -> None:
    command, _ = server_command("ghidra-mcp", paths(ghidra_mcp_repo=tmp_path), which_none, {"UV": "/opt/uv"})
    assert command[0] == "/opt/uv"


def test_ghidra_unset_names_the_key_and_the_guide() -> None:
    with pytest.raises(ConfigError, match=r"ghidra_mcp_repo.*workspace\.md"):
        server_command("ghidra-mcp", paths(), which_all, {})


def test_ghidra_missing_folder_names_the_path(tmp_path: Path) -> None:
    missing = tmp_path / "nowhere"
    with pytest.raises(ConfigError, match="nowhere"):
        server_command("ghidra-mcp", paths(ghidra_mcp_repo=missing), which_all, {})


def test_pcsx2_runs_node_on_the_built_server(tmp_path: Path) -> None:
    (tmp_path / "dist").mkdir()
    (tmp_path / "dist" / "index.js").write_text("", encoding="utf-8")
    command, _ = server_command("pcsx2", paths(pcsx2_mcp_dir=tmp_path), which_all, {})
    assert command == ["/bin/node", str(tmp_path / "dist" / "index.js")]


def test_pcsx2_accepts_the_release_folder(tmp_path: Path) -> None:
    server = tmp_path / "pcsx2-mcp-server" / "dist"
    server.mkdir(parents=True)
    (server / "index.js").write_text("", encoding="utf-8")
    command, _ = server_command("pcsx2", paths(pcsx2_mcp_dir=tmp_path), which_all, {})
    assert command[1] == str(server / "index.js")


def test_pcsx2_without_node_says_so(tmp_path: Path) -> None:
    (tmp_path / "dist").mkdir()
    (tmp_path / "dist" / "index.js").write_text("", encoding="utf-8")
    with pytest.raises(ConfigError, match=r"Node\.js"):
        server_command("pcsx2", paths(pcsx2_mcp_dir=tmp_path), which_none, {})


def test_pcsx2_folder_without_the_server_is_named(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match=r"pcsx2_mcp_dir.*index\.js"):
        server_command("pcsx2", paths(pcsx2_mcp_dir=tmp_path), which_all, {})


def test_serve_without_a_config_fails_quietly(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # stdout belongs to the MCP protocol: a failure prints one line on stderr and nothing on stdout.
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    assert main(["mcp", "serve", "pcsx2"]) == 2
    captured = capsys.readouterr()
    assert captured.out == ""
    assert len(captured.err.strip().splitlines()) == 1


def test_serve_finds_the_checkout_from_claude_project_dir(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    checkout = tmp_path / "checkout"
    checkout.mkdir()
    (checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
    (checkout / "coney.local.toml").write_text('pcsx2_mcp_dir = "nowhere"\n', encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(checkout))
    assert main(["mcp", "serve", "pcsx2"]) == 2
    assert "nowhere" in capsys.readouterr().err


def make_pcsx2_server(folder: Path) -> Path:
    """A made-up PCSX2-MCP server folder; returns its script."""
    (folder / "dist").mkdir(parents=True)
    script = folder / "dist" / "index.js"
    script.write_text("", encoding="utf-8")
    return script


def test_pcsx2_server_requires_the_agent_argument_so_each_agent_has_its_own_connection(tmp_path: Path) -> None:
    make_pcsx2_server(tmp_path)
    _, extra = server_command("pcsx2", paths(pcsx2_mcp_dir=tmp_path), which_all, {})
    assert extra == {"PCSX2_REQUIRE_AGENT": "1"}
    _, ghidra = server_command("ghidra-mcp", paths(ghidra_mcp_repo=tmp_path), which_all, {"UV": "/bin/uv"})
    assert "PCSX2_REQUIRE_AGENT" not in ghidra


def test_pcsx2_with_a_copy_passes_its_ports(tmp_path: Path) -> None:
    make_pcsx2_server(tmp_path)
    copy = pcsx2_claims.Copy("pcsx2-c", tmp_path / "pcsx2-c", 28013, debug=21514)
    _, extra = server_command("pcsx2", paths(pcsx2_mcp_dir=tmp_path), which_all, {}, copy)
    assert extra == {"PCSX2_REQUIRE_AGENT": "1", "PCSX2_DEBUG_PORT": "21514", "PCSX2_PINE_PORT": "28013"}


def test_pcsx2_with_a_copy_without_a_debug_server_says_so(tmp_path: Path) -> None:
    make_pcsx2_server(tmp_path)
    copy = pcsx2_claims.Copy("pcsx2", tmp_path / "pcsx2", 28011)
    with pytest.raises(ConfigError, match="pcsx2: the debug server is off"):
        server_command("pcsx2", paths(pcsx2_mcp_dir=tmp_path), which_all, {}, copy)


def test_a_copy_is_only_for_the_pcsx2_server(tmp_path: Path) -> None:
    copy = pcsx2_claims.Copy("pcsx2", tmp_path / "pcsx2", 28011, debug=21512)
    with pytest.raises(ConfigError, match="--copy is for the pcsx2 server"):
        server_command("ghidra-mcp", paths(ghidra_mcp_repo=tmp_path), which_all, {}, copy)


def test_serve_with_a_copy_runs_the_server_with_its_ports(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    make_pcsx2_server(tmp_path / "server")
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text('pcsx2_mcp_dir = "server"\n', encoding="utf-8")
    copy = pcsx2_claims.Copy("pcsx2-b", tmp_path / "pcsx2-b", 28012, debug=21513)

    class FakeRegistry:
        """Knows one copy."""

        def copy(self, name: str) -> pcsx2_claims.Copy:
            assert name == "pcsx2-b"
            return copy

    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda start=None: FakeRegistry())
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(tmp_path))
    monkeypatch.setattr(shutil, "which", which_all)
    seen: list[dict[str, str]] = []

    def fake_run(command: list[str], env: dict[str, str]) -> int:
        seen.append(env)
        return 0

    monkeypatch.setattr(mcp_serve, "_run_server", fake_run)
    assert main(["mcp", "serve", "pcsx2", "--copy", "pcsx2-b"]) == 0
    assert (seen[0]["PCSX2_DEBUG_PORT"], seen[0]["PCSX2_PINE_PORT"]) == ("21513", "28012")


def test_serve_passes_the_servers_exit_status_through(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    make_pcsx2_server(tmp_path / "server")
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text('pcsx2_mcp_dir = "server"\n', encoding="utf-8")
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(tmp_path))
    monkeypatch.setattr(shutil, "which", which_all)
    ran: list[list[str]] = []

    def fake_run(command: list[str], env: dict[str, str]) -> int:
        ran.append(command)
        return 3

    monkeypatch.setattr(mcp_serve, "_run_server", fake_run)
    assert main(["mcp", "serve", "pcsx2"]) == 3
    assert ran == [["/bin/node", str((tmp_path / "server" / "dist" / "index.js").resolve())]]


def test_serve_from_a_worktree_uses_the_main_checkouts_config(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    script = make_pcsx2_server(tmp_path / "tools" / "pcsx2-mcp")
    main_checkout = tmp_path / "repos" / "coney"
    main_checkout.mkdir(parents=True)
    (main_checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
    (main_checkout / "coney.local.toml").write_text('pcsx2_mcp_dir = "../../tools/pcsx2-mcp"\n', encoding="utf-8")
    worktree = tmp_path / "worktrees" / "coney" / "branch"
    env = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid",
           "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@example.invalid"}  # fmt: skip
    for args in (["init", "-q"], ["add", "coney.local.example.toml"], ["commit", "-q", "-m", "x"],
                 ["worktree", "add", "-q", "--detach", str(worktree)]):  # fmt: skip
        subprocess.run(["git", "-C", str(main_checkout), *args], check=True, capture_output=True, env=env)
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", str(worktree))
    monkeypatch.setattr(shutil, "which", which_all)
    ran: list[list[str]] = []

    def fake_run(command: list[str], env: dict[str, str]) -> int:
        ran.append(command)
        return 0

    monkeypatch.setattr(mcp_serve, "_run_server", fake_run)
    assert main(["mcp", "serve", "pcsx2"]) == 0
    # ../../tools resolves from the main checkout, not from the worktree three levels deeper.
    assert ran == [["/bin/node", str(script.resolve())]]


def test_the_committed_mcp_json_starts_every_server_through_coney_tools() -> None:
    servers = json.loads((REPO / ".mcp.json").read_text(encoding="utf-8"))["mcpServers"]
    assert set(servers) == set(SERVERS)
    for name, entry in servers.items():
        assert entry["args"][-3:] == ["mcp", "serve", name]
        # --isolated keeps the launcher's environment out of the checkout (no python/.venv in every worktree);
        # --frozen never rewrites uv.lock.
        assert "--isolated" in entry["args"] and "--frozen" in entry["args"]
        # No machine's paths in the repository: everything is found through coney.local.toml.
        assert not any(":\\" in arg or arg.startswith("/") for arg in entry["args"])


def test_xemu_runs_the_package_server_with_this_python() -> None:
    command, extra = server_command("xemu", paths(), which_all, {})
    assert command == [sys.executable, "-m", "coney_tools.xemu_mcp"] and extra == {}

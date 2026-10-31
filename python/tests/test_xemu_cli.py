# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.xemu_cli: the command line xemu is started with, the disc set in a copy's settings, the read
and its formats, `record-run` writing an xemu run record from a fake monitor, the install search and the parser."""

import argparse
import json
import shutil
import struct
import time
import tomllib
from pathlib import Path

import pytest
from conventions_support import REPO
from xemu_support import fake_xemu, make_copy, make_install

from coney_tools import cli, pcsx2_proc, run_records, run_records_cli, xemu_claims, xemu_cli
from coney_tools.config import ConfigError
from coney_tools.pcsx2_claims import ClaimError
from coney_tools.xemu_claims import Registry
from coney_tools.xemu_copies import Copy


def test_xemu_starts_with_its_stub_and_monitor_on_localhost(tmp_path: Path) -> None:
    copy = Copy("xemu-b", tmp_path, 1235, 4445)
    command = xemu_cli.launch_command(copy, paused=True)
    assert command[1:] == ["-gdb", "tcp:127.0.0.1:1235", "-monitor", "tcp:127.0.0.1:4445,server,nowait", "-S"]
    assert "-S" not in xemu_cli.launch_command(copy, paused=False)


def test_the_disc_goes_into_the_copy_settings(tmp_path: Path) -> None:
    folder = make_copy(tmp_path, "xemu", 1234, 4444)
    xemu_cli.set_disc(Copy("xemu", folder, 1234, 4444), tmp_path / "disc.iso")
    settings = tomllib.loads((folder / "xemu.toml").read_text(encoding="utf-8"))
    assert settings["sys"]["files"]["dvd_path"] == str(tmp_path / "disc.iso")
    assert settings["general"]["show_welcome"] is False


def test_values_are_shown_by_type() -> None:
    data = struct.pack("<2f", 1.5, -2.0)
    assert xemu_cli.format_values(0x00010000, data, "f32") == ["0x00010000 1.5", "0x00010004 -2.0"]
    assert xemu_cli.format_values(0x00010000, bytes(18), "hex")[1] == "0x00010010: 00 00"


def test_installs_are_found_in_the_usual_places(tmp_path: Path) -> None:
    (tmp_path / "home" / "Desktop" / "xemu-win-release").mkdir(parents=True)
    (tmp_path / "home" / "Desktop" / "xemu-win-release" / "xemu.exe").write_bytes(b"")
    (tmp_path / "programs" / "xemu").mkdir(parents=True)
    (tmp_path / "programs" / "xemu" / "xemu.exe").write_bytes(b"")
    found = xemu_cli.find_installs({"USERPROFILE": str(tmp_path / "home"), "ProgramFiles": str(tmp_path / "programs")})
    assert found == [tmp_path / "home" / "Desktop" / "xemu-win-release", tmp_path / "programs" / "xemu"]


def test_every_xemu_command_and_argument_has_help() -> None:
    parser = cli._build_parser()
    groups = next(a for a in parser._actions if isinstance(a, argparse._SubParsersAction))
    xemu = groups.choices["xemu"]
    commands = next(a for a in xemu._actions if isinstance(a, argparse._SubParsersAction))
    assert set(commands.choices) == {
        "claim",
        "release",
        "status",
        "check-ports",
        "setup-copies",
        "launch",
        "read",
        "record-run",
        "find-install",
    }
    for name, command in commands.choices.items():
        for action in command._actions:
            if not isinstance(action, argparse._HelpAction):
                assert action.help, f"xemu {name} {action.dest} has no help"


def test_record_run_writes_an_xemu_record(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    root = tmp_path / "checkout"
    (root / ".github").mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    shutil.copy(REPO / ".github" / "conventions.yml", root / ".github" / "conventions.yml")
    monkeypatch.chdir(root)
    monkeypatch.setattr(xemu_claims, "xemu_processes", dict)
    monkeypatch.setattr(xemu_claims, "is_xemu", lambda pid: False)
    with fake_xemu() as (stub, monitor):
        make_copy(tmp_path / "emulators", "xemu", stub.port, monitor.port)
        registry = Registry(tmp_path / "emulators", tmp_path / "claims")
        monkeypatch.setattr(xemu_claims, "port_open", lambda port: False)
        registry.claim("alice", "xemu")
        monkeypatch.setattr(xemu_claims, "port_open", lambda port: port in (stub.port, monitor.port))
        monkeypatch.setattr(xemu_claims, "default_registry", lambda start=None: registry)
        monkeypatch.setattr(run_records_cli, "today", lambda: __import__("datetime").date(2026, 10, 25))
        parser = cli._build_parser()
        args = parser.parse_args(
            [
                "xemu",
                "record-run",
                "--agent",
                "alice",
                "--name",
                "ramp index",
                "--method",
                "read the index",
                "--value",
                "ramp_index=3",
                "--snapshot",
                "Level_1.start",
            ]
        )
        assert xemu_cli.run(args) == 0
    record = run_records.load(root, "r-20261025-ramp-index")
    assert (record.emulator, record.pcsx2_version, record.copy, record.state) == (
        "xemu",
        "v0.8.110",
        "xemu",
        "level-1-start",
    )
    assert record.tool == "xemu record-run" and record.values == {"ramp_index": 3}


# --- claim, release, set-up and launch, on a synthetic install and copies -------------------------------------------


@pytest.fixture
def machine(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Registry:
    """Two fake copies under `emulators/` and their registry as `default_registry` returns it; no xemu process runs and
    nothing serves a port."""
    monkeypatch.setattr(xemu_claims, "xemu_processes", dict)
    monkeypatch.setattr(xemu_claims, "is_xemu", lambda pid: False)
    monkeypatch.setattr(xemu_claims, "port_open", lambda port: False)
    make_copy(tmp_path / "emulators", "xemu", 61101, 61102)
    make_copy(tmp_path / "emulators", "xemu-b", 61103, 61104)
    registry = Registry(tmp_path / "emulators", tmp_path / "claims")
    monkeypatch.setattr(xemu_claims, "default_registry", lambda start=None: registry)
    return registry


def configure(monkeypatch: pytest.MonkeyPatch, tmp_path: Path, **paths: Path) -> None:
    """The config `xemu_cli` reads: these paths and the default numbers."""
    numbers = {"xemu_copies": 2, "xemu_gdb_base": 61201, "xemu_monitor_base": 61301}
    monkeypatch.setattr(xemu_cli, "_config", lambda: (dict(paths), numbers, tmp_path))


def test_claim_prints_the_copy_and_its_ports_and_release_frees_it(
    machine: Registry, capsys: pytest.CaptureFixture[str]
) -> None:
    assert xemu_cli.run_claim("alice", None, False, None) == 0
    out = capsys.readouterr().out
    assert "alice holds xemu:" in out and "GDB port 61101, monitor port 61102" in out
    assert machine.read("xemu") is not None
    assert xemu_cli.run_release("alice", None, False) == 0
    assert "released xemu (held by alice)" in capsys.readouterr().out
    assert machine.read("xemu") is None


def test_claim_json_and_a_second_agent_gets_the_next_copy(
    machine: Registry, capsys: pytest.CaptureFixture[str]
) -> None:
    xemu_cli.run_claim("alice", None, True, None)
    first = json.loads(capsys.readouterr().out)
    xemu_cli.run_claim("bob", None, True, None)
    second = json.loads(capsys.readouterr().out)
    assert (first["copy"], second["copy"]) == ("xemu", "xemu-b") and first["took_over"] is None


def test_a_copy_another_agent_holds_is_neither_claimed_nor_released_without_force(machine: Registry) -> None:
    xemu_cli.run_claim("alice", "xemu", False, None)
    with pytest.raises(ClaimError):
        xemu_cli.run_claim("bob", "xemu", False, None)
    with pytest.raises(ClaimError):
        xemu_cli.run_release("bob", "xemu", False)
    assert machine.read("xemu") is not None
    assert xemu_cli.run_release("bob", "xemu", True) == 0 and machine.read("xemu") is None


def test_setup_copies_makes_copies_from_the_install_and_leaves_its_console_files(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(xemu_claims, "xemu_processes", dict)
    install = make_install(tmp_path)
    root = tmp_path / "emulators"
    configure(monkeypatch, tmp_path, xemu_install=install, xemu_root=root)
    assert xemu_cli.run_setup_copies(None, False, False) == 0
    assert sorted(p.name for p in root.iterdir()) == ["xemu", "xemu-b"]
    assert (root / "xemu" / "xemu.exe").is_file() and (root / "xemu" / "SDL2.dll").is_file()
    assert not (root / "xemu" / "bios.exe").exists()  # a console file with a program's suffix is never a program
    ports = tomllib.loads((root / "xemu-b" / "coney-copy.toml").read_text(encoding="utf-8"))
    assert ports == {"gdb_port": 61202, "monitor_port": 61302}
    assert "show_welcome = true" in (install / "xemu.toml").read_text(encoding="utf-8")  # the install is read only
    assert xemu_cli.run_setup_copies(1, False, True) == 0  # a dry run changes nothing


def test_setup_copies_refuses_a_root_inside_a_checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(xemu_claims, "xemu_processes", dict)
    inside = REPO / "emulators-never-made"
    configure(monkeypatch, tmp_path, xemu_install=make_install(tmp_path), xemu_root=inside)
    with pytest.raises(ConfigError, match="inside the repository"):
        xemu_cli.run_setup_copies(None, False, False)
    assert not inside.exists()


def test_setup_copies_without_an_install_names_the_key(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    configure(monkeypatch, tmp_path, xemu_root=tmp_path / "emulators")
    with pytest.raises(ConfigError, match="xemu_install"):
        xemu_cli.run_setup_copies(None, False, False)


def test_setup_copies_exits_1_when_a_copys_xemu_runs(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(xemu_claims, "xemu_processes", dict)
    root = tmp_path / "emulators"
    configure(monkeypatch, tmp_path, xemu_install=make_install(tmp_path), xemu_root=root)
    assert xemu_cli.run_setup_copies(1, False, False) == 0
    monkeypatch.setattr(xemu_claims, "xemu_processes", lambda: {99: root / "xemu" / "xemu.exe"})
    assert xemu_cli.run_setup_copies(1, False, False) == 1


class Launched:
    """What `run_launch` started: the command lines, and whether the process it started is alive."""

    def __init__(self) -> None:
        self.commands: list[list[str]] = []
        self.alive = True


@pytest.fixture
def launched(machine: Registry, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Launched:
    """`run_launch` with the process start, its liveness and the monitor's port faked, and a disc of one byte."""
    seen = Launched()

    def start(command: list[str]) -> int:
        seen.commands.append(command)
        return 4242

    monkeypatch.setattr(pcsx2_proc, "start_detached", start)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: seen.alive)
    monkeypatch.setattr(xemu_cli, "port_open", lambda port: True)
    (tmp_path / "disc").mkdir()
    (tmp_path / "disc" / "game.xiso").write_bytes(b"x")
    configure(monkeypatch, tmp_path, xemu_dvd=tmp_path / "disc" / "game.xiso")
    return seen


def test_launch_sets_the_disc_starts_xemu_and_notes_its_pid(
    launched: Launched, machine: Registry, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert xemu_cli.run_launch("alice", None, False, 1.0) == 0
    assert "alice runs xemu: xemu pid 4242, GDB port 61101, monitor port 61102" in capsys.readouterr().out
    (command,) = launched.commands
    assert command[1:] == ["-gdb", "tcp:127.0.0.1:61101", "-monitor", "tcp:127.0.0.1:61102,server,nowait"]
    settings = tomllib.loads((tmp_path / "emulators" / "xemu" / "xemu.toml").read_text(encoding="utf-8"))
    assert settings["sys"]["files"]["dvd_path"] == str(tmp_path / "disc" / "game.xiso")
    claim = machine.read("xemu")
    assert claim is not None and claim.xemu_pid == 4242


def test_launch_paused_adds_the_stop_flag(launched: Launched) -> None:
    xemu_cli.run_launch("alice", None, True, 1.0)
    assert launched.commands[0][-1] == "-S"


def test_launch_leaves_a_running_xemu_alone(
    launched: Launched, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    xemu_cli.run_claim("alice", "xemu", False, None)  # a copy with an xemu running and no claim is not handed out
    monkeypatch.setattr(xemu_claims, "xemu_processes", lambda: {77: tmp_path / "emulators" / "xemu" / "xemu.exe"})
    capsys.readouterr()
    assert xemu_cli.run_launch("alice", None, False, 1.0) == 0
    assert "already runs (pid 77)" in capsys.readouterr().out and launched.commands == []


@pytest.mark.reads("docs_only")
def test_launch_needs_a_disc_and_refuses_one_inside_a_checkout(
    launched: Launched, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    configure(monkeypatch, tmp_path)
    with pytest.raises(ConfigError, match="no disc image"):
        xemu_cli.run_launch("alice", None, False, 1.0)
    with pytest.raises(ConfigError, match="no disc image"):
        xemu_cli.run_launch("alice", tmp_path / "missing.xiso", False, 1.0)
    inside = REPO / "docs" / "guides" / "coney-tools-xemu.md"  # any real file: the rule is about where it lies
    with pytest.raises(ConfigError, match="inside the repository"):
        xemu_cli.run_launch("alice", inside, False, 1.0)
    assert launched.commands == []


def test_launch_names_a_copy_with_no_program(launched: Launched, tmp_path: Path) -> None:
    (tmp_path / "emulators" / "xemu" / "xemu.exe").unlink()
    with pytest.raises(ConfigError, match="setup-copies"):
        xemu_cli.run_launch("alice", None, False, 1.0)


def test_launch_reports_an_xemu_that_ends_or_never_answers(launched: Launched, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(xemu_cli, "port_open", lambda port: False)
    launched.alive = False
    with pytest.raises(ConfigError, match="ended before its monitor answered"):
        xemu_cli.run_launch("alice", None, False, 5.0)
    launched.alive = True
    monkeypatch.setattr(time, "sleep", lambda seconds: None)
    with pytest.raises(ConfigError, match="did not answer within 0 s"):
        xemu_cli.run_launch("alice", None, False, 0.0)

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.xemu_cli: the command line xemu is started with, the disc set in a copy's settings, the read
and its formats, `record-run` writing an xemu run record from a fake monitor, the install search and the parser."""

import argparse
import shutil
import struct
import tomllib
from pathlib import Path

import pytest
from conventions_support import REPO
from xemu_support import fake_xemu, make_copy

from coney_tools import cli, run_records, run_records_cli, xemu_claims, xemu_cli
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
                "Level-1",
            ]
        )
        assert xemu_cli.run(args) == 0
    record = run_records.load(root, "r-20261025-ramp-index")
    assert (record.emulator, record.pcsx2_version, record.copy, record.state) == ("xemu", "v0.8.110", "xemu", "level-1")
    assert record.tool == "xemu record-run" and record.values == {"ramp_index": 3}

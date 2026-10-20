# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.pcsx2_copies: `pcsx2 setup-copies` over made-up builds and copies, and `pcsx2 recordings
push | pull`. No PCSX2, no BIOS and no game data: every file here is a few made-up bytes."""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest

from coney_tools import pcsx2_claims, pcsx2_copies, pcsx2_proc
from coney_tools.config import ConfigError
from coney_tools.pcsx2_claims import Registry
from coney_tools.pcsx2_copies import (
    copy_checked,
    copy_name,
    plan_copies,
    program_entries,
    recording_path,
    setup_copies,
    transfer_recording,
)

OLD_INI = (
    "[UI]\nSetupWizardIncomplete = false\n\n[EmuCore]\nEnablePINE = true\nPINESlot = 28011\n\n"
    "[InputSources]\nSDL = true\n"
)


def write(path: Path, data: str | bytes = "x") -> Path:
    """Write a made-up file, making its folders."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, bytes):
        path.write_bytes(data)
    else:
        path.write_text(data, encoding="utf-8")
    return path


def make_build(folder: Path) -> Path:
    """A made-up fork release folder: PCSX2's program and the MCP server beside it."""
    for name in ("pcsx2-qt.exe", "Qt6Core.dll", "qt.conf", "resources/shaders/a.glsl", "translations/x.qm"):
        write(folder / name, f"new {name}")
    write(folder / "pcsx2-mcp-server" / "dist" / "index.js", "server")
    return folder


def make_old_copy(folder: Path) -> Path:
    """A made-up copy of an older PCSX2 with its own data."""
    for name in (
        "pcsx2-qt.exe",
        "Qt6Core.dll",
        "updater.exe",
        "old.dll",
        "resources/stale.txt",
        "resources/shaders/a.glsl",
    ):
        write(folder / name, f"old {name}")
    write(folder / "inis" / "PCSX2.ini", OLD_INI)
    for name in ("bios/scph.bin", "memcards/Mcd001.ps2", "sstates/a.p2s", "crash.dmp", "logs/emulog.txt"):
        write(folder / name, f"mine {name}")
    return folder


def tree(folder: Path) -> dict[str, bytes]:
    """Every file below `folder` with its bytes."""
    return {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(folder.rglob("*")) if p.is_file()}


def test_copies_are_named_and_numbered_from_pcsx2() -> None:
    assert [copy_name(i) for i in (0, 1, 4, 25)] == ["pcsx2", "pcsx2-b", "pcsx2-e", "pcsx2-z"]
    with pytest.raises(ConfigError, match="at most 26"):
        copy_name(26)
    plans = plan_copies(Path("emulators"), 3, 28011, 21512)
    assert [(p.name, p.settings[1][2], p.settings[3][2]) for p in plans] == [
        ("pcsx2", "28011", "21512"),
        ("pcsx2-b", "28012", "21513"),
        ("pcsx2-c", "28013", "21514"),
    ]


def test_setup_updates_the_program_and_keeps_the_copys_own_data(tmp_path: Path) -> None:
    build = make_build(tmp_path / "release" / "PCSX2-MCP-coney-v2.9.129.7-win64")
    root = tmp_path / "emulators"
    make_old_copy(root / "pcsx2")
    mine = {k: v for k, v in tree(root / "pcsx2").items() if k.split("/")[0] in ("bios", "memcards", "sstates", "logs")}
    refused = setup_copies(root, build, plan_copies(root, 2, 28011, 21512), {}, log=lambda line: None)
    assert refused == []
    files = tree(root / "pcsx2")
    assert files["pcsx2-qt.exe"] == b"new pcsx2-qt.exe"
    assert files["resources/shaders/a.glsl"] == b"new resources/shaders/a.glsl"
    assert "translations/x.qm" in files and "portable.txt" in files
    # The MCP server runs from the release, and old program files the build lacks are gone.
    assert not any(k.startswith("pcsx2-mcp-server") for k in files)
    for gone in ("updater.exe", "old.dll", "resources/stale.txt"):
        assert gone not in files
    # Its own data stays: BIOS, memory cards, states, logs, a crash dump.
    assert {k: files[k] for k in mine} == mine
    assert files["crash.dmp"] == b"mine crash.dmp"
    copies = {c.name: c for c in pcsx2_claims.discover_copies(root)}
    assert [(c.port, c.debug, c.sdl) for c in copies.values()] == [(28011, 21512, False), (28012, 21513, False)]
    ini = (root / "pcsx2" / "inis" / "PCSX2.ini").read_text(encoding="utf-8")
    assert "SetupWizardIncomplete = false" in ini


def test_a_new_copy_is_seeded_from_the_first_copy(tmp_path: Path) -> None:
    build = make_build(tmp_path / "build")
    root = tmp_path / "emulators"
    make_old_copy(root / "pcsx2")
    setup_copies(root, build, plan_copies(root, 3, 28011, 21512), {}, log=lambda line: None)
    for name, port in (("pcsx2-b", 28012), ("pcsx2-c", 28013)):
        files = tree(root / name)
        assert files["bios/scph.bin"] == b"mine bios/scph.bin"
        assert "memcards/Mcd001.ps2" not in files and "sstates/a.p2s" not in files
        assert pcsx2_claims.read_pine_port(root / name) == port


def test_a_new_copy_without_another_to_copy_gets_a_minimal_ini(tmp_path: Path) -> None:
    build = make_build(tmp_path / "build")
    root = tmp_path / "emulators"
    setup_copies(root, build, plan_copies(root, 1, 28011, 21512), {}, log=lambda line: None)
    assert pcsx2_claims.read_debug_port(root / "pcsx2") == 21512
    assert not (root / "pcsx2" / "bios").exists()


def test_a_running_copy_is_refused_and_left_alone(tmp_path: Path) -> None:
    build = make_build(tmp_path / "build")
    root = tmp_path / "emulators"
    make_old_copy(root / "pcsx2")
    before = tree(root / "pcsx2")
    lines: list[str] = []
    refused = setup_copies(
        root, build, plan_copies(root, 2, 28011, 21512), {42: root / "pcsx2" / "pcsx2-qt.exe"}, log=lines.append
    )
    assert refused == ["pcsx2"]
    assert tree(root / "pcsx2") == before
    assert any("refused, its PCSX2 runs (pid 42)" in line for line in lines)
    assert (root / "pcsx2-b" / "pcsx2-qt.exe").is_file()


def test_a_dry_run_changes_nothing(tmp_path: Path) -> None:
    build = make_build(tmp_path / "build")
    root = tmp_path / "emulators"
    make_old_copy(root / "pcsx2")
    before = tree(root)
    lines: list[str] = []
    setup_copies(root, build, plan_copies(root, 2, 28011, 21512), {}, log=lines.append, dry_run=True)
    assert tree(root) == before
    assert not (root / "pcsx2-b").exists()
    assert any("would remove updater.exe" in line for line in lines)


def test_a_second_run_copies_nothing(tmp_path: Path) -> None:
    build = make_build(tmp_path / "build")
    root = tmp_path / "emulators"
    plans = plan_copies(root, 1, 28011, 21512)
    setup_copies(root, build, plans, {}, log=lambda line: None)
    lines: list[str] = []
    setup_copies(root, build, plans, {}, log=lines.append)
    assert "    copied 0 program file(s) in all" in lines
    assert not any(line.strip().startswith("set ") for line in lines)


def test_a_folder_without_pcsx2_is_not_a_build(tmp_path: Path) -> None:
    write(tmp_path / "server" / "dist" / "index.js")
    with pytest.raises(ConfigError, match="holds no PCSX2 build"):
        program_entries(tmp_path / "server")


def test_setup_copies_reads_its_numbers_and_folders_from_the_config(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    make_build(tmp_path / "tools" / "build")
    checkout = tmp_path / "repos" / "coney"
    write(checkout / "coney.local.example.toml", "")
    write(
        checkout / "coney.local.toml",
        'pcsx2_mcp_dir = "../../tools/build"\npcsx2_root = "../../emu"\npcsx2_copies = 2\npcsx2_pine_base = 30000\n',
    )
    monkeypatch.chdir(checkout)
    monkeypatch.setattr(pcsx2_proc, "pcsx2_processes", dict)
    assert pcsx2_copies.run_setup_copies(None, False) == 0
    copies = pcsx2_claims.discover_copies(tmp_path / "emu")
    assert [(c.name, c.port, c.debug) for c in copies] == [("pcsx2", 30000, 21512), ("pcsx2-b", 30001, 21513)]


# --- recordings ------------------------------------------------------------------------------------------------------


def test_a_recording_is_named_below_recordings() -> None:
    assert recording_path("fights/rumble-1") == Path("fights/rumble-1.p2m2")
    assert recording_path("fights\\a.p2m2") == Path("fights/a.p2m2")
    for bad in ("../a", "a/./b", "/a", "a b", "a/b/c/d/e/f/g/h/i", ""):
        with pytest.raises(ConfigError, match="a recording is named"):
            recording_path(bad)


def test_a_recording_moves_with_its_savestate_and_hashes(tmp_path: Path) -> None:
    source, target = tmp_path / "scratch", tmp_path / "copy"
    write(source / "fights" / "a.p2m2", b"recording")
    write(source / "fights" / "a.p2m2_SaveState.p2s", b"state")
    written = transfer_recording(source, target, Path("fights/a.p2m2"), force=False)
    assert [p.relative_to(target).as_posix() for p, _ in written] == ["fights/a.p2m2", "fights/a.p2m2_SaveState.p2s"]
    assert (target / "fights" / "a.p2m2_SaveState.p2s").read_bytes() == b"state"
    assert written[0][1] == pcsx2_copies.sha256(source / "fights" / "a.p2m2")
    # The same file again is fine; a different one needs --force.
    transfer_recording(source, target, Path("fights/a.p2m2"), force=False)
    write(source / "fights" / "a.p2m2", b"another take")
    with pytest.raises(ConfigError, match="--force"):
        transfer_recording(source, target, Path("fights/a.p2m2"), force=False)
    transfer_recording(source, target, Path("fights/a.p2m2"), force=True)
    assert (target / "fights" / "a.p2m2").read_bytes() == b"another take"
    with pytest.raises(ConfigError, match="no such recording"):
        transfer_recording(source, target, Path("b.p2m2"), force=False)


def test_a_copy_whose_hash_differs_is_not_kept(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    source = write(tmp_path / "a.p2m2", b"recording")
    monkeypatch.setattr(shutil, "copyfile", lambda s, d: Path(d).write_bytes(b"torn"))
    with pytest.raises(ConfigError, match="SHA-256"):
        copy_checked(source, tmp_path / "out" / "a.p2m2", force=False)
    assert list((tmp_path / "out").iterdir()) == []


def test_push_and_pull_need_the_claim(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    from test_pcsx2_claims import make_copy

    monkeypatch.setattr(pcsx2_proc, "pcsx2_processes", dict)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: False)
    monkeypatch.setattr(pcsx2_claims, "port_open", lambda port: False)
    copy = make_copy(tmp_path / "root", "pcsx2", 28011, debug=21512)
    registry = Registry(tmp_path / "root", tmp_path / "claims")
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    monkeypatch.setattr(pcsx2_copies, "scratch_recordings", lambda: tmp_path / "scratch" / "recordings")
    write(tmp_path / "scratch" / "recordings" / "a.p2m2", b"recording")
    with pytest.raises(ConfigError, match="held by nobody"):
        pcsx2_copies.run_recordings("push", "alice", "pcsx2", "a", False)
    registry.claim("alice", "pcsx2")
    assert pcsx2_copies.run_recordings("push", "alice", "pcsx2", "a", False) == 0
    assert (copy / "recordings" / "a.p2m2").read_bytes() == b"recording"
    write(copy / "recordings" / "new.p2m2", b"made in pcsx2")
    write(copy / "recordings" / "new.p2m2_SaveState.p2s", b"its state")
    assert pcsx2_copies.run_recordings("pull", "alice", "pcsx2", "new.p2m2", False) == 0
    assert (tmp_path / "scratch" / "recordings" / "new.p2m2_SaveState.p2s").read_bytes() == b"its state"

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.xemu_copies: copy names and ports, xemu.toml read and written, finding the install's settings,
and `setup-copies` making copies from a synthetic install without taking its console files for program files."""

import tomllib
from pathlib import Path

import pytest
from xemu_support import make_copy

from coney_tools.config import ConfigError
from coney_tools.xemu_copies import (
    COPY_SETTINGS,
    HDD_FILE,
    Copy,
    Install,
    copy_name,
    discover_copies,
    dump_toml,
    find_install,
    get_setting,
    logged_release,
    plan_copies,
    setup_copies,
)


def make_install(root: Path, portable: bool = True) -> Path:
    """A synthetic xemu install: an empty xemu.exe and a dll, and the user's own files (stand-ins of a few bytes)
    named by its xemu.toml: a boot ROM and BIOS beside the program, a disk image and an EEPROM in a subfolder."""
    folder = root / "xemu-install"
    (folder / "console").mkdir(parents=True)
    (folder / "xemu.exe").write_bytes(b"MZ")
    (folder / "SDL2.dll").write_bytes(b"MZ")
    (folder / "mcpx.bin").write_bytes(b"\x01" * 4)
    (folder / "bios.exe").write_bytes(b"\x02" * 4)  # a console file with a program's suffix must still be left alone
    (folder / "console" / "xbox_hdd.qcow2").write_bytes(b"QFI\xfb")
    (folder / "console" / "eeprom.bin").write_bytes(b"\x03" * 4)
    settings = (
        "[general]\nshow_welcome = true\n\n[sys.files]\n"
        f"bootrom_path = '{(folder / 'mcpx.bin').as_posix()}'\n"
        "flashrom_path = 'bios.exe'\n"
        "hdd_path = 'console/xbox_hdd.qcow2'\n"
        "eeprom_path = 'console/eeprom.bin'\n"
        "\n[display.quality]\nsurface_scale = 3\n"
    )
    (folder / ("xemu.toml" if portable else "elsewhere.toml")).write_text(settings, encoding="utf-8")
    return folder


def test_copy_names_go_through_the_alphabet() -> None:
    assert [copy_name(i) for i in range(3)] == ["xemu", "xemu-b", "xemu-c"]
    with pytest.raises(ConfigError):
        copy_name(26)


def test_each_copy_gets_the_next_ports() -> None:
    plans = plan_copies(Path("root"), 2, 1234, 4444)
    assert [(p.name, p.gdb, p.monitor) for p in plans] == [("xemu", 1234, 4444), ("xemu-b", 1235, 4445)]


def test_ports_that_overlap_are_refused() -> None:
    with pytest.raises(ConfigError, match="overlap"):
        plan_copies(Path("root"), 3, 4000, 4001)


def test_toml_round_trips_with_nested_tables() -> None:
    data = {"general": {"show_welcome": False, "name": "it's"}, "sys": {"files": {"hdd_path": "C:/x.qcow2"}}, "n": 1}
    text = dump_toml(data)
    assert tomllib.loads(text) == data
    assert text.startswith("n = 1\n")


def test_the_portable_settings_file_is_found_first(tmp_path: Path) -> None:
    folder = make_install(tmp_path)
    install = find_install(folder, None, {"APPDATA": str(tmp_path / "appdata")})
    assert install.settings_path == folder / "xemu.toml"
    assert install.file("hdd_path") == (folder / "console" / "xbox_hdd.qcow2").resolve()


def test_without_a_portable_file_the_user_settings_are_read(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    folder = make_install(tmp_path, portable=False)
    monkeypatch.setattr("sys.platform", "win32")
    user = tmp_path / "appdata" / "xemu" / "xemu" / "xemu.toml"
    user.parent.mkdir(parents=True)
    user.write_text((folder / "elsewhere.toml").read_text(encoding="utf-8"), encoding="utf-8")
    assert find_install(folder, None, {"APPDATA": str(tmp_path / "appdata")}).settings_path == user


def test_no_install_names_the_key_to_set(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="xemu_install"):
        find_install(None, None, {})
    with pytest.raises(ConfigError, match="no xemu program"):
        find_install(tmp_path, None, {})


def test_console_files_are_never_program_files(tmp_path: Path) -> None:
    install = find_install(make_install(tmp_path), None, {})
    assert [p.name for p in install.program_files()] == ["SDL2.dll", "xemu.exe"]


def _setup(tmp_path: Path, **options: bool) -> tuple[Install, Path, list[str]]:
    install = find_install(make_install(tmp_path), None, {})
    root = tmp_path / "emulators"
    lines: list[str] = []
    refused = setup_copies(install, plan_copies(root, 2, 1234, 4444), lambda _: [], log=lines.append, **options)
    assert refused == []
    return install, root, lines


def test_setup_makes_copies_with_their_own_disk_ports_and_settings(tmp_path: Path) -> None:
    install, root, _ = _setup(tmp_path)
    copy = root / "xemu-b"
    assert sorted(p.name for p in copy.iterdir()) == [
        "SDL2.dll",
        "coney-copy.toml",
        "eeprom.bin",
        "hdd",
        "xemu.exe",
        "xemu.toml",
    ]
    assert (copy / HDD_FILE).read_bytes() == b"QFI\xfb"
    settings = tomllib.loads((copy / "xemu.toml").read_text(encoding="utf-8"))
    assert settings["sys"]["files"]["hdd_path"] == str(copy / HDD_FILE)
    # The boot ROM and BIOS stay the install's: read by absolute path, never copied.
    assert settings["sys"]["files"]["flashrom_path"] == str(install.folder / "bios.exe")
    assert not (copy / "bios.exe").exists() and not (copy / "mcpx.bin").exists()
    for table, key, value in COPY_SETTINGS:
        assert get_setting(settings, table, key) == value
    assert [(c.name, c.gdb, c.monitor) for c in discover_copies(root)] == [("xemu", 1234, 4444), ("xemu-b", 1235, 4445)]


def test_the_install_is_never_changed(tmp_path: Path) -> None:
    folder = make_install(tmp_path)
    before = {p: p.read_bytes() for p in folder.rglob("*") if p.is_file()}
    install = find_install(folder, None, {})
    setup_copies(install, plan_copies(tmp_path / "emulators", 1, 1234, 4444), lambda _: [], log=lambda _: None)
    assert {p: p.read_bytes() for p in folder.rglob("*") if p.is_file()} == before


def test_a_second_setup_keeps_the_copy_disk_unless_refreshed(tmp_path: Path) -> None:
    install, root, _ = _setup(tmp_path)
    disk = root / "xemu" / HDD_FILE
    disk.write_bytes(b"snapshots of the copy")
    setup_copies(install, plan_copies(root, 1, 1234, 4444), lambda _: [], log=lambda _: None)
    assert disk.read_bytes() == b"snapshots of the copy"
    setup_copies(install, plan_copies(root, 1, 1234, 4444), lambda _: [], refresh_hdd=True, log=lambda _: None)
    assert disk.read_bytes() == b"QFI\xfb"


def test_a_dry_run_changes_nothing(tmp_path: Path) -> None:
    install = find_install(make_install(tmp_path), None, {})
    lines: list[str] = []
    setup_copies(
        install, plan_copies(tmp_path / "emulators", 1, 1234, 4444), lambda _: [], log=lines.append, dry_run=True
    )
    assert not (tmp_path / "emulators").exists()
    assert any("would copy" in line for line in lines)


def test_a_running_copy_is_refused(tmp_path: Path) -> None:
    install, root, _ = _setup(tmp_path)
    lines: list[str] = []
    refused = setup_copies(
        install, plan_copies(root, 2, 1234, 4444), lambda p: [7] if p.name == "xemu" else [], log=lines.append
    )
    assert refused == ["xemu"]


def test_two_copies_on_one_port_are_refused(tmp_path: Path) -> None:
    make_copy(tmp_path, "xemu", 1234, 4444)
    make_copy(tmp_path, "xemu-b", 1235, 1234)
    with pytest.raises(ConfigError, match="also used by xemu"):
        discover_copies(tmp_path)


def test_a_bad_port_file_names_the_key(tmp_path: Path) -> None:
    folder = make_copy(tmp_path, "xemu", 1234, 4444)
    (folder / "coney-copy.toml").write_text("gdb_port = 80\nmonitor_port = 4444\n", encoding="utf-8")
    with pytest.raises(ConfigError, match="gdb_port"):
        discover_copies(tmp_path)


def test_the_release_is_read_from_the_copys_log(tmp_path: Path) -> None:
    copy = Copy("xemu", make_copy(tmp_path, "xemu", 1234, 4444), 1234, 4444)
    assert logged_release(copy) is None  # never run
    (copy.path / "xemu.log").write_text("Created QEMU launch parameters: x\nxemu_version: 0.8.139\n", encoding="utf-8")
    assert logged_release(copy) == "0.8.139"
    (copy.path / "xemu.log").write_text("xemu_version: not-a-number\n", encoding="utf-8")
    assert logged_release(copy) is None

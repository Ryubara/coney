# SPDX-License-Identifier: GPL-3.0-or-later
"""The xemu copies agents claim, and `coney-tools xemu setup-copies`, which makes them from the maintainer's own install
(docs/guides/coney-tools-xemu.md#copies).

Copy i (`xemu`, `xemu-b`, `xemu-c`, ...) is a folder under `xemu_root` (the workspace's `emulators/`) holding:

* xemu's program files, copied from `xemu_install` (only `.exe` and `.dll` files: never a file the install's settings
  name, so a boot ROM, BIOS or disk image kept beside xemu.exe is never taken for a program file);
* `xemu.toml`, xemu's settings in portable mode (xemu reads the file beside its executable first): the install's own
  settings with the copy's disk image and EEPROM, and the settings that keep an agent's copy out of the user's way
  (COPY_SETTINGS);
* `hdd/xbox_hdd.qcow2`, the copy's own hard-disk image, copied once from the install's: xemu locks the image while it
  runs, so two copies cannot share one, and the image also holds the copy's snapshots (its save states);
* `eeprom.bin`, copied once from the install's when it has one;
* `coney-copy.toml`, Coney's own file: the copy's GDB stub and monitor ports (`xemu_gdb_base` + i,
  `xemu_monitor_base` + i). xemu takes them on its command line, so they are kept here, where `xemu launch` and the
  claims read them.

The boot ROM and the BIOS are not copied: each copy's settings point at the install's files, which xemu only reads.
None of these files may enter the repository (LEGAL.md): the root is refused when it lies inside a checkout.
"""

from __future__ import annotations

import os
import re
import shutil
import string
import sys
import tomllib
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError

#: Coney's file in a copy: its ports.
COPY_FILE = "coney-copy.toml"
#: xemu's settings file, beside its executable in a portable install.
SETTINGS_FILE = "xemu.toml"
#: The copy's own disk image, relative to the copy.
HDD_FILE = Path("hdd") / "xbox_hdd.qcow2"
#: The copy's own EEPROM, relative to the copy.
EEPROM_FILE = Path("eeprom.bin")
#: The files of an install that are xemu's program.
PROGRAM_SUFFIXES: tuple[str, ...] = (".exe", ".dll")

#: The settings every copy gets on top of the install's (table, key, value), so that an agent's xemu stays out of the
#: user's way and its frames compare: no welcome window or boot animation, no update check, a window (never full
#: screen) of a fixed size at a fixed render scale, no notifications, and no gamepad bound automatically (the user's pad
#: must never drive an agent's game; port 1 is bound to the keyboard, which only a key sent to the copy's window
#: reaches, and no Coney command sends one yet). xemu warns of a key it does not know (xemu.log: `Warning:
#: unrecognized key`). Checked against xemu 0.8.139 at the first live run (docs/guides/coney-tools-xemu.md#settings).
COPY_SETTINGS: tuple[tuple[str, str, object], ...] = (
    ("general", "show_welcome", False),
    ("general", "skip_boot_anim", True),
    ("general.updates", "check", False),
    ("display.window", "fullscreen_on_startup", False),
    ("display.window", "startup_size", "640x480"),
    ("display.quality", "surface_scale", 1),
    ("display.ui", "show_notifications", False),
    ("input", "auto_bind", False),
    ("input", "background_input_capture", False),
    ("input.bindings", "port1", "keyboard"),
)


@dataclass(frozen=True)
class Copy:
    """An xemu copy: its name (the folder's), folder, GDB stub port and QEMU monitor port."""

    name: str
    path: Path
    gdb: int
    monitor: int

    @property
    def executable(self) -> Path:
        """The copy's xemu program."""
        windows = self.path / "xemu.exe"
        return windows if windows.is_file() or sys.platform == "win32" else self.path / "xemu"


def logged_release(copy: Copy) -> str | None:
    """The xemu release the copy's program last started as (`0.8.139`), from the `xemu_version:` line xemu writes at the
    top of `xemu.log` beside its executable: the monitor's `info version` names QEMU's version, never xemu's. None
    when the copy has not run or the line is not a version number."""
    try:
        with (copy.path / "xemu.log").open(encoding="utf-8", errors="replace") as log:
            head = log.read(4096)
    except OSError:
        return None
    found = re.search(r"^xemu_version:\s*(\d+\.\d+\.\d+)\s*$", head, re.MULTILINE)
    return found.group(1) if found else None


def copy_name(index: int) -> str:
    """The folder name of copy `index`: `xemu`, then `xemu-b`, `xemu-c`, ..."""
    if not 0 <= index < len(string.ascii_lowercase):
        raise ConfigError(f"copy {index}: at most {len(string.ascii_lowercase)} copies (xemu to xemu-z)")
    return "xemu" if index == 0 else f"xemu-{string.ascii_lowercase[index]}"


# --- TOML ---------------------------------------------------------------------------------------------------------


def _scalar(value: object) -> str:
    """One TOML value: a string, a boolean, a number or a list of them."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int | float):
        return repr(value)
    if isinstance(value, str):
        return "'" + value + "'" if "'" not in value and "\n" not in value else _basic(value)
    if isinstance(value, list):
        return "[" + ", ".join(_scalar(item) for item in value) + "]"
    raise ConfigError(f"xemu.toml: cannot write a {type(value).__name__}")


def _basic(value: str) -> str:
    """A TOML basic string (double quotes, escapes)."""
    escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{escaped}"'


def dump_toml(data: Mapping[str, Any]) -> str:
    """`data` (tables of strings, booleans, numbers and lists, nested) as TOML text, the plain keys of a table before
    its subtables, as xemu writes its own file."""
    lines: list[str] = []

    def table(prefix: str, values: Mapping[str, Any]) -> None:
        plain = {k: v for k, v in values.items() if not isinstance(v, dict)}
        nested = {k: v for k, v in values.items() if isinstance(v, dict)}
        if plain and prefix:
            lines.extend(["", f"[{prefix}]"] if lines else [f"[{prefix}]"])
        for key, value in plain.items():
            lines.append(f"{key} = {_scalar(value)}")
        for key, value in nested.items():
            table(f"{prefix}.{key}" if prefix else key, value)

    table("", data)
    return "\n".join(lines) + "\n"


def set_setting(data: dict[str, Any], dotted: str, key: str, value: object) -> None:
    """Set `[dotted] key = value` in a parsed settings file, making the tables on the way."""
    node = data
    for part in dotted.split("."):
        child = node.setdefault(part, {})
        if not isinstance(child, dict):
            raise ConfigError(f"xemu.toml: {part} is a value, not a table")
        node = child
    node[key] = value


def get_setting(data: Mapping[str, Any], dotted: str, key: str) -> object:
    """`[dotted] key` of a parsed settings file, or None."""
    node: object = data
    for part in dotted.split("."):
        node = node.get(part) if isinstance(node, Mapping) else None
    return node.get(key) if isinstance(node, Mapping) else None


def read_toml(path: Path) -> dict[str, Any]:
    """A TOML file; ConfigError naming it when it cannot be read."""
    try:
        with path.open("rb") as handle:
            return tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: {error}") from error


# --- the copies -----------------------------------------------------------------------------------------------------


def read_ports(folder: Path) -> tuple[int, int]:
    """The GDB and monitor ports in a copy's coney-copy.toml. Raises ConfigError for a missing or bad file."""
    data = read_toml(folder / COPY_FILE)
    ports = []
    for key in ("gdb_port", "monitor_port"):
        value = data.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or not 1024 <= value < 65536:
            raise ConfigError(f"{folder / COPY_FILE}: {key} must be a port from 1024 to 65535, got {value!r}")
        ports.append(value)
    return ports[0], ports[1]


def discover_copies(root: Path) -> list[Copy]:
    """The `xemu*` folders of `root` that hold a coney-copy.toml, `xemu` first. Raises ConfigError when two copies
    would listen on one port (`xemu setup-copies` gives each its own)."""
    found = []
    for folder in sorted(root.glob("xemu*")):
        if (folder / COPY_FILE).is_file():
            gdb, monitor = read_ports(folder)
            found.append(Copy(folder.name, folder.resolve(), gdb, monitor))
    owners: dict[int, str] = {}
    for copy in found:
        for kind, port in (("GDB", copy.gdb), ("monitor", copy.monitor)):
            if port in owners:
                raise ConfigError(
                    f"{copy.name}: its {kind} port {port} is also used by {owners[port]}; every copy needs its own "
                    "ports (`coney-tools xemu setup-copies` sets them)"
                )
            owners[port] = copy.name
    return sorted(found, key=lambda copy: (copy.name != "xemu", copy.name))


# --- the install --------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Install:
    """The maintainer's own xemu: its folder, its settings file and what the settings say. Nothing of it is changed."""

    folder: Path
    settings_path: Path
    settings: Mapping[str, Any]

    def file(self, key: str) -> Path | None:
        """The file `[sys.files] key` names (relative paths from the settings' folder), or None when it names none."""
        value = get_setting(self.settings, "sys.files", key)
        if not isinstance(value, str) or not value:
            return None
        path = Path(value)
        return path if path.is_absolute() else (self.settings_path.parent / path).resolve()

    def console_files(self) -> set[Path]:
        """The console's files the settings name (boot ROM, BIOS, disk image, EEPROM, disc): never program files."""
        named = (self.file(key) for key in ("bootrom_path", "flashrom_path", "hdd_path", "eeprom_path", "dvd_path"))
        return {path.resolve() for path in named if path is not None}

    def program_files(self) -> list[Path]:
        """The install's program files: its top-level `.exe` and `.dll` files that the settings do not name."""
        console = self.console_files()
        return sorted(
            entry
            for entry in self.folder.iterdir()
            if entry.is_file() and entry.suffix.lower() in PROGRAM_SUFFIXES and entry.resolve() not in console
        )


def user_settings_file(env: Mapping[str, str]) -> Path | None:
    """Where xemu keeps its settings when the install is not portable: `%APPDATA%/xemu/xemu/xemu.toml` on Windows,
    `~/.local/share/xemu/xemu/xemu.toml` elsewhere (SDL's preference path); None when the variable is missing."""
    if sys.platform == "win32":
        base = env.get("APPDATA")
        return Path(base) / "xemu" / "xemu" / SETTINGS_FILE if base else None
    home = env.get("XDG_DATA_HOME") or (str(Path(env["HOME"]) / ".local" / "share") if env.get("HOME") else "")
    return Path(home) / "xemu" / "xemu" / SETTINGS_FILE if home else None


def find_install(folder: Path | None, settings: Path | None, env: Mapping[str, str] = os.environ) -> Install:
    """The install `xemu_install` names, with its settings: `xemu_config` when set, else xemu.toml beside xemu.exe
    (a portable install), else the user's settings file. Raises ConfigError naming the key to set."""
    if folder is None:
        raise ConfigError(
            "xemu_install is not set in coney.local.toml: name the folder of your own xemu (the one with xemu.exe), "
            "docs/guides/coney-tools-xemu.md#copies"
        )
    if not folder.is_dir() or not any(folder.glob("xemu*")):
        raise ConfigError(f"xemu_install: {folder} holds no xemu program")
    candidates = [settings] if settings is not None else [folder / SETTINGS_FILE, user_settings_file(env)]
    for candidate in candidates:
        if candidate is not None and candidate.is_file():
            return Install(folder, candidate, read_toml(candidate))
    where = ", ".join(str(c) for c in candidates if c is not None)
    raise ConfigError(
        f"no xemu.toml found ({where}): start your xemu once and set its files, or name the file with xemu_config"
    )


# --- making a copy --------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class CopyPlan:
    """One copy `setup-copies` makes or updates: its index, name, folder and ports."""

    index: int
    name: str
    path: Path
    gdb: int
    monitor: int


def plan_copies(root: Path, count: int, gdb_base: int, monitor_base: int) -> list[CopyPlan]:
    """The copies 0 .. count-1 under `root`, with their ports."""
    if count < 1:
        raise ConfigError("--count must be at least 1")
    if not all(base + count - 1 < 65536 for base in (gdb_base, monitor_base)):
        raise ConfigError("xemu_gdb_base and xemu_monitor_base leave no room for that many copies")
    gdb = {gdb_base + i for i in range(count)}
    if gdb & {monitor_base + i for i in range(count)}:
        raise ConfigError("xemu_gdb_base and xemu_monitor_base overlap: the copies' GDB and monitor ports would clash")
    return [CopyPlan(i, copy_name(i), root / copy_name(i), gdb_base + i, monitor_base + i) for i in range(count)]


def copy_settings(install: Install, plan: CopyPlan) -> dict[str, Any]:
    """The copy's xemu.toml: the install's settings, the copy's disk image and EEPROM, the boot ROM and BIOS of the
    install by absolute path, and COPY_SETTINGS."""
    data: dict[str, Any] = tomllib.loads(dump_toml(install.settings))  # a deep copy
    for key in ("bootrom_path", "flashrom_path"):
        path = install.file(key)
        if path is not None:
            set_setting(data, "sys.files", key, str(path))
    set_setting(data, "sys.files", "hdd_path", str(plan.path / HDD_FILE))
    set_setting(data, "sys.files", "eeprom_path", str(plan.path / EEPROM_FILE))
    for table, key, value in COPY_SETTINGS:
        set_setting(data, table, key, value)
    return data


def _same_file(source: Path, target: Path) -> bool:
    """Whether `target` is already a copy of `source` (size and modification time, which copy2 carries over)."""
    if not target.is_file():
        return False
    a, b = source.stat(), target.stat()
    return a.st_size == b.st_size and a.st_mtime_ns == b.st_mtime_ns


def setup_copy(install: Install, plan: CopyPlan, refresh_hdd: bool, log: Callable[[str], None], dry_run: bool) -> None:
    """Make or update one copy (see the module comment). The disk image and EEPROM are copied only when the copy has
    none (or with `refresh_hdd`, which replaces the image and so the copy's snapshots)."""
    verb = "would" if dry_run else "did"
    log(
        f"  {plan.name}: {'update' if plan.path.exists() else 'create'} {plan.path}" + (" (dry run)" if dry_run else "")
    )
    if not dry_run:
        plan.path.mkdir(parents=True, exist_ok=True)
    copied = 0
    for source in install.program_files():
        target = plan.path / source.name
        if not _same_file(source, target):
            copied += 1
            if not dry_run:
                shutil.copy2(source, target)
    log(f"    {verb} copy {copied} program file(s)")
    for key, relative, replace in (("hdd_path", HDD_FILE, refresh_hdd), ("eeprom_path", EEPROM_FILE, False)):
        found = install.file(key)
        target = plan.path / relative
        if found is None or not found.is_file():
            if key == "hdd_path":
                raise ConfigError(f"{install.settings_path}: [sys.files] hdd_path names no disk image")
            continue
        if target.exists() and not replace:
            continue
        log(f"    {verb} copy {found.name} to {relative.as_posix()}")
        if not dry_run:
            target.parent.mkdir(parents=True, exist_ok=True)
            partial = target.with_name(target.name + ".part")
            shutil.copyfile(found, partial)
            partial.replace(target)
    settings = dump_toml(copy_settings(install, plan))
    ports = f"# Written by `coney-tools xemu setup-copies`.\ngdb_port = {plan.gdb}\nmonitor_port = {plan.monitor}\n"
    for name, text in ((SETTINGS_FILE, settings), (COPY_FILE, ports)):
        path = plan.path / name
        current = path.read_text(encoding="utf-8") if path.is_file() else None
        if current != text:
            log(f"    {verb} write {name}")
            if not dry_run:
                path.write_text(text, encoding="utf-8")


def setup_copies(
    install: Install,
    plans: list[CopyPlan],
    running: Callable[[Path], list[int]],
    refresh_hdd: bool = False,
    log: Callable[[str], None] = print,
    dry_run: bool = False,
) -> list[str]:
    """Set up every planned copy; returns the names refused because their xemu runs (`running` gives the pids of the
    xemu processes started from a folder)."""
    log(f"xemu install: {install.folder} (settings {install.settings_path})" + (" (dry run)" if dry_run else ""))
    refused = []
    for plan in plans:
        pids = running(plan.path) if plan.path.exists() else []
        if pids:
            log(f"  {plan.name}: refused, its xemu runs (pid {', '.join(map(str, pids))}); release it and run again")
            refused.append(plan.name)
            continue
        setup_copy(install, plan, refresh_hdd, log, dry_run)
    return refused

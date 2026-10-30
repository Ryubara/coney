# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools xemu ...`: the xemu copies (claims, set-up, launch), a quick read of the running Xbox build, and run
records for what was observed in it (docs/guides/coney-tools-xemu.md#xemu).

The copies and claims are coney_tools.xemu_copies and coney_tools.xemu_claims, the connections coney_tools.xemu_debug,
and the MCP server agents drive the game with coney_tools.xemu_mcp. Nothing here takes the keyboard focus: xemu is
started by WMI, outside the agent's process tree (coney_tools.pcsx2_proc.start_detached).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import time
from dataclasses import asdict, replace
from pathlib import Path

from coney_tools import pcsx2_proc, run_records_cli, xemu_claims, xemu_copies
from coney_tools.config import (
    INT_KEYS,
    ConfigError,
    find_repo_root,
    load_config,
    local_config_file,
    main_checkout,
    workspace_folder,
)
from coney_tools.pcsx2_claims import ClaimError, port_open
from coney_tools.run_records import UNKNOWN
from coney_tools.wad import refuse_inside_repo
from coney_tools.xemu_debug import SNAPSHOT_NAME, XemuSession
from coney_tools.xemu_mcp import parse_address

#: The value types `xemu read` decodes (little-endian, as the x86 guest).
READ_TYPES: dict[str, str] = {"hex": "", "u8": "<B", "u16": "<H", "u32": "<I", "i32": "<i", "f32": "<f"}
#: Where a person usually keeps xemu on Windows, below a few known folders: `xemu find-install` looks there.
SEARCH_FOLDERS = ("Desktop", "Downloads", "Documents")
_PROGRAM_FOLDERS = ("ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA", "APPDATA")


def _config() -> tuple[dict[str, Path | None], dict[str, int], Path]:
    """coney.local.toml's paths and numbers (defaults without one) and the main checkout."""
    root = find_repo_root(Path.cwd())
    main = main_checkout(root)
    if local_config_file(root) is None:
        return {}, dict(INT_KEYS), main
    config = load_config(root)
    return config.paths, config.numbers, main


def _registry(max_age_hours: float | None = None) -> xemu_claims.Registry:
    """This machine's registry, with the stale age from `--max-age-hours` when given."""
    registry = xemu_claims.default_registry()
    if max_age_hours is not None:
        registry.max_age = max_age_hours * 3600
    return registry


# --- claims ---------------------------------------------------------------------------------------------------------


def run_claim(agent: str, copy: str | None, as_json: bool, max_age_hours: float | None) -> int:
    """`xemu claim`: claim a free copy (or the named one) and print its folder and ports."""
    claim, took_over = _registry(max_age_hours).claim(agent, copy)
    if as_json:
        print(json.dumps({**asdict(claim), "took_over": asdict(took_over) if took_over else None}))
        return 0
    if took_over is not None:
        print(f"took over a stale claim of {took_over.agent} (since {took_over.time}) on {claim.copy}")
    print(
        f"{agent} holds {claim.copy}: folder {claim.path}, GDB port {claim.gdb_port}, monitor port {claim.monitor_port}"
    )
    print(f'pass agent: "{agent}" on every xemu_* call of the xemu MCP server; it connects to this copy only')
    return 0


def run_release(agent: str, copy: str | None, force: bool) -> int:
    """`xemu release`: close the copy's xemu and drop the claim."""
    for claim, closed in _registry().release(agent, copy, force):
        print(f"released {claim.copy} (held by {claim.agent}){'; closed its xemu' if closed else ''}")
    return 0


def _state(row: xemu_claims.Row) -> str:
    """The state word of a status row."""
    if row.claim is None:
        return "in use?" if row.running else "free"
    return "stale" if row.stale else "held"


def status_rows(max_age_hours: float | None = None) -> list[dict[str, object]]:
    """The copies as `xemu status --json` lists them."""
    return [
        {
            "copy": r.copy.name,
            "path": str(r.copy.path),
            "gdb_port": r.copy.gdb,
            "monitor_port": r.copy.monitor,
            "state": _state(r),
            "claim": asdict(r.claim) if r.claim else None,
            "running": r.running,
            "pids": list(r.pids),
        }
        for r in _registry(max_age_hours).status()
    ]


def run_status(as_json: bool, max_age_hours: float | None) -> int:
    """`xemu status`: every copy, who holds it, and whether its xemu runs."""
    rows = status_rows(max_age_hours)
    if as_json:
        print(json.dumps(rows))
        return 0
    table = [("copy", "state", "agent", "since", "gdb", "monitor", "running")]
    for row in rows:
        claim = row["claim"] if isinstance(row["claim"], dict) else None
        pids = row["pids"] if isinstance(row["pids"], list) else []
        running = ("yes (pid " + ",".join(map(str, pids)) + ")") if pids else ("port" if row["running"] else "no")
        table.append(
            (
                str(row["copy"]),
                str(row["state"]),
                str(claim["agent"]) if claim else "-",
                str(claim["time"]) if claim else "-",
                str(row["gdb_port"]),
                str(row["monitor_port"]),
                running,
            )
        )
    widths = [max(len(line[i]) for line in table) for i in range(len(table[0]))]
    for line in table:
        print("  ".join(cell.ljust(width) for cell, width in zip(line, widths, strict=True)).rstrip())
    return 0


def run_check_ports(agent: str, gdb_port: int | None, monitor_port: int | None) -> int:
    """`xemu check-ports`: succeed only when the ports are those of a copy `agent` holds (exit 2 otherwise)."""
    claim = xemu_claims.default_registry().require_ports([agent], gdb_port, monitor_port)
    print(f"ok: {agent} holds {claim.copy}")
    return 0


# --- set-up and launch ------------------------------------------------------------------------------------------------


def run_setup_copies(count: int | None, refresh_hdd: bool, dry_run: bool) -> int:
    """`xemu setup-copies`: make or update the copies from `xemu_install`; 1 when one was refused because it runs."""
    paths, numbers, main = _config()
    install = xemu_copies.find_install(paths.get("xemu_install"), paths.get("xemu_config"))
    root = paths.get("xemu_root") or workspace_folder(main, "emulators")
    refuse_inside_repo(root)
    plans = xemu_copies.plan_copies(
        root,
        count if count is not None else numbers["xemu_copies"],
        numbers["xemu_gdb_base"],
        numbers["xemu_monitor_base"],
    )
    processes = xemu_claims.xemu_processes()

    def running(folder: Path) -> list[int]:
        return sorted(pid for pid, image in processes.items() if image.parent.resolve() == folder.resolve())

    refused = xemu_copies.setup_copies(install, plans, running, refresh_hdd, dry_run=dry_run)
    return 1 if refused else 0


def launch_command(copy: xemu_copies.Copy, paused: bool) -> list[str]:
    """xemu's command line for a copy: QEMU's GDB stub and monitor on the copy's ports, both on localhost only, and
    `-S` to start with the guest stopped."""
    command = [
        str(copy.executable),
        "-gdb",
        f"tcp:127.0.0.1:{copy.gdb}",
        "-monitor",
        f"tcp:127.0.0.1:{copy.monitor},server,nowait",
    ]
    return [*command, "-S"] if paused else command


def set_disc(copy: xemu_copies.Copy, disc: Path) -> None:
    """Point the copy's xemu.toml at the disc image to boot (`[sys.files] dvd_path`)."""
    settings_path = copy.path / xemu_copies.SETTINGS_FILE
    settings = xemu_copies.read_toml(settings_path) if settings_path.is_file() else {}
    xemu_copies.set_setting(settings, "sys.files", "dvd_path", str(disc))
    settings_path.write_text(xemu_copies.dump_toml(settings), encoding="utf-8")


def run_launch(agent: str, disc: Path | None, paused: bool, wait: float) -> int:
    """`xemu launch`: start the claimed copy's xemu on the disc image (never taking the focus) and wait until its
    monitor answers. A copy whose xemu already runs is left as it is."""
    registry = xemu_claims.default_registry()
    claim, _ = registry.claim(agent)
    copy = registry.copy(claim.copy)
    row = next(r for r in registry.status() if r.copy.name == copy.name)
    if row.pids:
        print(f"{copy.name}: xemu already runs (pid {', '.join(map(str, row.pids))}); nothing started")
        return 0
    paths, _, _ = _config()
    image = disc or paths.get("xemu_dvd")
    if image is None or not image.is_file():
        raise ConfigError("no disc image: pass --dvd or set xemu_dvd in coney.local.toml (an XISO of your own disc)")
    refuse_inside_repo(image)
    if not copy.executable.is_file():
        raise ConfigError(f"{copy.name}: no {copy.executable.name}; run `coney-tools xemu setup-copies`")
    set_disc(copy, image)
    pid = pcsx2_proc.start_detached(launch_command(copy, paused))
    registry.record_pid(claim, pid)
    deadline = time.monotonic() + wait
    while not port_open(copy.monitor):
        if not pcsx2_proc.pid_alive(pid):
            raise ConfigError(f"{copy.name}: xemu (pid {pid}) ended before its monitor answered")
        if time.monotonic() > deadline:
            raise ConfigError(f"{copy.name}: xemu (pid {pid}) runs but its monitor did not answer within {wait:g} s")
        time.sleep(0.25)
    print(f"{agent} runs {copy.name}: xemu pid {pid}, GDB port {copy.gdb}, monitor port {copy.monitor}")
    return 0


# --- reading and recording ------------------------------------------------------------------------------------------


def format_values(address: int, data: bytes, kind: str) -> list[str]:
    """What `xemu read` prints: hex rows, or one `0x<address> <value>` line per value."""
    if kind == "hex":
        return [f"0x{address + i:08x}: {data[i : i + 16].hex(' ')}" for i in range(0, len(data), 16)]
    size = struct.calcsize(READ_TYPES[kind])
    return [f"0x{address + i * size:08x} {v!r}" for i, (v,) in enumerate(struct.iter_unpack(READ_TYPES[kind], data))]


def run_read(agent: str, address: str, kind: str, count: int) -> int:
    """`xemu read`: read values from the claimed copy's running game (stopped only for the read)."""
    start = parse_address(address)
    size = struct.calcsize(READ_TYPES[kind]) if kind != "hex" else 1
    if not 1 <= count * size <= 4096:
        raise ConfigError("read 1 to 4096 bytes")
    _, copy = xemu_claims.default_registry().require_claim(agent)
    session = XemuSession(copy)
    try:
        data = session.read(start, count * size)
    finally:
        session.close()
    print("\n".join(format_values(start, data, kind)))
    return 0


def run_record_run(args: argparse.Namespace) -> int:
    """`xemu record-run`: write a run record for an observation made on the claimed copy, with xemu's release from
    the copy's `xemu.log` (QEMU's version from the monitor when the log names none)."""
    values = run_records_cli.parse_values(args.value)
    addresses = run_records_cli.parse_addresses(args.address)
    root = find_repo_root(Path.cwd())
    registry = xemu_claims.default_registry()
    _, copy = registry.require_claim(args.agent, args.copy)
    snapshot = args.snapshot or UNKNOWN
    if snapshot != UNKNOWN and not SNAPSHOT_NAME.match(snapshot):
        raise ConfigError(
            f"--snapshot {snapshot!r}: a snapshot name of the copy (letters, digits, `.`, `_`, `-`), or unknown"
        )
    pending = run_records_cli.draft(
        root,
        kind="method",
        tool="xemu record-run",
        run_name=args.name,
        copy=copy.name,
        state=UNKNOWN,
        method=args.method,
        values=values,
        addresses=addresses,
        emulator="xemu",
    )
    pending = replace(pending, state=re.sub(r"[^a-z0-9]+", "-", snapshot.lower()).strip("-")[:48] or UNKNOWN)
    run_records_cli.precheck(root, pending)
    session = XemuSession(copy)
    try:
        version = xemu_version(session.version())
    except ConfigError as error:
        raise ConfigError(f"{copy.name}: {error}") from error
    finally:
        session.close()
    run_records_cli.save(root, replace(pending, pcsx2_version=version))
    return 0


def xemu_version(text: str) -> str:
    """xemu's version as a record keeps it, from `XemuSession.version`: the xemu release (`v0.8.139` from
    `xemu v0.8.139 (QEMU 10.2.0)`, or `v0.8.110` from `... (xemu-v0.8.110)`) when the text names one, else the QEMU
    version it is built on, else `unknown`."""
    found = re.search(r"xemu[- ]?v?(\d+\.\d+\.\d+)", text, re.IGNORECASE)
    return f"v{found.group(1)}" if found else run_records_cli.short_version(text)


def find_installs(env: dict[str, str]) -> list[Path]:
    """Folders that hold an xemu program in the usual places: the user's Desktop, Downloads and Documents, the program
    folders and xemu's own settings folder, up to three levels down. Read-only; nothing is downloaded or changed."""
    bases: list[Path] = []
    home = env.get("USERPROFILE") or env.get("HOME")
    if home:
        bases += [Path(home) / name for name in SEARCH_FOLDERS]
        bases.append(Path(home) / "OneDrive" / "Desktop")
    bases += [Path(env[key]) for key in _PROGRAM_FOLDERS if env.get(key)]
    found: list[Path] = []
    for base in bases:
        if not base.is_dir():
            continue
        for depth in ("", "*", "*/*", "*/*/*"):
            for exe in base.glob(f"{depth}/xemu.exe" if depth else "xemu.exe"):
                if exe.parent not in found:
                    found.append(exe.parent)
    return found


def run_find_install() -> int:
    """`xemu find-install`: print the xemu installs found and the settings file of each (exit 1 when none)."""
    found = find_installs(dict(os.environ))
    user = xemu_copies.user_settings_file(dict(os.environ))
    for folder in found:
        portable = folder / xemu_copies.SETTINGS_FILE
        settings = portable if portable.is_file() else user if user is not None and user.is_file() else None
        print(f"{folder}  (settings: {settings or 'none yet: start it once'})")
    if not found:
        print("no xemu install found in the usual places; set xemu_install in coney.local.toml to yours")
        return 1
    print("set xemu_install in coney.local.toml to the one to copy, then run `coney-tools xemu setup-copies`")
    return 0


# --- the parser -----------------------------------------------------------------------------------------------------


def add_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Register `coney-tools xemu ...`."""
    group = groups.add_parser(
        "xemu", help="the xemu copies that run the Xbox build: claims, set-up, reads, run records"
    )
    commands = group.add_subparsers(dest="command", required=True)
    claim = commands.add_parser("claim", help="claim a free xemu copy for an agent (every xemu use starts here)")
    claim.add_argument("--agent", required=True, help="your id: letters, digits, '.', '_' and '-'")
    claim.add_argument("--copy", help="a copy by name (xemu, xemu-b, ...); default: the first free one")
    claim.add_argument("--json", action="store_true", help="print the claim as one JSON line")
    claim.add_argument("--max-age-hours", type=float, help="a claim with no process is stale after this (default 4)")
    release = commands.add_parser("release", help="close the copy's xemu and drop the claim")
    release.add_argument("--agent", required=True, help="your id")
    release.add_argument("--copy", help="one copy; default: all of the agent's")
    release.add_argument("--force", action="store_true", help="release another agent's claim")
    status = commands.add_parser("status", help="who holds each xemu copy and whether its xemu runs")
    status.add_argument("--json", action="store_true", help="print JSON instead of a table")
    status.add_argument("--max-age-hours", type=float, help="a claim with no process is stale after this (default 4)")
    check = commands.add_parser("check-ports", help="exit 2 unless the ports are those of a copy you hold")
    check.add_argument("--agent", required=True, help="your id (must hold the claim)")
    check.add_argument("--gdb-port", type=int, help="the GDB stub port you are about to connect to")
    check.add_argument("--monitor-port", type=int, help="the monitor port you are about to connect to")
    setup = commands.add_parser(
        "setup-copies", help="make or update the xemu copies from your own install (xemu_install)"
    )
    setup.add_argument("--count", type=int, help="how many copies: xemu, xemu-b, ... (default: xemu_copies, 2)")
    setup.add_argument(
        "--refresh-hdd", action="store_true", help="copy the install's disk image again (the copy's snapshots go)"
    )
    setup.add_argument("--dry-run", action="store_true", help="print what would change, change nothing")
    launch = commands.add_parser("launch", help="start your claimed copy's xemu (never taking the focus)")
    launch.add_argument("--agent", required=True, help="your id; xemu starts under your claim (made if you hold none)")
    launch.add_argument("--dvd", type=Path, help="the disc image to boot (default: xemu_dvd)")
    launch.add_argument("--paused", action="store_true", help="start with the guest stopped (-S), for a breakpoint")
    launch.add_argument("--wait", type=float, default=60.0, help="seconds to wait for the monitor (default 60)")
    read = commands.add_parser("read", help="read values from your claimed copy's running game (a quick look)")
    read.add_argument("--agent", required=True, help="your id (must hold the claim)")
    read.add_argument("address", help="an XBE virtual address, such as 0x0032dd40")
    read.add_argument("--type", choices=tuple(READ_TYPES), default="hex", help="how to show it (default hex)")
    read.add_argument("--count", type=int, default=16, help="values (or hex bytes) to read (default 16)")
    run = commands.add_parser(
        "record-run", help="write a run record for an observation made on your claimed copy (research/runs/)"
    )
    run.add_argument("--agent", required=True, help="your id (must hold the claim)")
    run.add_argument("--copy", help="the claimed copy (default: the one you hold)")
    run.add_argument("--snapshot", help="the snapshot of the copy the run started from, or unknown (default)")
    run.add_argument("--name", required=True, help="what the run is about; the record's id is r-<date>-<name>")
    run.add_argument("--method", required=True, help="how it was observed, in words (a read, a watch, a frame)")
    run.add_argument("--value", action="append", default=[], metavar="NAME=NUMBER", help="a number read; repeatable")
    run.add_argument(
        "--address", action="append", default=[], metavar="NAME=0xADDRESS", help="an address named; repeatable"
    )
    commands.add_parser("find-install", help="look for an xemu install in the usual places (read-only)")


def run(args: argparse.Namespace) -> int:
    """Dispatch `coney-tools xemu ...`."""
    if args.command == "claim":
        return run_claim(args.agent, args.copy, args.json, args.max_age_hours)
    if args.command == "release":
        return run_release(args.agent, args.copy, args.force)
    if args.command == "status":
        return run_status(args.json, args.max_age_hours)
    if args.command == "check-ports":
        return run_check_ports(args.agent, args.gdb_port, args.monitor_port)
    if args.command == "setup-copies":
        return run_setup_copies(args.count, args.refresh_hdd, args.dry_run)
    if args.command == "launch":
        return run_launch(args.agent, args.dvd, args.paused, args.wait)
    if args.command == "read":
        return run_read(args.agent, args.address, args.type, args.count)
    if args.command == "record-run":
        return run_record_run(args)
    if args.command == "find-install":
        return run_find_install()
    raise ClaimError(f"unknown xemu command {args.command!r}")

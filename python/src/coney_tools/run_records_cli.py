# SPDX-License-Identifier: GPL-3.0-or-later
"""The commands that write run records (docs/guides/research-evidence.md#run-records): `pcsx2 record-run` for an
observation made by hand (through the pcsx2 MCP server), and the `--record` of `pcsx2 recipes run` and `--run-record` of
`trace original`, which write the record of the run they made. The record itself is coney_tools.run_records.

Every record is written by a tool that ran under a claim on a PCSX2 copy that is running now, and fills in what the
tool knows itself: the date, who ran it, the PCSX2 version, the copy, the recipe or scenario, the patches and the
state's patches from the library. What the engineer adds is the state's id, a name and the method or the values.
"""

from __future__ import annotations

import hashlib
import math
import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, replace
from datetime import UTC, date, datetime
from pathlib import Path
from typing import Any

from coney_tools import pcsx2_claims as claims
from coney_tools import run_records
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.pine import PineClient, PineError
from coney_tools.recorder import Recording
from coney_tools.run_records import UNKNOWN, RunRecord
from coney_tools.stamps import who_from_env

_VERSION = re.compile(r"v?(\d+\.\d+\.\d+)")
_ADDRESS = re.compile(r"^0[xX][0-9a-fA-F]{1,16}$")


@dataclass(frozen=True)
class RecordRequest:
    """What an engineer says about a run that is to be recorded: the library state the copy was started from (an id
    of `pcsx2 states`, or `unknown`) and the name of the run (default: the recipe's or the scenario's)."""

    state: str
    name: str | None = None


def trace_request(run_record: bool, state: str | None, name: str | None) -> RecordRequest | None:
    """The `--run-record` of a `trace original`: what to record, or None without the flag. ConfigError for the other two
    flags without it, or for `--run-record` without `--library-state`."""
    if not run_record:
        if state is not None or name is not None:
            raise ConfigError("--library-state and --run-name belong to --run-record")
        return None
    if state is None:
        raise ConfigError(
            "--run-record needs --library-state ID (the state library id the run started from, or unknown)"
        )
    return RecordRequest(state=state, name=name)


def today() -> date:
    """The day in UTC; a function so that tests can set the clock."""
    return datetime.now(UTC).date()


def short_version(text: str) -> str:
    """PCSX2's version text as a record keeps it: `v2.9.94` when the text holds a version number, else the printable
    ASCII of the text cut to 40 characters, else `unknown`."""
    found = _VERSION.search(text)
    if found:
        return f"v{found.group(1)}"
    plain = "".join(c for c in text if " " <= c <= "~").strip()[:40].strip()
    return plain or UNKNOWN


def pcsx2_version(client: PineClient) -> str:
    """The version of the PCSX2 behind `client`, as `short_version` writes it."""
    return short_version(client.version())


def parse_values(pairs: Sequence[str]) -> dict[str, int | float]:
    """`NAME=NUMBER` pairs (a whole number as `5` or `0x10`, else a decimal) as a dict. Raises ConfigError for a pair
    without `=`, a text that is no number, one that is not finite or a name given twice."""
    out: dict[str, int | float] = {}
    for pair in pairs:
        name, sep, text = pair.partition("=")
        name = name.strip()
        if not sep or not name:
            raise ConfigError(f"--value {pair!r}: write NAME=NUMBER")
        if name in out:
            raise ConfigError(f"--value {name}: given twice")
        value: int | float
        try:
            value = int(text, 0)
        except ValueError:
            try:
                value = float(text)
            except ValueError:
                raise ConfigError(f"--value {pair!r}: {text!r} is not a number") from None
        if isinstance(value, float) and not math.isfinite(value):
            raise ConfigError(f"--value {pair!r}: a record holds finite numbers only")
        out[name] = value
    return out


def parse_addresses(pairs: Sequence[str]) -> dict[str, int]:
    """`NAME=0xADDRESS` pairs as a dict. Raises ConfigError for a pair without `=`, a text that is not hex written with
    `0x`, one above 0xffffffff or a name given twice."""
    out: dict[str, int] = {}
    for pair in pairs:
        name, sep, text = pair.partition("=")
        name = name.strip()
        if not sep or not name:
            raise ConfigError(f"--address {pair!r}: write NAME=0xADDRESS")
        if name in out:
            raise ConfigError(f"--address {name}: given twice")
        if not _ADDRESS.match(text):
            raise ConfigError(f"--address {pair!r}: {text!r} is not an address (hex with 0x)")
        address = int(text, 16)
        if address > 0xFFFFFFFF:
            raise ConfigError(f"--address {pair!r}: expected an address from 0 to 0xffffffff")
        out[name] = address
    return out


def claimed_copy(agent: str, copy: str) -> claims.Copy:
    """The copy `copy`, when `agent` holds its claim, its PCSX2 runs and PINE is on; ConfigError otherwise (never
    someone else's game, and never a record of a run nobody made)."""
    registry = claims.default_registry()
    registry.require_claim(agent, copy)
    held = registry.copy(copy)
    row = next(r for r in registry.status() if r.copy.name == held.name)
    if not row.pids:
        raise ConfigError(f"{copy} runs no PCSX2; start it with `coney-tools pcsx2 launch --agent {agent} STATE`")
    if held.port is None:
        raise ConfigError(f"{copy}: PINE is off in its ini (`coney-tools pcsx2 setup-copies`)")
    return held


def state_patches(root: Path, state: str) -> tuple[str, ...]:
    """The patches the library says `state` holds; none for `unknown`. ConfigError for an id the library lacks."""
    if state == UNKNOWN:
        return ()
    # Imported here: pcsx2_states reaches pcsx2_cli, which writes run records through this module.
    from coney_tools.pcsx2_states import Library, library_dir

    return Library(library_dir(root)).get(state).patches


def precheck(root: Path, record: RunRecord) -> None:
    """Before a run that is to be recorded: check the record it will write (`draft`, with stand-ins for what only the
    run can tell) against every rule, and refuse one that already exists, so that no run is made only to find out
    afterwards that its record cannot be written."""
    rules = run_records.rules(root)
    run_records.to_toml(record, rules)
    path = run_records.path_of(root, record.id, rules)
    if path.exists():
        raise ConfigError(f"{rules.folder}/{record.id}.toml already exists; give the run another --name")


def shown_numbers(shown: Mapping[str, Any]) -> tuple[dict[str, int | float], int, int]:
    """What a recipe showed, cut to what a record keeps: its numbers by label, then how many texts and how many
    non-finite numbers were left out."""
    numbers: dict[str, int | float] = {}
    texts = infinite = 0
    for label, value in shown.items():
        if isinstance(value, bool) or not isinstance(value, int | float):
            texts += 1
        elif isinstance(value, float) and not math.isfinite(value):
            infinite += 1
        else:
            numbers[label] = value
    return numbers, texts, infinite


#: Stand-ins for what a run measures, as long as each can be, for `precheck` to check the size before the run: PCSX2's
#: version (`short_version` keeps at most 40 characters), the copy's name (40 at most), a trace's counts and its CSV's
#: hash.
VERSION_STAND_IN = "v" + "9" * 39
COPY_STAND_IN = "c" * 40
_COUNT_STAND_IN = -(2**63 - 1)
TRACE_STAND_IN: tuple[dict[str, int | float], dict[str, str]] = (
    dict.fromkeys(("updates", "missed_updates", "columns"), _COUNT_STAND_IN),
    {"trace_csv": "0" * 64},
)


def trace_facts(recording: Recording, csv: bytes) -> tuple[dict[str, int | float], dict[str, str]]:
    """The counts of a recorded trace and the SHA-256 of its CSV file: the record keeps no row of the trace, which is a
    measurement of the game and stays in scratch."""
    values: dict[str, int | float] = {
        "updates": recording.updates,
        "missed_updates": len(recording.missed),
        "columns": len(recording.header) - 1,
    }
    return values, {"trace_csv": hashlib.sha256(csv).hexdigest()}


def draft(
    root: Path,
    *,
    kind: str,
    tool: str,
    run_name: str,
    copy: str,
    state: str,
    name: str = "",
    args: Mapping[str, str] | None = None,
    method: str = "",
    values: Mapping[str, int | float] | None = None,
    addresses: Mapping[str, int] | None = None,
    hashes: Mapping[str, str] | None = None,
    patches: Sequence[str] = (),
    version: str = VERSION_STAND_IN,
    emulator: str = "pcsx2",
) -> RunRecord:
    """The record a run is to write, made before the run for `precheck` (`emulator` names the table its version and
    copy go in: pcsx2, or xemu for the Xbox build): `run_name` makes the id
    (`r-<today>-<run_name>`), `name` is the recipe or scenario. The who comes from the environment
    (docs/guides/stamps.md#who), the patches are the ones given with the state's in the library. What the run measures
    is filled in afterwards (`dataclasses.replace`). Raises ConfigError when the library lacks the state."""
    rules = run_records.rules(root)
    day = today()
    who = who_from_env(tool)
    agent: Mapping[str, str] = who["agent"]
    return RunRecord(
        id=run_records.new_id(run_name, day, rules),
        date=f"{day:%Y-%m-%d}",
        kind=kind,
        tool=tool,
        agent=agent["id"],
        agent_type=agent["type"],
        model=agent["model"],
        source=who["source"],
        pcsx2_version=version,
        copy=copy,
        state=state,
        patches=tuple(sorted({*patches, *state_patches(root, state)})),
        name=name,
        args=dict(args or {}),
        method=method,
        values=dict(values or {}),
        addresses=dict(addresses or {}),
        hashes=dict(hashes or {}),
        emulator=emulator,
    )


def save(root: Path, record: RunRecord) -> None:
    """Write `record` into the checkout at `root` and say where it went. Raises RunRecordError when it breaks a rule
    (nothing is written then)."""
    say_saved(root, record, run_records.write(root, record))


def say_saved(root: Path, record: RunRecord, path: Path) -> None:
    """Tell where the record went, for the engineer to cite: `confirmed (runtime), run <id>`."""
    shown = path.relative_to(root).as_posix() if path.is_relative_to(root) else str(path)
    print(f"recorded {record.id} ({shown}); cite it as `confirmed (runtime), run {record.id}`")


# --- the commands -----------------------------------------------------------------------------------------------------


def add_commands(commands: Any) -> None:
    """Register `pcsx2 record-run` under the pcsx2 group's subcommands."""
    run = commands.add_parser(
        "record-run",
        help="write a run record for an observation made by hand on a PCSX2 copy you hold (research/runs/)",
    )
    run.add_argument("--agent", required=True, help="your id (must hold the claim)")
    run.add_argument("--copy", required=True, help="the claimed copy the observation was made on")
    run.add_argument("--state", required=True, help="the state library id the copy started from, or unknown")
    run.add_argument("--name", required=True, help="what the run is about; the record's id is r-<date>-<name>")
    run.add_argument("--method", required=True, help="how it was observed, in words (a watch, a read, a frame count)")
    run.add_argument("--value", action="append", default=[], metavar="NAME=NUMBER", help="a number read; repeatable")
    run.add_argument(
        "--address", action="append", default=[], metavar="NAME=0xADDRESS", help="an address named; repeatable"
    )
    run.add_argument("--patch", action="append", default=[], metavar="NAME", help="a patch the run needed; repeatable")


def run_command(args: Any) -> int:
    """`pcsx2 record-run`: check the claim and that PCSX2 runs, read its version over PINE and write the record."""
    values = parse_values(args.value)
    addresses = parse_addresses(args.address)
    root = find_repo_root(Path.cwd())
    held = claimed_copy(args.agent, args.copy)
    pending = draft(
        root,
        kind="method",
        tool="pcsx2 record-run",
        run_name=args.name,
        copy=args.copy,
        state=args.state,
        method=args.method,
        values=values,
        addresses=addresses,
        patches=args.patch,
    )
    precheck(root, pending)
    if held.port is None:
        raise ConfigError(f"{args.copy}: PINE is off in its ini")
    try:
        with PineClient(held.port) as pine:
            version = pcsx2_version(pine)
    except PineError as error:
        raise ConfigError(f"{args.copy}: {error}") from error
    save(root, replace(pending, pcsx2_version=version))
    return 0

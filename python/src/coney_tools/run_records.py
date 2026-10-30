# SPDX-License-Identifier: GPL-3.0-or-later
"""Run records: what a research page's `confirmed (runtime)` claim cites (docs/guides/research-evidence.md#run-records).

A record is `research/runs/<id>.toml`, written by the tools that drive PCSX2 (`pcsx2 recipes run --record`,
`trace original --run-record`, `pcsx2 record-run`) or xemu (`xemu record-run`) and by nothing else, so the method cannot
be paraphrased away. It holds who ran it, the emulator's version and copy (a `[pcsx2]` table for the PS2 game, an
`[xemu]` table in its place for the Xbox build), the state and patches it started from, the recipe, the trace or the
method in words, and the values read: names, numbers and addresses only, never memory or game text (LEGAL.md). The
rules (the id, the sizes, the kinds) are `runs` in `.github/conventions.yml`, and the folder is `research.files.runs`
there. Those limits cap how much a record can hold; they do not tell game data from our own (English game text is
printable ASCII too, and numbers can carry bytes), so the reviewer reads the method, the args and the value names
(docs/guides/research-evidence.md#run-records).

```toml
version = 1
id = "r-20261023-grab-reach"
date = "2026-10-23"
kind = "recipe"                  # recipe | trace | method
tool = "pcsx2 recipes run"
digest = "<sha256 of the fields below>"

[who]
agent = "engineer-3"
type = "engineer"
model = "claude-opus-5-5"
source = "hook"                  # hook | unverified (docs/guides/stamps.md#who)

[pcsx2]
version = "v2.9.94"
copy = "pcsx2-b"

[start]
state = "pedestrian-grab"        # the state library's id, or "unknown"
patches = ["scripted-pad"]

[run]
name = "find-human"              # the recipe or the scenario; empty for a method
method = ""                      # the method in words (a method record needs it)

[run.args]
who = "Hero"

[values]
"player.x" = 1.5

[addresses]
human = 0x006e0000

[hashes]
trace_csv = "<sha256>"
```

The digest is the SHA-256 of the record's fields, so a hand edit of a value, a name or the who shows up when the record
is read. It detects an edit; it does not stop someone who writes a whole record and its digest, which is why the
reviewer reads the values against the claim (docs/guides/research-evidence.md#run-records).
"""

from __future__ import annotations

import hashlib
import json
import math
import re
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass, field, replace
from datetime import date as Date
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError
from coney_tools.conventions import Runs, load_conventions

VERSION = 1
#: Who ran it: the hook dispatcher's marker, or what the command's environment said (docs/guides/stamps.md#who).
SOURCES = ("hook", "unverified")
#: The emulators a record can name, each its own table: PCSX2 runs the PS2 game, xemu the Xbox build.
EMULATORS = ("pcsx2", "xemu")
#: A state of the library (`pcsx2 states`) or this word when the run did not say.
UNKNOWN = "unknown"
_STATE = re.compile(r"^[a-z0-9][a-z0-9-]{0,47}$")
_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
_BARE = re.compile(r"^[A-Za-z0-9_-]+$")
_DAY = re.compile(r"^(\d{4})-(\d{2})-(\d{2})$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_ID_DAY = re.compile(r"^r-(\d{4})(\d{2})(\d{2})-")
#: Printable ASCII only, on one line: it keeps a record plain text that diffs and reads well. It does not keep game
#: text out (the game's text is English), and nor do the size limits: the reviewer reads what a record says.
_PRINTABLE = re.compile(r"^[\x20-\x7e]*$")


class RunRecordError(ConfigError):
    """A run record that does not meet the rules, or a missing one; the message names the field or the file."""


@dataclass(frozen=True)
class RunRecord:
    """One run, as the file holds it."""

    id: str
    #: `YYYY-MM-DD`, UTC.
    date: str
    #: One of `runs.kinds`: a recipe, a recorded trace, or a method in words.
    kind: str
    #: The command that wrote it.
    tool: str
    agent: str
    agent_type: str
    model: str
    #: `hook` or `unverified` (docs/guides/stamps.md#who).
    source: str
    #: The emulator's version (PCSX2's, or xemu's when `emulator` is xemu).
    pcsx2_version: str
    copy: str
    #: The state library's id of the state the run started from, or `unknown`.
    state: str
    patches: tuple[str, ...]
    #: The recipe or the scenario; empty for a method.
    name: str
    #: The recipe's arguments as given, or the trace's `--let` names.
    args: Mapping[str, str]
    #: The method in words; required for a method record.
    method: str
    #: What was read: names to numbers.
    values: Mapping[str, int | float]
    #: Addresses (of memory in the original) the run names, written in hex.
    addresses: Mapping[str, int]
    #: SHA-256 of a file the run made (a trace's CSV), by name; the file itself stays in scratch.
    hashes: Mapping[str, str] = field(default_factory=dict)
    #: Which emulator ran it (EMULATORS): its table in the file. A PCSX2 record's fields, and so its digest, are
    #: what they were before xemu records existed.
    emulator: str = "pcsx2"


def rules(root: Path) -> Runs:
    """The run-record rules of the checkout at `root` (`runs` in `.github/conventions.yml`)."""
    return load_conventions(root).runs


def new_id(name: str, day: Date, rules_: Runs) -> str:
    """`r-<YYYYMMDD>-<name>`: `name` lower-cased with every run of other characters made a hyphen and cut to fit.
    Raises RunRecordError when nothing of it is left or the result is not an id."""
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")[:48].strip("-")
    if not slug:
        raise RunRecordError(f"{name!r}: a run's name needs a letter or a digit")
    run_id = f"r-{day:%Y%m%d}-{slug}"
    if not re.fullmatch(rules_.id_pattern, run_id):
        raise RunRecordError(f"{run_id}: not a run id (runs.id_pattern)")
    return run_id


# --- checking ---------------------------------------------------------------------------------------------------------


def _text(value: object, what: str, limit: int, empty_ok: bool = False) -> str:
    """`value` as a short line of printable ASCII, or RunRecordError naming `what`."""
    if not isinstance(value, str):
        raise RunRecordError(f"{what}: expected text")
    if not value and not empty_ok:
        raise RunRecordError(f"{what}: must not be empty")
    if "\n" in value or "\r" in value:
        raise RunRecordError(f"{what}: must be on one line")
    if not _PRINTABLE.match(value):
        raise RunRecordError(f"{what}: only printable ASCII (a run record holds our own words, never game text)")
    if len(value) > limit:
        raise RunRecordError(f"{what}: {len(value)} characters, longer than {limit}")
    return value


def _number(value: object, what: str) -> int | float:
    """`value` as a finite int or float (not a boolean)."""
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise RunRecordError(f"{what}: expected a number")
    if isinstance(value, float) and not math.isfinite(value):
        raise RunRecordError(f"{what}: expected a finite number")
    if isinstance(value, int) and abs(value) >= 1 << 63:
        raise RunRecordError(f"{what}: out of range")
    return value


def _check(record: RunRecord, rules_: Runs) -> None:
    """RunRecordError unless `record` meets every rule."""
    limit = rules_.max_text
    if not re.fullmatch(rules_.id_pattern, record.id):
        raise RunRecordError(f"{record.id!r}: not a run id (runs.id_pattern: {rules_.id_pattern})")
    day = _DAY.match(record.date)
    try:
        parsed = Date(int(day.group(1)), int(day.group(2)), int(day.group(3))) if day else None
    except ValueError:
        parsed = None
    if parsed is None:
        raise RunRecordError(f"date {record.date!r}: expected YYYY-MM-DD")
    id_day = _ID_DAY.match(record.id)
    if id_day is None or f"{id_day.group(1)}-{id_day.group(2)}-{id_day.group(3)}" != record.date:
        raise RunRecordError(f"date {record.date}: the id {record.id} must start with the same day")
    if record.kind not in rules_.kinds:
        raise RunRecordError(f"kind {record.kind!r}: expected one of {', '.join(rules_.kinds)}")
    _text(record.tool, "tool", 64)
    _text(record.agent, "who.agent", 64)
    _text(record.agent_type, "who.type", 64)
    _text(record.model, "who.model", 64)
    if record.source not in SOURCES:
        raise RunRecordError(f"who.source {record.source!r}: expected hook or unverified")
    if record.emulator not in EMULATORS:
        raise RunRecordError(f"emulator {record.emulator!r}: expected one of {', '.join(EMULATORS)}")
    _text(record.pcsx2_version, f"{record.emulator} version", 40)
    _text(record.copy, f"{record.emulator} copy", 40)
    if record.state != UNKNOWN and not _STATE.match(record.state):
        raise RunRecordError(f"start.state {record.state!r}: a state library id (a slug) or {UNKNOWN}")
    for patch in record.patches:
        if not _NAME.match(patch):
            raise RunRecordError(f"start.patches: {patch!r} is not a patch name")
    _text(record.name, "run.name", 64, empty_ok=True)
    if record.kind != "method" and not record.name:
        what = "recipe" if record.kind == "recipe" else "scenario"
        raise RunRecordError(f"a {record.kind} record needs the name of its {what}")
    _text(record.method, "run.method", limit, empty_ok=True)
    if record.kind == "method" and not record.method:
        raise RunRecordError("a method record needs the method in words")
    for key, value in record.args.items():
        _text(key, "run.args name", 64)
        _text(value, f"run.args.{key}", limit, empty_ok=True)
    count = len(record.values) + len(record.addresses) + len(record.args)
    if count > rules_.max_values:
        raise RunRecordError(f"{count} values, addresses and args, more than {rules_.max_values} (runs.max_values)")
    for key, number in record.values.items():
        _text(key, "a value's name", 120)
        _number(number, f"values.{key}")
    for key, address in record.addresses.items():
        _text(key, "an address's name", 120)
        if isinstance(address, bool) or not isinstance(address, int) or not 0 <= address <= 0xFFFFFFFF:
            raise RunRecordError(f"addresses.{key}: expected an address from 0 to 0xffffffff")
    for key, digest in record.hashes.items():
        _text(key, "a hash's name", 64)
        if not isinstance(digest, str) or not _SHA256.match(digest):
            raise RunRecordError(f"hashes.{key}: expected a SHA-256 (64 lower-case hex digits)")


# --- the file ---------------------------------------------------------------------------------------------------------


def _fields(record: RunRecord) -> dict[str, Any]:
    """The record as the plain data the digest covers and the file shows, in file order."""
    return {
        "version": VERSION,
        "id": record.id,
        "date": record.date,
        "kind": record.kind,
        "tool": record.tool,
        "who": {"agent": record.agent, "type": record.agent_type, "model": record.model, "source": record.source},
        record.emulator: {"version": record.pcsx2_version, "copy": record.copy},
        "start": {"state": record.state, "patches": list(record.patches)},
        "run": {"name": record.name, "method": record.method, "args": dict(record.args)},
        "values": dict(record.values),
        "addresses": dict(record.addresses),
        "hashes": dict(record.hashes),
    }


def digest_of(record: RunRecord) -> str:
    """The SHA-256 of the record's fields (the canonical JSON: sorted keys, no spaces)."""
    body = json.dumps(_fields(record), sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(body.encode("ascii")).hexdigest()


def _key(name: str) -> str:
    """A TOML key: bare when it can be, else a quoted string."""
    return name if _BARE.match(name) else json.dumps(name)


def _scalar(value: object, hexadecimal: bool = False) -> str:
    """One TOML value."""
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, float):
        return repr(value)
    assert isinstance(value, int)
    return f"0x{value:08x}" if hexadecimal else str(value)


def to_toml(record: RunRecord, rules_: Runs) -> str:
    """The text of `record`'s file, after checking it against every rule the reader applies, its size included, so the
    writer never makes a file the reader refuses. Raises RunRecordError for a record that breaks a rule."""
    _check(record, rules_)
    text = _render(record)
    size = len(text.encode("utf-8"))
    if size > rules_.max_bytes:
        raise RunRecordError(f"{size} bytes, more than runs.max_bytes ({rules_.max_bytes})")
    return text


def fit(record: RunRecord, numbers: Mapping[str, int | float], rules_: Runs) -> tuple[dict[str, int | float], int]:
    """Of `numbers` (what a run showed, known only after it), the ones `record` can take in order without breaking a
    rule (a name a record allows, the count, the size), and how many were left out. A run that went well is then
    always recorded, with what fits."""
    kept: dict[str, int | float] = dict(record.values)
    left = 0
    for name, value in numbers.items():
        trial = {**kept, name: value}
        try:
            to_toml(replace(record, values=trial), rules_)
        except RunRecordError:
            left += 1
            continue
        kept = trial
    return kept, left


def _render(record: RunRecord) -> str:
    """The text of `record`'s file, unchecked."""
    out = [
        f"version = {VERSION}",
        f"id = {_scalar(record.id)}",
        f"date = {_scalar(record.date)}",
        f"kind = {_scalar(record.kind)}",
        f"tool = {_scalar(record.tool)}",
        f"digest = {_scalar(digest_of(record))}",
        "",
        "[who]",
        f"agent = {_scalar(record.agent)}",
        f"type = {_scalar(record.agent_type)}",
        f"model = {_scalar(record.model)}",
        f"source = {_scalar(record.source)}",
        "",
        f"[{record.emulator}]",
        f"version = {_scalar(record.pcsx2_version)}",
        f"copy = {_scalar(record.copy)}",
        "",
        "[start]",
        f"state = {_scalar(record.state)}",
        f"patches = [{', '.join(_scalar(p) for p in record.patches)}]",
        "",
        "[run]",
        f"name = {_scalar(record.name)}",
        f"method = {_scalar(record.method)}",
    ]
    for title, table, hexadecimal in (
        ("run.args", record.args, False),
        ("values", record.values, False),
        ("addresses", record.addresses, True),
        ("hashes", record.hashes, False),
    ):
        if table:
            out += ["", f"[{title}]", *(f"{_key(k)} = {_scalar(v, hexadecimal)}" for k, v in table.items())]
    return "\n".join(out) + "\n"


def _table(data: Mapping[str, Any], key: str, known: tuple[str, ...], where: str) -> Mapping[str, Any]:
    """`data[key]` as a table with only the `known` keys (a missing table is empty)."""
    table = data.get(key, {})
    if not isinstance(table, dict):
        raise RunRecordError(f"{where}{key}: expected a table")
    for name in table:
        if name not in known:
            raise RunRecordError(f"{where}{key}: unknown key '{name}' (known: {', '.join(known)})")
    return table


def _free(table: object, what: str) -> dict[str, Any]:
    """A table whose keys are the author's own names."""
    if not isinstance(table, dict):
        raise RunRecordError(f"{what}: expected a table")
    return table


def _string(table: Mapping[str, Any], key: str, where: str) -> str:
    """`table[key]` (empty when missing) when it is text; RunRecordError otherwise, for a value TOML read as another
    type, such as an unquoted date or a number, so that a bad file is reported and never crashes the reader."""
    value = table.get(key, "")
    if not isinstance(value, str):
        raise RunRecordError(f"{where}{key} {value!r}: expected text in quotes, not a {type(value).__name__}")
    return value


def parse(text: str, rules_: Runs) -> RunRecord:
    """The record in `text`, checked against the rules and its digest. Raises RunRecordError naming the problem."""
    size = len(text.encode("utf-8"))
    if size > rules_.max_bytes:
        raise RunRecordError(
            f"{size} bytes, more than runs.max_bytes ({rules_.max_bytes}): a record holds numbers only"
        )
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise RunRecordError(f"not valid TOML ({error})") from error
    top = (
        "version",
        "id",
        "date",
        "kind",
        "tool",
        "digest",
        "who",
        *EMULATORS,
        "start",
        "run",
        "values",
        "addresses",
        "hashes",
    )
    for name in data:
        if name not in top:
            raise RunRecordError(f"unknown key '{name}' (known: {', '.join(top)})")
    if data.get("version") != VERSION or isinstance(data.get("version"), bool):
        raise RunRecordError(f"version {data.get('version')!r}: this tool reads version {VERSION}")
    who = _table(data, "who", ("agent", "type", "model", "source"), "")
    present = [name for name in EMULATORS if name in data]
    if len(present) != 1:
        raise RunRecordError(f"a record names one emulator, a table of {' or '.join(EMULATORS)}; it has {len(present)}")
    emulator = present[0]
    pcsx2 = _table(data, emulator, ("version", "copy"), "")
    start = _table(data, "start", ("state", "patches"), "")
    run = _table(data, "run", ("name", "method", "args"), "")
    patches = start.get("patches", [])
    if not isinstance(patches, list):
        raise RunRecordError("start.patches: expected a list")
    record = RunRecord(
        id=_string(data, "id", ""),
        date=_string(data, "date", ""),
        kind=_string(data, "kind", ""),
        tool=_string(data, "tool", ""),
        agent=_string(who, "agent", "who."),
        agent_type=_string(who, "type", "who."),
        model=_string(who, "model", "who."),
        source=_string(who, "source", "who."),
        pcsx2_version=_string(pcsx2, "version", f"{emulator}."),
        copy=_string(pcsx2, "copy", f"{emulator}."),
        state=_string(start, "state", "start."),
        patches=tuple(_string({"patch": p}, "patch", "start.patches ") for p in patches),
        name=_string(run, "name", "run."),
        args=_free(run.get("args", {}), "run.args"),
        method=_string(run, "method", "run."),
        values=_free(data.get("values", {}), "values"),
        addresses=_free(data.get("addresses", {}), "addresses"),
        hashes=_free(data.get("hashes", {}), "hashes"),
        emulator=emulator,
    )
    _check(record, rules_)
    if data.get("digest") != digest_of(record):
        raise RunRecordError(
            "the digest does not match the fields: the record was edited by hand; make it again with the tool"
        )
    return record


# --- the folder -------------------------------------------------------------------------------------------------------


def path_of(root: Path, run_id: str, rules_: Runs) -> Path:
    """Where the record `run_id` lives under the checkout at `root`."""
    return root / rules_.folder / f"{run_id}.toml"


def write(root: Path, record: RunRecord) -> Path:
    """Write `record` to `research/runs/<id>.toml` of the checkout at `root` and return the path. Never overwrites: a
    second run of the same day and name needs another name. Raises RunRecordError."""
    rules_ = rules(root)
    text = to_toml(record, rules_)
    path = path_of(root, record.id, rules_)
    if path.exists():
        raise RunRecordError(f"{rules_.folder}/{record.id}.toml already exists; give the run another --name")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return path


def _read(path: Path, rules_: Runs, label: str) -> RunRecord:
    """The record in `path`; every problem is prefixed with `label`, the path as the reader knows it."""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise RunRecordError(f"{label}: cannot be read ({error})") from error
    try:
        record = parse(text, rules_)
    except RunRecordError as error:
        raise RunRecordError(f"{label}: {error}") from error
    if record.id != path.stem:
        raise RunRecordError(f"{label}: the id is {record.id}, so the file is {record.id}.toml")
    return record


def load(root: Path, run_id: str) -> RunRecord:
    """The record `run_id` of the checkout at `root`. Raises RunRecordError when it is missing or breaks a rule."""
    rules_ = rules(root)
    path = path_of(root, run_id, rules_)
    if not path.is_file():
        raise RunRecordError(f"no run record {run_id} ({rules_.folder}/{run_id}.toml)")
    return _read(path, rules_, f"{rules_.folder}/{path.name}")


def load_all(root: Path) -> tuple[dict[str, RunRecord], list[str]]:
    """Every record under the runs folder by id, and one line for each file that is not a good record."""
    rules_ = rules(root)
    folder = root / rules_.folder
    records: dict[str, RunRecord] = {}
    problems: list[str] = []
    for path in sorted(folder.glob("*.toml")) if folder.is_dir() else []:
        try:
            record = _read(path, rules_, f"{rules_.folder}/{path.name}")
        except RunRecordError as error:
            problems.append(str(error))
        else:
            records[record.id] = record
    return records, problems

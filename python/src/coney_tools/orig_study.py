# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools original study <addr>`: read one original function from Ghidra, save it, number it, cut it.

The study is the first half of the side-by-side check (docs/guides/stamps.md#study). From the running ghidra-mcp server
(the same HTTP endpoints `orig index` reads, reading ones only) it takes the function's decompile, Ghidra's hash of its
bytes, its callers and callees and the globals it touches; it saves them in the unit's scratch folder, numbers the
statements and cuts the pieces (orig_cut), and appends a `studied` stamp. Everything here is saved under scratch only:
the decompile is text from the game and never goes in the repository (LEGAL.md).

Freshness: Ghidra is renamed all the time, so a decompile's text changes while the function does not. A saved study is
*fresh* when Ghidra's function hash and the cut's shape are unchanged, even if the text differs (*renamed*), and
*stale* otherwise.
"""

from __future__ import annotations

import importlib.metadata
import json
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import orig_cut, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Stamps
from coney_tools.orig_cut import Cut, CutError
from coney_tools.orig_ghidra import Fetch
from coney_tools.unit import Unit

#: The program whose addresses the research cites (docs/guides/ghidra.md).
PROGRAM = "SLUS_212.15"
#: The endpoints read (all reading ones; the orig-reviewer agent is allowed the same).
LIMIT = 1000


class StudyError(ConfigError):
    """The function cannot be studied (exit 2): no such function, a wrong program, nothing saved."""


class GhidraDown(StudyError):
    """No ghidra-mcp answers (exit 3): nothing was written."""


@dataclass(frozen=True)
class Study:
    """A saved study, read back from the unit's scratch folder."""

    address: str
    name: str
    program: str
    signature: str
    text: str  # the saved decompile, newlines normalised
    decompile_sha256: str
    ghidra_function_hash: str
    shape_sha256: str
    cut_sha256: str
    cut: Cut
    callers: list[dict[str, Any]]
    callees: list[dict[str, Any]]
    globals: list[dict[str, Any]]

    @property
    def covers(self) -> dict[str, str]:
        """The hashes a `studied` stamp covers."""
        return {
            "decompile_sha256": self.decompile_sha256,
            "ghidra_function_hash": self.ghidra_function_hash,
            "shape_sha256": self.shape_sha256,
            "cut_sha256": self.cut_sha256,
            "callers_sha256": _addresses_hash(self.callers),
            "callees_sha256": _addresses_hash(self.callees),
            "globals_sha256": _addresses_hash(self.globals),
        }


@dataclass(frozen=True)
class StudyResult:
    """What a run of `study` did."""

    study: Study
    #: "new" (first study), "unchanged", "renamed" (the text changed, the function and its cut did not) or "changed".
    change: str
    stamped: bool

    @property
    def cut(self) -> Cut:
        """The study's cut."""
        return self.study.cut


def _addresses_hash(items: list[dict[str, Any]]) -> str:
    """The hash of the sorted addresses of a list of Ghidra entries (names are left out: renames must not change it)."""
    return stamps.sha256_hex("\n".join(sorted(str(item.get("address", "")) for item in items)).encode("utf-8"))


def _json_bytes(value: object) -> bytes:
    """Readable JSON with LF newlines, for the scratch files."""
    return (json.dumps(value, indent=1, sort_keys=True) + "\n").encode("utf-8")


# --- Ghidra ------------------------------------------------------------------------------------------------------


def _ask(fetch: Fetch, path: str) -> Any:
    """One reading call; a connection failure is GhidraDown, an `error` answer a StudyError."""
    try:
        answer = fetch(path)
    except Exception as error:  # urllib's, the bridge's: any failure to talk means "no server"
        raise GhidraDown(
            f"{error}; start ghidra-mcp (docs/guides/ghidra.md#start-and-stop-the-server) and run it again"
        ) from error
    if isinstance(answer, dict) and "error" in answer:
        raise StudyError(f"ghidra-mcp: {answer['error']} ({path.split('?')[0]})")
    return answer


def _fetch_all(fetch: Fetch, address: str) -> dict[str, Any]:
    """Everything the study reads about the function at `address`, before anything is written."""
    try:
        program = fetch("/get_current_program_info")
    except Exception as error:
        raise GhidraDown(
            f"no ghidra-mcp answers ({error}); start it (docs/guides/ghidra.md#start-and-stop-the-server)"
        ) from error
    if not isinstance(program, dict) or "error" in program or not program.get("name"):
        raise GhidraDown("ghidra-mcp has no program open; open the project (docs/guides/ghidra.md)")
    if program["name"] != PROGRAM:
        raise StudyError(f"the open program is {program['name']!r}; the research cites addresses in {PROGRAM}")
    query = f"?address={address}"
    try:
        info = fetch(f"/get_function_by_address{query}")
    except Exception as error:
        raise GhidraDown(str(error)) from error
    if not isinstance(info, dict) or "error" in info or "entry_point" not in info:
        raise StudyError(f"Ghidra has no function at {address}")
    start = f"0x{int(str(info['entry_point']), 16):08x}"
    if start != address:
        raise StudyError(f"{address} is inside {info.get('name', 'a function')}, which starts at {start}; study that")
    decompiled = _ask(fetch, f"/decompile_function{query}")
    text = decompiled.get("decompiled") if isinstance(decompiled, dict) else None
    if not isinstance(text, str) or not text.strip():
        raise StudyError(f"ghidra-mcp gave no decompile for {address}")
    hashed = _ask(fetch, f"/get_function_hash{query}")
    function_hash = str(hashed.get("hash", "")) if isinstance(hashed, dict) else ""
    callers = _ask(fetch, f"/get_function_callers{query}&limit={LIMIT}")
    callees = _ask(fetch, f"/get_function_callees{query}&limit={LIMIT}")
    audit = _ask(fetch, f"/audit_globals_in_function{query}")
    return {
        "name": str(info.get("name", "")),
        "program": program["name"],
        "signature": str(info.get("signature", "")),
        "text": orig_cut.normalise(text),
        "function_hash": function_hash,
        "callers": _entries(callers, "callers", ("name", "address")),
        "callees": _entries(callees, "callees", ("name", "address")),
        "globals": _entries(audit, "globals", ("address", "name", "type", "length")),
    }


def _entries(answer: object, key: str, fields: tuple[str, ...]) -> list[dict[str, Any]]:
    """The list under `key` of an answer, each entry cut down to `fields` (no comments, no analysis noise)."""
    rows = answer.get(key, []) if isinstance(answer, dict) else []
    return sorted(
        ({name: row.get(name) for name in fields if name in row} for row in rows if isinstance(row, dict)),
        key=lambda row: (str(row.get("address", "")), str(row.get("name", ""))),
    )


# --- the saved study ---------------------------------------------------------------------------------------------


def _build(address: str, fetched: Mapping[str, Any]) -> Study:
    """The study of fetched (or re-read) values: hash, number and cut the text."""
    text = str(fetched["text"])
    try:
        cut = orig_cut.cut_decompile(text)
    except CutError as error:
        raise StudyError(f"{address}: {error}") from error
    function_hash = str(fetched["function_hash"])
    if not stamps.HEX.match(function_hash):
        raise StudyError(f"ghidra-mcp gave no usable hash for the function at {address}")
    return Study(
        address=address,
        name=str(fetched["name"]),
        program=str(fetched["program"]),
        signature=str(fetched["signature"]),
        text=text,
        decompile_sha256=stamps.sha256_hex(text.encode("utf-8")),
        ghidra_function_hash=function_hash,
        shape_sha256=orig_cut.shape_sha256(cut),
        cut_sha256=orig_cut.cut_sha256(cut),
        cut=cut,
        callers=list(fetched["callers"]),
        callees=list(fetched["callees"]),
        globals=list(fetched["globals"]),
    )


def _save(folder: Path, study: Study) -> None:
    """Write the study's files into `folder` (scratch)."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "decompile.c").write_bytes(study.text.encode("utf-8"))
    (folder / "numbered.txt").write_bytes(orig_cut.render_numbered(study.text, study.cut).encode("utf-8"))
    (folder / "callers.json").write_bytes(_json_bytes(study.callers))
    (folder / "callees.json").write_bytes(_json_bytes(study.callees))
    (folder / "globals.json").write_bytes(_json_bytes(study.globals))
    meta = {
        "address": study.address,
        "name": study.name,
        "program": study.program,
        "signature": study.signature,
        "decompile_sha256": study.decompile_sha256,
        "ghidra_function_hash": study.ghidra_function_hash,
        "shape_sha256": study.shape_sha256,
        "cut_sha256": study.cut_sha256,
        "statements": len(study.cut.statements),
        "pieces": [{"id": p.id, "kind": p.kind, "lines": orig_cut.format_lines(p.lines)} for p in study.cut.pieces],
        "tool": _tool_version(),
        "fetched": datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    (folder / "study.json").write_bytes(_json_bytes(meta))


def _tool_version() -> str:
    """The version of coney-tools, for the record."""
    try:
        return importlib.metadata.version("coney-tools")
    except importlib.metadata.PackageNotFoundError:
        return "unknown"


def _read_json(path: Path) -> Any:
    """A saved JSON file; StudyError when it is missing or damaged."""
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise StudyError(f"{path}: cannot be read ({error}); run `original study` again") from error


def load_study(unit: Unit, address: str) -> Study:
    """The saved study of `address`: its files re-read, the decompile's hash and the cut checked against `study.json`.

    StudyError when nothing is saved, the decompile was changed since, or the cutter now cuts it differently (then
    `original study --recut` makes a new study from the saved text).
    """
    folder = unit.study(address)
    meta = _read_json(folder / "study.json")
    try:
        raw = (folder / "decompile.c").read_bytes()
    except OSError as error:
        raise StudyError(f"{folder / 'decompile.c'}: cannot be read ({error})") from error
    if stamps.sha256_hex(raw) != meta["decompile_sha256"]:
        raise StudyError(f"{folder / 'decompile.c'}: its hash is not the one studied (the saved file was changed)")
    study = _build(
        address,
        {
            **meta,
            "text": raw.decode("utf-8"),
            "function_hash": meta["ghidra_function_hash"],
            "callers": _read_json(folder / "callers.json"),
            "callees": _read_json(folder / "callees.json"),
            "globals": _read_json(folder / "globals.json"),
        },
    )
    if study.cut_sha256 != meta["cut_sha256"]:
        raise StudyError(f"{address}: the cutter now cuts it differently; run `original study {address} --recut`")
    return study


def _classify_change(before: Study | None, after: Study) -> str:
    """How the new study differs from the one saved before."""
    if before is None:
        return "new"
    if (
        before.ghidra_function_hash != after.ghidra_function_hash
        or before.shape_sha256 != after.shape_sha256
        or before.cut_sha256 != after.cut_sha256
    ):
        return "changed"
    return "unchanged" if before.covers == after.covers else "renamed"


def study(
    unit: Unit,
    address: str,
    fetch: Fetch | None,
    *,
    rules: Stamps,
    commit: str,
    who: Mapping[str, Any],
    sign: stamps.Sign | None = None,
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> StudyResult:
    """Study the function at `address` (a `0x%08x` string): fetch it from Ghidra (`fetch`), or with `fetch=None` re-cut
    the saved text; save the files under the unit; append a `studied` stamp unless the log already holds one that
    covers the same hashes.

    Raises GhidraDown (nothing written) when no server answers, StudyError for a function that cannot be studied.
    """
    try:
        before: Study | None = load_study(unit, address)
    except StudyError:
        before = None
    if fetch is None:
        folder = unit.study(address)
        if not (folder / "study.json").is_file():
            raise StudyError(f"{address}: nothing saved to re-cut; run `original study {address}` with Ghidra up")
        meta = _read_json(folder / "study.json")
        try:
            raw = (folder / "decompile.c").read_bytes().decode("utf-8")
        except OSError as error:
            raise StudyError(f"{address}: nothing saved to re-cut ({error})") from error
        fetched: Mapping[str, Any] = {
            **meta,
            "text": raw,
            "function_hash": meta["ghidra_function_hash"],
            "callers": _read_json(folder / "callers.json"),
            "callees": _read_json(folder / "callees.json"),
            "globals": _read_json(folder / "globals.json"),
        }
    else:
        fetched = _fetch_all(fetch, address)
    after = _build(address, fetched)
    change = _classify_change(before, after)
    _save(unit.study(address), after)
    report = stamps.verify(unit.stamps, rules)
    stamped = False
    if report.current("studied", address, after.covers) is None:
        stamps.append(
            unit.stamps,
            rules=rules,
            task=unit.task,
            step="studied",
            subject=address,
            result="pass",
            commit=commit,
            covers=after.covers,
            detail={
                "name": after.name,
                "statements": len(after.cut.statements),
                "pieces": len(after.cut.pieces),
                "callers": len(after.callers),
                "callees": len(after.callees),
                "globals": len(after.globals),
            },
            who=who,
            sign=sign,
            clock=clock,
        )
        stamped = True
    return StudyResult(after, change, stamped)


def freshness(saved: Study, fetch: Fetch) -> str:
    """Whether a saved study still describes Ghidra's function: "fresh", "renamed" (the text differs only by names) or
    "stale" (the function's bytes or the cut's shape changed). Reads Ghidra; raises GhidraDown when it is down."""
    live = _build(saved.address, _fetch_all(fetch, saved.address))
    if live.ghidra_function_hash != saved.ghidra_function_hash or live.shape_sha256 != saved.shape_sha256:
        return "stale"
    return "fresh" if live.decompile_sha256 == saved.decompile_sha256 else "renamed"

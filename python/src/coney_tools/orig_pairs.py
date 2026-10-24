# SPDX-License-Identifier: GPL-3.0-or-later
"""The pairs file of one original function: `mapping/<addr>.toml` (docs/guides/stamps.md#pairs).

A *pair* joins a piece of the original (orig_cut) to the Coney code that does it: which numbered lines, what kind of
statement, where in `src/`, how they relate (`same`, `stand-in`, `not-ported`), a note and, for a function not ported
yet, the issue that ports it. The file holds line numbers, Coney locations and notes only, never text of the
decompile; it stays in the task's scratch folder and is never committed (the lasting part, each port's stand-ins and
not-ported pieces with their issues, goes to the function database). The header carries the cut as numbers (each
piece's lines), so coverage can be checked without Ghidra.

`prefill` makes the file with one blank pair per piece, the engineer fills it in, and the checks here say what is
missing: every numbered line in exactly one pair, no piece split, merged pieces adjacent and given a reason, each
relation complete, each Coney location real, each stand-in marked at the code.
"""

from __future__ import annotations

import hashlib
import json
import posixpath
import re
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from coney_tools import orig_cut
from coney_tools.config import ConfigError
from coney_tools.conventions import PairRules
from coney_tools.orig_cut import Cut, CutError

VERSION = 1
_PAIR_KEYS = ("lines", "kind", "coney", "relation", "note", "issue", "merge_reason")
_HEADER_KEYS = (
    "address",
    "name",
    "program",
    "decompile_sha256",
    "ghidra_function_hash",
    "shape_sha256",
    "statements",
    "pieces",
    "cut_sha256",
    "coney",
)
LOCATION = re.compile(r"^(?P<path>[^:]+):(?P<first>\d+)(?:-(?P<last>\d+))?$")
_COMMENT = re.compile(r"//|/\*|^\s*\*")


class PairsError(ConfigError):
    """A pairs file cannot be read or written."""


@dataclass(frozen=True)
class Header:
    """The `[function]` table: which function, which study it was made against, and the cut as numbers."""

    address: str
    name: str
    program: str
    decompile_sha256: str
    ghidra_function_hash: str
    shape_sha256: str
    cut_sha256: str
    statements: int
    pieces: tuple[str, ...]  # each piece's lines, `1-3,7`, in order
    coney: str  # the Coney function's key, for the reader


@dataclass(frozen=True)
class Pair:
    """One `[[pair]]`."""

    lines: str
    kind: str
    coney: str = ""
    relation: str = ""
    note: str = ""
    issue: str = ""
    merge_reason: str = ""


@dataclass
class PairsFile:
    """A whole pairs file."""

    header: Header
    pairs: list[Pair] = field(default_factory=list)


def header_from_cut(
    cut: Cut,
    *,
    address: str,
    name: str,
    program: str,
    decompile_sha256: str,
    ghidra_function_hash: str,
    coney: str,
) -> Header:
    """The header of a function's pairs file from its cut and the hashes of its study."""
    return Header(
        address=address,
        name=name,
        program=program,
        decompile_sha256=decompile_sha256,
        ghidra_function_hash=ghidra_function_hash,
        shape_sha256=orig_cut.shape_sha256(cut),
        cut_sha256=orig_cut.cut_sha256(cut),
        statements=len(cut.statements),
        pieces=tuple(orig_cut.format_lines(p.lines) for p in cut.pieces),
        coney=coney,
    )


def prefill(header: Header, cut: Cut) -> PairsFile:
    """The pairs file with one pair per piece and nothing else filled in."""
    return PairsFile(header, [Pair(orig_cut.format_lines(p.lines), p.kind) for p in cut.pieces])


# --- writing and reading -----------------------------------------------------------------------------------------


def _q(text: str) -> str:
    """A TOML basic string (JSON's escapes are a subset of TOML's)."""
    return json.dumps(text, ensure_ascii=False)


def render(file: PairsFile) -> str:
    """The file as TOML text with LF newlines."""
    h = file.header
    out = [
        f"# Pairs for the original function at {h.address} ({h.name}): `coney-tools original compare {h.address}`",
        "# checks and stamps this file. Edit the pairs; leave [function] alone (it binds the file to its study).",
        '# relation: same | stand-in (note = why; a comment at the code says so too) | not-ported (issue = "#N").',
        "# A pair over several pieces needs merge_reason. No text of the original goes in this file.",
        f"version = {VERSION}",
        "",
        "[function]",
        f"address = {_q(h.address)}",
        f"name = {_q(h.name)}",
        f"program = {_q(h.program)}",
        f"decompile_sha256 = {_q(h.decompile_sha256)}",
        f"ghidra_function_hash = {_q(h.ghidra_function_hash)}",
        f"shape_sha256 = {_q(h.shape_sha256)}",
        f"statements = {h.statements}",
        "pieces = [" + ", ".join(_q(p) for p in h.pieces) + "]",
        f"cut_sha256 = {_q(h.cut_sha256)}",
        f"coney = {_q(h.coney)}",
    ]
    for pair in file.pairs:
        out += ["", "[[pair]]"]
        out += [f"{key} = {_q(getattr(pair, key))}" for key in _PAIR_KEYS]
    return "\n".join(out) + "\n"


def write(path: Path, file: PairsFile) -> None:
    """Write the file (LF newlines), creating its folder."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(render(file).encode("utf-8"))


def file_sha256(path: Path) -> str:
    """The hash of the file's bytes with CRLF turned into LF: what a `compared` stamp covers."""
    try:
        data = path.read_bytes().replace(b"\r\n", b"\n")
    except OSError as error:
        raise PairsError(f"{path}: cannot be read ({error})") from error
    return hashlib.sha256(data).hexdigest()


def load(path: Path) -> PairsFile:
    """Read a pairs file; PairsError for a file that is missing, not TOML, of another version or with unknown keys."""
    try:
        with path.open("rb") as handle:
            data = tomllib.load(handle)
    except OSError as error:
        raise PairsError(f"{path}: cannot be read ({error})") from error
    except (tomllib.TOMLDecodeError, UnicodeDecodeError) as error:
        raise PairsError(f"{path}: not valid TOML ({error})") from error
    if data.get("version") != VERSION:
        raise PairsError(f"{path}: version {data.get('version')!r}; this tool reads version {VERSION}")
    unknown = sorted(set(data) - {"version", "function", "pair"})
    if unknown:
        raise PairsError(f"{path}: unknown key(s) {', '.join(unknown)}")
    function = data.get("function")
    if not isinstance(function, dict) or set(function) != set(_HEADER_KEYS):
        raise PairsError(f"{path}: [function] must have exactly the keys {', '.join(_HEADER_KEYS)}")
    pieces = function["pieces"]
    if not isinstance(pieces, list) or not all(isinstance(p, str) for p in pieces):
        raise PairsError(f"{path}: [function] pieces must be a list of text")
    header = Header(**{**function, "pieces": tuple(pieces)})
    pairs = []
    for index, row in enumerate(data.get("pair", []), start=1):
        extra = sorted(set(row) - set(_PAIR_KEYS))
        if extra:
            raise PairsError(f"{path}: pair {index}: unknown key(s) {', '.join(extra)}")
        if not all(isinstance(value, str) for value in row.values()):
            raise PairsError(f"{path}: pair {index}: every value is text")
        pairs.append(Pair(**row))
    return PairsFile(header, pairs)


# --- checks ------------------------------------------------------------------------------------------------------


#: The header fields that bind a pairs file to one study.
BOUND_FIELDS = ("program", "decompile_sha256", "ghidra_function_hash", "shape_sha256", "cut_sha256")


def studies_differ(header: Header, study: Header) -> str:
    """The first field in which a pairs file's header is bound to another study than `study`, or ""."""
    return next((name for name in BOUND_FIELDS if getattr(header, name) != getattr(study, name)), "")


def check_structure(
    file: PairsFile, cut: Cut, address: str, rules: PairRules, study: Header | None = None
) -> list[str]:
    """Why the file does not cover the cut properly (empty: it does). Reads no file but the arguments.

    With `study` (the header made from the saved study) the file must also be bound to it: the same program and
    the same decompile, function, shape and cut hashes.
    """
    problems: list[str] = []
    header = file.header
    if header.address != address:
        problems.append(f"the file is for address {header.address}, not {address}")
    if study is not None:
        differs = studies_differ(header, study)
        if differs:
            problems.append(
                f"the file is bound to another study ({differs} differs); bring its pairs forward with "
                f"`original compare {address} --carry`"
            )
            return problems
    expected = tuple(orig_cut.format_lines(p.lines) for p in cut.pieces)
    if (
        header.cut_sha256 != orig_cut.cut_sha256(cut)
        or header.pieces != expected
        or header.statements != len(cut.statements)
    ):
        problems.append(
            "the file was made for another cut of the function (the study changed); bring its pairs forward with "
            f"`original compare {address} --carry`"
        )
        return problems
    owner = orig_cut.piece_of_line(cut)
    by_id = {p.id: p for p in cut.pieces}
    taken: dict[int, int] = {}  # piece id -> index of the pair that holds it
    for index, pair in enumerate(file.pairs, start=1):
        name = f"pair {index} (lines {pair.lines})"
        try:
            numbers = orig_cut.parse_lines(pair.lines)
        except CutError as error:
            problems.append(f"{name}: {error}")
            continue
        outside = [n for n in numbers if n not in owner]
        if outside:
            problems.append(f"{name}: line {outside[0]} is not a line of the function (1-{len(cut.statements)})")
            continue
        if len(set(numbers)) != len(numbers):
            problems.append(f"{name}: a line is listed twice")
        ids = sorted({owner[n] for n in numbers})
        problems += _piece_problems(name, pair, ids, set(numbers), by_id)
        for piece_id in ids:
            if piece_id in taken:
                first = by_id[piece_id].lines[0]
                problems.append(f"{name}: line {first} is already in pair {taken[piece_id]}")
            taken[piece_id] = index
        problems += _relation_problems(name, pair, rules)
    for piece in cut.pieces:
        if piece.id not in taken:
            problems.append(
                f"line {piece.lines[0]} (piece {piece.id}, lines {orig_cut.format_lines(piece.lines)}) is in no pair"
            )
    return problems


def _piece_problems(
    name: str, pair: Pair, ids: list[int], lines: set[int], pieces: dict[int, orig_cut.Piece]
) -> list[str]:
    """A pair's lines must be whole pieces, and several pieces must be neighbours with a reason."""
    problems = []
    for piece_id in ids:
        piece = pieces[piece_id]
        if not set(piece.lines) <= lines:
            problems.append(
                f"{name}: splits piece {piece_id} (lines {orig_cut.format_lines(piece.lines)}); "
                "a pair holds whole pieces"
            )
    if len(ids) > 1:
        if ids != list(range(ids[0], ids[-1] + 1)):
            problems.append(f"{name}: merges pieces {ids} that are not adjacent")
        if not pair.merge_reason.strip():
            problems.append(f"{name}: covers {len(ids)} pieces; merge_reason must say why they are one pair")
    return problems


def _relation_problems(name: str, pair: Pair, rules: PairRules) -> list[str]:
    """The fields a pair's kind and relation need."""
    problems = []
    if pair.kind not in rules.kinds:
        problems.append(f"{name}: kind {pair.kind!r} is not one of {', '.join(rules.kinds)}")
    if pair.relation not in rules.relations:
        shown = f"{pair.relation!r}" if pair.relation else "not set"
        problems.append(f"{name}: relation {shown}; it is one of {', '.join(rules.relations)}")
        return problems
    if pair.relation in ("same", "stand-in") and not pair.coney.strip():
        problems.append(f"{name}: coney (path:line) is needed for relation {pair.relation}")
    if pair.relation == "stand-in" and not pair.note.strip():
        problems.append(f"{name}: a stand-in needs a note that says why")
    if pair.relation == "not-ported" and not re.fullmatch(r"#\d+", pair.issue.strip()):
        problems.append(f'{name}: not-ported needs the issue that ports it as issue = "#N"')
    return problems


def normal_path(path: str, tracked: Mapping[str, str] | None = None) -> str:
    """`path` as git stores it: forward slashes, `./` and `//` collapsed and, with `tracked` (lower-case name to stored
    name, see unit.tracked_files), the stored case; "" when it names no tracked file."""
    text = posixpath.normpath(path.strip().replace(chr(92), "/"))
    if tracked is None:
        return text
    return tracked.get(text.casefold(), "")


def pair_files(file: PairsFile, tracked: Mapping[str, str] | None = None) -> set[str]:
    """The files the pairs' Coney locations name, normalised (a location that names no tracked file is left out)."""
    found = set()
    for pair in file.pairs:
        match = LOCATION.match(pair.coney.strip())
        if match and (path := normal_path(match["path"], tracked)):
            found.add(path)
    return found


def check_code(file: PairsFile, root: Path, rules: PairRules, tracked: Mapping[str, str] | None = None) -> list[str]:
    """Why a pair's Coney location is not real (empty: all are): a tracked file under `src/` (with `tracked`), an
    existing line, and for a stand-in a comment with one of the markers on that line or the three above it."""
    problems = []
    for index, pair in enumerate(file.pairs, start=1):
        if not pair.coney.strip():
            continue
        name = f"pair {index} (lines {pair.lines})"
        match = LOCATION.match(pair.coney.strip())
        if match is None:
            problems.append(f"{name}: coney {pair.coney!r} is not path:line (or path:first-last)")
            continue
        stored = normal_path(match["path"], tracked)
        relative = PurePosixPath(stored or posixpath.normpath(match["path"].strip().replace(chr(92), "/")))
        if relative.is_absolute() or ".." in relative.parts or relative.parts[:1] != ("src",):
            problems.append(f"{name}: coney {pair.coney!r} must be a file under src/")
            continue
        if tracked is not None and not stored:
            problems.append(f"{name}: coney {pair.coney!r}: {relative} is not a file tracked by git (commit it first)")
            continue
        path = root / relative
        text = path.read_text(encoding="utf-8", errors="replace").splitlines() if path.is_file() else None
        first, last = int(match["first"]), int(match["last"] or match["first"])
        if text is None:
            problems.append(f"{name}: coney {pair.coney!r}: {relative} does not exist")
        elif not 1 <= first <= last <= len(text):
            problems.append(f"{name}: coney {pair.coney!r}: {relative} has {len(text)} lines")
        elif pair.relation == "stand-in" and not _marked(text, first, rules.stand_in_markers):
            problems.append(
                f"{name}: a stand-in needs a comment with {' or '.join(repr(m) for m in rules.stand_in_markers)} "
                f"at {relative}:{first} (on that line or the three above)"
            )
    return problems


def _marked(text: list[str], line: int, markers: tuple[str, ...]) -> bool:
    """Whether a comment with a marker is on `line` (1-based) or the three lines above it."""
    for row in text[max(0, line - 4) : line]:
        if _COMMENT.search(row) and any(marker.casefold() in row.casefold() for marker in markers):
            return True
    return False


# --- bringing a file forward ------------------------------------------------------------------------------------


def carry(old: PairsFile, header: Header, cut: Cut) -> tuple[PairsFile, list[Pair]]:
    """The pairs of `old` brought onto a new study: (the new file, the old pairs that could not be kept).

    Line numbers mean the same statements only when the shape is unchanged, so with another shape nothing is kept and
    the new file is blank. With the same shape a pair is kept when its lines are still whole pieces of the new cut;
    the pieces nobody holds get blank pairs.
    """
    fresh = prefill(header, cut)
    if old.header.shape_sha256 != header.shape_sha256:
        return fresh, list(old.pairs)
    owner = orig_cut.piece_of_line(cut)
    pieces = {p.id: p for p in cut.pieces}
    kept: list[Pair] = []
    dropped: list[Pair] = []
    held: set[int] = set()
    for pair in old.pairs:
        try:
            numbers = set(orig_cut.parse_lines(pair.lines))
        except CutError:
            dropped.append(pair)
            continue
        ids = {owner.get(n, 0) for n in numbers}
        if 0 in ids or any(not set(pieces[i].lines) <= numbers for i in ids) or ids & held:
            dropped.append(pair)
            continue
        held |= ids
        kept.append(pair)
    kept += [
        pair
        for pair in fresh.pairs
        if not (set(orig_cut.parse_lines(pair.lines)) & {n for i in held for n in pieces[i].lines})
    ]
    kept.sort(key=lambda pair: orig_cut.parse_lines(pair.lines)[0])
    return PairsFile(header, kept), dropped

# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools original compare <addr>`: the side-by-side check of one ported function (docs/guides/stamps.md#compare).

After `original study` has numbered and cut the original function, the engineer pairs each piece with the Coney code
that does it (orig_pairs). This command shows the saved decompile beside the Coney function, pre-fills the pairs file
the first time, and, once the pairs are filled in, checks them: every numbered line in a pair, merges with a reason,
every relation complete, every Coney location real and every stand-in marked at the code, the Coney file committed, and
`orig copy-check` clean on it. Only then does it append the `compared` stamp. A run that finds something wrong prints
it and writes no stamp: compare is an edit-and-rerun loop, and the log records verdicts, not drafts.

The side-by-side view is written to the unit's scratch folder only: it holds the decompile.
"""

from __future__ import annotations

import itertools
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import orig_copy, orig_cut, orig_pairs, orig_review, orig_study, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Stamps
from coney_tools.orig_pairs import PairsFile
from coney_tools.orig_study import Study
from coney_tools.unit import Unit, dirty_paths

#: The columns of the side-by-side view.
LEFT_WIDTH = 74
RIGHT_WIDTH = 90


class CompareError(ConfigError):
    """The comparison cannot start (exit 2): nothing studied, no Coney function, an ambiguous tag."""


@dataclass
class CompareResult:
    """What a run of `compare` did."""

    status: str  # "created" (a blank pairs file was written), "refused" (see problems), "stamped" or "unchanged"
    pairs_path: Path
    view_path: Path
    problems: list[str] = field(default_factory=list)
    dropped: list[orig_pairs.Pair] = field(default_factory=list)
    stamp: stamps.Stamp | None = None
    counts: dict[str, int] = field(default_factory=dict)


def find_unit(root: Path, address: str, key: str | None = None) -> orig_review.Unit:
    """The Coney function that carries `@orig <address>`; CompareError when none or several (then `key` picks one)."""
    number = int(address, 16)
    found = [u for u in orig_review.units_now(root) if number in u.addresses]
    if key is not None:
        found = [u for u in found if u.key == key]
    if not found:
        raise CompareError(f"no Coney function carries @orig {address}" + (f" with key {key}" if key else ""))
    if len(found) > 1:
        keys = ", ".join(u.key for u in found)
        raise CompareError(
            f"{len(found)} Coney functions carry @orig {address} ({keys}); pick one with --coney-function"
        )
    return found[0]


def _span(lines: list[str], first: int) -> int:
    """The last line (1-based) of the definition that starts at line `first`: where its braces balance."""
    code = orig_copy.code_only("\n".join(lines))
    rows = code.splitlines()
    depth = 0
    opened = False
    for index in range(first - 1, len(rows)):
        for char in rows[index]:
            if char == "{":
                depth += 1
                opened = True
            elif char == "}":
                depth -= 1
        if opened and depth <= 0:
            return index + 1
    return min(len(rows), first + 80)


def _cut(text: str, width: int) -> str:
    """`text` padded or cut to `width` columns."""
    return text if len(text) <= width else text[: width - 1] + "…"


def side_by_side(study: Study, file: PairsFile | None, source_path: str, source: list[str], first: int) -> str:
    """The saved decompile (numbered, with its piece and pair) beside the Coney function (with the pairs that point at
    each line), as text for the terminal and the scratch folder."""
    last = _span(source, first)
    owner = orig_cut.piece_of_line(study.cut)
    pair_of_piece: dict[int, int] = {}
    pointers: dict[int, list[int]] = {}
    if file is not None:
        for index, pair in enumerate(file.pairs, start=1):
            try:
                for number in orig_cut.parse_lines(pair.lines):
                    pair_of_piece.setdefault(owner.get(number, 0), index)
            except orig_cut.CutError:
                continue
            location = orig_pairs.LOCATION.match(pair.coney.strip())
            if location and location["path"] == source_path:
                end = int(location["last"] or location["first"])
                for line in range(int(location["first"]), end + 1):
                    pointers.setdefault(line, []).append(index)
    left = []
    first_of = {s.first_line: s for s in study.cut.statements}
    for row, text in enumerate(orig_cut.normalise(study.text).split("\n"), start=1):
        statement = first_of.get(row)
        mark = number = ""
        if statement is not None:
            number = str(statement.number)
            mark = f"[{pair_of_piece.get(owner[statement.number], '?')}]" if file is not None else ""
        left.append(_cut(f"{mark:<5}{number:>3}  {text.rstrip()}", LEFT_WIDTH))
    right = []
    for number in range(first, last + 1):
        marks = ",".join(str(i) for i in pointers.get(number, []))
        right.append(_cut(f"{'<' + marks if marks else '':<5}{number:>5}  {source[number - 1]}", RIGHT_WIDTH))
    title = f"ORIGINAL (saved decompile; {study.address} {study.name})"
    head = f"{title:<{LEFT_WIDTH}}  | CONEY ({source_path}:{first}-{last})"
    rows = [f"{a:<{LEFT_WIDTH}}  | {b}" for a, b in itertools.zip_longest(left, right, fillvalue="")]
    return "\n".join([head, "-" * (LEFT_WIDTH + 3 + 40), *rows]) + "\n"


def _counts(file: PairsFile, cut: orig_cut.Cut) -> dict[str, int]:
    """Numbers for the stamp and the summary line."""
    owner = orig_cut.piece_of_line(cut)
    merged = 0
    for pair in file.pairs:
        try:
            ids = {owner.get(n, 0) for n in orig_cut.parse_lines(pair.lines)}
        except orig_cut.CutError:
            continue
        merged += max(0, len(ids) - 1)
    counts = {
        "statements": len(cut.statements),
        "pieces": len(cut.pieces),
        "pairs": len(file.pairs),
        "merged": merged,
    }
    for relation in ("same", "stand-in", "not-ported"):
        counts[relation] = sum(1 for pair in file.pairs if pair.relation == relation)
    return counts


def compare(
    unit: Unit,
    root: Path,
    address: str,
    rules: Stamps,
    *,
    commit: str,
    who: Mapping[str, object],
    base: str = "origin/main",
    key: str | None = None,
    carry: bool = False,
    sign: stamps.Sign | None = None,
    clock: Callable[[], datetime] = lambda: datetime.now(UTC),
    copy_check: Callable[[Path, str], list[orig_copy.Finding]] = orig_copy.check,
) -> CompareResult:
    """Run the comparison of the function at `address` (see the module docstring).

    Raises CompareError (or StudyError) when it cannot start; otherwise returns the status and, for a refusal, why.
    """
    study = orig_study.load_study(unit, address)
    report = stamps.verify(unit.stamps, rules)
    wanted = {k: study.covers[k] for k in ("decompile_sha256", "ghidra_function_hash", "cut_sha256")}
    if report.current("studied", address, wanted) is None:
        raise CompareError(f"{address}: no current `studied` stamp for the saved study; run `original study {address}`")
    coney = find_unit(root, address, key)
    path = unit.mapping(address)
    view = unit.compare(address)
    header = orig_pairs.header_from_cut(
        study.cut,
        address=address,
        name=study.name,
        program=study.program,
        decompile_sha256=study.decompile_sha256,
        ghidra_function_hash=study.ghidra_function_hash,
        coney=coney.key,
    )
    source = (root / coney.path).read_text(encoding="utf-8", errors="replace").splitlines()
    result = CompareResult("refused", path, view)

    def show(file: PairsFile | None) -> None:
        view.parent.mkdir(parents=True, exist_ok=True)
        view.write_bytes(side_by_side(study, file, coney.path, source, coney.line).encode("utf-8"))

    if not path.is_file():
        blank = orig_pairs.prefill(header, study.cut)
        orig_pairs.write(path, blank)
        show(blank)
        result.status = "created"
        result.counts = _counts(blank, study.cut)
        return result
    file = orig_pairs.load(path)
    if carry and file.header.cut_sha256 != header.cut_sha256:
        backup = path.with_suffix(".toml.old")
        backup.write_bytes(path.read_bytes())
        file, result.dropped = orig_pairs.carry(file, header, study.cut)
        orig_pairs.write(path, file)
    show(file)
    result.counts = _counts(file, study.cut)
    problems = orig_pairs.check_structure(file, study.cut, address, rules.pairs)
    if not problems:
        problems += orig_pairs.check_code(file, root, rules.pairs)
    if problems:
        result.problems = problems
        return result
    files = sorted({coney.path, *(_location_path(pair.coney) for pair in file.pairs if pair.coney.strip())} - {""})
    dirty = dirty_paths(root, files)
    if dirty:
        result.problems = [
            f"{name}: has uncommitted changes; commit them, the stamp is bound to the commit" for name in dirty
        ]
        return result
    flagged = [f for f in copy_check(root, base) if f.path in files]
    if flagged:
        result.problems = [str(f) for f in flagged]
        return result
    covers = {
        "decompile_sha256": study.decompile_sha256,
        "ghidra_function_hash": study.ghidra_function_hash,
        "cut_sha256": study.cut_sha256,
        "pairs_sha256": orig_pairs.file_sha256(path),
        "coney_body_sha256": coney.body_sha256,
    }
    existing = report.current("compared", address, covers)
    if existing is not None:
        result.status, result.stamp = "unchanged", existing
        return result
    result.stamp = stamps.append(
        unit.stamps,
        rules=rules,
        task=unit.task,
        step="compared",
        subject=address,
        result="pass",
        commit=commit,
        covers=covers,
        detail={**result.counts, "coney": coney.key, "copy_check": "ok"},
        who=dict(who),
        sign=sign,
        clock=clock,
    )
    result.status = "stamped"
    return result


def _location_path(location: str) -> str:
    """The file of a `path:line` Coney location, or "" when it has none."""
    match = orig_pairs.LOCATION.match(location.strip())
    return match["path"] if match else ""

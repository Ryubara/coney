# SPDX-License-Identifier: GPL-3.0-or-later
"""The `orig index` checks against the shared Ghidra project: is each `@orig` address a function start whose name
matches the tag, and which original functions do the tagged ones call that no tag in `src/` covers.

Two sources answer the first question. A running ghidra-mcp (docs/guides/ghidra.md) answers it for the project as
it is now, and also gives each function's callees. Without one, the committed listing
`docs/progress/ghidra-functions.tsv` (addresses, sizes and our names, exported by `coney-tools progress ghidra`)
answers it as of its last export; the callees then stay unknown. Only reading endpoints are called, so the checks
are safe on the server every engineer shares, and nothing of the original's code is read or written.
"""

from __future__ import annotations

import bisect
import re
from collections import defaultdict
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from typing import Any

from coney_tools.orig_index import IndexRow
from coney_tools.progress_research import ListedFunction

#: Fetches a ghidra-mcp HTTP path (with its query) and returns the decoded JSON (progress_research.http_fetch).
Fetch = Callable[[str], Any]
#: Problems, from the most to the least certain: no function there, the address is inside another function, the
#: tag and Ghidra name different functions, the same class or prefix with another verb (often a rename since), and a
#: function Ghidra still calls by its default name.
PROBLEMS = ("no-function", "not-a-start", "names-differ", "renamed", "unnamed")
_DEFAULT_NAME = re.compile(r"^(?:thunk_)?(?:FUN|SUB|LAB)_[0-9a-fA-F]{8}$")


@dataclass(frozen=True)
class GhidraFinding:
    """One tag whose address or name Ghidra does not bear out."""

    row: IndexRow
    problem: str  # one of PROBLEMS
    ghidra_name: str  # what Ghidra calls the function at (or around) the address; "" for none
    start: int | None  # the start of the function the address is in, for not-a-start


@dataclass(frozen=True)
class Callee:
    """An original function that tagged functions call and no tag in `src/` covers."""

    address: int
    name: str
    callers: tuple[int, ...]  # the tagged addresses that call it


def _normalise(name: str) -> str:
    """A name with case, `::`, `_` and other punctuation dropped: `DVDWadIndex::Find` and `DVDWadIndex_Find` agree."""
    return re.sub(r"[^0-9a-z]", "", name.casefold())


def _words(name: str) -> set[str]:
    """The words of a name, lower case: `Gang_SetSuspendedById` -> gang, set, suspended, by, id."""
    spaced = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1 \2", name))
    return {w.casefold() for w in re.split(r"[^A-Za-z0-9]+", spaced) if w}


#: Prefixes Ghidra's names give a script binding's wrapper (`lua_GangSuspend`) where a tag names the binding.
_WRAPPER_PREFIX = re.compile(r"^(?:lua|Script)_", re.IGNORECASE)


def compare_names(tag_name: str, ghidra_name: str) -> str | None:
    """None when the names agree, else "unnamed", "renamed" or "names-differ" (see PROBLEMS).

    Names agree when they are equal once case and punctuation are dropped (`DVDWadIndex::Find`, `DVDWadIndex_Find`),
    also after a binding wrapper's `lua_` prefix. They count as renamed when half their words are shared (`Gang_Suspend`
    and `Gang_SetSuspendedById`), which is usually a later rename in Ghidra and the tag's name to update; otherwise
    the tag may well point at the wrong function.
    """
    if not ghidra_name or _DEFAULT_NAME.match(ghidra_name):
        return "unnamed"
    first = tag_name.split()[0] if tag_name.split() else ""
    plain = _WRAPPER_PREFIX.sub("", ghidra_name)
    if _normalise(first) in (_normalise(ghidra_name), _normalise(plain)) or _normalise(tag_name) == _normalise(plain):
        return None
    ours, theirs = _words(first), _words(plain)
    shared = len(ours & theirs)
    return "renamed" if shared and shared * 2 >= min(len(ours), len(theirs)) else "names-differ"


def check_listing(rows: Iterable[IndexRow], listing: list[ListedFunction]) -> list[GhidraFinding]:
    """Check each tag against the committed Ghidra listing (its starts, sizes and names)."""
    by_address = {f.address: f for f in listing}
    starts = [f.address for f in listing]
    findings = []
    for row in rows:
        found = by_address.get(row.address)
        if found is None:
            i = bisect.bisect_right(starts, row.address) - 1
            inside = listing[i] if i >= 0 and row.address < listing[i].address + listing[i].size else None
            if inside is None:
                findings.append(GhidraFinding(row, "no-function", "", None))
            else:
                findings.append(GhidraFinding(row, "not-a-start", inside.name, inside.address))
            continue
        problem = compare_names(row.name, found.name)
        if problem:
            findings.append(GhidraFinding(row, problem, found.name, None))
    return findings


def check_live(rows: list[IndexRow], fetch: Fetch) -> tuple[list[GhidraFinding], list[Callee]]:
    """Check each tag against the running Ghidra project, and list the untagged functions the tagged ones call.

    A callee counts when Ghidra knows it as a function of the program (not an external) and no tag names its address.
    The caller decides which callees are game code and which are library code (orig_queue does, from totals.toml).
    """
    findings: list[GhidraFinding] = []
    tagged = {row.address for row in rows}
    starts: dict[int, tuple[str, int]] = {}
    by_address: dict[int, list[IndexRow]] = defaultdict(list)
    for row in rows:
        by_address[row.address].append(row)
    for address in sorted(by_address):
        info = fetch(f"/get_function_by_address?address=0x{address:08x}")
        if not isinstance(info, dict) or "error" in info or "entry_point" not in info:
            findings.extend(GhidraFinding(row, "no-function", "", None) for row in by_address[address])
            continue
        start = int(str(info["entry_point"]), 16)
        name = str(info.get("name", ""))
        starts[address] = (name, start)
        for row in by_address[address]:
            if start != address:
                findings.append(GhidraFinding(row, "not-a-start", name, start))
                continue
            problem = compare_names(row.name, name)
            if problem:
                findings.append(GhidraFinding(row, problem, name, None))
    callers: dict[int, set[int]] = defaultdict(set)
    names: dict[int, str] = {}
    for address, (_, start) in sorted(starts.items()):
        if start != address:
            continue
        answer = fetch(f"/get_function_callees?address=0x{address:08x}&limit=1000")
        for callee in answer.get("callees", []) if isinstance(answer, dict) else []:
            target = int(str(callee["address"]), 16)
            if target not in tagged and not callee.get("isExternal"):
                callers[target].add(address)
                names[target] = str(callee.get("name", ""))
    callees = [Callee(a, names[a], tuple(sorted(c))) for a, c in callers.items()]
    callees.sort(key=lambda c: (-len(c.callers), c.address))
    return findings, callees


def server_answers(fetch: Fetch) -> bool:
    """True when a ghidra-mcp server answers on the fetch's URL and has a program open."""
    try:
        info = fetch("/get_current_program_info")
    except Exception:  # any failure means "no server": the checks fall back to the listing
        return False
    return isinstance(info, dict) and "error" not in info

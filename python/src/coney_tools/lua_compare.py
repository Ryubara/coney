# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools lua compare DIR_A DIR_B`: two `lua decompile --all` folders compared script by script.

Made to check that the Xbox disc holds the PS2 disc's mission code: decompile each disc into its own folder (`lua
decompile --all --out A`, `lua decompile --all --xbox-disc XBOX_DISC --out B`), then compare. Every decompile ends with
the guard's marker line, which carries a hash of the chunk it came from and the chunk's name, so the scripts are paired
by chunk first (the same bytes, whatever each disc calls them), then by name. Both the chunks and the decompiled text
are compared. It prints counts, names and hashes only, never a line of a script (LEGAL.md#no-game-data).

Guide: docs/guides/coney-tools-fidelity.md#lua. Research: docs/research/xbox-assets.md#scripts.
"""

from __future__ import annotations

import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError

#: The marker line `lua decompile` ends every file with (`.claude/hooks/lua_guard.py` defines it): the first 16 hex
#: digits of the chunk's SHA-256, then its name.
_MARKER = re.compile(r"^-- lua4 decompile (?P<hash>[0-9a-f]{16}) (?P<name>\S.*)$")


@dataclass(frozen=True)
class Script:
    """One decompiled script: its name, the hash of the chunk it came from, and its text without the marker line."""

    name: str
    chunk: str
    text: str


def read_folder(folder: Path) -> list[Script]:
    """Every decompile in `folder` (its `.lua` files, at any depth), in name order. A `.lua` file without the marker
    line as its last line was not written by `lua decompile`, and is refused."""
    if not folder.is_dir():
        raise ConfigError(f"{folder}: no such folder")
    scripts = []
    for path in sorted(folder.rglob("*.lua")):
        lines = path.read_text(encoding="latin-1").splitlines()
        match = _MARKER.match(lines[-1]) if lines else None
        if match is None:
            raise ConfigError(f"{path}: no `lua decompile` marker line at its end")
        scripts.append(Script(match.group("name"), match.group("hash"), "\n".join(lines[:-1])))
    if not scripts:
        raise ConfigError(f"{folder}: no decompiled scripts (run `coney-tools lua decompile --all --out` first)")
    return scripts


@dataclass
class Comparison:
    """How two folders of decompiles compare: the scripts read from each, then the lists of results, each shaped as
    its comment says."""

    first: list[Script]
    second: list[Script]
    #: The same chunk in both folders: (chunk, names in A, names in B).
    same: list[tuple[str, list[str], list[str]]]
    #: Of `same`, the pairs whose decompiled text differs (never, as one chunk always decompiles alike).
    same_text_differs: list[str]
    #: A name in both folders whose chunks differ: (name, chunk in A, chunk in B, whether the texts are equal).
    changed: list[tuple[str, str, str, bool]]
    #: Chunks in one folder only, under a name the other folder does not have: (chunk, names).
    only_first: list[tuple[str, list[str]]]
    only_second: list[tuple[str, list[str]]]


def _by_chunk(scripts: list[Script]) -> dict[str, list[Script]]:
    """The scripts grouped by chunk hash (two names for one chunk share a group)."""
    groups: dict[str, list[Script]] = defaultdict(list)
    for script in scripts:
        groups[script.chunk].append(script)
    return groups


def compare(first: list[Script], second: list[Script]) -> Comparison:
    """Pair the scripts of two folders by chunk, then the rest by name."""
    a, b = _by_chunk(first), _by_chunk(second)
    same = []
    text_differs = []
    for chunk in sorted(a.keys() & b.keys()):
        same.append((chunk, [s.name for s in a[chunk]], [s.name for s in b[chunk]]))
        if a[chunk][0].text != b[chunk][0].text:
            text_differs.append(chunk)
    rest_a = {s.name.lower(): s for chunk in a.keys() - b.keys() for s in a[chunk]}
    rest_b = {s.name.lower(): s for chunk in b.keys() - a.keys() for s in b[chunk]}
    changed = [
        (rest_a[key].name, rest_a[key].chunk, rest_b[key].chunk, rest_a[key].text == rest_b[key].text)
        for key in sorted(rest_a.keys() & rest_b.keys())
    ]
    paired = {rest_a[key].chunk for key in rest_a.keys() & rest_b.keys()} | {
        rest_b[key].chunk for key in rest_a.keys() & rest_b.keys()
    }
    only_first = [(c, sorted(s.name for s in a[c])) for c in sorted(a.keys() - b.keys()) if c not in paired]
    only_second = [(c, sorted(s.name for s in b[c])) for c in sorted(b.keys() - a.keys()) if c not in paired]
    return Comparison(first, second, same, text_differs, changed, only_first, only_second)


def _names(names: list[str]) -> str:
    """Names joined for one line."""
    return ", ".join(names)


def report(result: Comparison, first: Path, second: Path, listing: bool) -> list[str]:
    """The comparison as lines: counts first, then the names of what differs; with `listing`, one line per chunk."""
    lines = []
    for label, scripts in ((f"A {first}", result.first), (f"B {second}", result.second)):
        lines.append(f"{label}: {len(scripts)} scripts, {len(_by_chunk(scripts))} distinct chunks")
    alike = sum(1 for _, names_a, names_b in result.same if sorted(names_a) == sorted(names_b))
    lines.append(
        f"the same chunk in both: {len(result.same)} (decompiles identical: "
        f"{len(result.same) - len(result.same_text_differs)}; under the same names: {alike}, "
        f"under other names: {len(result.same) - alike})"
    )
    text_equal = sum(1 for *_, equal in result.changed if equal)
    lines.append(f"the same name, another chunk: {len(result.changed)} (decompiles identical: {text_equal})")
    lines.append(f"only in A: {len(result.only_first)}")
    lines.append(f"only in B: {len(result.only_second)}")
    for name, chunk_a, chunk_b, equal in result.changed:
        lines.append(f"  changed {name}: {chunk_a} -> {chunk_b}" + (" (same text)" if equal else ""))
    for label, only in (("A", result.only_first), ("B", result.only_second)):
        for chunk, names in only:
            lines.append(f"  only in {label}: {chunk} {_names(names)}")
    if listing:
        lines.append("per chunk (result, chunk, names in A | names in B):")
        for chunk, names_a, names_b in result.same:
            lines.append(f"  same    {chunk} {_names(names_a)} | {_names(names_b)}")
        for name, chunk_a, chunk_b, _ in result.changed:
            lines.append(f"  changed {chunk_a}/{chunk_b} {name} | {name}")
        for chunk, names in result.only_first:
            lines.append(f"  only-a  {chunk} {_names(names)} |")
        for chunk, names in result.only_second:
            lines.append(f"  only-b  {chunk} | {_names(names)}")
    return lines


def run(first: Path, second: Path, listing: bool = False) -> int:
    """`lua compare DIR_A DIR_B [--list]`: print the comparison. 1 when a script of A has no identical chunk in B (B
    does not hold all of A's code), else 0."""
    result = compare(read_folder(first), read_folder(second))
    for line in report(result, first, second, listing):
        print(line)
    return int(bool(result.changed or result.only_first))

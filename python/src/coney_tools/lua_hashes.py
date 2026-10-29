# SPDX-License-Identifier: GPL-3.0-or-later
"""The frozen form of the Lua decompiler's output: the SHA-256 of every script's decompile, written and checked.

A mission mod's patch is pinned to the exact text a script decompiles to (docs/guides/studio/mission-mods.md#forms), so
that text must never change silently. `coney-tools lua decompile --all` hashes every decompile and checks the hashes
against the committed list (`research/lua-decompile-hashes.toml`); a script whose text changed fails the check by name.

* **Hashes and names only.** An entry holds the script's name, the SHA-256 of its chunk (the bytes the WAD stores) and
  the SHA-256 of its decompile; never a line of the text (LEGAL.md#no-game-data). The text hashed is what `lua4.render`
  returns, as it is written to a file (one byte per character, LF line ends), without the guard's marker line, which
  holds the name and the chunk's hash and is not part of the form.
* **Matched by chunk.** An entry is found by its chunk hash, not its name, so a script keeps its entry when its WAD name
  is recovered later. A chunk the list does not hold (another disc's) is counted, not failed; so is a listed chunk the
  disc does not have (the Xbox's 36 scripts on a PS2 disc).
* **A form** is a numbered version of the output. The file names the form it holds; a change to the output is a new
  form (`--new-form`), never a quiet edit of the hashes.

Guide: docs/guides/coney-tools-fidelity.md#lua-form.
"""

from __future__ import annotations

import hashlib
import re
import tomllib
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools.config import ConfigError

#: The form of the output this decompiler produces: form 1 is the first output frozen.
FORM = 1
#: The committed list, relative to the checkout.
DEFAULT_LIST = "research/lua-decompile-hashes.toml"
#: What `--all` writes beside the guard's fingerprints, in the same format.
OUTPUT = "decompile-hashes.toml"
_SHA = re.compile(r"^[0-9a-f]{64}$")
_KEYS = frozenset({"name", "chunk", "text"})

_HEADER = """\
# The SHA-256 of every Lua script's decompile in the frozen form below: hashes and names only, never text
# (LEGAL.md#no-game-data). Written by `coney-tools lua decompile --all --update-hashes` and checked by
# `coney-tools lua decompile --all`; how the form is frozen and changed: docs/guides/coney-tools-fidelity.md#lua-form.
# chunk = the SHA-256 of the script's bytes in the WAD; text = the SHA-256 of its decompile, without the marker line.
"""


@dataclass(frozen=True, order=True)
class Entry:
    """One script: its name (the WAD's file name, or the entry's hash in hex), the hash of its chunk and the hash of
    its decompile."""

    name: str
    chunk: str
    text: str


@dataclass(frozen=True)
class HashList:
    """A list of entries and the form they are in."""

    form: int
    entries: list[Entry]


@dataclass
class Comparison:
    """A run against a list: how many scripts match, the names of those whose text changed, the names the list does not
    hold, and how many listed chunks the run did not have."""

    same: int = 0
    changed: list[str] = field(default_factory=list)
    unlisted: list[str] = field(default_factory=list)
    absent: int = 0

    @property
    def ok(self) -> bool:
        """No script's text changed."""
        return not self.changed


def sha256(data: bytes) -> str:
    """The SHA-256 of `data`, in lowercase hex."""
    return hashlib.sha256(data).hexdigest()


def text_bytes(body: str) -> bytes:
    """A decompile's text as the command writes it: one byte per character (the game's strings are Latin-1)."""
    return body.encode("latin-1", errors="replace")


def entry_for(name: str, chunk: bytes, body: str) -> Entry:
    """The entry of the script `name`: `chunk` is its bytes, `body` its decompile without the marker line."""
    return Entry(name, sha256(chunk), sha256(text_bytes(body)))


def list_path(root: Path, given: Path | None) -> Path:
    """The list to use: `--hashes`, else the checkout's committed one."""
    return given if given is not None else root / DEFAULT_LIST


def _quote(value: str) -> str:
    """A TOML basic string; non-ASCII and control characters are escaped, so the file stays plain ASCII."""
    escaped = "".join(
        {'"': '\\"', "\\": "\\\\"}.get(char, char if " " <= char <= "~" else f"\\u{ord(char):04x}") for char in value
    )
    return f'"{escaped}"'


def render(form: int, entries: Iterable[Entry]) -> str:
    """The list as the file holds it: the form, then one `[[script]]` per entry in name order."""
    lines = [_HEADER, f"form = {form}\n"]
    for entry in sorted(entries):
        lines.append(
            f"\n[[script]]\nname = {_quote(entry.name)}\nchunk = {_quote(entry.chunk)}\ntext = {_quote(entry.text)}\n"
        )
    return "".join(lines)


def write(path: Path, form: int, entries: Iterable[Entry]) -> None:
    """Write the list to `path`, creating its folder; ASCII with LF line ends."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(render(form, entries).encode("ascii"))


def load(path: Path) -> HashList:
    """Read a list. Raises ConfigError when it is missing or damaged: another key, a hash that is not 64 hex digits, a
    script twice, or one chunk with two different texts (a chunk decompiles to one text)."""
    if not path.is_file():
        raise ConfigError(
            f"{path}: no list of decompile hashes; write it with `lua decompile --all --update-hashes` "
            "(docs/guides/coney-tools-fidelity.md#lua-form)"
        )
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except (tomllib.TOMLDecodeError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: not a list of decompile hashes ({error})") from error
    form = data.get("form")
    if not isinstance(form, int) or isinstance(form, bool) or form < 1:
        raise ConfigError(f"{path}: `form` must be a whole number from 1")
    if extra := sorted(set(data) - {"form", "script"}):
        raise ConfigError(f"{path}: unknown key {extra[0]!r}; the list holds `form` and `[[script]]` entries")
    entries: list[Entry] = []
    texts: dict[str, str] = {}
    pairs: set[tuple[str, str]] = set()
    for index, raw in enumerate(data.get("script", []), start=1):
        where = f"{path}: script {index}"
        if extra := sorted(set(raw) - _KEYS):
            raise ConfigError(f"{where}: unknown key {extra[0]!r}; an entry holds only name, chunk and text")
        for key in ("chunk", "text"):
            if not isinstance(raw.get(key), str) or not _SHA.match(raw[key]):
                raise ConfigError(f"{where}: `{key}` must be 64 lowercase hex digits (a SHA-256)")
        if not isinstance(raw.get("name"), str) or not raw["name"]:
            raise ConfigError(f"{where}: `name` must be a script's name")
        entry = Entry(raw["name"], raw["chunk"], raw["text"])
        if (entry.name, entry.chunk) in pairs:
            raise ConfigError(f"{where}: {entry.name} is listed twice with the same chunk")
        if texts.setdefault(entry.chunk, entry.text) != entry.text:
            raise ConfigError(f"{where}: {entry.name}'s chunk is listed with two different texts")
        pairs.add((entry.name, entry.chunk))
        entries.append(entry)
    return HashList(form, sorted(entries))


def compare(listed: HashList, found: Iterable[Entry]) -> Comparison:
    """Check the entries a run found against the list, matching by chunk hash. The names of a changed or unlisted
    script are the run's."""
    by_chunk = {entry.chunk: entry.text for entry in listed.entries}
    result = Comparison()
    seen: set[str] = set()
    for entry in found:
        seen.add(entry.chunk)
        text = by_chunk.get(entry.chunk)
        if text is None:
            result.unlisted.append(entry.name)
        elif text == entry.text:
            result.same += 1
        else:
            result.changed.append(entry.name)
    result.absent = len(set(by_chunk) - seen)
    return result


def merge(listed: HashList, found: Iterable[Entry]) -> list[Entry]:
    """The listed entries plus those of `found` whose chunk the list does not hold. A listed entry is never changed."""
    known = {entry.chunk for entry in listed.entries}
    added = [entry for entry in found if entry.chunk not in known]
    return [*listed.entries, *added]

# SPDX-License-Identifier: GPL-3.0-or-later
"""The game reference lists (research/references/*.yaml): their schema, reading, checking, writing and merging.

Each list is one YAML file with a fixed shape:

```yaml
# a header comment, rewritten by the tools
title: Characters (humans)            # the page title
about: |                              # Markdown: what the list is, in our own words
  ...
complete: |                           # Markdown: what is complete and what is not
  ...
defaults: {source: ..., evidence: inferred}   # values an entry takes when it leaves them out
entries:
  - {id: 32, model: warr_re_cv, ...}
```

The fields an entry may have are defined per topic in `refs_topics.py`; every entry ends up with a `source` and an
`evidence` level, given or from `defaults`. Fields marked *curated* are hand-written and never touched by
`coney-tools refs extract`; the others are what the extractor reads from the player's disc.

Legal: only names, ids, numbers and our own short descriptions go into these files (LEGAL.md, "Reference lists").
Research: docs/guides/research-workflow.md#reference-lists
"""

from __future__ import annotations

import json
import re
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from coney_tools.config import ConfigError

#: The evidence levels of docs/guides/research-workflow.md#evidence-levels, as the YAML spells them.
EVIDENCE = ("confirmed-code", "confirmed-runtime", "inferred", "speculative")
#: Fields every entry has, whatever its topic.
COMMON_FIELDS = ("source", "evidence", "notes")
_TOP_KEYS = ("title", "about", "complete", "defaults", "entries")
_PLAIN = re.compile(r"^[A-Za-z_][A-Za-z0-9_./+-]*$")
_YAML_WORDS = {"null", "true", "false", "yes", "no", "on", "off", "y", "n", "~"}


class RefsError(ConfigError):
    """A reference list that does not match its schema; the message names the file and the entry."""


@dataclass(frozen=True)
class Field:
    """One field of a topic's entries.

    `kind` is one of: int, hex (an int written as 0x...), float, str, bool, list, dict, any. A `column` names the
    field's heading in the rendered table; None keeps it out of the table (it still appears in the YAML). A curated
    or `prose` string is rendered as Markdown text; other strings are names and are rendered as code.
    """

    name: str
    kind: str
    doc: str
    column: str | None = None
    required: bool = False
    curated: bool = False
    link: str | None = None  # "characters.md#char": each value links to that page's entry
    prose: bool = False  # a string rendered as text, not as code, though not hand-written


@dataclass(frozen=True)
class Topic:
    """One reference list: its file stem, its key field, its fields and how its page is laid out."""

    key: str  # the file stem and page name: characters -> research/references/characters.yaml
    key_field: str  # unique per entry; also the entry's anchor on the page
    anchor: str  # anchor prefix: "char" gives #char-32
    fields: tuple[Field, ...]
    group_by: str | None = None  # a field whose values split the page into sections
    compact: bool = False  # one flow mapping per line in the YAML (long lists)
    nav: str = ""  # the short name in the navigation and the index
    images: bool = False  # entries may carry an `image` thumbnail (a path below docs/references/images/)

    def field_map(self) -> dict[str, Field]:
        """Every field by name, the common ones included."""
        common = (
            # An entry's own source and evidence are set by hand (the extractor leaves them to `defaults`).
            Field(
                "source", "str", "Where the entry comes from: a script and binding, an address, a chunk.", curated=True
            ),
            Field(
                "evidence",
                "str",
                "How we know: confirmed-code, confirmed-runtime, inferred or speculative "
                "([evidence levels](../guides/research-workflow.md#evidence-levels)).",
                curated=True,
            ),
            Field("notes", "str", "Our own short notes.", column="Notes", curated=True),
        )
        image = (
            (
                Field(
                    "image",
                    "str",
                    "A thumbnail, a path below `docs/references/images/` (`characters/warr_re_cv.png`); set by extract "
                    "from the files present.",
                    column="Image",
                ),
            )
            if self.images
            else ()
        )
        return {f.name: f for f in (*self.fields, *image, *common)}


@dataclass
class RefList:
    """One loaded reference list."""

    topic: Topic
    title: str
    about: str
    complete: str
    defaults: dict[str, Any]
    entries: list[dict[str, Any]] = field(default_factory=list)

    def resolved(self) -> list[dict[str, Any]]:
        """The entries with the defaults filled in."""
        return [{**self.defaults, **entry} for entry in self.entries]


def _check_kind(kind: str, value: Any) -> bool:
    """Whether `value` fits a field of `kind` (None always fits: a field may be unknown)."""
    if value is None or kind == "any":
        return True
    if kind in ("int", "hex"):
        return isinstance(value, int) and not isinstance(value, bool)
    if kind == "float":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    return isinstance(value, {"str": str, "bool": bool, "list": list, "dict": dict}[kind])


def validate(reflist: RefList, where: str) -> list[str]:
    """Check every entry against the topic's schema; return one problem per line (empty when valid)."""
    problems = []
    fields = reflist.topic.field_map()
    seen: set[Any] = set()
    for key, value in reflist.defaults.items():
        if key not in fields:
            problems.append(f"{where}: defaults: unknown field {key!r}")
        elif not _check_kind(fields[key].kind, value):
            problems.append(f"{where}: defaults: {key} should be {fields[key].kind}")
    for index, entry in enumerate(reflist.entries):
        if not isinstance(entry, dict):
            problems.append(f"{where}: entry {index}: not a mapping")
            continue
        label = f"{where}: entry {index} ({reflist.topic.key_field}={entry.get(reflist.topic.key_field)!r})"
        full = {**reflist.defaults, **entry}
        for key, value in entry.items():
            if key not in fields:
                problems.append(f"{label}: unknown field {key!r}")
            elif not _check_kind(fields[key].kind, value):
                problems.append(f"{label}: {key} should be {fields[key].kind}, not {value!r}")
        for spec in fields.values():
            if (spec.required or spec.name in ("source", "evidence")) and full.get(spec.name) is None:
                problems.append(f"{label}: no {spec.name}")
        if full.get("evidence") is not None and full["evidence"] not in EVIDENCE:
            problems.append(f"{label}: evidence {full['evidence']!r} is not one of {', '.join(EVIDENCE)}")
        key_value = entry.get(reflist.topic.key_field)
        if key_value in seen:
            problems.append(f"{label}: {reflist.topic.key_field} {key_value!r} appears twice")
        seen.add(key_value)
    return problems


def load(path: Path, topic: Topic) -> RefList:
    """Read and check one reference list. Raises RefsError on a YAML error or a schema problem."""
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, yaml.YAMLError) as error:
        raise RefsError(f"{path}: cannot be read ({error})") from error
    if not isinstance(data, dict):
        raise RefsError(f"{path}: not a mapping with title, about, complete, defaults and entries")
    unknown = sorted(set(data) - set(_TOP_KEYS))
    if unknown:
        raise RefsError(f"{path}: unknown top-level keys {', '.join(unknown)}")
    reflist = RefList(
        topic,
        str(data.get("title") or topic.key),
        str(data.get("about") or "").strip(),
        str(data.get("complete") or "").strip(),
        dict(data.get("defaults") or {}),
        list(data.get("entries") or []),
    )
    problems = validate(reflist, str(path))
    if problems:
        raise RefsError("\n".join(problems))
    return reflist


# --- writing -----------------------------------------------------------------------------------------------------


def _scalar(value: Any, kind: str | None = None) -> str:
    """One value in YAML flow syntax; strings are quoted (as JSON) unless plainly safe."""
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        if kind == "hex" and value >= 0:
            return f"0x{value:08x}" if value > 0xFFFF else f"0x{value:04x}"
        return str(value)
    if isinstance(value, float):
        text = repr(value)
        return text if ("." in text or "e" in text or "n" in text) else text + ".0"
    if isinstance(value, str):
        plain = (
            _PLAIN.match(value)
            and value.lower() not in _YAML_WORDS
            and not re.match(r"^[0-9+.-]", value)
            and not value.startswith("0x")
        )
        return value if plain else json.dumps(value, ensure_ascii=False)
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(_scalar(item) for item in value) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(f"{_scalar(str(k))}: {_scalar(v)}" for k, v in value.items()) + "}"
    raise RefsError(f"cannot write {value!r} to YAML")


def _block(text: str, indent: str = "  ") -> str:
    """A literal block scalar (`|`) for a Markdown paragraph, or an empty string."""
    if not text:
        return '""'
    return "|\n" + "\n".join((indent + line) if line else "" for line in text.splitlines())


def dump(reflist: RefList) -> str:
    """The list as YAML text in the canonical layout: fields in schema order, one entry after another."""
    topic = reflist.topic
    fields = topic.field_map()
    order = list(fields)
    lines = [
        f"# {reflist.title}: a Coney game reference list. The page docs/references/{topic.key}.md is generated",
        "# from this file by `coney-tools refs render`; `coney-tools refs extract` refreshes the fields read from the",
        "# disc and keeps the hand-written ones. Schema and rules: docs/guides/research-workflow.md#reference-lists.",
        f"title: {_scalar(reflist.title)}",
        f"about: {_block(reflist.about)}",
        f"complete: {_block(reflist.complete)}",
        "defaults: " + _scalar(reflist.defaults),
        "entries:",
    ]
    for entry in reflist.entries:
        keys = sorted(entry, key=lambda k: order.index(k) if k in order else len(order))
        pairs = [(k, entry[k]) for k in keys if entry[k] is not None or k == topic.key_field]
        if topic.compact:
            lines.append("  - {" + ", ".join(f"{k}: {_scalar(v, fields[k].kind)}" for k, v in pairs) + "}")
            continue
        first = True
        for k, v in pairs:
            lines.append(f"  {'- ' if first else '  '}{k}: {_scalar(v, fields[k].kind if k in fields else None)}")
            first = False
    return "\n".join(lines) + "\n"


def write(path: Path, reflist: RefList) -> None:
    """Write the list in canonical form, after checking it."""
    problems = validate(reflist, str(path))
    if problems:
        raise RefsError("\n".join(problems))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(dump(reflist), encoding="utf-8", newline="\n")


# --- merging extracted facts -------------------------------------------------------------------------------------


def merge(existing: RefList, extracted: Iterable[dict[str, Any]]) -> RefList:
    """Merge freshly extracted entries into a list, keeping everything hand-written.

    For an entry the extractor produced, its extracted fields replace the old ones and the curated fields are kept.
    Entries the extractor did not produce stay as they are (hand-added entries, or names the extractor cannot find by
    itself but that still match). The order is the extractor's, followed by the remaining old entries.
    """
    topic = existing.topic
    fields = topic.field_map()
    old = {entry.get(topic.key_field): entry for entry in existing.entries}
    merged: list[dict[str, Any]] = []
    produced = set()
    for fresh in extracted:
        key = fresh.get(topic.key_field)
        produced.add(key)
        entry = {k: v for k, v in fresh.items() if v is not None}
        for name, value in old.get(key, {}).items():
            if fields.get(name) is not None and fields[name].curated and value is not None:
                entry[name] = value
        merged.append(entry)
    merged.extend(entry for key, entry in old.items() if key not in produced)
    return RefList(topic, existing.title, existing.about, existing.complete, existing.defaults, merged)

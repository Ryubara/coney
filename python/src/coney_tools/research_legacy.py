# SPDX-License-Identifier: GPL-3.0-or-later
"""The legacy file: research pages that do not meet the standard yet, and what each is excused from.

`research/legacy.toml` lists a page under the rules it fails, with the number of findings at the last scan:

    ["docs/research/camera.md"]
    evidence = 312
    length = 1910

A listed page is held to every rule it is not listed under, as any page is. Under a rule it is listed for, its findings
are reported as one warning and do not fail the check, while the change does not touch the page. A change that touches
the page ends the excuse, except for two things that keep the page moving
(docs/guides/writing-docs.md#legacy-pages, the maintainer's decision "every touch fixes the mechanical rules page-wide
and grades the touched section"):

* grading (`Finding.by_section`): an address with no level, and a runtime claim of the old form (`confirmed (runtime)`
  with no run id, on a page that does not name PCSX2 where it explains them), is an error in a section (a level-2
  heading) the change touches and stays a warning elsewhere;
* `length`: waived until the page's restructure.

A page that moved with git (a rename the change's diff detects, `Touch.moved_from`) is read as any touched page, from
the lines git's diff of the renamed pair changes, with one difference: its entry excuses the grading alone. Only its
`evidence` entry counts; the front matter, sections, length and every other rule it lists are checked in full. The entry
follows the page to its new path (`docs_legacy`), so the grading of the sections the move left alone stays excused until
a later change touches them.

No new page may be listed, and nothing but a scan with `coney-tools docs legacy --init` ever adds one. The numbers
tell people deciding what to fix first how much is left; they are never compared with a page.
"""

from __future__ import annotations

import re
import tomllib
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.conventions import Research
from coney_tools.docs_page import Finding

HEADER = (
    "# Research pages that do not meet the standard yet (docs/guides/writing-docs.md#legacy-pages), each under the\n"
    "# rules it fails, with the number of findings at the last scan (for `length`, the lines over the limit).\n"
    "# A page here is excused from those rules only. It leaves the file as it passes: remove a rule or a page once\n"
    "# `coney-tools docs check` says it passes, or run `coney-tools docs legacy --prune`. Never add a page: a new\n"
    "# page meets the standard in full, and a page moved with git keeps only its `evidence` entry. Written first by\n"
    "# `coney-tools docs legacy --init`.\n"
)


@dataclass(frozen=True)
class Legacy:
    """The pages of the legacy file, each with the rules it is excused from and the findings counted for each."""

    pages: Mapping[str, Mapping[str, int]] = field(default_factory=dict)

    def rules(self, path: str) -> Mapping[str, int]:
        """The rules `path` is excused from (empty for a page that is not listed)."""
        return self.pages.get(path, {})

    def __contains__(self, path: object) -> bool:
        """Whether the page is listed."""
        return path in self.pages


@dataclass(frozen=True)
class Touch:
    """What a change did to a page: the lines it added or changed (None when not known), whether the page is new and,
    for a page git sees as moved from another path, that path (the page is then touched throughout and not new)."""

    lines: frozenset[int] | None = None
    new: bool = False
    moved_from: str | None = None


def parse_legacy(text: str, known_rules: Iterable[str], where: str = "research/legacy.toml") -> Legacy:
    """The legacy file's `text`. ConfigError names the page or rule that is not allowed."""
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise ConfigError(f"{where}: not valid TOML ({error})") from error
    known = tuple(known_rules)
    pages: dict[str, dict[str, int]] = {}
    for path, rules in data.items():
        if not re.fullmatch(r"docs/research/[\w./-]+\.md", path):
            raise ConfigError(f"{where}: '{path}' is not a page under docs/research/")
        if not isinstance(rules, dict) or not rules:
            raise ConfigError(f"{where}: '{path}' lists no rule")
        for rule, count in rules.items():
            if rule not in known:
                raise ConfigError(f"{where}: '{path}': '{rule}' is not a rule that can be waived ({', '.join(known)})")
            if not isinstance(count, int) or isinstance(count, bool) or count < 1:
                raise ConfigError(f"{where}: '{path}': '{rule}' is a count of findings, a whole number of 1 or more")
        pages[path] = {rule: rules[rule] for rule in known if rule in rules}
    return Legacy(pages)


def format_legacy(legacy: Legacy) -> str:
    """The file's text: the header, then each page under its rules, pages in path order and rules in the order of the
    standard."""
    blocks = [HEADER.rstrip("\n")]
    for path in sorted(legacy.pages):
        lines = [f'["{path}"]']
        lines.extend(f"{rule} = {count}" for rule, count in legacy.pages[path].items())
        blocks.append("\n".join(lines))
    return "\n\n".join(blocks) + "\n"


def load_legacy(root: Path, research: Research, known_rules: Iterable[str]) -> Legacy:
    """The legacy file of the checkout at `root`; empty when it has none."""
    path = root / research.files.legacy
    if not path.is_file():
        return Legacy()
    return parse_legacy(path.read_text(encoding="utf-8"), known_rules, research.files.legacy)


def apply_legacy(
    findings: list[Finding],
    excused: Mapping[str, int],
    legacy_file: str,
    touched: Callable[[Finding], bool] | None,
) -> list[Finding]:
    """`findings` of a listed page with its excuses applied.

    `touched` is None when the change does not touch the page (it is checked by name): every finding of an excused rule
    becomes part of one warning per rule. When the change touches the page, `touched` says whether a finding lies in a
    section the change touches, and only `length` and a grading finding (`Finding.by_section`) outside those sections
    stay excused; every other finding of an excused rule is an error. A rule in `excused` without any finding is an
    error: take it out of the file. Findings of other rules, warnings included, pass through; a warning of an excused
    rule is dropped.
    """
    kept: list[Finding] = []
    waived: dict[str, list[Finding]] = {rule: [] for rule in excused}
    for finding in findings:
        if finding.rule not in excused:
            if not finding.held:  # a page not excused from the rule has no use for the reminder
                kept.append(finding)
        elif finding.severity != "error":
            if finding.held:
                waived[finding.rule].append(finding)  # an old-form claim still owing a run record keeps the entry
            continue  # any other warning of an excused rule is part of what the legacy page is excused from
        elif _stays_excused(finding, touched):
            waived[finding.rule].append(finding)
        else:
            kept.append(finding)
    for rule, items in waived.items():
        if not items:
            if not any(f.rule == rule and f.severity == "error" for f in kept):
                kept.append(
                    Finding(
                        "legacy",
                        1,
                        f"the page now passes '{rule}': remove it from {legacy_file} (the file only shrinks)",
                    )
                )
            continue
        first = items[0]
        kept.append(
            Finding(
                rule,
                first.line,
                f"legacy page: {len(items)} finding{'s' if len(items) != 1 else ''} excused by {legacy_file} "
                f"(first: {first.message})",
                "warning",
            )
        )
    return sorted(kept, key=lambda item: item.line)


def _stays_excused(finding: Finding, touched: Callable[[Finding], bool] | None) -> bool:
    """Whether an error of an excused rule is still excused: always while the change leaves the page alone, and on a
    touched page only the length and the grading of a section the change does not touch."""
    if touched is None or finding.rule == "length":
        return True
    return finding.by_section and not touched(finding)

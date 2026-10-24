# SPDX-License-Identifier: GPL-3.0-or-later
"""The legacy file of code and test files: the files below the standard, each under the rules it fails.

`research/legacy-files.toml` (`standards.legacy_file` in .github/conventions.yml) lists a source or test file under the
rules it fails, with the number of findings at the last scan:

    ["src/ai/brain.cpp"]
    comment = 4
    tag = 31

    ["tests/ai/brain_test.cpp"]
    citation = 9

It is the counterpart of `research/legacy.toml` (research pages, `research_legacy`), and it works the same way: the file
is written once by `coney-tools standards init`, it only shrinks, and a file leaves it as it meets the standard. The
numbers tell people deciding what to fix first how much is left; they are never compared with a file.
"""

from __future__ import annotations

import tomllib
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.conventions import Standards
from coney_tools.standards_scan import CODE_RULES, RULES, TEST_RULES

HEADER = (
    "# Source and test files that do not meet the standard yet (docs/guides/coney-tools-standards.md#standards), each\n"
    "# under the rules it fails with the number of findings at the last scan. The file only shrinks: a file leaves it\n"
    "# once it passes (`coney-tools standards prune`); nothing is added after the first scan, which is\n"
    "# `coney-tools standards init`. Research pages are in research/legacy.toml and a function's status is in the\n"
    "# function database.\n"
)


@dataclass(frozen=True)
class LegacyFiles:
    """The files of the legacy file, each with the rules it fails and the findings counted for each."""

    files: Mapping[str, Mapping[str, int]] = field(default_factory=dict)

    def rules(self, path: str) -> Mapping[str, int]:
        """The rules `path` is listed under (empty for a file that is not listed)."""
        return self.files.get(path, {})

    def __contains__(self, path: object) -> bool:
        """Whether the file is listed."""
        return path in self.files


def rules_of(path: str, standards: Standards) -> tuple[str, ...]:
    """The rules a file at `path` can be listed under: the code rules for a source file, the test rules for a test
    file. A path in neither's folders has none."""
    suffix = Path(path).suffix
    if path.startswith(standards.tests.folders) and suffix in standards.tests.suffixes:
        return TEST_RULES
    if path.startswith(standards.code.folders) and suffix in standards.code.suffixes:
        return CODE_RULES
    return ()


def parse_legacy_files(text: str, standards: Standards, where: str) -> LegacyFiles:
    """The legacy file's `text`. ConfigError names the file or rule that is not allowed."""
    try:
        data = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise ConfigError(f"{where}: not valid TOML ({error})") from error
    files: dict[str, dict[str, int]] = {}
    for path, rules in data.items():
        allowed = rules_of(path, standards)
        if not allowed:
            raise ConfigError(f"{where}: '{path}' is neither a source file nor a test file of the standards")
        if not isinstance(rules, dict) or not rules:
            raise ConfigError(f"{where}: '{path}' lists no rule")
        for rule, count in rules.items():
            if rule not in allowed:
                raise ConfigError(f"{where}: '{path}': '{rule}' is not a rule of this file ({', '.join(allowed)})")
            if not isinstance(count, int) or isinstance(count, bool) or count < 1:
                raise ConfigError(f"{where}: '{path}': '{rule}' is a count of findings, a whole number of 1 or more")
        files[path] = {rule: rules[rule] for rule in RULES if rule in rules}
    return LegacyFiles(files)


def format_legacy_files(legacy: LegacyFiles) -> str:
    """The file's text: the header, then each file under its rules, in path order and the order of the standard."""
    blocks = [HEADER.rstrip("\n")]
    for path in sorted(legacy.files):
        lines = [f'["{path}"]']
        lines.extend(f"{rule} = {count}" for rule, count in legacy.files[path].items())
        blocks.append("\n".join(lines))
    return "\n\n".join(blocks) + "\n"


def load_legacy_files(root: Path, standards: Standards) -> LegacyFiles:
    """The legacy file of the checkout at `root`; empty when it has none."""
    path = root / standards.legacy_file
    if not path.is_file():
        return LegacyFiles()
    return parse_legacy_files(path.read_text(encoding="utf-8"), standards, standards.legacy_file)

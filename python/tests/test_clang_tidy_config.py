# SPDX-License-Identifier: GPL-3.0-or-later
"""The shape of the repository's `.clang-tidy`: every check named, none by wildcard.

A wildcard (`bugprone-*`) lets a newer clang-tidy add checks nobody chose, so a pull request can go red after a runner
image update with no change of ours. The file lists the checks it runs by name, sorted as `clang-tidy --list-checks`
prints them, so a new check is one added line (`docs/guides/clang-tidy.md`).
"""

import re

import yaml
from conventions_support import REPO

CONFIG = REPO / ".clang-tidy"

#: A check name: a family, a dash, then the rest (`bugprone-use-after-move`, `clang-analyzer-core.NullDereference`).
CHECK_NAME = re.compile(r"[a-z][a-z0-9]*-[A-Za-z0-9_.-]+")


def check_entries() -> list[str]:
    """The comma- or newline-separated entries of `Checks`, as clang-tidy splits them."""
    config = yaml.safe_load(CONFIG.read_text(encoding="utf-8"))
    return [entry for entry in re.split(r"[,\s]+", config["Checks"]) if entry]


def test_the_list_starts_by_turning_every_default_check_off() -> None:
    # `-*` is the one wildcard: without it clang-tidy's own defaults (clang-diagnostic-*, clang-analyzer-*) are on too
    assert check_entries()[0] == "-*"


def test_every_other_entry_is_a_check_named_in_full() -> None:
    names = check_entries()[1:]
    assert names, "no check is turned on"
    wildcards = [name for name in names if "*" in name]
    assert not wildcards, f"a check is added by name, never by wildcard: {wildcards}"
    removals = [name for name in names if name.startswith("-")]
    assert not removals, f"leave a check out of the list instead of removing it after: {removals}"
    malformed = [name for name in names if not CHECK_NAME.fullmatch(name)]
    assert not malformed, f"not a check name: {malformed}"


def test_the_checks_are_sorted_and_each_is_listed_once() -> None:
    names = check_entries()[1:]
    assert names == sorted(set(names)), "keep the list sorted, as `clang-tidy --list-checks` prints it, one entry each"


def test_findings_still_fail_the_run_and_headers_are_filtered_to_coney() -> None:
    config = yaml.safe_load(CONFIG.read_text(encoding="utf-8"))
    assert config["WarningsAsErrors"] == "*"
    assert config["HeaderFilterRegex"] == ".*/(src|tests)/.*"

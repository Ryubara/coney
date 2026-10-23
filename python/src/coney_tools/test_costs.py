# SPDX-License-Identifier: GPL-3.0-or-later
"""How long each test took before, for the build's lane choice (docs/guides/coney-tools-build.md#build).

A filtered test run that selects one or two tests is not always quick: one disc playthrough can hold a slot for half an
hour. So the lane is chosen by the selected tests' recorded times as well as by their count (build_run.py). ctest
keeps each test's average time in the build folder (`Testing/Temporary/CTestCostData.txt`, a line `name runs seconds`
per test, then `---` and the tests that failed last), but a build folder is often new: agents delete theirs when a unit
is done. So after each test run the folder's times are also kept per preset in one small JSON file in the claims
folder, which every worktree sees. A damaged or missing file costs the estimate, never a run, so nothing here raises.
"""

from __future__ import annotations

import contextlib
import json
import os
import re
import uuid
from pathlib import Path

#: The file in the claims folder.
FILE_NAME = "test-costs.json"
#: Where ctest keeps the times in a build folder.
COST_DATA = Path("Testing") / "Temporary" / "CTestCostData.txt"

_LISTED = re.compile(r"^\s*Test\s+#\d+: (?P<name>.+?)\s*$", re.MULTILINE)


def parse_cost_data(text: str) -> dict[str, float]:
    """Each test's average seconds from a `CTestCostData.txt`; a name may hold spaces, so the two numbers are taken from
    the end of the line. Lines that do not parse are skipped."""
    costs: dict[str, float] = {}
    for line in text.splitlines():
        if line.strip() == "---":
            break  # the failed tests follow, without times
        parts = line.rsplit(" ", 2)
        if len(parts) != 3 or not parts[0]:
            continue
        try:
            runs, seconds = int(parts[1]), float(parts[2])
        except ValueError:
            continue
        if runs > 0 and seconds >= 0:
            costs[parts[0]] = seconds
    return costs


def parse_test_names(listing: str) -> list[str]:
    """The names of the tests a `ctest -N` listing selects, in its order."""
    return [found.group("name") for found in _LISTED.finditer(listing)]


class TestCosts:
    """The shared record at `path`: per preset, each test's last known average seconds."""

    __test__ = False  # not a pytest class, despite the name

    def __init__(self, path: Path) -> None:
        self.path = path

    def _load_all(self) -> dict[str, dict[str, float]]:
        """Every preset's usable times; an unreadable or odd file reads as empty."""
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}
        if not isinstance(data, dict):
            return {}
        found: dict[str, dict[str, float]] = {}
        for preset, tests in data.items():
            if isinstance(preset, str) and isinstance(tests, dict):
                found[preset] = {
                    name: float(seconds)
                    for name, seconds in tests.items()
                    if isinstance(seconds, (int, float)) and not isinstance(seconds, bool) and seconds >= 0
                }
        return found

    def load(self, preset: str, folder_costs: Path) -> dict[str, float]:
        """The times of `preset`'s tests: the shared record, overridden by the build folder's own cost file, which is
        newer when it exists."""
        costs = dict(self._load_all().get(preset, {}))
        with contextlib.suppress(OSError):
            costs.update(parse_cost_data(folder_costs.read_text(encoding="utf-8")))
        return costs

    def record(self, preset: str, folder_costs: Path) -> None:
        """Merge a build folder's cost file into the shared record after a test run. The file is replaced whole (a
        temporary file renamed over it), so a reader never sees half of one."""
        try:
            fresh = parse_cost_data(folder_costs.read_text(encoding="utf-8"))
        except OSError:
            return
        if not fresh:
            return
        data = self._load_all()
        data[preset] = {**data.get(preset, {}), **{name: round(seconds, 2) for name, seconds in fresh.items()}}
        temporary = self.path.with_name(f".{self.path.name}.{os.getpid()}.{uuid.uuid4().hex[:6]}")
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps(data, indent=0, sort_keys=True), encoding="utf-8")
            temporary.replace(self.path)
        except OSError:
            # POSIX says NotADirectoryError, not FileNotFoundError, when the folder is a file.
            with contextlib.suppress(OSError):
                temporary.unlink()

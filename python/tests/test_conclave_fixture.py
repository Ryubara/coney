# SPDX-License-Identifier: GPL-3.0-or-later
"""The Conclave mod's fixture snapshot is what `conclave snapshot` prints, so the mod's tests and the command share one
shape. Run with CONEY_UPDATE_FIXTURES=1 to write the file again after a change to the snapshot."""

import json
import os
from pathlib import Path
from typing import Any

from conclave_support import FakeGh, Layout, github_answers, make_layout, make_task, sources, stamp_steps
from conventions_support import REPO

from coney_tools.conclave_snapshot import collect
from coney_tools.conventions import load_conventions

FIXTURE = REPO / "tools" / "claude-mod" / "tests" / "fixtures" / "snapshot.ts"
#: The mod loads only code files, so the snapshot is a TypeScript module that holds the JSON.
HEADER = (
    "// Written by python/tests/test_conclave_fixture.py from the Python tests' synthetic units: do not edit.\n"
    "// Run that test with CONEY_UPDATE_FIXTURES=1 to write it again after a change to the snapshot.\n"
    "export default "
)


def portable(value: Any, root: Path) -> Any:
    """The value with the test's temporary folder written as `/work` and backslashes as slashes."""
    if isinstance(value, str):
        return value.replace(str(root), "/work").replace("\\", "/")
    if isinstance(value, list):
        return [portable(item, root) for item in value]
    if isinstance(value, dict):
        return {key: portable(item, root) for key, item in value.items()}
    return value


def fixture_layout(root: Path) -> Layout:
    """The two units of the snapshot tests and a third that has merged."""
    made = make_layout(root)
    make_task(made.scratch, "add-task-start", (799,), ("python/src/coney_tools/task_cli.py",))
    steps = load_conventions(REPO).stamps.steps
    stamp_steps(made.scratch, "add-task-start", [(step, "pass") for step in steps], hours_ago=1)
    return made


def test_the_fixture_snapshot_is_what_the_command_prints(tmp_path: Path) -> None:
    made = fixture_layout(tmp_path)
    report = portable(collect(sources(made, FakeGh(github_answers())), github=True), tmp_path)
    text = HEADER + json.dumps(report, indent=1, ensure_ascii=False) + "\n"
    if os.environ.get("CONEY_UPDATE_FIXTURES") == "1":
        FIXTURE.parent.mkdir(parents=True, exist_ok=True)
        FIXTURE.write_text(text, encoding="utf-8", newline="\n")
    written = FIXTURE.read_text(encoding="utf-8").replace("\r\n", "\n")
    assert written == text, "run this test with CONEY_UPDATE_FIXTURES=1"

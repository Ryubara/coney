# SPDX-License-Identifier: GPL-3.0-or-later
"""An optional test against a running ghidra-mcp (`pytest -m ghidra`): skipped when none answers or the function is
absent. It asserts counts and hashes only, never any text of the game."""

from datetime import UTC, datetime
from pathlib import Path

import pytest
from stamp_support import rules

from coney_tools import orig_study
from coney_tools.orig_ghidra import server_answers
from coney_tools.progress_research import DEFAULT_GHIDRA_URL, http_fetch
from coney_tools.unit import Unit

pytestmark = pytest.mark.ghidra

#: A small function every project built from the disc has (docs/research/combat.md#code-index).
ADDRESS = "0x00230d00"


def test_a_real_function_is_studied_the_same_way_twice(tmp_path: Path) -> None:
    fetch = http_fetch(DEFAULT_GHIDRA_URL)
    if not server_answers(fetch):
        pytest.skip("no ghidra-mcp answers")
    unit = Unit("live", tmp_path / "live")
    arguments = {"rules": rules(), "commit": "ab" * 20, "who": {"kind": "tool", "tool": "test"}}
    try:
        first = orig_study.study(unit, ADDRESS, fetch, clock=lambda: datetime(2026, 10, 22, tzinfo=UTC), **arguments)  # type: ignore[arg-type]
    except orig_study.StudyError as error:
        pytest.skip(f"this project cannot study {ADDRESS}: {error}")
    assert len(first.cut.statements) > 5 and first.stamped
    second = orig_study.study(unit, ADDRESS, fetch, **arguments)  # type: ignore[arg-type]
    assert second.change == "unchanged" and not second.stamped
    assert second.study.covers == first.study.covers
    assert orig_study.freshness(orig_study.load_study(unit, ADDRESS), fetch) == "fresh"

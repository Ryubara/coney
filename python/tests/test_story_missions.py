# SPDX-License-Identifier: GPL-3.0-or-later
"""The debug menu's story missions table (src/debug/story_missions.cpp) matches the mission list and levels list."""

import re
from pathlib import Path

from coney_tools import missions

ROOT = Path(__file__).resolve().parents[2]


def _table() -> list[tuple[int, str, str, int]]:
    """The rows of the C++ table: number, title, level, checkpoints."""
    text = (ROOT / "src/debug/story_missions.cpp").read_text(encoding="utf-8")
    rows = re.findall(r'\{(\d+), "([^"]*)", "(level\d+)", (\d+)\}', text)
    return [(int(n), title, level, int(cp)) for n, title, level, cp in rows]


def test_the_debug_menus_story_missions_match_the_mission_list() -> None:
    story = [m for m in missions.load(ROOT).missions if m.group == "story"]
    expected = [(i + 1, m.title, m.name, len(m.checkpoints)) for i, m in enumerate(story)]
    assert _table() == expected

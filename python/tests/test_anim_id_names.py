# SPDX-License-Identifier: GPL-3.0-or-later
"""The debug menu's anim id names (src/debug/anim_id_names.cpp) match the anim ids reference list."""

import re
from pathlib import Path

import pytest
import yaml

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("docs_only", "engine")

ROOT = Path(__file__).resolve().parents[2]


def _table() -> list[str]:
    """The names of the C++ table, in id order."""
    text = (ROOT / "src/debug/anim_id_names.cpp").read_text(encoding="utf-8")
    return re.findall(r'^\s+"(ANIM_[A-Z0-9_]*)",$', text, flags=re.MULTILINE)


def test_the_debug_menus_anim_id_names_match_the_reference_list() -> None:
    data = yaml.safe_load((ROOT / "research/references/anim-ids.yaml").read_text(encoding="utf-8"))
    entries = sorted(data["entries"], key=lambda entry: entry["id"])
    assert [entry["id"] for entry in entries] == list(range(len(entries)))
    assert _table() == [entry.get("name", "") for entry in entries]

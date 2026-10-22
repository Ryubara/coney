# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that every agent definition in .claude/agents/ is one Claude Code will load.

Claude Code skips an agent file whose frontmatter is not valid YAML, silently: the role is simply missing from the
session. On 2026-10-22 `tooling.md` had an unquoted colon in its description and never loaded. These tests read each
file the way Claude Code does (a YAML block between the first two `---` lines) and require the fields it needs.
"""

from pathlib import Path

import pytest
import yaml

AGENTS = Path(__file__).resolve().parents[2] / ".claude" / "agents"


def _definitions() -> list[Path]:
    """Every agent definition file in the repository."""
    return sorted(AGENTS.glob("*.md"))


def _frontmatter(path: Path) -> object:
    """The parsed YAML block at the top of an agent file."""
    text = path.read_text(encoding="utf-8")
    assert text.startswith("---"), f"{path.name}: no frontmatter"
    return yaml.safe_load(text.split("---", 2)[1])


def test_there_are_agent_definitions() -> None:
    assert _definitions()


@pytest.mark.parametrize("path", _definitions(), ids=lambda p: p.name)
def test_agent_frontmatter_loads_with_a_name_and_description(path: Path) -> None:
    data = _frontmatter(path)
    assert isinstance(data, dict), f"{path.name}: frontmatter is not a mapping"
    assert data.get("name") == path.stem, f"{path.name}: name must match the file name"
    assert isinstance(data.get("description"), str) and data["description"], f"{path.name}: no description"

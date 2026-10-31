# SPDX-License-Identifier: GPL-3.0-or-later
"""What an agent that is not Claude Code reads (docs/guides/agents-other-tools.md): Codex loads `AGENTS.md` and the
files of its folders up to a combined 32 KiB (`project_doc_max_bytes`), and opens nothing a file merely links to, so
`AGENTS.md` stays small and itself names the commands that stand in for the Claude Code hooks."""

from __future__ import annotations

import pytest
from conventions_support import REPO

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("docs_only")

#: Codex's default `project_doc_max_bytes` (OpenAI's AGENTS.md page): all instruction files of a run together.
CODEX_LIMIT = 32 * 1024


def test_agents_md_fits_in_half_of_codexs_limit() -> None:
    size = (REPO / "AGENTS.md").stat().st_size
    assert size <= CODEX_LIMIT // 2, (
        f"AGENTS.md is {size} bytes: Codex stops reading at {CODEX_LIMIT} for the whole chain"
    )


def test_agents_md_itself_names_the_commands_that_stand_in_for_the_hooks() -> None:
    text = (REPO / "AGENTS.md").read_text(encoding="utf-8")
    for command in ("uvx pre-commit install", "ci pr --title", "CONTRIBUTING.md"):
        assert command in text, command


def test_contributing_describes_the_same_three_commands() -> None:
    text = (REPO / "CONTRIBUTING.md").read_text(encoding="utf-8")
    assert "### Agents other than Claude Code" in text
    for command in (
        "coney-tools doctor",
        "uvx pre-commit install",
        'coney-tools ci pr --title "area: Verb the rest" --body description.md',
    ):
        assert command in text, command


#: The command-line tools the maintainer decided on 2026-10-29 Coney supports, with the hook file each reads.
SUPPORTED_CLIS = {
    "Codex": ".codex/hooks.json",
    "Antigravity": ".agents/hooks.json",
    "Grok": ".grok/hooks/coney.json",
}


def test_the_codex_antigravity_and_grok_clis_are_supported_not_open() -> None:
    enforcement = (REPO / "docs/guides/enforcement.md").read_text(encoding="utf-8")
    assert "2026-10-29" in enforcement and "**supports**" in enforcement
    contributing = (REPO / "CONTRIBUTING.md").read_text(encoding="utf-8")
    assert "supported tools, not a trial" in contributing
    for tool, hook_file in SUPPORTED_CLIS.items():
        assert (REPO / hook_file).is_file(), f"{tool}: {hook_file} is not committed"
        assert tool in contributing, tool

# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the /repo-hygiene skill: it exists, its links work and it covers every check of the audit's registry."""

import re

import pytest
from conventions_support import REPO

from coney_tools.conventions import load_conventions

SKILL = REPO / ".claude/skills/repo-hygiene/SKILL.md"
CONVENTIONS = load_conventions(REPO)
#: A Markdown link whose target is a relative path (not a web address or a bare anchor).
LINK = re.compile(r"\]\((?!https?:|#)([^)#\s]+)(?:#([^)\s]+))?\)")


def text() -> str:
    """The skill file's text."""
    return SKILL.read_text(encoding="utf-8")


def test_the_skill_has_a_name_that_matches_its_folder_and_a_description() -> None:
    front = re.match(r"^---\n(.*?)\n---\n", text(), re.DOTALL)
    assert front is not None
    fields = dict(line.split(": ", 1) for line in front.group(1).splitlines())
    assert fields["name"] == SKILL.parent.name == "repo-hygiene"
    assert len(fields["description"]) > 40 and "Coordinator" in fields["description"]


@pytest.mark.parametrize("fix", ["auto", "skill", "owner"])
def test_every_check_is_covered_by_its_fix_class(fix: str) -> None:
    body = text()
    missing = [
        spec.id for spec in CONVENTIONS.hygiene.checks.values() if spec.fix == fix and f"`{spec.id}`" not in body
    ]
    assert not missing, f"add these {fix} checks to {SKILL.name}: {missing}"


def test_the_skill_names_no_check_that_does_not_exist() -> None:
    named = set(re.findall(r"`((?:files|issue|pr|gh|ws)\.[a-z-]+)`", text()))
    assert named <= set(CONVENTIONS.hygiene.checks), named - set(CONVENTIONS.hygiene.checks)


def test_the_three_fix_classes_each_have_a_section() -> None:
    body = text()
    for heading in ("### `auto`", "### `skill`", "### `owner`"):
        assert heading in body


def test_every_relative_link_reaches_a_page_and_its_anchor() -> None:
    problems = []
    for target, anchor in LINK.findall(text()):
        page = (SKILL.parent / target).resolve()
        if not page.is_file():
            problems.append(f"{target}: no such file")
        elif anchor and f"{{#{anchor}}}" not in page.read_text(encoding="utf-8"):
            problems.append(f"{target}#{anchor}: no such anchor")
    assert not problems, problems


def test_the_skill_never_lets_an_agent_write_to_github_and_stops_at_maintainer_decisions() -> None:
    body = text()
    assert "Only the coordinator writes to GitHub" in body
    assert "Do not change the ruleset" in body

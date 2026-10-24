# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the link schemes that `mkdocs_hooks.py` resolves at build time: `repo:<path>` and `issue:<number>`, both
from the one repository address the build works out (docs/guides/writing-docs.md#adding-a-page)."""

from __future__ import annotations

import importlib.util
from pathlib import Path
from types import ModuleType

import pytest

HOOKS_PATH = Path(__file__).resolve().parents[2] / "mkdocs_hooks.py"
BASE = "https://example.org/someone/coney"


# The hooks file sits at the repository root, outside the package, so it is loaded by path.
@pytest.fixture(scope="module")
def hooks() -> ModuleType:
    spec = importlib.util.spec_from_file_location("mkdocs_hooks", HOOKS_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_an_issue_link_becomes_the_issues_page(hooks: ModuleType) -> None:
    assert hooks.resolve_repo_links("See [#123](issue:123).", BASE) == f"See [#123]({BASE}/issues/123)."
    assert hooks.resolve_repo_links("[a](issue:1) and [b](issue:22)", BASE + "/") == (
        f"[a]({BASE}/issues/1) and [b]({BASE}/issues/22)"
    )


def test_an_issue_link_reference_definition_becomes_the_issues_page(hooks: ModuleType) -> None:
    text = "See [#123][t].\n\n[t]: issue:123\n   [u]: issue:45  \n"
    assert (
        hooks.resolve_repo_links(text, BASE) == f"See [#123][t].\n\n[t]: {BASE}/issues/123\n   [u]: {BASE}/issues/45\n"
    )


def test_a_repo_link_still_becomes_the_file_on_the_default_branch(hooks: ModuleType) -> None:
    text = "[x](repo:LEGAL.md#no-game-data) and [#1](issue:1)"
    assert (
        hooks.resolve_repo_links(text, BASE) == f"[x]({BASE}/blob/main/LEGAL.md#no-game-data) and [#1]({BASE}/issues/1)"
    )


def test_only_a_number_makes_an_issue_link(hooks: ModuleType) -> None:
    for text in ("[x](issue:abc)", "[x](issue:)", "[x](issue:12x)", "see issue:123 here", "x [t]: issue:1"):
        assert hooks.resolve_repo_links(text, BASE) == text


def test_without_a_repository_both_links_are_empty_anchors(hooks: ModuleType) -> None:
    text = "[x](repo:LEGAL.md) [#1](issue:1)\n\n[t]: issue:2\n"
    assert hooks.resolve_repo_links(text, None) == "[x](#) [#1](#)\n\n[t]: #\n"


def test_on_page_markdown_resolves_issue_links_from_repo_url(hooks: ModuleType) -> None:
    out = hooks.on_page_markdown("[#7](issue:7)", {"repo_url": BASE})
    assert out == f"[#7]({BASE}/issues/7)"


def test_links_in_code_are_left_as_written(hooks: ModuleType) -> None:
    text = "Write `[#123](issue:123)` or `[x](repo:LEGAL.md)`, as in [#1](issue:1).\n"
    assert hooks.resolve_repo_links(text, BASE) == (
        f"Write `[#123](issue:123)` or `[x](repo:LEGAL.md)`, as in [#1]({BASE}/issues/1).\n"
    )
    assert hooks.resolve_repo_links("Write ``[x](repo:a.md)`` here.", BASE) == "Write ``[x](repo:a.md)`` here."


@pytest.mark.parametrize(("opening", "closing"), [("```", "```"), ("~~~~", "~~~~"), ("```markdown", "````")])
def test_links_in_a_fenced_block_are_left_as_written(hooks: ModuleType, opening: str, closing: str) -> None:
    inside = "[b](issue:2) [c](repo:LEGAL.md)\n[t]: issue:3\n"
    done = hooks.resolve_repo_links(f"[a](issue:1)\n\n{opening}\n{inside}{closing}\n\n[d](issue:4)\n", BASE)
    assert f"[a]({BASE}/issues/1)" in done and f"[d]({BASE}/issues/4)" in done
    assert inside in done


def test_an_unclosed_fence_runs_to_the_end_of_the_page(hooks: ModuleType) -> None:
    text = "[a](issue:1)\n\n```\n[b](issue:2)\n"
    assert hooks.resolve_repo_links(text, BASE) == f"[a]({BASE}/issues/1)\n\n```\n[b](issue:2)\n"

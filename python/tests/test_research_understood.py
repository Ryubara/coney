# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that the "Understood" measure reads the function database (research/functions/) as citations."""

from __future__ import annotations

from pathlib import Path

import pytest
from test_progress import make_research_checkout
from test_research_functions import write_systems

from coney_tools import progress, progress_history, progress_research, research_functions

# Alpha_Step (0x1100) is cited by the page without a level; Beta_Run (0x1200) is cited only by the source map.
ALPHA = """\
[[function]]
address = 0x00001100
name = "Alpha_Step"
page = "alpha.md"
role = "steps"
level = "confirmed-code"

[[function]]
address = 0x00001200
name = "Beta_Run"
page = "alpha.md"
level = "speculative"
"""
NOT_NEEDED = """\
[[function]]
address = 0x00001500
name = "Odd_Helper"
page = "alpha.md"
role = "dead code"
level = "not-needed"
"""


def test_an_entry_cites_its_function_with_evidence(tmp_path: Path) -> None:
    """An entry with a role carries a level and says what it does, so it is a citation with evidence."""
    root = make_research_checkout(tmp_path)
    write_systems(root, {"alpha": ALPHA})
    citations = progress_research.scan_citations(root)
    step = citations[0x1100]
    assert step.evidence  # the page's own row states none; the entry does
    assert {"research/alpha.md", "research/functions/alpha.toml"} <= step.pages
    assert step.page_name == "Alpha_Step"
    assert citations[0x1200].page_name == "Beta_Run"


def test_an_entry_without_a_role_is_a_citation_without_evidence(tmp_path: Path) -> None:
    """Understood asks for a citation that says what the function does: an entry with no `role` says nothing."""
    root = make_research_checkout(tmp_path)
    write_systems(root, {"alpha": ALPHA})
    citations = progress_research.scan_citations(root)
    assert "research/functions/alpha.toml" in citations[0x1200].pages
    assert not citations[0x1200].evidence


def test_the_database_name_is_the_page_name(tmp_path: Path) -> None:
    """The backlog's "name on the page" is the entry's name when there is one, not a guess from prose."""
    root = make_research_checkout(tmp_path)
    write_systems(root, {"alpha": NOT_NEEDED.replace("Odd_Helper", "Real_Name")})
    assert progress_research.scan_citations(root)[0x1500].page_name == "Real_Name"


def test_the_measure_counts_database_entries(tmp_path: Path) -> None:
    """A function named in Ghidra and entered in the database is understood, with no prose citation at all."""
    root = make_research_checkout(tmp_path)
    before = progress.load(root).understanding
    assert before is not None
    assert before.understood_functions == 1
    write_systems(root, {"alpha": ALPHA})
    after = progress.load(root).understanding
    assert after is not None
    assert after.understood_functions == 2  # Alpha_Init and Alpha_Step; Beta_Run's entry has no role


def test_an_unnamed_function_in_the_database_still_needs_a_name(tmp_path: Path) -> None:
    """The bar asks for a Ghidra name as well as a citation: `0x1500` is unnamed in the listing."""
    root = make_research_checkout(tmp_path)
    write_systems(root, {"alpha": NOT_NEEDED})
    understanding = progress.load(root).understanding
    assert understanding is not None
    status = next(s for s in understanding.statuses if s.function.address == 0x1500)
    assert status.cited
    assert status.missing() == ["name"]


def test_a_malformed_database_stops_the_tracker(tmp_path: Path) -> None:
    """A broken system file is an error the docs build shows, not a silently smaller count."""
    root = make_research_checkout(tmp_path)
    write_systems(root, {"alpha": ALPHA.replace("speculative", "sure")})
    with pytest.raises(progress.ProgressError, match="level"):
        progress_research.scan_citations(root)


def test_no_database_changes_nothing(tmp_path: Path) -> None:
    """A checkout without research/functions/ measures exactly as before."""
    root = make_research_checkout(tmp_path)
    citations = progress_research.scan_citations(root)
    assert not any("functions" in page for c in citations.values() for page in c.pages)


def test_the_history_measures_the_database_too() -> None:
    """Each day's snapshot includes research/functions, and a change to its schema code invalidates the cache."""
    assert "research/functions" in progress_history.INPUTS
    assert research_functions in progress_history.DEFINITION_MODULES

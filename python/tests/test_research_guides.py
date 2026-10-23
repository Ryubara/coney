# SPDX-License-Identifier: GPL-3.0-or-later
"""The guides say what the research page checks do: statements that go stale when a check changes.

Each test reads the text of a guide and holds one statement of it to what the checks now do (`research_evidence`,
`research_legacy`, `docs_check`); the checks' own behaviour is tested in `test_research_check.py` and
`test_research_legacy.py`. So a change to a check that leaves its guide behind fails here.
"""

from __future__ import annotations

import re

from conventions_support import REPO

GUIDES = REPO / "docs/guides"


def guide(name: str) -> str:
    """The text of a guide with its line breaks folded, so a sentence can be searched whole."""
    return re.sub(r"\s+", " ", (GUIDES / name).read_text(encoding="utf-8"))


def test_the_guide_counts_the_false_hits_of_the_short_form_rule() -> None:
    # A colour and a size (the executable's memory size in docs/research/overview.md) both land in `.bss`.
    text = guide("writing-docs.md")
    match = re.search(r"the short form of a named function's entry.*?false hits?[^;]*", text)
    assert match is not None
    sentence = match.group(0)
    assert "two false hits" in sentence and "colour" in sentence and "size" in sentence
    assert "one false hit" not in text


def test_the_guides_name_no_plan_for_the_run_record_tool() -> None:
    for name in ("writing-docs.md", "research-workflow.md"):
        text = guide(name)
        assert "arrive with T4" not in text and "tool is planned" not in text, name
    assert "pcsx2 record-run" in guide("research-workflow.md")


def test_the_guide_does_not_advise_inferred_for_a_runtime_claim_and_links_the_commands() -> None:
    text = guide("writing-docs.md")
    start = text.index("- `confirmed (runtime)` says")
    bullet = text[start : text.index("- `speculative` links", start)]
    # `inferred` is the level of what the code implies: a claim waiting for a run record is `speculative`. The
    # commands that write a record belong to research-workflow.md#run-records: the bullet links them, not repeats them.
    assert "inferred" not in bullet and "`speculative`" in bullet
    assert "research-workflow.md#run-records" in bullet and "record-run" not in bullet


def test_the_guide_scopes_an_old_runtime_claim_to_the_touched_section() -> None:
    text = guide("writing-docs.md")
    start = text.index("### Legacy pages")
    legacy = text[start : text.index("`coney-tools docs legacy` prints", start)]
    # The old-form runtime claim is graded like a block with no level: in the sections the change touches.
    assert re.search(r"grading of a block is held to the sections[^;]*old-form runtime claim", legacy)

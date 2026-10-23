# SPDX-License-Identifier: GPL-3.0-or-later
"""The evidence rule of the research page standard: every block that cites an address states how it is known.

The levels and their spellings are those of docs/guides/research-evidence.md#evidence-levels. `check_evidence` yields
the `evidence` findings of a page: an address with no level in its block, row or lead line and an old-form runtime claim
on a page that does not explain it (both `Finding.by_section`, which a legacy page owes only in the sections a change
touches), and a level in a form that is refused (which it owes page-wide).
"""

from __future__ import annotations

import re
from collections.abc import Iterable
from dataclasses import dataclass

from coney_tools.docs_page import Block, Finding, address_line, section_text, split_cells
from coney_tools.research_subject import ADDRESS, LOCATOR, VERIFIED_LINE, Subject

# --- the levels -------------------------------------------------------------------------------------------------------

_CODE = re.compile(r"confirmed\s*\(\s*code\s*\)", re.IGNORECASE)
_RUNTIME = re.compile(
    r"confirmed\s*\(\s*runtime\s*\)(?:\s*,\s*run\s+(?P<id>[A-Za-z0-9](?:[\w.-]*[\w-])?))?", re.IGNORECASE
)
_INFERRED = re.compile(r"\binferred\b", re.IGNORECASE)
_SPECULATIVE = re.compile(r"\bspeculative\b", re.IGNORECASE)
_NOT_NEEDED = re.compile(r"\bnot needed\s*:", re.IGNORECASE)
#: `confirmed (disc)`, `confirmed (name hash)`, `confirmed (runtime, coney)`: a level that is none of ours.
_OTHER_CONFIRMED = re.compile(r"\bconfirmed\s*\((?!\s*(?:code|runtime)\s*\))[^)\n]*\)", re.IGNORECASE)
_CONFIDENCE = re.compile(r"\bconfidence\s*:", re.IGNORECASE)
_HEDGE = re.compile(r"\b(?:probably|likely)\b", re.IGNORECASE)
#: What an `inferred` claim rests on, in prose.
_BASIS = re.compile(
    r"\binferred\s+(?:from|by|because|since)\b|\b(?:because|since|follows|based on|basis)\b", re.IGNORECASE
)
#: A link to an open question: `[q-name](#q-name)` or `[text](other.md#q-name)`.
QUESTION_LINK = re.compile(r"\]\((?P<page>[^)#\s]*)#(?P<anchor>q-[^)\s]+)\)")


@dataclass(frozen=True)
class Levels:
    """The levels one piece of text states, and what is wrong with how it states them."""

    kinds: frozenset[str] = frozenset()  # code, runtime, inferred, speculative, not-needed
    runs: tuple[str, ...] = ()  # the run ids of its `confirmed (runtime), run <id>` claims
    bare_runtime: bool = False  # a `confirmed (runtime)` with no run id
    refused: tuple[str, ...] = ()  # spellings that are no level of ours


def levels_in(text: str) -> Levels:
    """The levels `text` states (writing-docs.md#evidence-levels): the four spellings, `not needed: <reason>`, and the
    spellings that are refused."""
    kinds: set[str] = set()
    runs: list[str] = []
    bare = False
    if _CODE.search(text):
        kinds.add("code")
    for match in _RUNTIME.finditer(text):
        kinds.add("runtime")
        if match.group("id"):
            runs.append(match.group("id"))
        else:
            bare = True
    if _INFERRED.search(text):
        kinds.add("inferred")
    if _SPECULATIVE.search(text):
        kinds.add("speculative")
    if _NOT_NEEDED.search(text):
        kinds.add("not-needed")
    refused = [match.group(0) for match in _OTHER_CONFIRMED.finditer(text)]
    refused.extend(match.group(0) for match in _CONFIDENCE.finditer(text))
    return Levels(frozenset(kinds), tuple(runs), bare, tuple(refused))


def cites(text: str) -> re.Match[str] | None:
    """The first address or script locator `text` cites."""
    return ADDRESS.search(text) or LOCATOR.search(text)


# --- the evidence -----------------------------------------------------------------------------------------------------


def graded_section(subject: Subject, heading: int) -> bool:
    """Whether the blocks under heading index `heading` state behaviour: they sit in a level-2 section, and not in one
    of the ungraded ones (Summary, In Coney, Open questions, How this was checked)."""
    name = subject.sections[heading] if heading >= 0 else ""
    ungraded = {entry.casefold() for entry in subject.env.research.ungraded_sections}
    return bool(name) and name.casefold() not in ungraded


def lead_lines(subject: Subject) -> dict[int, int]:
    """The heading indexes (level 3 and deeper) whose first block is a lead line, to that block's index: a statement
    that cites no address and states a valid level for the blocks under the heading."""
    page = subject.page
    leads: dict[int, int] = {}
    for index, block in enumerate(page.blocks[:-1]):
        if block.kind != "heading" or page.headings[block.section].level < 3:
            continue
        follower = page.blocks[index + 1]
        if follower.kind != "text" or follower.section != block.section or cites(follower.text):
            continue
        levels = levels_in(follower.text)
        if levels.kinds and not levels.refused:
            leads[block.section] = index + 1
    return leads


def _pcsx2_documented(subject: Subject) -> bool:
    """Whether the page names PCSX2 where its old-form runtime claims are explained: the Verified line or the section
    How this was checked."""
    page = subject.page
    for index in range(len(page.headings)):
        if (
            subject.sections[index].casefold() == "how this was checked"
            and "pcsx2" in section_text(page, index).casefold()
        ):
            return True
    head = " ".join(line.text for line in page.lines if VERIFIED_LINE.match(line.text))
    return "pcsx2" in head.casefold()


def check_evidence(subject: Subject) -> Iterable[Finding]:
    """Every block that cites an address outside the ungraded sections is graded in the forms of the standard; every
    level that is stated is stated in its form. A level word is never an excuse: `confirmed (disc)` is none."""
    page, env = subject.page, subject.env
    leads = lead_lines(subject)
    old_runtime_ok = subject.legacy_page and _pcsx2_documented(subject)
    anchors = page.anchors
    for index, block in enumerate(page.blocks):
        if block.kind == "heading" or is_header_row(subject, index):
            continue
        column = level_column_of(subject, block)
        cell = level_cell(subject, block)
        levels = levels_in(cell)
        whole = levels_in(block.text) if block.kind == "row" and column is not None else levels
        line = block.line
        for spelling in whole.refused:
            yield Finding(
                "evidence",
                line,
                f"'{spelling}' is no evidence level: use confirmed (code), confirmed (runtime), run <id>, inferred "
                "or speculative",
            )
        if _HEDGE.search(cell) and column is not None and block.kind == "row":
            yield Finding(
                "evidence",
                line,
                "an Evidence cell states a level, not 'probably' or 'likely': write inferred, with its basis",
            )
        if not graded_section(subject, block.section):
            continue
        if block.kind == "row" and column is None and subject.sections[block.section] == env.research.map_section:
            continue  # a Map table with no level column indexes functions, whose levels are in their entries
        address = cites(block.text)
        line = _cite_line(block, address)
        if address is not None and not levels.kinds:
            covered = block.kind != "row" or column is None
            lead = leads.get(block.section)
            if lead is not None and covered and lead != index:
                address = None
            else:
                where = "its Evidence cell" if block.kind == "row" and column is not None else "its block"
                yield Finding(
                    "evidence",
                    line,
                    f"{address.group(0)} is cited with no evidence level in {where}: write confirmed (code), "
                    "confirmed (runtime), run <id>, inferred from <basis> or speculative ([q-...](#q-...)); "
                    "or put a lead line under an ### heading (writing-docs.md#evidence-levels)",
                    by_section=True,
                )
        yield from _level_forms(subject, block, levels, cell, line, anchors, old_runtime_ok)


def level_column_of(subject: Subject, block: Block) -> int | None:
    """The Evidence column of the table a row sits in, or None (a paragraph, or a table with no such column)."""
    table = subject.page.tables[block.table] if block.table is not None else None
    return table.level_column if table is not None else None


def level_cell(subject: Subject, block: Block) -> str:
    """The text that grades `block`: a row's Evidence cell when its table has one, else the whole block."""
    column = level_column_of(subject, block)
    if block.kind != "row" or column is None:
        return block.text
    cells = split_cells(block.text)
    return cells[column] if column < len(cells) else ""


def is_header_row(subject: Subject, index: int) -> bool:
    """Whether block `index` is the first row of its table (the header, which labels columns and claims nothing)."""
    block = subject.page.blocks[index]
    return block.kind == "row" and block.table is not None and subject.page.tables[block.table].first == index


def _cite_line(block: Block, address: re.Match[str] | None) -> int:
    """The line a citation sits on: a paragraph's address's own line, a row's line."""
    if address is None or block.kind != "text":
        return block.line
    return address_line(block, address.start())


def _level_forms(
    subject: Subject,
    block: Block,
    levels: Levels,
    cell: str,
    line: int,
    anchors: set[str],
    old_runtime_ok: bool,
) -> Iterable[Finding]:
    """The forms of the levels `levels` (read from `cell`, the part of the block that grades it)."""
    catalog = subject.env.catalog
    if (levels.bare_runtime or levels.runs) and not catalog.has_records and not subject.legacy_page:
        # With no run record yet a run id names nothing and a bare claim has nowhere to point: refuse both, so no
        # page learns to write ids that lead nowhere. Not `inferred`: a PCSX2 observation is not what the code implies.
        yield Finding(
            "evidence",
            line,
            "no run record exists yet (research/runs/ is missing): a runtime claim waits for one. Write it with "
            "`coney-tools pcsx2 record-run` (or `pcsx2 recipe run --record`, `pcsx2 record --run-record`), commit it "
            "with the page and cite it as `confirmed (runtime), run <id>`. What no run observed is speculative, with "
            "an open question",
        )
    elif levels.bare_runtime and not old_runtime_ok:
        yield Finding(
            "evidence",
            line,
            "confirmed (runtime) cites the run that observed it: `confirmed (runtime), run <id>` "
            "(a record in research/runs/)",
            by_section=True,
        )
    for run in levels.runs if catalog.has_records else ():
        if not catalog.has_run(run):
            yield Finding("evidence", line, f"no run record research/runs/{run}.toml")
    if "speculative" in levels.kinds:
        links = list(QUESTION_LINK.finditer(block.text))
        if not links:
            yield Finding(
                "evidence", line, "speculative links the open question that says how to test it: ([q-name](#q-name))"
            )
        for link in links:
            if not link.group("page") and link.group("anchor") not in anchors:
                yield Finding("evidence", line, f"no open question {{#{link.group('anchor')}}} on this page")
    if "inferred" in levels.kinds and block.kind == "text" and not _BASIS.search(cell):
        yield Finding("evidence", line, "inferred names what it follows from: `inferred from <basis>`", "warning")

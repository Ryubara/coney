# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools orig index` and `orig queue`, on a made-up checkout with invented addresses and names."""

import json
from pathlib import Path
from typing import Any

import pytest

from coney_tools import orig_cli, orig_ghidra, orig_index, orig_queue
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.progress_research import ListedFunction

BRAIN_H = """// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A made-up brain. Research: docs/research/made-up.md#brain
namespace coney::ai {
class Brain {
  public:
    /// Thinks. Research: docs/research/made-up.md#thinking
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    void think();
    /// Waits.
    /// @orig 0x00100020 Brain_Wait (unknown)
    void wait();
    /// Coney's own count; the original keeps none.
    int count() const;
    /// A getter.
    int size() const { return 1; }
};
} // namespace coney::ai
"""

BRAIN_CPP = """// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brain.h"
namespace coney::ai {
namespace {
// A helper.
int helper() { return 1; }
} // namespace
void Brain::think() {}
void Brain::wait() {}
int Brain::count() const { return 0; }
/// Not tagged and not marked.
void Brain::forget() {
    helper();
}
/// Marked.
/// @coney-only a test aid
void Brain::debugDump() {}
} // namespace coney::ai
"""

PAGE = """# Made up

## The brain {#brain}

The brain at `0x00100010` (`Brain_Think`) thinks. **Evidence:** inferred.

## Thinking

It thinks hard; no address here.

## Waiting

`Brain_Wait` at `0x00100020` waits. **Evidence:** confirmed (code) at 0x00100020.
"""

LISTING = """# header
0x00100010\t16\tBrain_Think\t1
0x00100020\t32\tBrain_SetWaiting\t0
0x00100100\t16\tOther_Function\t0
"""


@pytest.fixture
def checkout(tmp_path: Path) -> Path:
    """A made-up checkout: a gameplay directory with a brain, a debug directory, a research page and a listing."""
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    files = {
        "src/ai/brain.h": BRAIN_H,
        "src/ai/brain.cpp": BRAIN_CPP,
        "src/debug/menu.cpp": "namespace coney { void menu() {} }\n",
        "docs/research/made-up.md": PAGE,
        "docs/progress/ghidra-functions.tsv": LISTING,
    }
    for name, text in files.items():
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text, encoding="utf-8")
    return tmp_path


def test_every_tag_is_indexed_with_its_coney_function_and_research(checkout: Path) -> None:
    index = orig_index.build_index(checkout)
    rows = [(f"0x{r.address:08x}", r.name, r.file, r.coney, r.where, r.research) for r in index.rows]
    assert rows == [
        (
            "0x00100010",
            "Brain_Think",
            "Brain.cpp",
            "Brain::think",
            "src/ai/brain.h:8",
            ("docs/research/made-up.md#thinking",),
        ),
        (
            "0x00100020",
            "Brain_Wait",
            "unknown",
            "Brain::wait",
            "src/ai/brain.h:11",
            ("docs/research/made-up.md#brain",),
        ),
    ]


def test_untagged_gameplay_functions_are_listed_and_marked_ones_are_not(checkout: Path) -> None:
    index = orig_index.build_index(checkout)
    untagged = {u.coney: u.kind for u in index.untagged}
    # think/wait take their declaration's tags; count has the older wording; debugDump the marker; the debug
    # directory is out of scope.
    assert untagged == {"Brain::size": "inline", "helper": "helper", "Brain::forget": "member"}
    assert (index.in_scope, index.tagged, index.marked, index.marked_legacy) == (7, 2, 1, 1)


def test_research_sections_without_an_address_are_reported(checkout: Path) -> None:
    index = orig_index.build_index(checkout)
    report = orig_index.check_research(checkout, index.rows)
    assert [(g.link, g.heading) for g in report.gaps] == [("docs/research/made-up.md#thinking", "Thinking")]
    assert report.missing == [] and report.unlinked == []
    # The page cites both addresses somewhere, so no tag lacks its address.
    assert report.uncited == []


def test_a_link_to_a_missing_section_is_reported(checkout: Path) -> None:
    (checkout / "src/ai/brain.h").write_text(BRAIN_H.replace("#thinking", "#gone"), encoding="utf-8")
    report = orig_index.check_research(checkout, orig_index.build_index(checkout).rows)
    assert [g.link for g in report.missing] == ["docs/research/made-up.md#gone"]


def test_evidence_levels_take_the_weakest_claim(checkout: Path) -> None:
    levels = orig_index.evidence_levels(checkout)
    assert orig_index.weakest_evidence(levels.get(0x00100010)) == "inferred"
    assert orig_index.weakest_evidence(levels.get(0x00100020)) == "confirmed"
    assert orig_index.weakest_evidence(levels.get(0x00109999)) == "uncited"


@pytest.mark.parametrize(
    ("tag", "ghidra", "expected"),
    [
        ("DVDWadIndex::Find", "DVDWadIndex_Find", None),
        ("GangSuspend", "lua_GangSuspend", None),
        ("Gang_Suspend", "Gang_SetSuspendedById", "renamed"),
        ("Car_AimPoint", "Human_UpdateDown", "names-differ"),
        ("Brain_Think", "FUN_00100010", "unnamed"),
    ],
)
def test_names_are_compared_without_case_punctuation_or_a_wrapper_prefix(
    tag: str, ghidra: str, expected: str | None
) -> None:
    assert orig_ghidra.compare_names(tag, ghidra) == expected


def test_the_listing_check_finds_starts_and_names(checkout: Path) -> None:
    index = orig_index.build_index(checkout)
    listing = [ListedFunction(0x00100010, 0x40, "Brain_Think", True), ListedFunction(0x00100100, 16, "X_Y", False)]
    findings = orig_ghidra.check_listing(index.rows, listing)
    assert [(f.row.address, f.problem, f.start) for f in findings] == [(0x00100020, "not-a-start", 0x00100010)]


class FakeGhidra:
    """A ghidra-mcp that answers the reading endpoints for two functions, one of which calls an untagged one."""

    def __init__(self) -> None:
        self.paths: list[str] = []

    def __call__(self, path: str) -> Any:
        self.paths.append(path)
        if path.startswith("/get_current_program_info"):
            return {"name": "made-up"}
        if path.startswith("/get_function_by_address?address=0x00100010"):
            return {"name": "Brain_Think", "entry_point": "00100010"}
        if path.startswith("/get_function_by_address?address=0x00100020"):
            return {"name": "Gang_Free", "entry_point": "00100020"}
        if path.startswith("/get_function_callees?address=0x00100010"):
            return {
                "callees": [{"name": "Brain_Wait", "address": "00100020"}, {"name": "Helper", "address": "00100100"}]
            }
        if path.startswith("/get_function_callees"):
            return {"callees": [{"name": "Helper", "address": "00100100"}]}
        return {"error": "unknown"}


def test_the_live_check_reads_names_and_lists_untagged_callees(checkout: Path) -> None:
    fake = FakeGhidra()
    result = orig_cli.collect(checkout, "auto", "http://made-up", fake)
    assert result.ghidra_source == "live http://made-up"
    assert [(f.row.address, f.problem, f.ghidra_name) for f in result.findings] == [
        (0x00100020, "names-differ", "Gang_Free")
    ]
    assert [(c.address, c.callers) for c in result.callees] == [(0x00100100, (0x00100010, 0x00100020))]
    assert all(not p.startswith(("/rename", "/set_", "/create", "/delete")) for p in fake.paths)


def test_auto_falls_back_to_the_listing_when_no_server_answers(checkout: Path) -> None:
    def down(path: str) -> Any:
        raise ConfigError("no server")

    result = orig_cli.collect(checkout, "auto", "http://made-up", down)
    assert result.ghidra_source.startswith("listing")
    assert [(f.row.address, f.problem) for f in result.findings] == [(0x00100020, "renamed")]
    with pytest.raises(ConfigError, match="no ghidra-mcp"):
        orig_cli.collect(checkout, "live", "http://made-up", down)


ISSUES = [
    {
        "number": 7,
        "title": "Brains wait forever",
        "body": "Brain_Wait never returns.",
        "labels": [{"name": "bug"}, {"name": "P-high"}],
        "comments": [],
    },
    {
        "number": 8,
        "title": "Something in the AI",
        "body": "No names here.",
        "labels": [{"name": "bug"}, {"name": "A-ai"}],
        "comments": [{"body": "see brain.cpp"}],
    },
]


def test_the_queue_puts_what_an_issue_names_first(checkout: Path) -> None:
    result = orig_cli.collect(checkout, "off", "")
    inputs = orig_cli.queue_inputs(checkout, result, orig_queue.issues_from_json(ISSUES))
    entries = orig_queue.rank(inputs)
    assert entries[0].key == "0x00100020"
    assert entries[0].reasons[0] == "#7 names it"
    assert entries[0].score == 10 * 3 + 3  # named by a high-priority issue, its file named by #8 (#8 has no P- label)
    forget = next(e for e in entries if e.title.startswith("Brain::forget"))
    assert "#8 names its file" in forget.reasons
    summary = orig_queue.subsystems(entries)
    assert [(s.directory, s.issues, s.tagged, s.untagged) for s in summary] == [("ai", (7, 8), 2, 2)]


def test_the_commands_write_their_files(checkout: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(checkout)
    issues = checkout / "issues.json"
    issues.write_text(json.dumps(ISSUES), encoding="utf-8")
    out = checkout.parent / "out"
    assert main(["orig", "queue", "--out", str(out), "--ghidra", "listing", "--issues", str(issues)]) == 0
    assert sorted(p.name for p in out.iterdir()) == sorted([*orig_cli.INDEX_FILES, "queue.md", "queue.tsv"])
    assert "0x00100010\tBrain_Think\tBrain.cpp\tBrain::think" in (out / "index.tsv").read_text(encoding="utf-8")
    assert "docs/research/made-up.md#thinking" in (out / "research-gaps.md").read_text(encoding="utf-8")
    assert "| renamed | 0x00100020 |" in (out / "report.md").read_text(encoding="utf-8")

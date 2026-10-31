# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the leads index and search (task_leads_index, task_leads): synthetic pages and entries, one real read."""

import json
from datetime import UTC, datetime, timedelta
from pathlib import Path

import pytest
from audit_support import CONVENTIONS
from conventions_support import REPO

from coney_tools import task_leads as leads
from coney_tools import task_leads_index as index
from coney_tools.task_leads_index import Entry, MergedPr

RULES = CONVENTIONS.tasks.leads
NOW = datetime(2026, 10, 22, 12, 0, tzinfo=UTC)


def entry(source: str, title: str, where: str, *exact: str, extra: str = "") -> Entry:
    """An entry whose parts come from its title (and `extra` words)."""
    return Entry(source, title, where, index.split_name(f"{title} {extra}"), tuple(exact))


# --- names --------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("name", "parts"),
    [
        ("Goal_ThrowObject", ("goal", "throw", "object")),
        ("GetEnemyWeaponsHeld", ("get", "enemy", "weapon", "held")),
        ("Foo::Bar2", ("foo", "bar", "2")),
        ("HTTPServer", ("http", "server")),
        ("fight goal", ("fight", "goal")),
        ("class", ("class",)),
        ("", ()),
    ],
)
def test_names_are_split_into_lower_case_parts(name: str, parts: tuple[str, ...]) -> None:
    assert index.split_name(name) == parts


def test_the_identifiers_an_issue_writes_are_found() -> None:
    text = (
        "The `ThrowSubTactic_Init` at 0x00322B78 in src/ai/sub_tactics.cpp and src/combat/ (see "
        "docs/research/ai-goals.md#goal-object-pile), after #317; but `plain` and `x` and `a_b` are words."
    )
    addresses, names, paths = leads.identifiers_in(text)
    assert addresses == ["0x00322b78"]
    assert names == ["ThrowSubTactic_Init", "a_b", "#317"]
    assert paths == ["src/ai/sub_tactics.cpp", "src/combat/", "docs/research/ai-goals.md#goal-object-pile"]


# --- ranking ------------------------------------------------------------------------------------------------------


def corpus() -> tuple[Entry, ...]:
    """A made-up index: many entries about `object`, few about `retarget`, one named function."""
    common = [entry("ghidra", f"Object_{n}", f"0x0010{n:04x}", f"0x0010{n:04x}") for n in range(30)]
    rare = [
        entry(
            "research",
            "Fight goal retarget",
            "docs/research/ai-goals.md#retarget",
            "docs/research/ai-goals.md#retarget",
        ),
        entry(
            "orig", "FightGoal_Retarget", "src/ai/fight.cpp:40", "0x00322b78", "FightGoal_Retarget", "src/ai/fight.cpp"
        ),
        entry("pr", "ai: Fix the fight goal's retarget", "PR #301", "#301", "src/ai/fight.cpp"),
    ]
    other = [entry("functions", "ThrowObject", "0x00111111", "0x00111111", "ThrowObject")]
    return (*common, *rare, *other)


def searcher(max_per_source: int = 8) -> leads.Searcher:
    rules = type(RULES)(
        rare_max_entries=3, common_min_entries=20, max_per_source=max_per_source, index_max_age_hours=24, merged_prs=100
    )
    return leads.Searcher(corpus(), rules)


def test_a_rare_part_ranks_first_and_a_common_one_alone_is_no_match() -> None:
    result = searcher().search(["retarget fight", "object"])
    titles = [match.entry.title for match in result.ranked]
    assert titles[:3] == ["FightGoal_Retarget", "Fight goal retarget", "ai: Fix the fight goal's retarget"] or set(
        titles[:3]
    ) == {"FightGoal_Retarget", "Fight goal retarget", "ai: Fix the fight goal's retarget"}
    assert all(match.tier == "rare" for match in result.ranked[:3])
    # "object" is in 31 entries (>= 20): common, so the bare term still matches (it is the only part) but ranks after.
    assert result.unmatched == []


def test_a_common_part_of_a_multi_part_term_is_dropped() -> None:
    result = searcher().search(["object retarget"])
    assert {match.entry.where for match in result.ranked} == {
        "docs/research/ai-goals.md#retarget",
        "src/ai/fight.cpp:40",
        "PR #301",
    }


def test_every_match_carries_its_reason() -> None:
    result = searcher().search(["retarget"])
    for match in result.ranked:
        assert 'term "retarget" matches the name part "retarget"' in match.reason and "(rare)" in match.reason


def test_an_exact_name_ranks_above_a_part_match_and_says_so() -> None:
    result = searcher().search(["FightGoal_Retarget"])
    assert result.ranked[0].entry.title == "FightGoal_Retarget" and "is written exactly" in result.ranked[0].reason


def test_a_term_that_matches_nothing_is_reported() -> None:
    result = searcher().search(["zebra", "retarget"])
    assert result.unmatched == ["zebra"]


def test_the_limit_applies_per_source() -> None:
    result = searcher(max_per_source=5).search(["object"])
    assert len([m for m in result.ranked if m.entry.source == "ghidra"]) == 5


def test_identifiers_in_the_issue_are_always_included_ahead_of_the_ranked_ones() -> None:
    text = "It lives at 0x00322b78, `ThrowObject`, in src/ai/fight.cpp, see #301 and src/ai/."
    result = searcher(max_per_source=1).search(["object"], text)
    named = {match.entry.where: match.reason for match in result.named}
    assert named["src/ai/fight.cpp:40"].startswith("named in the issue: ")
    assert named["PR #301"] == "named in the issue: name #301"
    assert named["0x00111111"] == "named in the issue: name ThrowObject"
    assert all(match.tier == "named" for match in result.named)
    assert not {(m.entry.source, m.entry.where) for m in result.ranked} & {
        (m.entry.source, m.entry.where) for m in result.named
    }
    assert result.matches()[: len(result.named)] == result.named


def test_a_folder_in_the_issue_lists_its_first_entries_only() -> None:
    many = tuple(entry("orig", f"F{n}", f"src/ai/f{n}.cpp:1", f"src/ai/f{n}.cpp") for n in range(40))
    rules = type(RULES)(
        rare_max_entries=3, common_min_entries=20, max_per_source=2, index_max_age_hours=24, merged_prs=1
    )
    result = leads.Searcher(many, rules).search([], "Everything under src/ai/ is affected.")
    assert len(result.named) == 4 and "folder src/ai/" in result.named[0].reason


def test_an_empty_search_finds_nothing() -> None:
    result = searcher().search([], "")
    assert result.matches() == [] and result.unmatched == []


# --- building the index -------------------------------------------------------------------------------------------


def make_repo(root: Path) -> None:
    """A made-up checkout: one research page, the function lists, a glossary."""
    (root / "docs/research").mkdir(parents=True)
    (root / "docs/progress").mkdir(parents=True)
    (root / "research").mkdir()
    (root / "src/ai").mkdir(parents=True)
    (root / "docs/research/ai-goals.md").write_text(
        "# AI goals\n\n## Throw order {#throw}\n\n| Address | Name |\n| --- | --- |\n"
        "| `0x00322b78` | `ThrowSubTactic_Init` |\n\n## Other\n\nNothing.\n",
        encoding="utf-8",
    )
    (root / "research/functions").mkdir()
    (root / "research/functions/ai.toml").write_text(
        '[[function]]\naddress = 0x00322b78\nname = "ThrowSubTactic_Init"\npage = "ai-goals.md#throw"\n'
        'level = "confirmed-code"\nconey = "src/ai/throw.cpp:ThrowOrder"\n\n'
        '[[function]]\naddress = 0x00322b80\nname = "Researched_Only"\npage = "ai-goals.md#throw"\n'
        'level = "inferred"\n',
        encoding="utf-8",
    )
    (root / "docs/progress/ghidra-functions.tsv").write_text(
        "# header\n0x00100008\t448\tentry\t0\n0x001001c8\t8\t\t0\n0x00100300\t8\tFUN_00100300\t0\n", encoding="utf-8"
    )
    (root / "research/glossary.yaml").write_text(
        'molotov: "Thrown by `ThrowSubTactic_Init` at 0x00322b78"\n', encoding="utf-8"
    )
    (root / "src/ai/throw.cpp").write_text(
        "// @orig 0x00322b78 ThrowSubTactic_Init (Throw.cpp)\nvoid ThrowOrder() {}\n", encoding="utf-8"
    )


def test_each_source_is_read(tmp_path: Path) -> None:
    make_repo(tmp_path)
    prs = [MergedPr(301, "ai: Add the throw order", ("src/ai/throw.cpp",))]
    entries = index.build_entries(tmp_path, CONVENTIONS, prs)
    by_source = {source: [e for e in entries if e.source == source] for source in index.SOURCES}
    assert [e.title for e in by_source["research"]] == ["AI goals", "Throw order", "Other"]
    assert by_source["research"][1].where == "docs/research/ai-goals.md#throw"
    assert {"0x00322b78", "ThrowSubTactic_Init"} <= set(by_source["research"][1].exact)
    assert [e.where for e in by_source["functions"]] == ["0x00322b78"]  # only the ported function, not the researched
    assert [e.title for e in by_source["ghidra"]] == ["entry"]  # unnamed and FUN_ entries are not names
    assert by_source["glossary"][0].title == "molotov" and "0x00322b78" in by_source["glossary"][0].exact
    assert by_source["pr"][0].exact == ("#301", "src/ai/throw.cpp")
    assert by_source["orig"], "the @orig tag is found"
    assert by_source["orig"][0].where.startswith("src/ai/throw.cpp:")


def test_the_issue_finds_the_function_by_its_address_in_every_source(tmp_path: Path) -> None:
    make_repo(tmp_path)
    entries = tuple(index.build_entries(tmp_path, CONVENTIONS, []))
    result = leads.Searcher(entries, RULES).search(["throw order"], "Port `ThrowSubTactic_Init` (0x00322b78).")
    assert {m.entry.source for m in result.named} >= {"research", "functions", "orig", "glossary"}
    assert result.unmatched == []


def test_a_missing_optional_file_is_no_entries(tmp_path: Path) -> None:
    (tmp_path / "docs/research").mkdir(parents=True)
    assert index.function_entries(tmp_path) == [] and index.ghidra_entries(tmp_path) == []
    assert index.glossary_entries(tmp_path) == [] and index.research_entries(tmp_path) == []


def test_a_glossary_list_form_is_read(tmp_path: Path) -> None:
    (tmp_path / "research").mkdir()
    (tmp_path / "research/glossary.yaml").write_text(
        "- term: grab\n  code: GrabState\n- notaterm: 1\n", encoding="utf-8"
    )
    assert [e.title for e in index.glossary_entries(tmp_path)] == ["grab"]


# --- the cache ----------------------------------------------------------------------------------------------------


def cached(head: str = "abc", built: datetime = NOW) -> index.CachedIndex:
    return index.CachedIndex((entry("pr", "t", "PR #1", "#1"),), built, head, ("a notice",))


def test_the_cache_round_trips_and_a_damaged_one_is_nothing(tmp_path: Path) -> None:
    path = tmp_path / "leads" / "index.json"
    index.save_index(path, cached())
    assert index.load_index(path) == cached()
    path.write_text("{", encoding="utf-8")
    assert index.load_index(path) is None
    path.write_text(json.dumps({"version": 0}), encoding="utf-8")
    assert index.load_index(path) is None
    assert index.load_index(tmp_path / "none.json") is None


def test_the_cache_is_fresh_for_the_same_commit_and_a_day() -> None:
    assert index.is_fresh(cached(), "abc", NOW + timedelta(hours=23), 24)
    assert not index.is_fresh(cached(), "abc", NOW + timedelta(hours=25), 24)
    assert not index.is_fresh(cached(), "def", NOW, 24)


def test_prepare_uses_a_fresh_cache_and_rebuilds_a_stale_one(tmp_path: Path) -> None:
    make_repo(tmp_path)
    cache = tmp_path / "scratch" / "index.json"
    calls: list[list[str]] = []

    def gh(arguments: list[str]) -> str:
        calls.append(arguments)
        return json.dumps([{"number": 5, "title": "ai: Add x", "files": [{"path": "src/ai/a.cpp"}]}])

    first = index.prepare_index(tmp_path, CONVENTIONS, cache, "abc", NOW, gh)
    assert any(e.source == "pr" for e in first.entries) and len(calls) == 1
    again = index.prepare_index(tmp_path, CONVENTIONS, cache, "abc", NOW + timedelta(hours=1), gh)
    assert again == first and len(calls) == 1
    moved = index.prepare_index(tmp_path, CONVENTIONS, cache, "def", NOW + timedelta(hours=1), gh)
    assert moved.head == "def" and len(calls) == 2
    forced = index.prepare_index(tmp_path, CONVENTIONS, cache, "def", NOW + timedelta(hours=1), gh, refresh=True)
    assert len(calls) == 3 and forced.head == "def"


def test_without_gh_or_when_gh_fails_the_rest_of_the_index_still_works(tmp_path: Path) -> None:
    make_repo(tmp_path)

    def broken(arguments: list[str]) -> str:
        raise OSError("gh could not run")

    no_gh = index.prepare_index(tmp_path, CONVENTIONS, tmp_path / "a.json", "abc", NOW, None)
    failed = index.prepare_index(tmp_path, CONVENTIONS, tmp_path / "b.json", "abc", NOW, broken)
    for built in (no_gh, failed):
        assert built.notices and "merged pull requests were not read" in built.notices[0]
        assert any(e.source == "functions" for e in built.entries) and not any(e.source == "pr" for e in built.entries)


@pytest.mark.reads("docs_only", "engine")
def test_the_real_repository_indexes_and_a_known_name_is_found() -> None:
    entries = tuple(index.build_entries(REPO, CONVENTIONS, None))
    sources = {e.source for e in entries}
    assert {"research", "functions", "ghidra", "orig"} <= sources
    first = next(e for e in entries if e.source == "functions")
    result = leads.Searcher(entries, RULES).search([], f"It is {first.where}.")
    assert any(m.entry.source == "functions" and m.entry.where == first.where for m in result.named)

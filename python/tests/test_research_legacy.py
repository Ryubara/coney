# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the legacy file of the research pages: reading and writing it, what it excuses, and the commands that make
and shrink it (`research_legacy`, `docs_legacy`, `docs check` on a whole change)."""

from __future__ import annotations

from pathlib import Path

import pytest
from conventions_support import REPO
from git_support import git
from research_support import (
    IN_FUNCTION,
    RESEARCH_PATH,
    env,
    errors,
    findings,
    page,
    research,
    rules_of,
)

from coney_tools import docs_check, docs_cli, docs_legacy
from coney_tools.config import ConfigError
from coney_tools.docs_check import Context
from coney_tools.docs_page import Finding
from coney_tools.research_check import RULES
from coney_tools.research_legacy import Legacy, Touch, apply_legacy, format_legacy, parse_legacy

LEGACY_FILE = "research/legacy.toml"

# --- the file ---------------------------------------------------------------------------------------------------------


def test_the_file_is_read_by_page_and_rule() -> None:
    text = '# note\n["docs/research/a.md"]\nevidence = 3\nskeleton = 1\n\n["docs/research/b/c.md"]\nlength = 90\n'
    legacy = parse_legacy(text, RULES)
    assert legacy.pages == {
        "docs/research/a.md": {"skeleton": 1, "evidence": 3},
        "docs/research/b/c.md": {"length": 90},
    }
    assert "docs/research/a.md" in legacy and "docs/research/z.md" not in legacy
    assert legacy.rules("docs/research/z.md") == {}


def test_writing_then_reading_gives_the_same_file() -> None:
    legacy = Legacy({"docs/research/b.md": {"evidence": 4, "length": 90}, "docs/research/a.md": {"front-matter": 1}})
    text = format_legacy(legacy)
    assert text.index("a.md") < text.index("b.md")
    assert "evidence = 4\nlength = 90\n" in text
    assert parse_legacy(text, RULES) == legacy
    assert text.splitlines()[0].startswith("# Research pages that do not meet the standard yet")


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ('["docs/guides/a.md"]\nevidence = 1\n', "not a page under docs/research/"),
        ('["docs/research/a.txt"]\nevidence = 1\n', "not a page under docs/research/"),
        ('["docs/research/a.md"]\nlegacy = 1\n', "not a rule that can be waived"),
        ('["docs/research/a.md"]\nevidence = 0\n', "a whole number of 1 or more"),
        ('["docs/research/a.md"]\nevidence = "many"\n', "a whole number of 1 or more"),
        ('["docs/research/a.md"]\n', "lists no rule"),
        ("this is not toml", "not valid TOML"),
    ],
)
def test_a_file_that_is_not_allowed_is_refused(text: str, message: str) -> None:
    with pytest.raises(ConfigError, match=message):
        parse_legacy(text, RULES)


# --- what a listed page is excused from -------------------------------------------------------------------------------


def finding(rule: str, line: int, severity: str = "error") -> Finding:
    """A finding of `rule` on `line`."""
    return Finding(rule, line, f"{rule} at {line}", severity)


def test_the_findings_of_an_excused_rule_become_one_warning() -> None:
    found = [finding("evidence", 5), finding("evidence", 9), finding("verified", 1)]
    kept = apply_legacy(found, {"evidence": 2}, LEGACY_FILE, lambda _: False)
    assert [(item.rule, item.severity) for item in kept] == [("verified", "error"), ("evidence", "warning")]
    assert "2 findings excused" in kept[1].message and "evidence at 5" in kept[1].message and kept[1].line == 5


def test_a_rule_the_page_is_not_excused_from_stays_an_error() -> None:
    kept = apply_legacy([finding("history", 3)], {"evidence": 2, "length": 10}, LEGACY_FILE, lambda _: False)
    assert [item.rule for item in kept if item.severity == "error"] == ["legacy", "legacy", "history"]


def test_an_excused_rule_the_page_now_passes_is_an_error() -> None:
    kept = apply_legacy([], {"evidence": 2}, LEGACY_FILE, lambda _: False)
    assert [(item.rule, item.severity) for item in kept] == [("legacy", "error")]
    assert "'evidence'" in kept[0].message and LEGACY_FILE in kept[0].message


def test_a_warning_of_an_excused_rule_is_dropped_and_does_not_keep_the_rule_alive() -> None:
    kept = apply_legacy([finding("evidence", 4, "warning")], {"evidence": 2}, LEGACY_FILE, lambda _: False)
    assert [item.rule for item in kept] == ["legacy"]


def test_evidence_findings_in_a_touched_section_stay_errors() -> None:
    found = [finding("evidence", 5), finding("evidence", 40), finding("skeleton", 8)]
    kept = apply_legacy(found, {"evidence": 2, "skeleton": 1}, LEGACY_FILE, lambda item: item.line < 10)
    assert [(item.rule, item.line, item.severity) for item in kept] == [
        ("evidence", 5, "error"),
        ("skeleton", 8, "warning"),
        ("evidence", 40, "warning"),
    ]
    assert "1 finding excused" in kept[2].message


def test_only_evidence_is_read_by_section() -> None:
    kept = apply_legacy([finding("history", 5)], {"history": 1}, LEGACY_FILE, lambda _: True)
    assert [(item.rule, item.severity) for item in kept] == [("history", "warning")]


# --- a listed page, checked ------------------------------------------------------------------------------------

OLD = page(f"The widget spins at {IN_FUNCTION}.\n", meta="")


def test_a_listed_page_passes_for_the_rules_it_is_excused_from() -> None:
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "evidence": 1}})
    found = findings(RESEARCH_PATH, OLD, listed)
    assert rules_of(found) == []
    assert rules_of(found, "warning") == ["front-matter", "evidence"]
    assert errors(OLD) == ["front-matter", "evidence"]


def test_a_listed_page_is_held_to_the_rules_it_is_not_excused_from() -> None:
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1}})
    assert errors(OLD, listed) == ["evidence"]


def test_a_listed_page_whose_excuse_has_run_out_is_told_to_leave_the_file() -> None:
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "history": 2}})
    found = findings(RESEARCH_PATH, page(f"The widget spins at {IN_FUNCTION} (confirmed (code)).\n"), listed)
    assert rules_of(found) == ["legacy", "legacy"]


def test_a_new_page_cannot_be_legacy() -> None:
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1}})
    found = findings(RESEARCH_PATH, OLD, listed, Touch(None, True))
    assert rules_of(found).count("legacy") == 1 and "front-matter" in rules_of(found)
    assert "new page" in next(item.message for item in found if item.rule == "legacy")


def three_sections() -> str:
    """A legacy-style page with one ungraded address in each of Behaviour and Data."""
    return page(
        f"The widget spins at {IN_FUNCTION}.\n",
        sections={"Data": f"The field is set at {IN_FUNCTION}.\n"},
        meta="",
    )


def test_a_section_the_change_touches_is_graded_on_a_listed_page() -> None:
    text = three_sections()
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "evidence": 2}})
    data_line = text.splitlines().index(f"The field is set at {IN_FUNCTION}.") + 1
    untouched = findings(RESEARCH_PATH, text, listed, Touch(frozenset({1})))
    assert rules_of(untouched) == []
    touched = findings(RESEARCH_PATH, text, listed, Touch(frozenset({data_line})))
    errored = [item for item in touched if item.severity == "error"]
    assert [(item.rule, item.line) for item in errored] == [("evidence", data_line)]
    # Without knowing the changed lines (a named page), nothing is touched.
    assert rules_of(findings(RESEARCH_PATH, text, listed)) == []


# --- making, shrinking and checking the file --------------------------------------------------------------------


def write(root: Path, name: str, text: str) -> None:
    """Write a file of the made-up checkout."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def checkout(tmp_path: Path, legacy: str | None = None) -> Path:
    """A repository with this repository's conventions, a good survey page, a bad page and, perhaps, a legacy file; all
    committed on `main`, with the branch `work` checked out."""
    git(tmp_path, "init", "-q", "-b", "main")
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / ".github").mkdir()
    (tmp_path / ".github/conventions.yml").write_bytes((REPO / ".github/conventions.yml").read_bytes())
    write(tmp_path, "docs/research/good.md", page(kind="survey", meta="---\nkind: survey\n---\n"))
    write(tmp_path, "docs/research/old.md", "# Old\n\nNo sections.\n\n## Data\n\nSomething at 0x00123456.\n")
    if legacy is not None:
        write(tmp_path, LEGACY_FILE, legacy)
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    git(tmp_path, "checkout", "-q", "-b", "work")
    return tmp_path


def context(root: Path) -> Context:
    """The context of the made-up checkout."""
    return docs_check.context_for(root)


def test_a_scan_counts_what_each_page_fails(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    scanned = docs_legacy.scan(root, context(root))
    assert list(scanned) == ["docs/research/old.md"]
    counts = scanned["docs/research/old.md"]
    assert counts["front-matter"] == 1 and counts["verified"] == 1 and counts["skeleton"] >= 1
    assert "evidence" in counts
    assert docs_legacy.summary_lines(scanned)[0] == "1 research pages do not meet the standard"


def test_a_scan_counts_a_long_pages_lines_over_the_limit(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    limit = research().kinds["system"].max_lines
    write(root, "docs/research/long.md", "# Long\n\n" + "x\n" * (limit + 50))
    counts = docs_legacy.scan(root, context(root))["docs/research/long.md"]
    assert counts["length"] == 52  # the title, a blank line and 1,550 more: 52 over the limit


def test_the_first_scan_writes_the_file_and_a_second_is_refused(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    text = docs_legacy.initial_text(root, context(root))
    assert text.startswith("# Research pages") and '["docs/research/old.md"]' in text and "good.md" not in text
    write(root, LEGACY_FILE, text)
    with pytest.raises(ConfigError, match="the file only shrinks"):
        docs_legacy.initial_text(root, context(root))


def test_pruning_removes_what_is_fixed_and_adds_nothing(tmp_path: Path) -> None:
    root = checkout(
        tmp_path, '["docs/research/old.md"]\nfront-matter = 1\nlength = 5\n["docs/research/good.md"]\nhistory = 2\n'
    )
    write(root, "docs/research/gone-soon.md", "# Gone\n")
    left = docs_legacy.pruned(root, context(root))
    # old.md still fails front-matter and no longer fails length; good.md passes; gone-soon.md is not added.
    assert left.pages == {"docs/research/old.md": {"front-matter": 1}}


def test_pruning_lowers_a_count_and_never_raises_it(tmp_path: Path) -> None:
    root = checkout(tmp_path, '["docs/research/old.md"]\nevidence = 50\nskeleton = 1\n')
    left = docs_legacy.pruned(root, context(root))
    assert left.pages["docs/research/old.md"]["evidence"] == 1
    assert left.pages["docs/research/old.md"]["skeleton"] == 1


def test_the_check_finds_a_gone_page_and_a_rule_that_passes(tmp_path: Path) -> None:
    root = checkout(
        tmp_path,
        '["docs/research/old.md"]\nfront-matter = 1\nhistory = 1\n["docs/research/missing.md"]\nevidence = 1\n',
    )
    found = docs_legacy.problems(root, context(root))
    assert found == [
        "research/legacy.toml: docs/research/old.md now passes 'history': remove it",
        "research/legacy.toml: docs/research/missing.md is gone: remove it",
    ]


def test_the_check_finds_a_page_or_a_rule_added_to_a_file_that_existed(tmp_path: Path) -> None:
    root = checkout(tmp_path, '["docs/research/old.md"]\nfront-matter = 1\n')
    write(
        root,
        LEGACY_FILE,
        '["docs/research/old.md"]\nfront-matter = 1\nverified = 1\n["docs/research/good.md"]\nlength = 1\n',
    )
    found = docs_legacy.problems(root, context(root), "main")
    assert any("old.md is excused from 'verified' that it was not before" in line for line in found)
    assert any("good.md is added" in line for line in found)


def test_the_first_legacy_file_may_list_any_page(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    assert docs_legacy.problems(root, context(root), "main") == []


# --- a whole change ---------------------------------------------------------------------------------------------------


def test_a_change_to_a_listed_page_passes_where_it_is_excused_and_fails_where_it_touches(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "legacy")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "work2")
    write(root, "docs/research/good.md", page(kind="survey", meta="---\nkind: survey\n---\n", extra="Added.\n\n"))
    assert docs_check.check_changed(root, "main", context(root)).failed == {}
    # A line added to the Data section of the legacy page makes that section's ungraded address an error.
    write(root, "docs/research/old.md", "# Old\n\nNo sections.\n\n## Data\n\nSomething at 0x00123456.\n\nMore.\n")
    result = docs_check.check_changed(root, "main", context(root))
    errored = [(item.rule, item.line) for item in result.failed["docs/research/old.md"] if item.severity == "error"]
    assert errored == [("evidence", 7)]


def test_a_new_page_in_the_legacy_file_is_an_error_even_when_it_would_be_excused(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, "docs/research/fresh.md", "# Fresh\n")
    write(root, LEGACY_FILE, '["docs/research/fresh.md"]\nfront-matter = 1\nskeleton = 1\nverified = 1\n')
    result = docs_check.check_changed(root, "main", context(root))
    messages = [item.message for item in result.failed["docs/research/fresh.md"] if item.rule == "legacy"]
    assert any("new page cannot be legacy" in message for message in messages)


def test_a_changed_legacy_file_is_checked_too(tmp_path: Path) -> None:
    root = checkout(tmp_path, '["docs/research/old.md"]\nfront-matter = 1\n')
    write(root, LEGACY_FILE, '["docs/research/old.md"]\nfront-matter = 1\nhistory = 1\n')
    result = docs_check.check_changed(root, "main", context(root))
    assert LEGACY_FILE in result.failed
    assert any("is excused from 'history'" in item.message for item in result.failed[LEGACY_FILE])


def test_the_summary_counts_the_legacy_pages(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    result = docs_check.check_paths(root, ["docs/research"], context(root))
    assert result.legacy == 1
    assert "1 research pages are legacy: excused from the rules their entry lists" in docs_cli.summary_lines(result)


# --- the command ------------------------------------------------------------------------------------------------------


def test_the_commands_scan_write_shrink_and_check(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = checkout(tmp_path)
    monkeypatch.chdir(root)
    assert docs_cli.run_legacy("scan", "main") == 0
    assert "1 research pages do not meet the standard" in capsys.readouterr().out
    assert not (root / LEGACY_FILE).exists()
    assert docs_cli.run_legacy("init", "main") == 0
    assert (root / LEGACY_FILE).read_text(encoding="utf-8").startswith("# Research pages")
    assert docs_cli.run_legacy("init", "main") == 2
    assert "the file only shrinks" in capsys.readouterr().out
    assert docs_cli.run_legacy("check", "main") == 0
    write(root, "docs/research/old.md", page(kind="survey", meta="---\nkind: survey\n---\n"))
    assert docs_cli.run_legacy("check", "main") == 1
    assert "now passes" in capsys.readouterr().out
    assert docs_cli.run_legacy("prune", "main") == 0
    assert (root / LEGACY_FILE).read_text(encoding="utf-8").count("[") == 0
    assert docs_cli.run_legacy("check", "main") == 0


def test_the_command_reports_an_unusable_checkout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(tmp_path)
    assert docs_cli.run_legacy("scan", "main") == 2
    assert "not inside a Coney checkout" in capsys.readouterr().out


# --- this repository --------------------------------------------------------------------------------------------------


def test_every_research_page_of_this_repository_meets_the_standard_or_is_excused() -> None:
    context_ = docs_check.context_for(REPO)
    result = docs_check.check_paths(REPO, ["docs/research"], context_)
    standard = {*RULES, "legacy"}
    failing = {
        page_: sorted({item.rule for item in found if item.severity == "error" and item.rule in standard})
        for page_, found in result.findings.items()
    }
    assert {page_: rules_ for page_, rules_ in failing.items() if rules_} == {}


def test_the_legacy_file_of_this_repository_lists_nothing_that_passes() -> None:
    assert docs_legacy.problems(REPO, docs_check.context_for(REPO)) == []

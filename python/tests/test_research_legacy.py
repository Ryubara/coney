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
    LEGACY_FILE,
    RESEARCH_PATH,
    checkout,
    context,
    env,
    errors,
    findings,
    page,
    research,
    rules_of,
    write,
)

from coney_tools import docs_check, docs_cli, docs_legacy
from coney_tools.config import ConfigError
from coney_tools.docs_legacy import LEGACY_RULES
from coney_tools.docs_page import Finding
from coney_tools.research_legacy import Legacy, Touch, apply_legacy, format_legacy, parse_legacy

# --- the file ---------------------------------------------------------------------------------------------------------


def test_the_file_is_read_by_page_and_rule() -> None:
    text = '# note\n["docs/research/a.md"]\nevidence = 3\nskeleton = 1\n\n["docs/research/b/c.md"]\nlength = 90\n'
    legacy = parse_legacy(text, LEGACY_RULES)
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
    assert parse_legacy(text, LEGACY_RULES) == legacy
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
        parse_legacy(text, LEGACY_RULES)


# --- what a listed page is excused from -------------------------------------------------------------------------------


def finding(rule: str, line: int, severity: str = "error", by_section: bool = False) -> Finding:
    """A finding of `rule` on `line` (`by_section`: it grades a block, which only the touched sections owe)."""
    return Finding(rule, line, f"{rule} at {line}", severity, by_section)


def test_the_findings_of_an_excused_rule_become_one_warning() -> None:
    found = [finding("evidence", 5), finding("evidence", 9), finding("verified", 1)]
    kept = apply_legacy(found, {"evidence": 2}, LEGACY_FILE, None)
    assert [(item.rule, item.severity) for item in kept] == [("verified", "error"), ("evidence", "warning")]
    assert "2 findings excused" in kept[1].message and "evidence at 5" in kept[1].message and kept[1].line == 5


def test_a_rule_the_page_is_not_excused_from_stays_an_error() -> None:
    kept = apply_legacy([finding("history", 3)], {"evidence": 2, "length": 10}, LEGACY_FILE, None)
    assert [item.rule for item in kept if item.severity == "error"] == ["legacy", "legacy", "history"]


def test_an_excused_rule_the_page_now_passes_is_an_error() -> None:
    kept = apply_legacy([], {"evidence": 2}, LEGACY_FILE, None)
    assert [(item.rule, item.severity) for item in kept] == [("legacy", "error")]
    assert "'evidence'" in kept[0].message and LEGACY_FILE in kept[0].message


def test_a_warning_of_an_excused_rule_is_dropped_and_does_not_keep_the_rule_alive() -> None:
    kept = apply_legacy([finding("evidence", 4, "warning")], {"evidence": 2}, LEGACY_FILE, None)
    assert [item.rule for item in kept] == ["legacy"]


def test_grading_in_a_touched_section_stays_an_error() -> None:
    found = [finding("evidence", 5, by_section=True), finding("evidence", 40, by_section=True)]
    kept = apply_legacy(found, {"evidence": 2}, LEGACY_FILE, lambda item: item.line < 10)
    assert [(item.rule, item.line, item.severity) for item in kept] == [
        ("evidence", 5, "error"),
        ("evidence", 40, "warning"),
    ]
    assert "1 finding excused" in kept[1].message


def test_a_touched_page_is_held_to_every_mechanical_rule_page_wide() -> None:
    # Maintainer decision Q5: "every touch fixes the mechanical rules page-wide and grades the touched section".
    mechanical = ["front-matter", "owns", "skeleton", "verified", "address-form", "address-resolve"]
    mechanical += ["decompile", "copied", "history", "stale-wording"]
    found = [finding(rule, 40) for rule in mechanical]
    found += [finding("evidence", 40), finding("evidence", 41, by_section=True), finding("length", 1)]
    excused = dict.fromkeys([*mechanical, "evidence", "length"], 1)
    kept = apply_legacy(found, excused, LEGACY_FILE, lambda _: False)
    # A refused spelling is page-wide; an ungraded block outside the touched sections and the length are excused.
    assert [item.rule for item in kept if item.severity == "error"] == [*mechanical, "evidence"]
    assert sorted(item.rule for item in kept if item.severity == "warning") == ["evidence", "length"]


def test_an_untouched_page_is_excused_everything_it_lists() -> None:
    found = [finding("history", 5), finding("evidence", 6)]
    kept = apply_legacy(found, {"history": 1, "evidence": 1}, LEGACY_FILE, None)
    assert [(item.rule, item.severity) for item in kept] == [("history", "warning"), ("evidence", "warning")]


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
    # Without knowing the changed lines (a named page), nothing is touched and everything listed is excused.
    assert rules_of(findings(RESEARCH_PATH, text, listed)) == []
    touched = findings(RESEARCH_PATH, text, listed, Touch(frozenset({data_line})))
    errored = [(item.rule, item.line) for item in touched if item.severity == "error"]
    # The touched section is graded, and the front matter, a mechanical rule, is fixed page-wide.
    assert errored == [("front-matter", 1), ("evidence", data_line)]


def test_a_touched_page_loses_its_excuse_for_the_mechanical_rules_and_the_refused_spellings() -> None:
    spelled = f"The field is confirmed (disc) at {IN_FUNCTION}.\n"
    old = page(
        f"The widget spins at {IN_FUNCTION}.\n",
        sections={"Data": spelled, "Summary": "Checked on 2026-10-12.\n"},
        meta="",
        head="",
    )
    lines = old.splitlines()
    summary_line = lines.index("Checked on 2026-10-12.") + 1
    behaviour_line = lines.index(f"The widget spins at {IN_FUNCTION}.") + 1
    spelling_line = lines.index(spelled.strip()) + 1
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "verified": 1, "history": 1, "evidence": 3}})
    touched = findings(RESEARCH_PATH, old, listed, Touch(frozenset({summary_line})))
    errored = sorted((item.rule, item.line) for item in touched if item.severity == "error")
    # Front matter, Verified line, the date and the refused spelling are errors although the change touched none of
    # their lines; the ungraded Behaviour claim is not, because its section is not the touched one.
    assert ("front-matter", 1) in errored and ("history", summary_line) in errored
    assert any(rule == "verified" for rule, _ in errored) and ("evidence", spelling_line) in errored
    assert ("evidence", behaviour_line) not in errored
    assert rules_of(findings(RESEARCH_PATH, old, listed)) == []


# --- making, shrinking and checking the file --------------------------------------------------------------------


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


def test_a_branch_behind_main_is_compared_with_the_file_where_it_left_main(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, "docs/research/old2.md", "# Old two\n\nNo sections.\n")
    both = '["docs/research/old.md"]\nfront-matter = 1\n\n["docs/research/old2.md"]\nfront-matter = 1\nskeleton = 1\n'
    write(root, LEGACY_FILE, both)
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "two legacy pages")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "behind")
    # The branch prunes a rule of old2.md; main, meanwhile, prunes old2.md out of the file altogether.
    write(root, LEGACY_FILE, '["docs/research/old.md"]\nfront-matter = 1\n\n["docs/research/old2.md"]\nskeleton = 1\n')
    git(root, "commit", "-q", "-am", "branch prunes a rule")
    git(root, "checkout", "-q", "main")
    write(root, LEGACY_FILE, '["docs/research/old.md"]\nfront-matter = 1\n')
    git(root, "commit", "-q", "-am", "main prunes old2")
    git(root, "checkout", "-q", "behind")
    # Nothing was added since the branch left main, whatever main has pruned since.
    assert docs_legacy.problems(root, context(root), "main") == []
    assert LEGACY_FILE not in docs_check.check_changed(root, "main", context(root)).failed


def test_the_first_legacy_file_may_list_any_page(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    assert docs_legacy.problems(root, context(root), "main") == []


STALE = "The mission list is yet to be written.\n"
SURVEY_META = "---\nkind: survey\n---\n"


def test_a_scan_counts_stale_wording_so_the_file_can_excuse_it(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    name = "docs/research/stale.md"
    write(root, name, page(kind="survey", meta=SURVEY_META, extra=STALE + "\n"))
    assert docs_legacy.scan(root, context(root))[name] == {"stale-wording": 1}
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    result = docs_check.check_paths(root, [name], context(root))
    assert result.failed == {} and result.findings[name][0].rule == "stale-wording"
    # Touched, the page is held to it like every mechanical rule.
    touched = docs_check.check_files(root, [name], context(root), {name: Touch(frozenset({1}))})
    assert [item.rule for item in touched.failed[name] if item.severity == "error"] == ["stale-wording"]


def test_an_unlisted_page_fails_stale_wording(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    name = "docs/research/stale.md"
    write(root, name, page(kind="survey", meta=SURVEY_META, extra=STALE + "\n"))
    failed = docs_check.check_paths(root, [name], context(root)).failed
    assert [item.rule for item in failed[name]] == ["stale-wording"]


def test_the_whole_legacy_file_is_checked_when_a_research_page_changes(tmp_path: Path) -> None:
    root = checkout(tmp_path, '["docs/research/old.md"]\nfront-matter = 1\nhistory = 1\n')
    write(root, "docs/research/good.md", page(kind="survey", meta=SURVEY_META, extra="Added.\n\n"))
    result = docs_check.check_changed(root, "main", context(root))
    assert any("old.md now passes 'history'" in item.message for item in result.failed[LEGACY_FILE])


def test_the_legacy_file_is_left_alone_when_the_change_has_nothing_to_do_with_research(tmp_path: Path) -> None:
    root = checkout(tmp_path, '["docs/research/old.md"]\nfront-matter = 1\nhistory = 1\n')
    write(root, "docs/guides/other.md", "# Other\n\nA line.\n")
    assert docs_check.check_changed(root, "main", context(root)).failed == {}


# --- a whole change ---------------------------------------------------------------------------------------------------


def test_a_change_to_a_listed_page_fails_the_mechanical_rules_page_wide_and_grades_the_section(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "legacy")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "work2")
    write(root, "docs/research/good.md", page(kind="survey", meta="---\nkind: survey\n---\n", extra="Added.\n\n"))
    assert docs_check.check_changed(root, "main", context(root)).failed == {}
    # A line added to the Data section of the legacy page: its front matter, sections and Verified line are errors
    # (page-wide), and so is the Data section's ungraded address.
    write(root, "docs/research/old.md", "# Old\n\nNo sections.\n\n## Data\n\nSomething at 0x00123456.\n\nMore.\n")
    result = docs_check.check_changed(root, "main", context(root))
    errored = sorted(
        (item.rule, item.line) for item in result.failed["docs/research/old.md"] if item.severity == "error"
    )
    assert ("front-matter", 1) in errored and ("evidence", 7) in errored
    assert any(rule == "verified" for rule, _ in errored) and any(rule == "skeleton" for rule, _ in errored)


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
    standard = {*LEGACY_RULES, "legacy"}
    failing = {
        page_: sorted({item.rule for item in found if item.severity == "error" and item.rule in standard})
        for page_, found in result.findings.items()
    }
    assert {page_: rules_ for page_, rules_ in failing.items() if rules_} == {}


def test_the_legacy_file_of_this_repository_lists_nothing_that_passes() -> None:
    assert docs_legacy.problems(REPO, docs_check.context_for(REPO)) == []


# --- an old-form runtime claim on a touched legacy page ---------------------------------------------------------------


def test_an_old_form_runtime_claim_is_graded_in_the_touched_section_only() -> None:
    # The Verified line does not name PCSX2, so neither claim is excused as an old-form one by itself.
    claim = f"It spins at {IN_FUNCTION} (confirmed (runtime)).\n"
    old = page(claim, sections={"Data": claim}, meta="")
    lines = old.splitlines()
    behaviour_line, data_line = (i + 1 for i, line in enumerate(lines) if line == claim.strip())
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "evidence": 2}})
    assert rules_of(findings(RESEARCH_PATH, old, listed)) == []
    touched = findings(RESEARCH_PATH, old, listed, Touch(frozenset({data_line})))
    errored = [(item.rule, item.line) for item in touched if item.severity == "error"]
    # The front matter is fixed page-wide; the claim in the section the change touches is graded; the other waits.
    assert errored == [("front-matter", 1), ("evidence", data_line)]
    assert [item.line for item in touched if item.rule == "evidence" and item.severity == "warning"] == [behaviour_line]


def test_a_refused_run_id_on_a_touched_legacy_page_stays_page_wide() -> None:
    claim = f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n"
    old = page(claim, sections={"Data": "Nothing more to say.\n"}, meta="")
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "evidence": 1}}, runs=frozenset({"r-2"}))
    data_line = old.splitlines().index("Nothing more to say.") + 1
    touched = findings(RESEARCH_PATH, old, listed, Touch(frozenset({data_line})))
    assert [item.rule for item in touched if item.severity == "error"] == ["front-matter", "evidence"]


# --- an old-form runtime claim keeps the entry that excuses it --------------------------------------------------------

OLD_FORM = f"It spins at {IN_FUNCTION} (confirmed (runtime)).\n"
PCSX2_SEEN = "The claims were seen in PCSX2 before run records existed.\n"


def old_form_page(claim: str = OLD_FORM) -> str:
    """A legacy page whose old-form runtime claim is explained in How this was checked (so it is no error)."""
    return page(claim, sections={"How this was checked": PCSX2_SEEN}, meta="")


def test_an_old_form_runtime_claim_keeps_the_evidence_entry_that_excuses_it() -> None:
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "evidence": 1}})
    found = findings(RESEARCH_PATH, old_form_page(), listed)
    # The claim is no error, and it is not "passing" either: it still owes a run record.
    assert rules_of(found) == []
    evidence = [item for item in found if item.rule == "evidence"]
    assert [item.severity for item in evidence] == ["warning"] and "1 finding excused" in evidence[0].message
    touched = findings(RESEARCH_PATH, old_form_page(), listed, Touch(frozenset({1})))
    assert rules_of(touched) == ["front-matter"] and [item.rule for item in touched if item.rule == "evidence"] == [
        "evidence"
    ]


def test_an_entry_without_evidence_does_not_make_the_old_form_claim_a_finding() -> None:
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1}})
    found = findings(RESEARCH_PATH, old_form_page(), listed)
    assert rules_of(found) == [] and "evidence" not in [item.rule for item in found]


def test_the_entry_goes_once_the_claim_cites_a_run_record() -> None:
    cited = f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n"
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "evidence": 1}}, runs=frozenset({"r-1"}))
    found = findings(RESEARCH_PATH, old_form_page(cited), listed)
    assert [(item.rule, item.severity) for item in found if item.rule in ("evidence", "legacy")] == [
        ("legacy", "error")
    ]
    assert any("now passes 'evidence'" in item.message for item in found)


def test_a_run_id_that_names_no_record_while_none_exist_is_held_too() -> None:
    claim = f"It spins at {IN_FUNCTION} (confirmed (runtime), run r-1).\n"
    listed = env(legacy={RESEARCH_PATH: {"front-matter": 1, "evidence": 1}})
    assert rules_of(findings(RESEARCH_PATH, old_form_page(claim), listed)) == []
    found = findings(RESEARCH_PATH, old_form_page(claim), listed)
    assert [item.rule for item in found if item.rule == "evidence"] == ["evidence"]


def test_a_scan_counts_the_old_form_claims_so_the_entry_is_kept_and_pruned_with_them(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    name = "docs/research/claims.md"
    write(root, name, old_form_page((OLD_FORM + "\n") * 3))
    assert docs_legacy.scan(root, context(root))[name]["evidence"] == 3
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    assert docs_legacy.problems(root, context(root)) == []
    assert docs_legacy.pruned(root, context(root)).pages[name]["evidence"] == 3
    # Once the claims are graded in the current form, the page passes and the rule goes.
    write(root, name, old_form_page(f"It spins at {IN_FUNCTION} (confirmed (code)).\n"))
    assert any(f"{name} now passes 'evidence'" in line for line in docs_legacy.problems(root, context(root)))
    assert "evidence" not in docs_legacy.pruned(root, context(root)).pages.get(name, {})


# --- the data the legacy file's truth depends on ----------------------------------------------------------------------

LISTING = "docs/progress/ghidra-functions.tsv"


def test_the_legacy_file_is_checked_when_the_function_listing_changes(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    # The listing holds one function, far from the old page's address, so that address resolves nowhere.
    write(root, LISTING, "0x00200000\t64\tOther_Function\t1\n")
    write(root, LEGACY_FILE, docs_legacy.initial_text(root, context(root)))
    assert "address-resolve" in (root / LEGACY_FILE).read_text(encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "listing")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "work2")
    assert docs_check.check_changed(root, "main", context(root)).failed == {}
    # Only the listing changes: it now holds the function that contains the old page's address, so the page passes
    # `address-resolve` and its entry is stale. The change touches no research page, yet the file must be checked.
    write(root, LISTING, "0x00123400\t256\tWidget_Spin\t1\n0x00200000\t64\tOther_Function\t1\n")
    result = docs_check.check_changed(root, "main", context(root))
    assert any("now passes 'address-resolve'" in item.message for item in result.failed[LEGACY_FILE])


def test_every_file_the_legacy_file_depends_on_triggers_its_check() -> None:
    files = research().files
    names = [
        files.ghidra_functions,
        files.elf_sections,
        files.legacy,
        f"{files.runs}/r-1.toml",
        f"{files.functions}/a.toml",
    ]
    for name in names:
        assert docs_check.legacy_relevant([name], research()), name
    assert not docs_check.legacy_relevant(["research/missions.yaml", "docs/guides/other.md"], research())

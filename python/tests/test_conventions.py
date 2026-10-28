# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.conventions: the loader reads .github/conventions.yml strictly and the real file is sound."""

import dataclasses
import re
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
from conventions_support import CI_CLASSES, REMOVE, REPO, changed, conventions_with, real_data, write_checkout

from coney_tools.conventions import (
    CONVENTIONS_FILE,
    Conventions,
    ConventionsError,
    load_conventions,
    parse_conventions,
)

# --- the real file ------------------------------------------------------------------------------------------------


def test_the_real_file_loads() -> None:
    conventions = load_conventions(REPO)
    assert conventions.version == 1 and conventions.repo.default_branch == "main"


def test_the_real_title_rules() -> None:
    title = load_conventions(REPO).commits.title
    assert title.max_length == 72 and title.breaking_suffix == "(BREAKING)" and not title.trailing_period
    assert set(title.areas) == {
        "build",
        "ci",
        "core",
        "gameplay",
        "fileio",
        "graphics",
        "audio",
        "platform",
        "tools",
        "research",
        "agents",
        "tests",
        "legal",
        "docs",
    }
    assert title.verbs == ("Add", "Fix", "Update", "Remove", "Rebuild", "Rework", "Improve", "Document")


def test_the_real_pull_request_rules() -> None:
    rules = load_conventions(REPO).pull_requests
    assert rules.sections == ("Summary", "Issues affected", "Checklist", "Testing", "Screenshots", "AI use")
    assert rules.filled == ("Summary", "Testing") and rules.checklist.section == "Checklist"
    assert rules.docs_none.line == "Docs: none"
    assert rules.ai_use.written_by.no_ai == "a person"


def test_pull_requests_open_only_is_read_as_a_yes_or_no() -> None:
    # The switch of .claude/hooks/pr_guard.py (test_pr_guard.py holds the hook's reader to this loader).
    assert conventions_with(("pull_requests.open_only", True)).pull_requests.open_only is True
    assert conventions_with(("pull_requests.open_only", False)).pull_requests.open_only is False


def test_the_real_review_gate() -> None:
    review = load_conventions(REPO).pull_requests.review
    # The legacy gate runs the one merged reviewer (docs/guides/review/index.md), like `task review`.
    assert review.agent == "reviewer"
    assert (review.verdict_file, review.report_file) == ("REVIEW.json", "REVIEW.md")
    assert review.body_hash == "sha256-lf" and review.identical_images == "refused"
    assert (REPO / ".claude" / "agents" / f"{review.agent}.md").is_file()


def test_the_real_orig_review_gate() -> None:
    orig = load_conventions(REPO).pull_requests.orig_review
    assert (orig.agent, orig.verdict_file, orig.report_file) == ("reviewer", "ORIG-REVIEW.json", "ORIG-REVIEW.md")
    assert orig.ledger == "orig-review/ledger.jsonl" and orig.rubric_version >= 1
    assert (REPO / ".claude" / "agents" / f"{orig.agent}.md").is_file()
    # The rubric page states the same version.
    assert f"Rubric version: {orig.rubric_version}." in (REPO / "docs/guides/review/orig.md").read_text(
        encoding="utf-8"
    )


def test_the_real_merge_settings_follow_the_owners_decisions() -> None:
    merge = load_conventions(REPO).merge
    assert merge.methods == ("squash",) and merge.squash_message == "PR_BODY" and merge.auto_merge
    assert len(merge.required_checks) == 11 and "body" in merge.required_checks


def test_the_real_hygiene_thresholds_follow_the_owners_decisions() -> None:
    limits = load_conventions(REPO).hygiene.thresholds
    assert (limits.needs_triage_days, limits.needs_info_days) == (7, 21)
    assert (limits.in_progress_idle_days, limits.pull_request_idle_days) == (30, 30)


def test_the_real_labels_are_in_their_families() -> None:
    labels = load_conventions(REPO).labels
    assert labels.names("type") == ("bug", "parity", "task", "docs", "enhancement")
    assert (
        len(labels.names("area")) == 11
        and labels.names("priority")[0] == "P-critical"
        and len(labels.names("priority")) == 4
    )
    assert labels.names("status")[0] == "S-needs-triage" and len(labels.names("status")) == 10
    assert labels.names("other") == (
        "good first issue",
        "help wanted",
        "duplicate",
        "wontfix",
        "workflow",
        "studio",
        "claim-trial",
    )
    assert labels.names() == tuple(label.name for label in labels.list)


def test_the_loaded_values_cannot_be_changed() -> None:
    conventions = load_conventions(REPO)
    with pytest.raises(dataclasses.FrozenInstanceError):
        conventions.version = 2  # type: ignore[misc]
    with pytest.raises(TypeError):
        conventions.commits.title.areas["x"] = "y"  # type: ignore[index]


# --- reading ------------------------------------------------------------------------------------------------------


def test_a_since_written_as_a_day_is_midnight_utc() -> None:
    """A plain `YYYY-MM-DD` still works: it means the start of that day in UTC."""
    since = conventions_with(("pull_requests.checklist.since", "2026-10-12")).pull_requests.checklist.since
    assert since == datetime(2026, 10, 12, tzinfo=UTC)


def test_a_since_with_an_offset_is_converted_to_utc() -> None:
    """A moment written with an offset is the same instant in UTC."""
    since = conventions_with(("pull_requests.checklist.since", "2026-10-12T12:39:13+02:00")).pull_requests.checklist
    assert since.since == datetime(2026, 10, 12, 10, 39, 13, tzinfo=UTC)
    assert since.since.utcoffset() == timedelta(0)


def test_a_missing_file_is_a_one_line_error(tmp_path: Path) -> None:
    with pytest.raises(ConventionsError, match=r"conventions\.yml: cannot be read"):
        load_conventions(tmp_path)


def test_a_file_that_is_not_yaml_is_an_error(tmp_path: Path) -> None:
    (tmp_path / ".github").mkdir()
    (tmp_path / CONVENTIONS_FILE).write_text("a: [unclosed\n", encoding="utf-8")
    with pytest.raises(ConventionsError, match="not valid YAML"):
        load_conventions(tmp_path)


def test_a_file_that_is_not_utf8_is_an_error(tmp_path: Path) -> None:
    (tmp_path / ".github").mkdir()
    (tmp_path / CONVENTIONS_FILE).write_bytes(b"version: \xff\xfe\n")
    with pytest.raises(ConventionsError, match="not valid YAML"):
        load_conventions(tmp_path)


def test_a_key_written_twice_is_refused(tmp_path: Path) -> None:
    """A bad merge often leaves a key twice; plain YAML would silently keep the later one."""
    (tmp_path / ".github").mkdir()
    text = (REPO / CONVENTIONS_FILE).read_text(encoding="utf-8") + "\nversion: 1\n"
    (tmp_path / CONVENTIONS_FILE).write_text(text, encoding="utf-8")
    with pytest.raises(ConventionsError, match="written twice"):
        load_conventions(tmp_path)


def test_a_duplicate_key_inside_a_table_is_refused(tmp_path: Path) -> None:
    (tmp_path / ".github").mkdir()
    text = (REPO / CONVENTIONS_FILE).read_text(encoding="utf-8").replace("    wrap: 72", "    wrap: 72\n    wrap: 80")
    (tmp_path / CONVENTIONS_FILE).write_text(text, encoding="utf-8")
    with pytest.raises(ConventionsError, match="'wrap' is written twice"):
        load_conventions(tmp_path)


def test_a_conventions_file_that_is_not_a_mapping_is_an_error() -> None:
    with pytest.raises(ConventionsError, match="expected a mapping"):
        parse_conventions(["version", 1])


def test_parse_builds_the_same_values_as_load() -> None:
    assert parse_conventions(real_data()) == load_conventions(REPO)


def test_a_checkout_with_the_file_loads(tmp_path: Path) -> None:
    assert isinstance(load_conventions(write_checkout(tmp_path)), Conventions)


# --- the release reads a branch's file leniently ----------------------------------------------------------------------


def test_a_lenient_read_skips_an_unknown_key_and_says_where_it_was() -> None:
    """A tool older than the file it reads (the first release of a branch that adds a rule) still reads the rest."""
    ignored: list[str] = []
    data = changed(("claims.a_later_rule", 3), ("a_later_section", {"x": 1}))
    assert parse_conventions(data, ignored=ignored) == parse_conventions(real_data())
    assert ignored == ["a_later_section", "claims.a_later_rule"]
    with pytest.raises(ConventionsError, match=r"claims: unknown key 'a_later_rule'"):
        parse_conventions(changed(("claims.a_later_rule", 3)))


def test_a_lenient_read_still_refuses_a_missing_key_or_a_wrong_value() -> None:
    """Only an unknown key is let through: a key the file lacks, a typo of one and a wrong value are still errors."""
    with pytest.raises(ConventionsError, match=r"missing the key 'warn_hours'"):
        parse_conventions(changed(("claims.warn_hours", REMOVE)), ignored=[])
    with pytest.raises(ConventionsError, match=r"missing the key 'warn_hours'"):
        parse_conventions(changed(("claims.warn_hour", 48), ("claims.warn_hours", REMOVE)), ignored=[])
    with pytest.raises(ConventionsError, match=r"merge\.auto_merge: expected true or false"):
        parse_conventions(changed(("merge.auto_merge", "yes"), ("claims.a_later_rule", 3)), ignored=[])


def test_a_lenient_read_skips_a_misspelt_optional_key_and_keeps_its_default() -> None:
    """Documented limit: a typo of an optional key is an unknown key like a new one, so the default applies; the strict
    commands (repo check, task check, the tests) still refuse it."""
    ignored: list[str] = []
    data = changed(("merge.manul_paths", ["AGENTS.md"]))
    assert parse_conventions(data, ignored=ignored) == parse_conventions(real_data())
    assert ignored == ["merge.manul_paths"]
    with pytest.raises(ConventionsError, match=r"merge: unknown key 'manul_paths'"):
        parse_conventions(data)


def test_a_lenient_load_of_a_checkout_reports_the_ignored_keys(tmp_path: Path) -> None:
    ignored: list[str] = []
    root = write_checkout(tmp_path, changed(("claims.a_later_rule", 3)))
    (tmp_path / "plain").mkdir()
    plain = load_conventions(write_checkout(tmp_path / "plain"))
    assert load_conventions(root, ignored=ignored) == plain
    assert ignored == ["claims.a_later_rule"]
    with pytest.raises(ConventionsError, match=r"unknown key 'a_later_rule'"):
        load_conventions(root)


# --- strictness: every kind of mistake names the key ---------------------------------------------------------------

BAD: list[tuple[str, tuple[tuple[str, object], ...], str]] = [
    ("an unknown top-level key", (("titles", {}),), r"unknown key 'titles'"),
    ("a missing section", (("merge", REMOVE),), r"missing the key 'merge'"),
    ("an unknown nested key", (("commits.title.verb", ["Add"]),), r"commits\.title: unknown key 'verb'"),
    (
        "a claim command that does not compile",
        (("claims.commands.renew", "(/claim"),),
        r"claims\.commands\.renew: not a valid",
    ),
    (
        "a claim pattern without its scope group",
        (("claims.commands.claim", "^/claim$"),),
        r"claims\.commands\.claim: the pattern needs a named group 'scope'",
    ),
    (
        "a claim label that is not a label",
        (("claims.working_label", "S-nope"),),
        r"claims: 'S-nope' is not in labels\.list",
    ),
    (
        "a warning after the deadline",
        (("claims.warn_hours", 80),),
        r"claims\.warn_hours: must be less than deadline_hours",
    ),
    (
        "a progress refresh that is not before the warning",
        (("claims.progress_refresh_hours", 48),),
        r"claims\.progress_refresh_hours: must be less than warn_hours",
    ),
    (
        "a progress post time of zero",
        (("claims.progress_post_seconds", 0),),
        r"claims\.progress_post_seconds: expected a whole number of 1 or more",
    ),
    (
        "a limit stage that is not a claim stage",
        (("claims.limit_ends_at", ["reviewed", "shipped"]),),
        r"claims\.limit_ends_at: 'shipped' is not in claims\.stages",
    ),
    (
        "a trial label that is not a label",
        (("claims.trial.label", "claim-trails"),),
        r"claims\.trial\.label: 'claim-trails' is not in labels\.list",
    ),
    (
        "a trial that warns after it releases",
        (("claims.trial.warn_hours", 2),),
        r"claims\.trial\.warn_hours: must be less than deadline_hours",
    ),
    (
        "a trial with no hours",
        (("claims.trial.deadline_hours", 0),),
        r"claims\.trial\.deadline_hours: expected a whole number of 1 or more",
    ),
    ("a require_on_pr that is not a flag", (("claims.require_on_pr", "yes"),), r"claims\.require_on_pr: expected true"),
    ("a missing trial", (("claims.trial", REMOVE),), r"claims: missing the key 'trial'"),
    ("a bad claim marker", (("claims.card_marker", "a b"),), r"claims\.card_marker: letters, digits and hyphens"),
    (
        "an exempt label that is not a label",
        (("claims.exempt_label", "workflw"),),
        r"claims: 'workflw' is not in labels\.list",
    ),
    (
        "a studio label that is not a label",
        (("claims.studio_label", "studo"),),
        r"claims: 'studo' is not in labels\.list",
    ),
    (
        "a hold label that does not pause the claim",
        (("claims.hold_labels", ["S-in-progress"]),),
        r"claims\.hold_labels: 'S-in-progress' is not in claims\.paused_labels",
    ),
    ("a task file with a path", (("tasks.file", "a/TASK.md"),), r"tasks\.file: a file name"),
    ("rare above common", (("tasks.leads.rare_max_entries", 500),), r"must be less than common_min_entries"),
    (
        "a review agent with a path",
        (("tasks.review.agent", "agents/reviewer"),),
        r"tasks\.review\.agent: .* must be an agent name",
    ),
    (
        "a review folder outside the task",
        (("tasks.review.folder", "../review"),),
        r"tasks\.review\.folder: .* must be a folder name",
    ),
    (
        "one file for the verdict and the report",
        (("tasks.review.report_file", "REVIEW.json"),),
        r"tasks\.review\.report_file: must differ",
    ),
    (
        "a review kind that is not one",
        (("tasks.review.reverse_engineering_kinds", ["research", "prose"]),),
        r"reverse_engineering_kinds: 'prose' is not a review kind",
    ),
    (
        "no research paths",
        (("tasks.review.research_paths", []),),
        r"tasks\.review\.research_paths: expected a non-empty",
    ),
    (
        "no addresses to check on a page",
        (("tasks.review.pages_min_addresses", 0),),
        r"tasks\.review\.pages_min_addresses: expected a whole number",
    ),
    (
        "a form with the old title prefix",
        (("issues.forms.task.title_prefix", "Task: "),),
        r"unknown key 'title_prefix'",
    ),
    ("a missing nested key", (("commits.title.max_length", REMOVE),), r"commits\.title: missing the key 'max_length'"),
    ("another version", (("version", 2),), r"version: this coney-tools reads version 1"),
    ("a boolean version", (("version", True),), r"version: "),
    (
        "text for a number",
        (("commits.title.max_length", "72"),),
        r"commits\.title\.max_length: expected a whole number",
    ),
    ("a boolean for a number", (("commits.title.max_length", True),), r"max_length: expected a whole number"),
    ("a too small number", (("hygiene.thresholds.needs_triage_days", 0),), r"needs_triage_days: expected a whole"),
    ("a number for a flag", (("merge.auto_merge", 1),), r"merge\.auto_merge: expected true or false"),
    ("text for a list", (("commits.title.verbs", "Add"),), r"commits\.title\.verbs: expected a non-empty list"),
    ("an empty list", (("commits.title.verbs", []),), r"commits\.title\.verbs: expected a non-empty list"),
    ("a number in a list", (("commits.title.verbs", ["Add", 3]),), r"verbs\[1\]: expected non-empty text"),
    ("a repeated verb", (("commits.title.verbs", ["Add", "Add"]),), r"verbs: a value is listed twice"),
    ("a two-word verb", (("commits.title.verbs", ["Add it"]),), r"'Add it' must be one word"),
    ("no areas", (("commits.title.areas", {}),), r"commits\.title\.areas: expected at least one area"),
    ("an area with a colon", (("commits.title.areas", {"a:b": "x"}),), r"'a:b' must be one word without a colon"),
    ("an area with a space", (("commits.title.areas", {"a b": "x"}),), r"'a b' must be one word"),
    ("an area without text", (("commits.title.areas.docs", ""),), r"areas\.docs: expected non-empty text"),
    (
        "an open_only that is text",
        (("pull_requests.open_only", "yes please"),),
        r"pull_requests\.open_only: expected true or false",
    ),
    (
        "a pull_requests table with no open_only",
        (("pull_requests.open_only", REMOVE),),
        r"pull_requests: missing the key 'open_only'",
    ),
    ("a boolean where text goes", (("repo.default_branch", False),), r"repo\.default_branch: expected non-empty"),
    (
        "a filled section that does not exist",
        (("pull_requests.filled", ["Nope"]),),
        r"'Nope' is not one of the sections",
    ),
    ("a checklist section that does not exist", (("pull_requests.checklist.section", "Nope"),), r"checklist\.section"),
    ("a no_ai choice that is not a choice", (("pull_requests.ai_use.written_by.no_ai", "robot"),), r"no_ai: 'robot'"),
    ("a docs_none class that does not exist", (("pull_requests.docs_none.when_changed", "tests"),), r"when_changed"),
    ("an unsupported unchecked rule", (("pull_requests.checklist.unchecked", "cited"),), r"checklist\.unchecked"),
    ("a since that is no day or moment", (("pull_requests.checklist.since", "someday"),), r"checklist\.since"),
    ("an unknown review hash", (("pull_requests.review.body_hash", "md5"),), r"review\.body_hash"),
    ("an agent name with a path", (("pull_requests.review.agent", "agents/x"),), r"review\.agent"),
    (
        "one file for the verdict and the report",
        (("pull_requests.review.report_file", "REVIEW.json"),),
        r"review\.report_file",
    ),
    (
        "an orig verdict named like review's",
        (("pull_requests.orig_review.verdict_file", "REVIEW.json"),),
        r"orig_review",
    ),
    ("an orig ledger outside the scratch folder", (("pull_requests.orig_review.ledger", "../x.jsonl"),), r"ledger"),
    ("an orig rubric version of zero", (("pull_requests.orig_review.rubric_version", 0),), r"rubric_version"),
    (
        "a squash message GitHub does not have",
        (("merge.squash_message", "BODY"),),
        r"merge\.squash_message: expected one of",
    ),
    (
        "a merge method GitHub does not have",
        (("merge.methods", ["fast-forward"]),),
        r"'fast-forward' is not a merge method",
    ),
    ("a label count rule that is unknown", (("issues.labels.area", "some"),), r"issues\.labels\.area: expected one of"),
    ("a colour that is a number", (("labels.list.0.color", 123456),), r"write the colour in quotes"),
    ("a colour that is not hex", (("labels.list.0.color", "red"),), r"six hex digits"),
    ("a colour with a hash", (("labels.list.0.color", "#d73a4a"),), r"six hex digits"),
    ("a label description over the limit", (("labels.list.0.description", "x" * 101),), r"over 100 characters"),
    ("a duplicate label, ignoring case", (("labels.list.1.name", "BUG"),), r"appears twice"),
    (
        "a type name that is not a label",
        (("labels.families.type.names", ["bug", "ghost"]),),
        r"'ghost' is not in the label",
    ),
    ("a type family with a prefix", (("labels.families.type.prefix", "T-"),), r"no prefix"),
    (
        "a form label that is not a label",
        (("issues.forms.bug.labels", ["bug", "ghost"]),),
        r"forms\.bug\.labels: 'ghost'",
    ),
    ("a report label that is not a label", (("hygiene.report_issue.labels", ["ghost"]),), r"report_issue\.labels"),
    ("a comment marker with a space", (("hygiene.comments.marker", "repo hygiene"),), r"comments\.marker: letters"),
    (
        "a comment marker that ends the HTML comment",
        (("hygiene.comments.marker", "x-->"),),
        r"comments\.marker: letters",
    ),
    (
        "a comment flag that is text",
        (("hygiene.comments.on_sweep", "no"),),
        r"comments\.on_sweep: expected true or false",
    ),
    ("no comments per sweep", (("hygiene.comments.max_per_sweep", 0),), r"max_per_sweep: expected a whole"),
    ("a comment author that is empty", (("hygiene.comments.author", ""),), r"comments\.author: expected non-empty"),
    (
        "a report-only check that is unknown",
        (("hygiene.comments.report_only", ["issue.ghost"]),),
        r"report_only: 'issue.ghost'",
    ),
    ("a missing comments key", (("hygiene.comments.on_events", REMOVE),), r"comments: missing the key 'on_events'"),
    ("a cli form that does not exist", (("issues.cli_uses_form", "wish"),), r"cli_uses_form: 'wish' is not a form"),
    (
        "a form section that is not a pair",
        (("issues.forms.bug.sections.os", ["Operating system"]),),
        r"expected \[heading",
    ),
    (
        "a form heading used twice",
        (("issues.forms.bug.sections.os", ["Controller", False]),),
        r"a heading is used twice",
    ),
    ("a form without sections", (("issues.forms.bug.sections", {}),), r"forms\.bug\.sections: expected a non-empty"),
    ("a run id pattern that is not a regex", (("runs.id_pattern", "r-("),), r"runs\.id_pattern: not a regular"),
    (
        "a run record too small to hold a header",
        (("runs.max_bytes", 100),),
        r"runs\.max_bytes: expected a whole number",
    ),
    ("a run kind listed twice", (("runs.kinds", ["recipe", "recipe"]),), r"runs\.kinds: a value is listed twice"),
    ("a misspelled runs key", (("runs.max_value", 3),), r"runs: unknown key 'max_value'"),
    ("a gameplay entry that is not a src/ folder", (("paths.gameplay", ["ai/"]),), r"paths\.gameplay: 'ai/'"),
    (
        "a folder both gameplay and Coney-only",
        (("paths.coney_only", ["src/ai/", "src/debug/"]),),
        r"paths\.coney_only: 'src/ai/' is also in paths\.gameplay",
    ),
    (
        "a research kind with an optional section it does not have",
        (("research.kinds.topic.optional", ["Appendix"]),),
        r"research\.kinds\.topic\.optional: 'Appendix' is not one of the kind's sections",
    ),
    (
        "a research alias for a section no kind has",
        (("research.aliases.Purpose", "Overview"),),
        r"research\.aliases: 'Overview' is not a section of some kind",
    ),
    (
        "a research kind whose system rule is a guess",
        (("research.kinds.topic.system", "maybe"),),
        r"research\.kinds\.topic\.system: expected one of: required, none",
    ),
    (
        "a research page read as a kind there is none of",
        (("research.inferred_kind.default", "essay"),),
        r"research\.inferred_kind\.default: expected one of",
    ),
    (
        "a research key that is not one",
        (("research.kinds.topic.lines", 10),),
        r"research\.kinds\.topic: unknown key 'lines'",
    ),
    (
        "a research limit that is too small to be one",
        (("research.kinds.topic.max_lines", 5),),
        r"research\.kinds\.topic\.max_lines: expected a whole number of 100 or more",
    ),
    (
        "a rename score below git's default",
        (("research.moved_similarity", 30),),
        r"research\.moved_similarity: expected a whole number of 50 or more",
    ),
    (
        "a rename score over 100",
        (("research.moved_similarity", 101),),
        r"research\.moved_similarity: expected a whole number of 100 or less",
    ),
]


@pytest.mark.parametrize(("what", "changes", "message"), BAD, ids=[case[0] for case in BAD])
def test_a_mistake_is_refused_with_the_key_named(
    what: str, changes: tuple[tuple[str, object], ...], message: str
) -> None:
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(changed(*changes))


def test_every_error_names_the_file() -> None:
    with pytest.raises(ConventionsError) as error:
        parse_conventions(changed(("merge.auto_merge", "yes")))
    assert str(error.value).startswith(f"{CONVENTIONS_FILE}: merge.auto_merge: ")


def test_a_trailing_period_may_be_allowed_by_the_file() -> None:
    assert conventions_with(("commits.title.trailing_period", True)).commits.title.trailing_period


def test_a_quoted_numeric_colour_keeps_its_zeros() -> None:
    colours = {label.name: label.color for label in load_conventions(REPO).labels.list}
    assert colours["S-blocked"] == "000000" and colours["help wanted"] == "008672"


def test_a_colour_is_kept_in_lower_case() -> None:
    assert conventions_with(("labels.list.0.color", "D73A4A")).labels.list[0].color == "d73a4a"


# --- helpers on the values ------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("path", "code", "docs", "docs_only"),
    [
        ("src/core/memory.cpp", True, False, False),
        ("python/src/coney_tools/cli.py", True, False, False),
        ("python/tests/test_cli.py", False, False, False),
        ("tests/core/memory_test.cpp", False, False, False),
        ("docs/guides/issues.md", False, True, True),
        ("research/missions.yaml", False, True, True),
        ("README.md", False, False, True),
        ("docs/sub/dir/page.md", False, True, True),
        ("src/README.md", True, False, False),
        (".github/CONTRIBUTING.md", False, False, True),
        (".github/workflows/pr.yml", False, False, True),
        (".github/workflows/build.yml", False, False, False),
        (".github/conventions.yml", False, False, True),
        ("mkdocs.yml", False, False, True),
        ("mkdocs.yml.bak", False, False, False),
        (".markdownlint-cli2.yaml", False, False, True),
        ("CMakeLists.txt", False, False, False),
        ("python/README.md", False, False, False),
    ],
)
def test_paths_are_classified(path: str, code: bool, docs: bool, docs_only: bool) -> None:
    paths = load_conventions(REPO).paths
    assert (paths.is_code(path), paths.is_docs(path), paths.is_docs_only(path)) == (code, docs, docs_only)


@pytest.mark.parametrize(("path", "expected"), CI_CLASSES)
def test_each_path_falls_in_its_ci_scope_class(path: str, expected: str) -> None:
    assert load_conventions(REPO).paths.ci_class(path) == expected


def test_no_scope_pattern_is_hidden_by_an_earlier_class() -> None:
    """A path written like each pattern falls in that pattern's own class: an earlier class never shadows it."""
    paths = load_conventions(REPO).paths
    for key in ("docs_only", "tools_only", "engine"):
        for pattern in getattr(paths, key):
            example = pattern.replace("*", "x") + ("x" if pattern.endswith("/") else "")
            assert paths.ci_class(example) == key, pattern


def test_the_first_matching_class_wins() -> None:
    paths = conventions_with(("paths.tools_only", ["a/"]), ("paths.engine", ["a/", "b/"])).paths
    assert [paths.ci_class(path) for path in ("a/x", "b/x", "c/x")] == ["tools_only", "engine", "unknown"]


def test_the_docs_only_regex_is_the_patterns_joined() -> None:
    regex = conventions_with(("paths.docs_only", ["docs/", "*.md", "a.b"])).paths.docs_only_regex()
    assert regex == r"^(docs/.*|[^/]*\.md|a\.b)$"


def test_label_families() -> None:
    labels = load_conventions(REPO).labels
    assert [labels.family(name) for name in ("bug", "A-ai", "P-low", "S-blocked", "wontfix")] == [
        "type",
        "area",
        "priority",
        "status",
        "other",
    ]


def test_a_form_keeps_its_sections_in_order() -> None:
    form = load_conventions(REPO).issues.forms["task"]
    assert [section.id for section in form.sections][:3] == ["summary", "area", "research"]
    assert form.sections[3].heading == 'What "done" means' and form.sections[3].required


def test_the_real_data_survives_a_round_trip_through_changed() -> None:
    data: dict[str, Any] = changed()
    assert data == real_data()


def test_the_comment_settings_start_at_warn_level() -> None:
    comments = load_conventions(REPO).hygiene.comments
    assert comments.marker == "coney-repo-hygiene" and comments.author == "github-actions[bot]"
    assert comments.on_events and not comments.on_sweep and comments.max_per_sweep == 25
    assert "pr.idle" in comments.report_only


def test_no_report_only_check_is_allowed() -> None:
    assert conventions_with(("hygiene.comments.report_only", [])).hygiene.comments.report_only == ()


# --- pull_requests.runtime ----------------------------------------------------------------------------------------


def test_the_real_runtime_rule() -> None:
    runtime = load_conventions(REPO).pull_requests.runtime
    assert {"gameplay", "graphics", "audio"} <= set(runtime.areas)
    assert runtime.section == "Testing" and runtime.line == "Runtime:" and runtime.none == "Runtime: none"


def test_a_runtime_area_must_be_a_commit_title_area() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime\.areas.*commits\.title\.areas"):
        conventions_with(("pull_requests.runtime.areas", ["gameplay", "not-an-area"]))


def test_the_runtime_none_line_must_start_with_the_runtime_line() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime\.none.*must start with"):
        conventions_with(("pull_requests.runtime.none", "Original: none"))


def test_the_runtime_section_must_be_one_of_the_sections() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime\.section"):
        conventions_with(("pull_requests.runtime.section", "Evidence"))


def test_a_misspelled_runtime_key_is_refused() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime"):
        conventions_with(("pull_requests.runtime.area", ["gameplay"]))


def test_the_gameplay_and_coney_only_folders_are_read() -> None:
    paths = conventions_with().paths
    assert "src/ai/" in paths.gameplay and "src/debug/" in paths.coney_only
    assert paths.gameplay_dir("src/ai/brain.cpp") and not paths.gameplay_dir("src/debug/menu.cpp")


def test_the_real_claim_rules() -> None:
    claims = load_conventions(REPO).claims
    assert claims.limits.maintainer == 3 and claims.limits.contributor == 2
    assert (claims.warn_hours, claims.deadline_hours) == (48, 72)
    assert claims.limits.issues_per_task == 3 and claims.closing_per_event == 10 and claims.scope_max_characters == 400
    assert claims.paused_labels == ("S-blocked", "S-needs-info", "S-on-hold")
    assert claims.hold_labels == ("S-on-hold",)
    # Both switch on at the cutover, by a change to this file.
    assert (claims.adopt, claims.release) == (False, False)
    assert claims.maintainer_permissions == ("admin", "write") and claims.maintainer_associations == ("OWNER",)
    assert claims.claimable_status == claims.released_label == "S-ready-to-start"
    assert claims.critical_label == "P-critical" and claims.bot_login == "github-actions[bot]"
    assert "assigned" in claims.stages and "fix" in claims.stages
    assert load_conventions(REPO).tasks.kinds == ("feature", "fix", "docs", "tooling", "ci-fix")


def test_the_real_task_start_rules() -> None:
    start = load_conventions(REPO).tasks.start
    assert start.unit_file == "unit.json"
    # Enforced since 2026-10-27, tracks since 2026-10-28; workflow units hold no place until the cutover.
    tracks = start.slots.tracks
    assert (tracks.reimplementation.min, tracks.fix.min, tracks.fix.max, start.slots.enforce) == (1, 1, 1, True)
    assert tracks.studio.max is None and start.slots.per_build_slot == 2
    assert start.slots.agent_types == ("engineer", "tooling")
    assert start.slots.reserve_minutes > 0


def test_the_workflow_exemption_from_the_slots_ends_at_the_cutover() -> None:
    # The cutover is the change that turns `claims.adopt` and `claims.release` on (docs/guides/issues.md#claiming); the
    # same change must turn `tasks.start.slots.exempt_workflow` off, or this fails (docs/guides/agents.md#slots).
    conventions = load_conventions(REPO)
    cut_over = conventions.claims.adopt or conventions.claims.release
    assert not (cut_over and conventions.tasks.start.slots.exempt_workflow)


@pytest.mark.parametrize(
    ("change", "why"),
    [
        (("tasks.start.unit_file", "a/unit.json"), r"tasks\.start\.unit_file: a file name"),
        (("tasks.start.unit_file", "TASK.md"), r"tasks\.start\.unit_file: a file name"),
        (("tasks.start.slots.task", 3), "unknown key 'task'"),
        (("tasks.start.slots.per_build_slot", 0), r"tasks\.start\.slots\.per_build_slot: expected a whole number"),
        (("tasks.start.slots.tracks.studio", REMOVE), r"tasks\.start\.slots\.tracks: missing the key 'studio'"),
        (("tasks.start.slots.tracks.extra", {"min": 0}), "unknown key 'extra'"),
        (("tasks.start.slots.tracks.fix.min", -1), r"tracks\.fix\.min: expected a whole number of 0 or more"),
        (("tasks.start.slots.tracks.fix.max", 0), r"tracks\.fix\.max: expected a whole number of 1 or more"),
        (("tasks.start.slots.tracks.fix.min", 2), r"tracks\.fix\.min: must be at most max \(1\)"),
        (("tasks.start.slots.tracks.studio.min", 1), r"the tracks keep 3 places, more than one build slot holds"),
        (("tasks.start.slots.tracks.studio.models", "opus"), r"tracks\.studio\.models: expected a list"),
        (
            ("tasks.start.slots.tracks.studio.models", [{"model": "gpt", "effort": "high", "use": "x"}]),
            r"tracks\.studio\.models\[0\]\.model",
        ),
        (
            ("tasks.start.slots.tracks.studio.models", [{"model": "opus", "effort": "huge", "use": "x"}]),
            r"tracks\.studio\.models\[0\]\.effort",
        ),
        (
            ("tasks.start.slots.tracks.studio.models", [{"model": "opus", "effort": "high"}]),
            r"missing the key 'use'",
        ),
        (("tasks.start.slots.enforce", "yes"), r"tasks\.start\.slots\.enforce"),
        (("tasks.start.slots.extra", 1), "unknown key 'extra'"),
        (("tasks.start.slots.exempt_workflow", "yes"), r"tasks\.start\.slots\.exempt_workflow"),
        (("tasks.start.slots.agent_types", []), r"tasks\.start\.slots\.agent_types: expected a non-empty list"),
        (("tasks.start.slots.reserve_minutes", 0), r"tasks\.start\.slots\.reserve_minutes: expected a whole number"),
        (("tasks.start", REMOVE), "missing the key 'start'"),
    ],
)
def test_wrong_task_start_rules_are_refused(change: tuple[str, object], why: str) -> None:
    with pytest.raises(ConventionsError, match=why):
        conventions_with(change)


def test_the_real_task_review_rules() -> None:
    review = load_conventions(REPO).tasks.review
    assert review.agent == "reviewer" and (REPO / ".claude" / "agents" / f"{review.agent}.md").is_file()
    assert (review.folder, review.packet_file) == ("review", "PACKET.json")
    assert (review.verdict_file, review.report_file) == ("REVIEW.json", "REVIEW.md")
    assert review.research_paths == ("docs/research/", "research/") and review.docs_suffixes == (".md",)
    assert review.pages_min_addresses == 2
    # Reverse-engineering review runs on Opus (AGENTS.md), so research and `@orig` reviews need that model.
    assert review.reverse_engineering_kinds == ("research", "orig") and review.reverse_engineering_models == ("opus",)


def test_the_real_run_record_rules() -> None:
    runs = load_conventions(REPO).runs
    assert runs.folder == "research/runs" and runs.kinds == ("recipe", "trace", "method")
    assert re.fullmatch(runs.id_pattern, "r-20261023-grab-reach")
    assert not re.fullmatch(runs.id_pattern, "grab-reach") and not re.fullmatch(runs.id_pattern, "r-20261023-Grab")
    assert runs.max_bytes <= 16 * 1024 and runs.max_values >= 16 and runs.max_text >= 200


def test_every_title_area_of_a_label_is_a_title_area() -> None:
    conventions = load_conventions(REPO)
    assert set(conventions.issues.title_area_by_label.values()) <= set(conventions.commits.title.areas)
    assert set(conventions.issues.title_area_by_label) <= set(conventions.labels.names())


def test_the_real_research_page_kinds() -> None:
    research = load_conventions(REPO).research
    assert set(research.kinds) == {"system", "topic", "format", "mission", "feature", "survey"}
    topic = research.kinds["topic"]
    assert topic.sections == (
        "Summary",
        "Map",
        "Data",
        "Behaviour",
        "In Coney",
        "Open questions",
        "How this was checked",
    )
    assert topic.required == topic.sections[:-1] and topic.max_lines == 1500
    assert all(research.kinds[name].max_lines == 1000 for name in ("format", "mission", "feature", "survey"))
    assert (research.verified_max_chars, research.copy_min_chars) == (200, 200)
    assert research.files.legacy == "research/legacy.toml"
    assert research.moved_similarity == 50


def test_a_research_section_is_found_by_its_name_or_an_old_name() -> None:
    research = load_conventions(REPO).research
    assert research.canonical("topic", "Summary") == research.canonical("topic", "Purpose") == "Summary"
    assert research.canonical("topic", "original structure") == "Map"
    assert research.canonical("topic", "Coney" + chr(0x2019) + "s implementation") == "In Coney"
    assert research.canonical("format", "Data") == "Layout" and research.canonical("topic", "Layout") == ""
    assert research.canonical("topic", "Data layout") == ""


def test_a_research_page_without_front_matter_is_read_as_the_kind_of_its_folder() -> None:
    research = load_conventions(REPO).research
    assert research.kind_of_path("docs/research/formats/wad-dir.md") == "format"
    assert research.kind_of_path("docs/research/combat.md") == "system"


# --- the conclave section -----------------------------------------------------------------------------------------


def test_the_real_conclave_section() -> None:
    conclave = load_conventions(REPO).conclave
    assert conclave.claude_code.count(".") == 2 and conclave.hooks_file.endswith("hooks.toml")
    assert 0 < conclave.local_seconds <= conclave.github_seconds <= conclave.github_idle_seconds
    # the numbers the band draws by come from here, so the mod holds none of them
    assert 0 < conclave.pane_min_columns <= conclave.pane_compact_columns <= conclave.pane_wide_columns
    assert conclave.stale_after_reads >= 1


def test_the_real_watch_settings_are_read() -> None:
    watch = load_conventions(REPO).conclave.watch
    # only the coordinator is woken, toasted and held to the stop guard; the wake text is one of these keys, so the mod
    # holds none of it
    assert watch.roles == ("coordinator",) and watch.preamble.endswith(":")
    assert 0 < watch.hold_default_hours <= watch.hold_max_hours
    assert watch.stop_max_blocks >= 1 and watch.wake_gap_seconds >= 1
    assert watch.holds_dir == "pr-holds" and watch.question_prefix != watch.answer_prefix
    assert watch.unit_hold_file == "HOLD.md"


def test_a_conventions_file_made_before_the_unit_hold_key_still_reads() -> None:
    watch = parse_conventions(changed(("conclave.watch.unit_hold_file", REMOVE))).conclave.watch
    assert watch.unit_hold_file == "HOLD.md"


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("conclave.watch", REMOVE, "missing the key 'watch'"),
        ("conclave.watch.roles", [], "expected a non-empty list"),
        ("conclave.watch.roles", ["coordinator", "coordinator"], "a value is listed twice"),
        ("conclave.watch.hold_default_hours", 0, "expected a whole number of 1 or more"),
        ("conclave.watch.hold_default_hours", 99, "must not be more than hold_max_hours"),
        ("conclave.watch.stop_max_blocks", 0, "expected a whole number of 1 or more"),
        ("conclave.watch.stop_preamble", "", "expected non-empty text"),
        ("conclave.watch.holds_dir", "../x", "a folder name"),
        ("conclave.watch.holds_dir", "a/b", "a folder name"),
        ("conclave.watch.question_prefix", "ANSWER", "must differ from answer_prefix"),
        ("conclave.watch.unit_hold_file", "../HOLD.md", "a file name"),
        ("conclave.watch.unit_hold_file", "a/HOLD.md", "a file name"),
        ("conclave.watch.unit_hold_file", "", "expected non-empty text"),
        ("conclave.watch.unknown", 1, "unknown key 'unknown'"),
    ],
)
def test_a_wrong_watch_section_is_refused(path: str, value: object, message: str) -> None:
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(changed((path, value)))


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("conclave.hooks_file", "../outside.toml", "a path inside the checkout"),
        ("conclave.hooks_file", "C:/hooks.toml", "a path inside the checkout"),
        ("conclave.github_idle_seconds", 10, "must not be less than github_seconds"),
        ("conclave.local_seconds", 0, "expected a whole number of 1 or more"),
        ("conclave.unknown", 1, "unknown key 'unknown'"),
        ("conclave.pane_min_columns", REMOVE, "missing the key 'pane_min_columns'"),
        ("conclave.pane_min_columns", 14, "must be at least 15"),
        ("conclave.pane_compact_columns", 20, "must not be less than pane_min_columns"),
        ("conclave.pane_compact_columns", REMOVE, "missing the key 'pane_compact_columns'"),
        ("conclave.pane_wide_columns", 50, "must not be less than pane_compact_columns"),
        ("conclave.pane_wide_columns", REMOVE, "missing the key 'pane_wide_columns'"),
        ("conclave.band_narrow_columns", REMOVE, "missing the key 'band_narrow_columns'"),
        ("conclave.stale_after_reads", 0, "expected a whole number of 1 or more"),
    ],
)
def test_a_wrong_conclave_section_is_refused(path: str, value: object, message: str) -> None:
    data = changed((path, value))
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(data)


# --- the standards section ----------------------------------------------------------------------------------------


def test_the_real_standards_section() -> None:
    standards = load_conventions(REPO).standards
    assert standards.legacy_file == "research/legacy-files.toml"
    assert standards.code.folders == ("src/",) and ".cpp" in standards.code.suffixes
    assert "TEST_CASE" in standards.tests.macros and standards.tests.citation.marker == "Expected:"


def test_a_cutover_day_is_read_as_a_moment() -> None:
    standards = conventions_with(("standards.cutover", "2026-11-01")).standards
    assert standards.cutover is not None and standards.cutover.year == 2026 and standards.cutover.day == 1


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("standards.legacy_file", "../legacy.toml", "a path inside the checkout"),
        ("standards.enforce", "yes", "expected true or false"),
        ("standards.cutover", "soon", "expected a day or moment"),
        ("standards.unknown", 1, "unknown key 'unknown'"),
        ("standards.code.folders", ["src"], "trailing slash"),
        ("standards.code.suffixes", ["cpp"], "not a file ending"),
        ("standards.tests.macros", [], "expected a non-empty list"),
        ("standards.tests.citation.reference", "([", "not a regular expression"),
        ("standards.tests.citation.marker", REMOVE, "missing the key 'marker'"),
    ],
)
def test_a_wrong_standards_section_is_refused(path: str, value: object, message: str) -> None:
    data = changed((path, value))
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(data)


# --- the ci section -----------------------------------------------------------------------------------------------


def test_the_real_ci_section_puts_every_required_check_in_a_place() -> None:
    conventions = load_conventions(REPO)
    named = {job.split("/", 1)[1] for job in conventions.ci.jobs()}
    assert set(conventions.merge.required_checks) <= named
    # the new required checks of the CI phase: a waiting pull request, and the lines a pull request changes
    assert {"dependencies", "clang-tidy"} <= set(conventions.merge.required_checks)
    assert conventions.ci.dependencies.workflow == "dependencies.yml"
    # the build setup that sends a pull request to the whole-codebase clang-tidy pass
    assert set(conventions.ci.tidy.whole_when) == {"CMakeLists.txt", "cmake/", "CMakePresets.json", ".clang-tidy"}


def test_the_ci_places_split_the_checks_between_local_and_ci() -> None:
    ci = load_conventions(REPO).ci
    local_only = {place.id for place in ci.at("local") if "ci" not in place.where}
    assert {"full-suite", "msvc-ci-build"} <= local_only
    assert "clang-tidy" in {place.id for place in ci.at("nightly")}
    # build.yml also runs on a push to release/** and by hand, and the failure issue follows a manual run on main
    assert {"clang-tidy"} <= {place.id for place in ci.at("release")}
    assert {"clang-tidy", "failure-issue"} <= {place.id for place in ci.at("manual")}
    # a local-only check names no CI job and every CI check names one
    assert all(not place.jobs for place in ci.places if place.where == ("local",))
    assert all(place.jobs for place in ci.places if place.where != ("local",))


#: How a trigger of the gate workflows reads as a `ci.places` word: (event, branch or None) -> word.
_TRIGGER_WORDS = {
    ("pull_request", None): "ci",
    ("push", "main"): "main",
    ("push", "release/**"): "release",
    ("schedule", None): "nightly",
    ("workflow_dispatch", None): "manual",
}


def _trigger_words(workflow: dict[Any, Any]) -> set[str]:
    """The `ci.places` words a workflow's `on:` triggers stand for (PyYAML reads the key `on` as True)."""
    triggers = workflow.get("on", workflow.get(True)) or {}
    words: set[str] = set()
    for event, spec in triggers.items():
        branches = (spec or {}).get("branches") if event == "push" else None
        for branch in branches or [None]:
            words.add(_TRIGGER_WORDS[(event, branch)])
    return words


def test_the_ci_places_of_the_gate_workflows_say_every_event_their_jobs_run_on() -> None:
    import yaml

    ci = load_conventions(REPO).ci
    runs_on: dict[str, set[str]] = {}
    for name in ("build", "python", "docs"):
        workflow = yaml.safe_load((REPO / ".github" / "workflows" / f"{name}.yml").read_text(encoding="utf-8"))
        words = _trigger_words(workflow)
        for job_id, job in workflow["jobs"].items():
            # a job whose `if:` picks events runs on fewer of them; those places are checked one by one below
            if "event_name" not in str(job.get("if", "")):
                runs_on[f"{name}/{job_id}"] = words
    for place in ci.places:
        if place.jobs and all(job in runs_on for job in place.jobs):
            expected = set().union(*(runs_on[job] for job in place.jobs))
            assert set(place.where) - {"local"} == expected, place.id
    # cache-prune runs on every event but a pull request; the docs site deploys on a push to main
    caches = next(place for place in ci.places if place.id == "caches")
    assert set(caches.where) == {"main", "release", "nightly", "manual"}


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("ci.places.0.where", ["somewhere"], "is not a place"),
        ("ci.places.0.jobs", ["build/format"], "only a check that runs in CI has jobs"),
        ("ci.places.4.where", ["ci", "ci"], "a value is listed twice"),
        ("ci.places.4.jobs", REMOVE, "names its jobs"),
        ("ci.places.4.jobs", ["Build/format"], "must be <workflow file"),
        ("ci.places.1.id", "full-suite", "an id is used twice"),
        ("ci.places.4.jobs", ["build/format"], "a job is named in two places"),
        ("ci.dependencies.workflow", "dependencies", "a workflow file name"),
        ("ci.dependencies.rerun_limit", 0, "expected a whole number of 1 or more"),
        ("ci.tidy.folders", ["src"], "ending in /"),
        ("ci.tidy.headers", [".cpp"], "both a source and a header"),
        ("ci.tidy.sources", ["cpp"], "must be an extension"),
        ("ci.tidy.whole_when", ["../CMakeLists.txt"], "a path inside the checkout"),
        ("ci.tidy.whole_when", ["cmake/*.cmake"], "no pattern"),
        ("ci.tidy.whole_when", [], "non-empty list"),
        ("ci.failure.labels", ["task", "P-critical", "A-nowhere", "S-ready-to-start"], "in labels.list"),
        ("ci.failure.area", "A-core", "one of the labels"),
        ("ci.failure.marker", "Not A Marker", "lower case letters"),
        ("ci.failure.log_lines", 0, "expected a whole number of 1 or more"),
    ],
)
def test_a_wrong_ci_section_is_refused(path: str, value: object, message: str) -> None:
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(changed((path, value)))


def test_a_required_check_in_no_place_is_refused() -> None:
    data = changed(("merge.required_checks", [*real_data()["merge"]["required_checks"], "unplaced"]))
    with pytest.raises(ConventionsError, match=r"'unplaced' .* is in no place"):
        parse_conventions(data)


# --- the pull request text keys and the hook identity rule --------------------------------------------------------


def test_the_real_pull_request_text_keys() -> None:
    conventions = load_conventions(REPO)
    assert conventions.stamps.require_hook_source is False
    images = conventions.pull_requests.images
    assert images.max_bytes == 10 * 1024 * 1024 and "png" in images.extensions and images.max_count >= 1
    assert conventions.pull_requests.markers.summary != conventions.pull_requests.markers.facts


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("stamps.require_hook_source", "yes", "expected true or false"),
        ("pull_requests.images.max_bytes", 0, "expected a whole number of 1 or more"),
        ("pull_requests.images.extensions", [], "expected a non-empty list"),
        ("pull_requests.markers.facts", "coney:summary", "must differ"),
        ("pull_requests.markers.summary", REMOVE, "missing the key 'summary'"),
    ],
)
def test_a_wrong_text_key_is_refused(path: str, value: object, message: str) -> None:
    data = changed((path, value))
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(data)


def test_the_issues_affected_example_is_a_none_line_with_a_reason() -> None:
    assert load_conventions(REPO).pull_requests.issues_affected.example == "None (one-line fix)"
    for bad in ("None", "None ()", "Nothing (one-line fix)", "one-line fix"):
        with pytest.raises(ConventionsError, match=r"issues_affected.example"):
            conventions_with(("pull_requests.issues_affected.example", bad))


def test_the_impact_limits_are_whole_numbers_of_one_or_more() -> None:
    impact = load_conventions(REPO).issues.impact
    assert (impact.cache_minutes, impact.common_max_issues, impact.max_reasons) == (30, 6, 3)
    for key in ("cache_minutes", "common_max_issues", "max_reasons"):
        with pytest.raises(ConventionsError, match=key):
            conventions_with((f"issues.impact.{key}", 0))
